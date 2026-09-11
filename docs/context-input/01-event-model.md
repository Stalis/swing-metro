# Этап 1. Модель событий

Статус: выполнено 2026-09-11.

## Задача для агента

Создать минимальные, не зависящие от Arduino типы входных событий для будущего
маршрутизатора. На этом этапе не реализовывать контексты, стек, адаптеры или
подключение к `main.cpp`.

## Обязательный контекст

Прочитать `00-introduction.md`, затем проверить:

- `lib/ContextInput/library.json`;
- `lib/ContextInput/src/context_input.h`;
- `platformio.ini`;
- структуру существующих native-тестов в `test/test_native`.

## Входные события

Библиотека предоставляет стандартные payload-типы:

```cpp
struct EncoderInput {
    int8_t delta;
};

enum class ButtonPhase : uint8_t {
    Pressed,
    Released,
    Clicked,
    LongPressed,
};

struct ButtonInput {
    ButtonPhase phase;
};

struct TriggerInput {
    bool active;
};
```

Общий вход параметризуется пользовательским идентификатором источника:

```cpp
template <typename TSourceId>
struct InputEvent {
    TSourceId source;
    std::variant<EncoderInput, ButtonInput, TriggerInput> payload;
};
```

`SourceId` принадлежит приложению и обычно является `enum class`. Не вводить в
универсальную библиотеку имена `TempoEncoder`, `StepButton` и подобные.

На этом этапе допускаются небольшие `constexpr`-фабрики для безопасного создания
событий, если они заметно упрощают API. Не создавать иерархию событий и не
использовать динамический полиморфизм.

## Выходные события

Библиотека не определяет `AppEvent`. Тип выходного события является параметром
шаблонов следующих этапов. В тестах использовать небольшой локальный тип или
`std::variant` тестовых смысловых событий.

## Manifest библиотеки

Обновить `library.json`, чтобы он описывал универсальный fixed-capacity router, а
не библиотеку, привязанную к Swing Metro. Ядро должно поддерживать native C++17;
не заявлять обязательную зависимость от Arduino. Зависимость от `Encoder` появится
только в дополнительном адаптере этапа 4 и не должна проникнуть в ядро.

## Тесты

Добавить native-тесты, подтверждающие:

- сохранение пользовательского `SourceId`;
- корректный signed `delta` энкодера;
- различение всех фаз кнопки;
- различение активного и неактивного trigger;
- отсутствие необходимости в Arduino-заголовках;
- копируемость/перемещаемость событий, если она требуется стандартным использованием.

Не фиксировать тестом точный `sizeof`: он зависит от ABI. Вместо этого проверить
кодом и ревью, что используемые типы имеют только встроенное фиксированное хранение.

## Критерии готовности

- Заголовок с моделью событий существует и подключается в native-тесте.
- Ядро `ContextInput` не подключает `Arduino.h`.
- Нет доменных идентификаторов Swing Metro.
- Нет динамической памяти и callback-механизма.
- Native-тесты и прошивка собираются.

## Вне этапа

- `DispatchResult` с семантикой pass/consume/emit реализуется в этапе 2.
- Не создавать стек и контексты.
- Не менять `Encoder`, `ButtonMatrix`, `main.cpp` или LVGL.

## Передача результата

### Фактический публичный API

Все типы находятся в согласованном namespace `ContextInput`:

```cpp
namespace ContextInput {

struct EncoderInput {
    std::int8_t delta;
};

enum class ButtonPhase : std::uint8_t {
    Pressed,
    Released,
    Clicked,
    LongPressed,
};

struct ButtonInput {
    ButtonPhase phase;
};

struct TriggerInput {
    bool active;
};

using InputPayload = std::variant<EncoderInput, ButtonInput, TriggerInput>;

template <typename TSourceId>
struct InputEvent {
    TSourceId source;
    InputPayload payload;
};

} // namespace ContextInput
```

Основная точка подключения — `<context_input.h>`; она включает `input_event.h`.
Временный пустой класс `ContextInput` удалён. Фабрики событий не добавлялись:
типы поддерживают обычную aggregate-инициализацию.

### Изменённые файлы

- `lib/ContextInput/library.json` — универсальное описание, `frameworks: "*"`;
- `lib/ContextInput/src/context_input.h` — umbrella header;
- `lib/ContextInput/src/input_event.h` — модель событий;
- `test/test_native/context_input/test_input_event.h` и `.cpp` — 6 тестов;
- `test/test_native/main.cpp` — регистрация набора тестов;
- `platformio.ini` — явный `-std=gnu++17` только для native environment.

### Проверки

- `make format` и `make format-check` — успешно;
- отдельный запуск clang-tidy для portable-заголовка — без ошибок; ожидаемое
  предупреждение `#pragma once in main file` связано с прямым анализом `.h`;
- `make tidy` — успешно, существующие предупреждения старого кода не исправлялись;
- `make test` — 19 из 19 тестов прошли: 13 существующих и 6 новых;
- `make build` — прошивка `rpipico2` успешно собрана;
- `git diff --check` — успешно.

### Условия для этапа 2

- Использовать namespace `ContextInput` с заглавных букв.
- Основной тип маршрутизатора следующих этапов называется `ContextInput::Router`,
  чтобы избежать конструкции `ContextInput::ContextInput`.
- `DispatchResult` и выходное событие ещё не существуют.
- Тип выходного события должен оставаться пользовательским параметром шаблона.
- Не менять состав `InputPayload` без нового согласованного сценария.
- Native-код теперь гарантированно собирается как GNU C++17.
- Ядро по-прежнему не зависит от Arduino и не выделяет динамическую память.
