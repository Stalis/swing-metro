# Этап 7. Универсальный trigger

Статус: выполнено 2026-09-12.

## Задача для агента

Добавить простейшую обёртку двоичного источника, которая выдаёт `TriggerInput`
только при изменении состояния. Подтвердить на контекстах сценарий временного Shift.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Прочитать передачи результата этапов 1–6.
- Использовать фактический `TriggerInput` из модели событий.

## Целевой API и поведение

Пример:

```cpp
TriggerInputAdapter<InputId> shiftTrigger{InputId::Shift, false};

shiftTrigger.set(true);  // event: TriggerInput{true}
shiftTrigger.set(true);  // no event
shiftTrigger.set(false); // event: TriggerInput{false}
```

Адаптер:

- хранит SourceId и текущее bool-состояние;
- не знает о кнопках, GPIO, debounce или времени;
- не знает о `ContextInput` и только создаёт optional InputEvent;
- может использоваться физическим и программным источником.

One-shot `pulse()` на этом этапе не добавлять: согласованная модель trigger имеет
устойчивые состояния active/inactive.

## Сценарий Shift

Добавить тестовые, не app-level, контексты:

1. Нижний контекст переводит `Shift + active` в смысловое `ActivateShift`.
2. После возврата события тестовый код вызывает `addContext(shiftContext)`.
3. `Shift + inactive` проходит через `ShiftContext` вниз и превращается в
   `DeactivateShift`.
4. После возврата события тестовый код вызывает `releaseContext(shiftContext)`.

Это доказывает важное правило: стек меняется приложением после `dispatch()`, а не
самим контекстом во время обработки.

Проверить также удаление Shift из середины, если над ним был добавлен временный
контекст.

## Тесты

- false -> true создаёт active event;
- true -> false создаёт inactive event;
- одинаковое значение не создаёт событие;
- настраиваемое начальное состояние;
- два trigger независимы;
- полный цикл Shift add/release;
- повторный active не добавляет второй ShiftContext;
- release Shift из середины сохраняет остальные контексты.

## Критерии готовности

- Trigger остаётся универсальным и Arduino-independent.
- Двоичное состояние не смешано с ButtonPhase.
- Сценарий Shift проходит без динамической памяти.
- Реальное приложение и LVGL на этом этапе не изменяются.

## Передача результата

### Фактический API

Адаптер реализован в отдельном необязательном заголовке
`lib/ContextInput/src/adapters/trigger_input.h`:

```cpp
#include <adapters/trigger_input.h>

namespace ContextInput {

template <typename TSourceId>
class TriggerInputAdapter {
public:
    explicit constexpr TriggerInputAdapter(const TSourceId& source,
                                           bool initialState = false);

    [[nodiscard]] auto set(bool active)
        -> std::optional<InputEvent<TSourceId>>;

    [[nodiscard]] constexpr auto isActive() const noexcept -> bool;
};

} // namespace ContextInput
```

`TSourceId` хранится по значению и должен быть copy-constructible. Начальное
состояние не отправляет событие. `set()` меняет состояние и выдаёт один
`InputEvent<TSourceId>` с `TriggerInput{active}` только при переходе между `false`
и `true`. Повторное значение возвращает `std::nullopt`. `isActive()` позволяет
опросить последнее установленное состояние.

Заголовок зависит только от универсального `input_event.h` и C++17. Он не хранит
`Router`, callback и время; не читает GPIO и не выделяет динамическую память.
Через общий `<context_input.h>` адаптер намеренно не экспортируется, как и другие
необязательные адаптеры.

### Подтверждённый цикл Shift

Нативные тестовые контексты, не используемые приложением, демонстрируют порядок:

```text
BaseContext в стеке
  -> trigger.set(true)
  -> dispatch(TriggerInput{true}) на прежнем стеке
  -> ActivateShift
  -> addContext(shiftContext) после возврата dispatch
  -> следующие события получают приоритет ShiftContext
  -> trigger.set(false)
  -> ShiftContext пропускает TriggerInput{false} вниз
  -> DeactivateShift от BaseContext
  -> releaseContext(shiftContext) после возврата dispatch
```

Событие активации не доставляется только что добавленному контексту: на момент
его единственного `dispatch()` контекста в стеке ещё нет. Повторный `set(true)` не
создаёт событие и не ведёт к повторному добавлению Shift. Если над Shift добавлен
временный контекст, `ShiftContext` удаляется из середины, а временный остаётся.

Верхние контексты должны пропускать `Shift=false` к обработчику деактивации;
универсальный `Router` не может гарантировать это за них.

### Граница будущей интеграции

Реальное приложение и LVGL не изменялись. Физическая Shift-кнопка пока не
подключена. В будущем её debounced `Pressed`/`Released` можно связать с
`trigger.set(true/false)`; какой именно источник станет Shift, решается на уровне
приложения и не меняет универсальный адаптер.

Изоляция **последующих фаз того же кнопочного удержания** после перехода в новый
контекст отдельно записана в `09-application-ui-coordination.md`. Текущая стадия
проверяет только отсутствие повторной доставки одного и того же `InputEvent` после
`addContext()`; она не вводит gesture capture или подавление `LongPressed`/`Released`.

### Файлы

- `lib/ContextInput/src/adapters/trigger_input.h` — адаптер;
- `test/test_native/context_input/test_trigger_input_adapter.h/.cpp` — переходы
  состояния и сценарий Shift;
- `test/test_native/main.cpp` — регистрация тестов;
- `docs/context-input/08-button-matrix-integration.md` и
  `09-application-ui-coordination.md` — отложенное требование об изоляции жеста.

### Проверки

- `make format` и `make format-check` — успешно;
- native-тесты — 90 из 90 прошли;
- прямой `clang-tidy` нового header-only адаптера — без замечаний к коду (только
  диагностическое сообщение о `#pragma once` при анализе заголовка как main file);
- полный `make verify` — успешно;
- firmware `rpipico2` — успешно собрана;
- RAM: 165620 из 524288 байт (31.6%);
- Flash: 661488 из 4190208 байт (15.8%).
