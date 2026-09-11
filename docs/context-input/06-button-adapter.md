# Этап 6. Адаптер отдельной кнопки

Статус: выполнено 2026-09-11.

## Задача для агента

Реализовать независимую от GPIO и Arduino state machine для отдельной кнопки. Она
принимает уже debounced переходы pressed/released и время, а выдаёт универсальные
события Pressed, Released, Clicked и LongPressed.

Кнопки энкодеров в следующих интеграциях считаются отдельными кнопками, а не частью
rotary-события `Encoder`.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Прочитать передачи результата этапов 1–5.
- Изучить `lib/ButtonMatrix/src/button_state.h` только как существующий источник
  debounced edges; не переносить его алгоритм внутрь нового адаптера.
- Проверить текущий интерфейс `Encoder` switch, но не зависеть от него: он сообщает
  только нажатие и недостаточен для long press.

## Граница ответственности

Button adapter не читает GPIO и не выполняет electrical debounce. Он получает:

```cpp
onPressed(now)
onReleased(now)
update(now)
```

Время передаётся снаружи как беззнаковое значение фиксированной ширины. Адаптер не
вызывает `millis()`, поэтому его можно тестировать на native. Разность времени
вычисляется способом, корректным при переполнении счётчика.

## Состояние и конфигурация

Объект хранит только фиксированные поля:

- `SourceId`;
- long-press threshold;
- признак текущего нажатия;
- время начала;
- признак уже отправленного LongPressed.

Никаких контейнеров переменного размера и callback.

## Семантика событий

1. Первая `onPressed(now)` выдаёт `Pressed` и запускает отсчёт.
2. Повторный press до release игнорируется.
3. `update(now)` при `elapsed >= threshold` один раз выдаёт `LongPressed`.
4. Последующие update во время удержания ничего не выдают.
5. Release до threshold выдаёт `Clicked`, затем `Released`.
6. Release после уже отправленного LongPressed выдаёт только `Released`.
7. Если цикл не вызвал update вовремя, release после threshold сначала выдаёт
   `LongPressed`, затем `Released`; Clicked не создаётся.
8. Release без активного press безопасно игнорируется.

Одна операция release может создавать два события, поэтому использовать небольшой
фиксированный `EventBatch<InputEvent, 2>` либо эквивалентный тип с массивом и
счётчиком. Динамический список запрещён. Порядок в batch должен соответствовать
описанию выше.

Короткое действие возникает на release, а не на press: только тогда достоверно
известно, что удержание не стало длинным.

## Контексты и удержание

Каждое сгенерированное событие маршрутизируется по стеку, активному в момент его
создания. После LongPressed обычный Clicked гарантированно подавляется, поэтому
release после перехода на новый экран не выполнит старое короткое действие.

Более сложный pointer-capture или запоминание контекста на момент press пока не
вводится. Если аппаратный сценарий выявит необходимость, она согласуется отдельно.

## Тесты

- обычный press/click/release и точный порядок;
- long press ровно на границе threshold;
- long press после границы;
- отсутствие повтора LongPressed;
- release после long press без Clicked;
- поздний release без промежуточного update;
- повторные press и release;
- переполнение таймера;
- независимость двух экземпляров;
- batch никогда не превышает compile-time capacity;
- созданные события проходят через существующий ContextInput.

## Критерии готовности

- Адаптер не зависит от Arduino и GPIO.
- Debounce остаётся ответственностью источника.
- Click и LongPressed взаимоисключающие.
- Нет heap и хранимых callbacks.
- Этап не меняет `main.cpp`, `Encoder` или `ButtonMatrix`.

## Передача результата

### Фиксированный пакет событий

В `lib/ContextInput/src/event_batch.h` добавлен универсальный тип:

```cpp
template <typename TEvent, std::size_t Capacity>
class EventBatch {
public:
    constexpr EventBatch() noexcept;

    template <typename TFirstEvent, typename... TRestEvents>
    explicit EventBatch(TFirstEvent&& firstEvent, TRestEvents&&... restEvents);

    [[nodiscard]] constexpr auto empty() const noexcept -> bool;
    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t;
    [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t;
    [[nodiscard]] auto operator[](std::size_t index) const -> const TEvent&;
};
```

`Capacity == 0` запрещена через `static_assert`. Количество переданных конструктору
событий также проверяется на этапе компиляции и не может превысить `Capacity`.
Занятые элементы образуют непрерывный префикс, доступный по индексам
`[0, size())`; выход за этот диапазон является нарушением предусловия, аналогично
unchecked-доступу к `std::array`.

Внутри используется `std::array<std::optional<TEvent>, Capacity>`, поэтому тип
события не обязан иметь конструктор по умолчанию. Heap и runtime-рост отсутствуют.
`EventBatch` подключён к общему `<context_input.h>` как нейтральный тип ядра.

### Публичный API адаптера

Адаптер находится в отдельном необязательном заголовке:

```cpp
#include <adapters/button_input.h>

namespace ContextInput {

template <typename TSourceId>
struct ButtonInputAdapterSettings {
    TSourceId source;
    std::uint32_t longPressThreshold;
};

template <typename TSourceId>
class ButtonInputAdapter {
public:
    using Event = InputEvent<TSourceId>;
    using Batch = EventBatch<Event, 2>;
    using Time = std::uint32_t;

    explicit ButtonInputAdapter(ButtonInputAdapterSettings<TSourceId> settings);

    [[nodiscard]] auto onPressed(Time now) -> Batch;
    [[nodiscard]] auto update(Time now) -> Batch;
    [[nodiscard]] auto onReleased(Time now) -> Batch;
    [[nodiscard]] auto isPressed() const noexcept -> bool;
};

} // namespace ContextInput
```

`TSourceId` хранится по значению и должен быть copy-constructible. Именованные
настройки не позволяют молча перепутать числовой source и threshold. Все операции
возвращают один и тот же `Batch`, поэтому вызывающий код может маршрутизировать их
общим циклом.

### Состояние и время

Экземпляр хранит только source, threshold, время начала и два флага: кнопка нажата
и `LongPressed` уже отправлен. Callback, `Router`, GPIO, Arduino и динамические
контейнеры не хранятся.

`Time` и threshold имеют тип `std::uint32_t`. Адаптер не вызывает `millis()`:
текущее время всегда передаёт источник. Время удержания вычисляется как беззнаковое
`now - pressedAt`, что корректно при одном переполнении таймера. Между связанными
операциями не должно проходить полного периода `uint32_t`.

Threshold `0` имеет определённую семантику: `onPressed()` выдаёт только `Pressed`,
а следующий `update()` или `onReleased()` немедленно считает нажатие долгим.

### Точный порядок событий

```text
onPressed впервые                       -> Pressed
onPressed повторно до release           -> пустой batch
update до threshold                     -> пустой batch
первый update на/после threshold        -> LongPressed
последующие update при удержании        -> пустой batch
release до threshold                    -> Clicked, Released
release после отправленного LongPressed -> Released
release на/после threshold без update   -> LongPressed, Released
release без активного press             -> пустой batch
```

После любого принятого release экземпляр готов к новому циклу. `Clicked` и
`LongPressed` взаимоисключающие. Каждое событие из batch должно отдельно проходить
через `Router` в указанном порядке.

### Граница интеграции

`src/main.cpp`, `Encoder` и `ButtonMatrix` на этапе не менялись. Существующий
`Encoder::SwitchHandler` сообщает только press, поэтому пока не может корректно
питать этот адаптер: для click/long press нужен также debounced release.

При будущей интеграции потребуется расширить источник кнопки энкодера двумя edges
или получить его debounced состояние иным способом. Это не меняет API адаптера.

### Файлы

- `lib/ContextInput/src/event_batch.h` — фиксированный пакет;
- `lib/ContextInput/src/adapters/button_input.h` — state machine кнопки;
- `lib/ContextInput/src/context_input.h` — экспорт `EventBatch`;
- `test/test_native/context_input/test_event_batch.h/.cpp` — тесты пакета;
- `test/test_native/context_input/test_button_input_adapter.h/.cpp` — тесты кнопки
  и маршрутизации;
- `test/test_native/main.cpp` — регистрация новых тестов.

### Проверки

- `make format` и `make format-check` — успешно;
- прямой `clang-tidy` обоих новых header-only компонентов — без замечаний к коду;
- native-тесты — 82 из 82 прошли;
- полный `make verify` — успешно;
- firmware `rpipico2` — успешно собрана;
- RAM: 165620 из 524288 байт (31.6%);
- Flash: 661488 из 4190208 байт (15.8%).

### Условия для следующих этапов

- Trigger остаётся отдельной моделью устойчивого bool-состояния и не должен
  переиспользовать `ButtonPhase`.
- Интеграция матрицы может создать по одному `ButtonInputAdapter<InputId>` на
  физическую кнопку и подавать ему debounced edges из `ButtonState`.
- Один результат источника может содержать до двух физических `InputEvent`; это не
  меняет правило `Router`: один вызов `dispatch()` возвращает максимум один
  смысловой `AppEvent`.
- Если обработка первого элемента batch меняет стек, второй элемент должен
  маршрутизироваться уже через обновлённый стек.
