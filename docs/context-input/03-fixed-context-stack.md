# Этап 3. Ограниченный стек контекстов

Статус: выполнено 2026-09-11.

## Задача для агента

Реализовать `ContextInput::Router<TInputEvent, TOutputEvent, Capacity>`: фиксированный
стек невладеющих ссылок на произвольные пользовательские контексты и маршрутизацию
события от верхушки к основанию. Имя `Router` используется, чтобы не создавать
неудобный тип `ContextInput::ContextInput` внутри согласованного namespace.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Прочитать секции передачи результата этапов 1 и 2.
- Не предполагать наличие интерфейсов, которые предыдущие этапы не реализовали.

## Хранение контекста без наследования

Каждый слот хранит только:

```text
void* identity
function pointer thunk
```

Шаблонный `addContext(TContext&)` создаёт thunk, который безопасно приводит
`void*` обратно к `TContext*` и вызывает `handle(const InputEvent&)`.

Через C++17 `static_assert` и type traits проверить, что `handle()` возвращает
правильный `DispatchResult<TOutputEvent>`. Concepts, `std::function`, virtual base
class и динамическое выделение памяти не использовать.

## Публичные операции

Целевой интерфейс:

```cpp
namespace ContextInput {

template <typename TInputEvent, typename TOutputEvent, size_t Capacity>
class Router {
public:
    template <typename TContext>
    AddContextResult addContext(TContext& context);

    template <typename TContext>
    ReleaseContextResult releaseContext(TContext& context);

    template <typename TContext>
    bool contains(const TContext& context) const;

    DispatchResult<TOutputEvent> dispatch(const TInputEvent& event);

    size_t size() const;
    static constexpr size_t capacity();
};

} // namespace ContextInput
```

Названия result-enum могут уточняться, но ошибки должны возвращаться значением, а
не исключением:

```text
add: Added / AlreadyPresent / StackFull
release: Released / NotFound
```

Нулевая `Capacity` должна быть либо запрещена понятным `static_assert`, либо иметь
полностью определённое безопасное поведение.

## Порядок и удаление

- Первый добавленный контекст находится в основании.
- Последний добавленный — на верхушке и получает событие первым.
- `Unhandled` продолжает обход вниз.
- `Consumed` и `Emitted` немедленно останавливают обход.
- Если все вернули `Unhandled`, стек возвращает `Unhandled`.
- Один объект нельзя добавить повторно.
- `releaseContext(object)` удаляет этот объект из любой позиции.
- После удаления середины массив компактируется с сохранением относительного
  порядка остальных контекстов.
- Библиотека не уничтожает объекты контекстов.

Изменять стек из `TContext::handle()` запрещено контрактом. Приложение сначала
получает результат `dispatch()`, затем применяет смысловое событие и меняет стек.

## Обязательные тесты

1. Пустой стек возвращает `Unhandled`.
2. Один контекст обрабатывает событие.
3. Верхний контекст имеет приоритет.
4. `Unhandled` проваливается на нижний контекст.
5. `Consumed` останавливает проход без события.
6. `Emitted` останавливает проход и возвращает точное событие.
7. Необработанное всеми событие возвращает `Unhandled`.
8. Добавление до ёмкости успешно.
9. Добавление сверх ёмкости возвращает `StackFull` без повреждения стека.
10. Повторное добавление возвращает `AlreadyPresent`.
11. Удаление верхнего, нижнего и среднего контекста.
12. Удаление неизвестного объекта безопасно.
13. После удаления порядок оставшихся обработчиков сохраняется.
14. Контекст с внутренним состоянием вызывается как исходный объект, а не копия.

## Критерии готовности

- Размер внутреннего хранилища определяется `Capacity` на этапе компиляции.
- Ни одна операция стека не выделяет память.
- Контексты не обязаны наследоваться от библиотечного класса.
- Реализована вся семантика pass/consume/emit.
- Native-тесты и firmware build проходят.

## Вне этапа

- Не добавлять адаптеры устройств.
- Не создавать контексты Swing Metro в `src/`.
- Не вводить priority: приоритет задаётся порядком добавления.
- Не вводить очередь событий или потокобезопасность.

## Передача результата

### Фактический публичный API

```cpp
namespace ContextInput {

enum class AddContextResult : std::uint8_t {
    Added,
    AlreadyPresent,
    StackFull,
};

enum class ReleaseContextResult : std::uint8_t {
    Released,
    NotFound,
};

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
class Router {
public:
    template <typename TContext>
    [[nodiscard]] auto addContext(TContext& context) noexcept
        -> AddContextResult;

    template <typename TContext>
    [[nodiscard]] auto releaseContext(TContext& context) noexcept
        -> ReleaseContextResult;

    template <typename TContext>
    [[nodiscard]] auto contains(const TContext& context) const noexcept -> bool;

    [[nodiscard]] auto dispatch(const TInputEvent& event)
        -> DispatchResult<TOutputEvent>;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t;
};

} // namespace ContextInput
```

`Capacity == 0` запрещена понятным `static_assert`. Проверка контекста реализована
через C++17 detection idiom: `handle(const TInputEvent&)` должен возвращать точно
`DispatchResult<TOutputEvent>`. Контекстный метод может быть неконстантным или
константным, но в стек добавляется неконстантный объект.

### Хранение и маршрутизация

Каждый занятый слот содержит только `void*` на исходный объект и обычный указатель
на type-erasure thunk. Массив слотов — `std::array<ContextRef, Capacity>`; текущий
размер хранится отдельно. `std::function`, virtual interface и heap отсутствуют.

Первый добавленный объект образует основание, последний — верхушку. `dispatch()`
идёт в обратном порядке. `Unhandled` продолжает проход, `Consumed` и `Emitted`
останавливают его немедленно. Если никто не обработал вход, возвращается `pass()`.
Поддерживаются move-only выходные события.

`addContext()` сначала проверяет наличие объекта, затем заполненность. Поэтому
повторное добавление в полный стек возвращает `AlreadyPresent`, а не `StackFull`.
Добавление нового объекта сверх ёмкости ничего не меняет.

`releaseContext()` удаляет объект из любой позиции и компактирует массив с
сохранением порядка. `contains()` и операции удаления идентифицируют контекст через
его стабильный адрес, полученный `std::addressof()`.

### Время жизни контекстов

- Контексты заранее создаются и принадлежат приложению.
- `Router` не создаёт, не копирует, не перемещает и не уничтожает их.
- `releaseContext()` удаляет только невладеющую ссылку.
- Удалённый объект сохраняет состояние и может быть добавлен снова.
- Контекст обязан жить и оставаться по тому же адресу всё время нахождения в стеке.
- Перед уничтожением или перемещением объект необходимо удалить из `Router`.
- Изменение состава стека из `handle()` запрещено контрактом и отдельно в runtime
  не отслеживается.

### Файлы

- `lib/ContextInput/src/router.h` — публичный шаблон, enum-результаты, detection
  trait и закрытый `ContextRef`;
- `lib/ContextInput/src/router.ipp` — определения операций и type-erasure thunk;
- `lib/ContextInput/src/context_input.h` — экспорт `router.h`;
- `test/test_native/context_input/test_router.h` и `.cpp` — 16 тестов;
- `test/test_native/main.cpp` — регистрация тестов маршрутизатора.

### Проверки

- `make format` и `make format-check` — успешно;
- прямой `clang-tidy` нового заголовка — без замечаний к коду; ожидаемое
  предупреждение относится к анализу `#pragma once` как отдельной единицы;
- `make verify` — успешно;
- native-тесты — 45 из 45 прошли;
- отдельный smoke-тест публичного API ARM-компилятором — успешно;
- firmware `rpipico2` — успешно собрана;
- RAM: 165532 из 524288 байт, 31.6%;
- Flash: 660880 из 4190208 байт, 15.8%.

### Условия для этапа 4

- Адаптер энкодера создаёт `InputEvent<TSourceId>`, но сам не вызывает и не хранит
  `Router`.
- Сквозной тест может создать обычный контекст, добавить его в `Router` и явно
  передать результат адаптера в `dispatch()`.
- Проверять результаты `addContext()` и `releaseContext()`: оба метода помечены
  `[[nodiscard]]`.
- Не изменять семантику порядка, `Unhandled`, `Consumed` и `Emitted`.
- Не добавлять владение контекстами или динамическую память.
