# Этап 5. Интеграция энкодеров Swing Metro

Статус: выполнено 2026-09-11.

## Задача для агента

Подключить три существующих энкодера к `ContextInput`. После этапа весь поворот
энкодеров проходит через `InputEvent -> context stack -> AppEvent`, при этом
поведение tempo, swing и volume на главном экране остаётся прежним.

## Предусловия и чтение

- Прочитать `00-introduction.md` и этот документ.
- Прочитать передачи результата этапов 1–4.
- Изучить актуальные `src/main.cpp`, `lib/Encoder/src/encoder.h`,
  `lib/utils/src/utils/counter.h` и `src/engine/sequencer.h`.
- Не предполагать, что будущие экраны или кнопочные адаптеры уже существуют.

## Типы приложения

Определить вне универсальной библиотеки идентификаторы примерно такой семантики:

```cpp
enum class InputId : uint8_t {
    TempoEncoder,
    SwingEncoder,
    VolumeEncoder,
};
```

И смысловые события:

```cpp
struct AdjustTempo { int8_t delta; };
struct AdjustSwing { int8_t delta; };
struct AdjustVolume { int8_t delta; };

using AppEvent = std::variant<AdjustTempo, AdjustSwing, AdjustVolume>;
```

Разместить их в небольшом app-level заголовке, если это уменьшает ответственность
`main.cpp`. Не помещать эти имена в `lib/ContextInput`.

## Контекст главного экрана

Создать `MainDisplayContext`, который:

```text
TempoEncoder  + EncoderInput{delta} -> AdjustTempo{delta}
SwingEncoder  + EncoderInput{delta} -> AdjustSwing{delta}
VolumeEncoder + EncoderInput{delta} -> AdjustVolume{delta}
прочие события                          -> pass
```

Контекст возвращает события и не меняет `Counter`, `Sequencer`, `UiViewModel` или
LVGL напрямую.

Создать `ContextInput::Router` с фиксированной ёмкостью. Значение ёмкости должно быть
именованной compile-time константой с кратким обоснованием; начальное значение `8`
является разумным ориентиром, а не требованием оборудования.

На этом этапе в стек достаточно добавить `MainDisplayContext`. Пустой
`GlobalContext` заранее не создавать.

## Callback-путь

Три существующих callback энкодеров должны только:

1. Передать `EncoderDirection` соответствующему адаптеру.
2. Если адаптер создал `InputEvent`, вызвать `contextInput.dispatch()`.
3. Если результат содержит `AppEvent`, передать его единому app-level обработчику.

Обработчик `AppEvent` применяет signed delta к существующим `Counter`. Не обходить
их ограничения min/max. Изменение tempo продолжает вызывать
`mainSequencer.setBpm()` в том же логическом месте обработки.

Не использовать `std::function`. Из-за текущего `EncoderHandler` свободные функции
или минимальный статический bridge допустимы; не добавлять глобальный registry
адаптеров.

## Поведение кнопок энкодера

Кнопки не являются частью rotary-адаптера. На этом этапе существующие
`SwitchHandler` можно оставить без функциональных изменений или отключить, если
это требуется для чистой интеграции и явно отражено в diff. Не реализовывать их
смысловые действия.

## Тесты

Добавить native-тесты `MainDisplayContext`:

- каждый InputId создаёт правильный AppEvent;
- delta `-1` и `+1` сохраняется;
- неизвестный source или payload возвращает pass;
- контекст не меняет внешние объекты.

Проверить существующие Counter-тесты и firmware build. Если app-level обработчик
можно протестировать без Arduino, проверить изменение значений и clamp. Не делать
крупный рефакторинг приложения только ради теста.

## Аппаратная проверка

- Каждый энкодер меняет только свой параметр.
- Один физический detent даёт одно изменение.
- Быстрое вращение не меняет направление.
- Tempo по-прежнему применяется к Sequencer.
- GUI продолжает получать значения через `UiViewModel`.

Аппаратная проверка отмечается отдельно от автоматических тестов и не заменяется
успешной сборкой.

## Критерии готовности

- В rotary callbacks нет прямого изменения tempo/swing/volume.
- Все три поворота проходят через контекстный стек.
- Внешнее поведение главного экрана не изменилось.
- Кнопки, матрица и навигация не реализованы раньше времени.

## Передача результата

### Фактические типы приложения

Все app-level типы находятся в namespace `SwingMetro` и определены в
`src/input/app_input.h`:

```cpp
enum class InputId : std::uint8_t {
    TempoEncoder,
    SwingEncoder,
    VolumeEncoder,
};

struct AdjustTempo { std::int8_t delta; };
struct AdjustSwing { std::int8_t delta; };
struct AdjustVolume { std::int8_t delta; };

using InputEvent = ContextInput::InputEvent<InputId>;
using AppEvent = std::variant<AdjustTempo, AdjustSwing, AdjustVolume>;
```

Эти типы не входят в универсальную библиотеку и могут расширяться приложением на
следующих этапах.

### Контекст и применение событий

`SwingMetro::MainDisplayContext` находится в
`src/input/main_display_context.h/.cpp`. Он не хранит состояние и преобразует
`ContextInput::EncoderInput` согласно таблице из постановки. Другие payload и
неизвестные значения `InputId` возвращают `DispatchResult::pass()`.

`SwingMetro::AppEventHandler` находится в `src/input/app_event_handler.h/.cpp`.
Зависимости передаются именованной структурой со ссылками на три существующих
`Counter<std::uint8_t>` и `Sequencer`, чтобы однотипные счётчики нельзя было молча
перепутать в позиционных аргументах:

```cpp
struct AppEventHandlerDependencies {
    Counter<std::uint8_t>& tempo;
    Counter<std::uint8_t>& swing;
    Counter<std::uint8_t>& volume;
    Sequencer& sequencer;
};
```

Обработчик применяет всё signed-значение `delta` через `Counter::stepUp()` и
`Counter::stepDown()`, поэтому сохраняет существующее clamp-поведение. После
`AdjustTempo` он синхронно вызывает `Sequencer::setBpm()`; swing и volume не меняют
sequencer.

### Стек и callback-путь

В `src/main.cpp` заранее созданы один `MainDisplayContext`, три
`EncoderInputAdapter<InputId>` и
`ContextInput::Router<InputEvent, AppEvent, 8>`. Ёмкость `8` оставляет место для
главного, глобального, экранных и временных контекстов без динамической памяти.
Главный контекст добавляется один раз в `setup()`.

Общий шаблонный bridge использует согласованный префикс `T` для типа адаптера:

```text
Encoder callback
  -> EncoderInputAdapter::translate(direction)
  -> Router::dispatch(input)
  -> AppEventHandler::handle(event)
  -> Counter и, только для tempo, Sequencer
```

При `EncoderDirection::Undefined` адаптер возвращает `std::nullopt`, поэтому ни
dispatch, ни app-level обработчик не вызываются. В rotary callbacks больше нет
прямых `stepUp()`/`stepDown()`. Существующие callbacks кнопок энкодеров оставлены
без функциональных изменений и в маршрутизацию пока не включены.

### Native-сборка

Для проверки app-level слоя native environment теперь собирает только переносимые
части `src`: `engine/sequencer.cpp` и каталог `input/`. Аппаратный `src/main.cpp` и
Arduino-драйверы в native-тест не попадают.

### Файлы

- `src/input/app_input.h` — идентификаторы физических источников и смысловые
  события;
- `src/input/main_display_context.h/.cpp` — преобразование входов главного экрана;
- `src/input/app_event_handler.h/.cpp` — применение смысловых событий;
- `src/main.cpp` — экземпляры стека и адаптеров, bridge и подключение callbacks;
- `platformio.ini` — выбор переносимых app-level исходников для native-тестов;
- `test/test_native/input/` — тесты контекста, обработчика и полного encoder path;
- `test/test_native/main.cpp` — регистрация новых наборов тестов;
- `compile_commands.json` — обновлённая база команд сборки для статического анализа.

### Проверки

- `make format` и `make format-check` — успешно;
- native-тесты — 67 из 67 прошли;
- `clang-tidy` в составе `make verify` — завершён успешно; оставшиеся сообщения
  относятся к ранее существующим style-предупреждениям проекта и SDK;
- firmware `rpipico2` — успешно собрана;
- RAM: 165620 из 524288 байт (31.6%);
- Flash: 661488 из 4190208 байт (15.8%).

Пользователь подтвердил аппаратную проверку 2026-09-11: энкодеры работают на
устройстве, поворот энкодера tempo увеличивает tempo. Детальные проверки каждого
пункта из списка выше отдельно не протоколировались.

### Условия для этапа 6

- Расширять существующие `SwingMetro::InputId` и `SwingMetro::AppEvent`, не вводя
  параллельную модель событий.
- `MainDisplayContext` уже принимает общий `InputEvent`; кнопочные payload могут
  быть добавлены в него или пропущены вниз согласно новой семантике.
- Кнопка энкодера остаётся отдельным физическим источником и не должна становиться
  частью `EncoderInputAdapter`.
- App-level обработчики не должны попадать в `lib/ContextInput`.
- Стек уже имеет ёмкость `8`; менять её для первого кнопочного адаптера не требуется.
