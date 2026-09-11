# Этап 2. Контракт контекста

Статус: выполнено 2026-09-11.

## Задача для агента

Реализовать три результата обработки и доказать на одном тестовом контексте, что
обычный пользовательский класс может преобразовать `InputEvent` в смысловое
событие. Ограниченный стек на этом этапе не создаётся.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Проверить секцию `Передача результата` в `01-event-model.md`.
- Использовать фактические имена типов этапа 1, если они отличаются от плана.

## Семантика результата

Результат должен однозначно выражать три состояния:

```cpp
enum class DispatchStatus : uint8_t {
    Unhandled, // текущий контекст не знает событие; передать ниже
    Consumed,  // событие обработано или заблокировано; результата для приложения нет
    Emitted,   // событие обработано; возвращено смысловое событие
};
```

Целевой интерфейс:

```cpp
template <typename TOutputEvent>
class DispatchResult {
public:
    static DispatchResult pass();
    static DispatchResult consume();
    static DispatchResult emit(TOutputEvent event);

    DispatchStatus status() const;
    bool hasEvent() const;
    const TOutputEvent& event() const;
};
```

Допустим эквивалентный API, если он сохраняет невозможность противоречивого
состояния. Например, `Emitted` без значения не должен конструироваться публично.
`event()` не должен молча создавать значение для `Unhandled` или `Consumed`.

## Протокол пользовательского контекста

Контекст — обычный объект с методом:

```cpp
DispatchResult<AppEvent> handle(const AppInputEvent& event);
```

Метод может быть неконстантным: контексту разрешено хранить собственное состояние.
Он не изменяет доменную модель и не обращается к LVGL; единственный результат его
работы — `pass`, `consume` или смысловой `AppEvent`.

Для теста создать локальные типы:

```text
TestInputId::Encoder
TestAppEvent::AdjustValue
```

и простой контекст, который:

- преобразует вращение известного энкодера в `AdjustValue{delta}`;
- возвращает `pass()` для неизвестного источника;
- умеет вернуть `consume()` для явно заблокированного события.

Тестовый контекст не должен становиться частью публичного API библиотеки.

## Тесты

Проверить:

- фабрику и статус `pass()`;
- фабрику и статус `consume()`;
- `emit()` и сохранение точного смыслового события;
- отсутствие события в первых двух состояниях;
- передачу signed encoder delta без изменения;
- пользовательский контекст как обычный класс без наследования;
- возможность хранить состояние внутри контекста.

## Критерии готовности

- Три результата невозможно спутать на стороне вызывающего кода.
- Один контекст протестирован отдельно от стека.
- Публичный контракт не зависит от Arduino и приложения.
- Один вход возвращает не более одного выходного события.
- Все проверки этапа 1 продолжают проходить.

## Вне этапа

- Не реализовывать type erasure и массив контекстов.
- Не добавлять `std::function`, virtual base class или heap.
- Не подключать реальные энкодеры приложения.

## Передача результата

### Фактический публичный API

Все типы находятся в namespace `ContextInput`. Параметры шаблонных типов следуют
соглашению с префиксом `T`:

```cpp
enum class DispatchStatus : std::uint8_t {
    Unhandled,
    Consumed,
    Emitted,
};

template <typename TOutputEvent>
class DispatchResult {
public:
    [[nodiscard]] static constexpr auto pass() noexcept -> DispatchResult;
    [[nodiscard]] static constexpr auto consume() noexcept -> DispatchResult;
    [[nodiscard]] static constexpr auto emit(TOutputEvent event) noexcept(
        std::is_nothrow_move_constructible_v<TOutputEvent>) -> DispatchResult;

    [[nodiscard]] constexpr auto status() const noexcept -> DispatchStatus;
    [[nodiscard]] constexpr auto hasEvent() const noexcept -> bool;

    constexpr auto event() noexcept -> TOutputEvent&;
    constexpr auto event() const noexcept -> const TOutputEvent&;
};
```

`pass()` и `consume()` создают результат без события. `emit()` всегда содержит
событие. Конструкторы закрыты, поэтому публичный API не позволяет создать
противоречивое состояние. Выходной тип не обязан иметь конструктор по умолчанию и
может быть move-only.

`event()` имеет предусловие `hasEvent() == true`, проверяемое через `assert` в
сборках, где assertions включены. Метод не создаёт запасного значения. После
отключения assertions нарушение предусловия является ошибкой вызывающего кода.

### Контракт пользовательского контекста

Контекст остаётся обычным объектом без наследования:

```cpp
auto handle(const AppInputEvent& event)
    -> ContextInput::DispatchResult<AppEvent>;
```

Тестовый контекст подтверждает `Emitted` для известного энкодера, `Consumed` для
заблокированного источника и `Unhandled` для неизвестного источника или
несовместимого payload. Счётчик вызовов подтверждает, что контекст может изменять
собственное состояние.

### Изменённые файлы

- `lib/ContextInput/src/dispatch_result.h` — статус и результат обработки;
- `lib/ContextInput/src/context_input.h` — экспорт нового заголовка;
- `lib/ContextInput/src/input_event.h` — параметр шаблона переименован в
  `TSourceId` без изменения публичной семантики;
- `test/test_native/context_input/test_dispatch_result.h` и `.cpp` — 10 тестов;
- `test/test_native/main.cpp` — регистрация нового набора тестов;
- документы этапов — соглашение `T...` для параметров типов.

### Проверки

- `make format` и `make format-check` — успешно;
- `make tidy` — успешно; существующие предупреждения старого кода не исправлялись;
- `make test` — 29 из 29 тестов прошли;
- `make build` — прошивка `rpipico2` успешно собрана;
- `git diff --check` — успешно;
- прямой анализ заголовков не выявил ошибок; предупреждение `#pragma once in main
  file` ожидаемо при анализе `.h` как отдельной единицы трансляции.

### Условия для этапа 3

- Использовать `ContextInput::DispatchResult<TOutputEvent>` без изменения его
  трёх состояний.
- Все параметры типов в шаблонах именовать с префиксом `T`.
- Проверять `hasEvent()` или `status() == DispatchStatus::Emitted` до `event()`.
- `Router` должен возвращать результат по значению, включая move-only событие.
- Контекст имеет неконстантный `handle(const TInputEvent&)`; конкретную сигнатуру
  следует проверить через C++17 type traits внутри `addContext<TContext>()`.
- Не добавлять базовый класс, `std::function`, heap или владение контекстом.
