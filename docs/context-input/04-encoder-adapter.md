# Этап 4. Адаптер энкодера

Статус: выполнено 2026-09-11.

## Задача для агента

Добавить необязательный адаптер, который переводит публичный результат существующей
библиотеки `Encoder` в универсальный `InputEvent`. Не менять алгоритм декодирования
энкодера и не интегрировать адаптер в приложение.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Прочитать передачи результата этапов 1–3.
- Изучить фактические `lib/Encoder/src/encoder.h` и `.cpp`.
- Проверить `lib/ContextInput/library.json` и публичные заголовки.

## Используемый интерфейс Encoder

Исходным контрактом является:

```cpp
enum class EncoderDirection : uint8_t {
    Clockwise,
    CounterClockwise,
    Left = CounterClockwise,
    Right = Clockwise,
    Undefined = 0xFF,
};
```

Адаптер не читает пины, не вызывает `Encoder::update()` и не распознаёт переходы
квадратуры. Он получает готовый `EncoderDirection` из существующего callback.

## Целевое поведение

```text
EncoderDirection::Left  / CounterClockwise -> EncoderInput{-1}
EncoderDirection::Right / Clockwise        -> EncoderInput{+1}
EncoderDirection::Undefined                -> no event
```

Алиасы перечисления имеют одинаковые значения, поэтому не создавать отдельные
ветви для `Left` и `CounterClockwise`.

Предпочтительная форма — небольшой объект, хранящий `SourceId`:

```cpp
EncoderInputAdapter<InputId> tempoInput{InputId::TempoEncoder};

std::optional<InputEvent<InputId>> event = tempoInput.translate(direction);
```

Допустим эквивалентный API с теми же свойствами. Адаптер только создаёт событие и
не хранит ссылку на `ContextInput`: dispatch остаётся явной операцией приложения.

## Изоляция зависимости

- Ядро `context_input.h` не должно подключать `encoder.h` или `Arduino.h`.
- Заголовок адаптера может подключать `encoder.h`.
- Если `encoder.h` мешает native-сборке только из-за использования `uint8_t`,
  допускается минимальный перенос `#include <Arduino.h>` из заголовка в `.cpp` с
  заменой в заголовке на стандартные C++ includes. Поведение и API `Encoder`
  изменять нельзя.
- Не добавлять обязательную зависимость Arduino всему ядру ради одного адаптера.

## Тесты

- Right создаёт ровно одно событие с `delta == +1` и правильным source.
- Left создаёт `delta == -1`.
- Clockwise и CounterClockwise соблюдают те же значения алиасов.
- Undefined возвращает отсутствие события.
- Адаптеры двух разных SourceId не смешивают идентификаторы.
- Созданное событие успешно проходит через тестовый стек и контекст этапа 3.

## Критерии готовности

- Адаптер использует существующий `EncoderDirection` как источник истины.
- В адаптере нет состояния аппаратного энкодера и динамической памяти.
- Ядро остаётся Arduino-independent.
- Существующий `Encoder` не меняет поведения.
- Все тесты и обе сборки проходят.

## Вне этапа

- Кнопка энкодера полностью игнорируется.
- Не менять callbacks и счётчики в `src/main.cpp`.
- Не добавлять acceleration, multiplier или накопление шагов.

## Передача результата

### Фактический публичный API

Адаптер находится в отдельном необязательном заголовке и не входит в umbrella
ядра:

```cpp
#include <adapters/encoder_input.h>

namespace ContextInput {

template <typename TSourceId>
class EncoderInputAdapter {
public:
    explicit constexpr EncoderInputAdapter(const TSourceId& source);

    [[nodiscard]] auto translate(EncoderDirection direction) const
        -> std::optional<InputEvent<TSourceId>>;
};

} // namespace ContextInput
```

`TSourceId` хранится по значению и должен быть copy-constructible, что проверяется
через `static_assert`. Адаптер не хранит `Encoder`, callback или `Router` и не имеет
изменяемого состояния.

Фактическое преобразование:

```text
Clockwise / Right         -> EncoderInput{+1}
CounterClockwise / Left  -> EncoderInput{-1}
Undefined                -> std::nullopt
любое неизвестное значение enum -> std::nullopt
```

Алиасы enum обрабатываются через канонические значения `Clockwise` и
`CounterClockwise`; дублирующих `case` нет.

### Изоляция зависимостей

- `lib/ContextInput/src/context_input.h` не подключает адаптер, `encoder.h` или
  `Arduino.h`.
- Пользователь явно подключает `<adapters/encoder_input.h>`.
- Адаптер подключает публичный `<encoder.h>` и использует `EncoderDirection` как
  единственный источник истины.
- Обязательная зависимость на Arduino или `Encoder` в `ContextInput/library.json`
  не добавлена.
- В native environment добавлен только `-I lib/Encoder/src`; несовместимая с
  native аппаратная реализация `encoder.cpp` не собирается.

### Изменения Encoder

`lib/Encoder/src/encoder.h` больше не включает `Arduino.h`: он использует
`<cstdint>`, `std::uint8_t` и `<optional>`. `lib/Encoder/src/encoder.cpp` теперь сам
явно включает `Arduino.h`, поскольку именно там вызываются `pinMode()`,
`digitalRead()` и используется `INPUT_PULLUP`.

Алгоритм квадратуры, callback-интерфейсы, debounce и поведение кнопки не менялись.

### Пример для этапа 5

```cpp
ContextInput::EncoderInputAdapter<InputId> tempoInput{InputId::TempoEncoder};

void tempoEncoderHandler(EncoderDirection direction) {
    const auto input = tempoInput.translate(direction);
    if (!input.has_value()) {
        return;
    }

    const auto result = router.dispatch(*input);
    if (result.hasEvent()) {
        handleAppEvent(result.event());
    }
}
```

На этапе 4 этот код в `src/main.cpp` не добавлялся.

### Файлы

- `lib/ContextInput/src/adapters/encoder_input.h` — адаптер;
- `lib/Encoder/src/encoder.h` и `.cpp` — перенос Arduino include к реализации;
- `platformio.ini` — include path переносимого `encoder.h` для native-тестов;
- `test/test_native/context_input/test_encoder_input_adapter.h` и `.cpp` — 8
  тестов;
- `test/test_native/main.cpp` — регистрация набора тестов.

### Проверки

- `make format` и `make format-check` — успешно;
- отдельный `clang-tidy` адаптера — без замечаний к новому коду; показаны только
  ранее существовавшие style warnings из `encoder.h` и ожидаемый `#pragma once`;
- native-тесты — 53 из 53 прошли;
- прямой ARM syntax smoke-test адаптера — успешно;
- `make verify` — успешно;
- firmware `rpipico2` — успешно собрана.

### Условия для этапа 5

- Создать по одному `EncoderInputAdapter<InputId>` для tempo, swing и volume.
- В callbacks передавать `EncoderDirection` соответствующему адаптеру, проверять
  `optional`, затем явно вызывать `Router::dispatch()`.
- `Undefined` не должен создавать dispatch-вызов.
- Кнопки энкодеров не входят в rotary adapter и пока не маршрутизируются.
- Не подключать `<adapters/encoder_input.h>` через общий `context_input.h`.
