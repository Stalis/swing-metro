# ContextInput

`ContextInput` — переносимая библиотека C++17 для преобразования входов в
смысловые события приложения. Источником может быть устройство или программный
код: библиотека получает `InputEvent`, передаёт его контекстам сверху вниз и
возвращает `DispatchResult` вызывающему коду. Она не управляет GPIO, экраном или
моделью приложения.

## Минимальный пример

```cpp
#include <context_input.h>

#include <cstdint>
#include <variant>

enum class Source : std::uint8_t { Tempo };
struct AdjustTempo { std::int8_t delta; };

using Input = ContextInput::InputEvent<Source>;
using AppEvent = std::variant<AdjustTempo>;
using Result = ContextInput::DispatchResult<AppEvent>;

class MainContext {
  public:
    auto handle(const Input& input) -> Result {
        if (input.source != Source::Tempo) {
            return Result::pass();
        }
        if (const auto* turn = std::get_if<ContextInput::EncoderInput>(&input.payload)) {
            return Result::emit(AppEvent{AdjustTempo{turn->delta}});
        }
        return Result::pass();
    }
};

MainContext mainContext;
ContextInput::Router<Input, AppEvent, 2> router;

void example() {
    (void)router.addContext(mainContext);
    const Input input{Source::Tempo, ContextInput::EncoderInput{1}};
    const auto result = router.dispatch(input);
    if (result.hasEvent()) {
        // Приложение само применяет result.event(), например меняет tempo.
    }
}
```

`handle(const Input&)` может быть `const` или неконстантным, но объект контекста,
передаваемый в `addContext()`, должен быть неконстантным. Его результат имеет три
смысла:

- `pass()` — этот слой не обработал вход; маршрутизатор пробует слой ниже;
- `consume()` — вход обработан без выходного события, обход прекращается;
- `emit(event)` — обход прекращается, вызывающий код получает одно событие.

`event()` вызывают только после `hasEvent()`. Контекст не изменяет модель
приложения и не добавляет/удаляет другие контексты внутри `handle()`.

## Стек и время жизни

`Router<TInputEvent, TOutputEvent, Capacity>` хранит фиксированный массив
невладеющих ссылок на контексты. Последний добавленный контекст находится сверху.
`addContext(context)` возвращает `Added`, `AlreadyPresent` или `StackFull`;
`releaseContext(context)` возвращает `Released` или `NotFound` и удаляет именно
этот объект, сохраняя порядок остальных. При пустом стеке результат `dispatch()`
равен `pass()`.

Контексты создаёт и хранит приложение. Пока объект присутствует в стеке, его
нельзя уничтожать или перемещать. Удаление из стека не уничтожает объект и не
сбрасывает его состояние; его можно добавить снова. Менять стек следует после
возврата `dispatch()`, при обработке смыслового события. `Router` рассчитан на
один поток/ядро; синхронизацию с другим потоком, например UI, обеспечивает
приложение.

В ядре нет `new`, `delete`, динамических контейнеров и `std::function`.
`std::array`, `std::optional` и `std::variant` хранятся без heap. Один
`dispatch()` возвращает не более одного смыслового события. Для нескольких
физических событий адаптер кнопки возвращает пакет фиксированной ёмкости,
который вызывающий код передаёт маршрутизатору поэлементно.

## Необязательные адаптеры

Адаптеры подключаются отдельно и не входят в `<context_input.h>`.

```cpp
#include <adapters/encoder_input.h>

ContextInput::EncoderInputAdapter<Source> encoder{Source::Tempo};
void encoderExample(EncoderDirection direction) {
    const auto input = encoder.translate(direction);
    if (input.has_value()) {
        const auto result = router.dispatch(*input);
        // Проверить result.hasEvent() и применить событие в приложении.
    }
}
```

`EncoderInputAdapter` использует интерфейс `EncoderDirection` из `<encoder.h>`:
`Clockwise` даёт `EncoderInput{+1}`, `CounterClockwise` — `{-1}`, а
`Undefined` — `std::nullopt`. Адаптер не читает пины и не владеет энкодером;
зависимость от `Encoder` остаётся только в этом необязательном заголовке.

`ButtonInputAdapter<TSourceId>` принимает заранее очищенные от дребезга границы
через `onPressed(now)` и `onReleased(now)`, а `update(now)` вызывается во время
удержания. Настройки — `{source, longPressThreshold}`; время и порог имеют тип
`std::uint32_t` в единицах, выбранных вызывающим кодом. События:

```text
короткое нажатие: Pressed -> Clicked -> Released
удержание:         Pressed -> LongPressed -> Released
```

Если время порога наступило только к отпусканию, один вызов `onReleased()`
вернёт пакет из `LongPressed` и `Released`. `Clicked` и `LongPressed`
взаимоисключающие. Каждый элемент `Batch` (максимум два) нужно направлять в
`dispatch()` отдельно и по порядку. Для проверки текущего состояния кнопки есть
`isPressed()`. Время передаёт приложение; арифметика беззнакового `uint32_t`
корректно переносит одно переполнение таймера при удержании короче полного
периода счётчика.

`TriggerInputAdapter<TSourceId>` хранит двоичное состояние. `set(active)`
возвращает `TriggerInput{active}` только при изменении состояния, иначе
`std::nullopt`; `isActive()` возвращает последнее значение. Он удобен для
временного Shift-контекста и не зависит от кнопочного таймера.

Все адаптеры лишь переводят данные источника в нейтральные события. Назначение
кнопок, выбор экрана и изменение параметров остаются в приложении.
