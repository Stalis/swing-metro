# Шаг 2.1. Типизированные события и USB-кодировщик

Статус: запланировано. Зависит от завершённого этапа 1.

## Цель

Убрать USB MIDI 1.0 packet layout из секвенсора, музыкальной очереди и dispatcher-а,
сохранив текущие bytes, порядок, tick/phase и поведение успешной отправки.

## Контекст текущей реализации

`MidiEvent` и `MidiEventRequest` в `src/engine/midi_event_queue.h` содержат
`MidiUsbPacket`. Queue определяет priority разбором `packet[1]`/`packet[3]`.
`Sequencer::scheduleThrough()` вручную создаёт `{CIN, status, data1, data2}` для
Note On/Off. `MidiDispatcher` тем же способом распознаёт отправленную ноту и отдельно
создаёт Start/Stop/Clock через `usbMidiRealTimePacket()`.

Затронутые области:

- `src/engine/midi_event_queue.h` — payload и ordering;
- `src/engine/sequencer.{h,cpp}` — создание Note On/Off;
- `src/engine/transport_controller.h` — outbound dispatcher;
- `src/engine/midi_usb_packet.h` и `src/drivers/usb_midi_adapter.h` — USB boundary;
- `test/test_native/engine/test_midi_event_queue.cpp`,
  `test_transport_controller.cpp`, `test_sequencer.cpp` и runner регистрации.

## Требуемая модель

Ввести минимальный hardware-independent outbound message type только для реально
используемых сообщений:

- Note On: channel, note, velocity;
- Note Off: channel и note; текущий encoder сохраняет release velocity `0`;
- realtime Start, Continue, Stop и Clock.

Конкретное представление (`std::variant`, tagged union или эквивалент) выбирается в
плане реализации, но оно должно быть фиксированного размера, trivially movable либо
дёшево копируемым и не выделять память. Недопустимые channel/data bytes не должны
проникать в encoder; ограничения и способ валидации фиксируются тестами.

`MidiEvent` продолжает содержать target и sequence identity, но payload становится
типизированным. Priority определяется семантическим типом, а не USB status byte:
для одинаковой phase 0 Clock → Note Off → Note On → остальные поддержанные события;
внутри одного priority сохраняется insertion order. Для phase != 0 сохраняется
текущий sequence order.

Чистый MIDI 1.0 USB encoder преобразует typed message в прежний `MidiUsbPacket`:

- realtime: CIN `0x0F`, соответствующий `0xF8/FA/FB/FC`, нулевые data bytes;
- Note On: CIN `0x09`, status `0x90 | channel`;
- Note Off: CIN `0x08`, status `0x80 | channel`, release velocity `0`.

Encoder не знает transport, retry и состояние ноты. TinyUSB adapter кодирует только
на USB boundary. Inbound `MidiRealtimeEvent` можно оставить отдельным типом: широкое
объединение input/output моделей не требуется для готовности шага.

## Работа

1. Ввести outbound typed messages и семантические helpers классификации/priority.
2. Перевести `MidiEventQueue`, requests и sequencer scheduling на typed payload.
3. Перевести прямые Start/Stop/Clock и аварийный Note Off dispatcher-а на тот же
   typed message API, не меняя пока `void`-семантику sink.
4. Вынести USB MIDI 1.0 encoding в чистый тестируемый компонент и использовать его
   только в adapter-е.
5. Удалить разбор CIN/status из engine state tracking и queue ordering.
6. Не выполнять попутно retry/backpressure redesign: это шаг 2.2.

## Детерминированные проверки

- Каждый поддержанный typed message кодируется в точные прежние четыре bytes.
- Channels 0 и 15, notes/velocity 0 и 127 покрывают границы выбранного контракта.
- Queue ordering для одинаковых tick/phase совпадает с текущим Clock/Off/On порядком.
- Sequence order сохраняется для одинакового semantic priority и ненулевой phase.
- Batch enqueue остаётся атомарным при capacity/tick quota failure.
- Sequencer schedule создаёт те же target positions и Note payload при swing 50 и
  ненулевом swing; USB bytes проверяются только encoder-тестами.
- Start/Stop/Clock успешный path выдаёт те же host-visible сообщения.

## Готовность шага

- Engine headers, sequencer и queue не включают и не анализируют `MidiUsbPacket`.
- Единственное outbound USB MIDI 1.0 кодирование находится на adapter boundary.
- Существующая музыкальная семантика и все тесты сохранены.
- Выполнен `make verify`.

## Вне объёма

Результат `writePacket()`, retry, disconnect, overdue policy, новые diagnostics,
Gate, UMP и дополнительные MIDI message types.
