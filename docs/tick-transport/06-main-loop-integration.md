# Этап 6. Интеграция transport в приложение

Статус: запланировано. Зависит от этапов 1–5.

## Задача

Собрать новый путь в `main.cpp`, удалить старые параллельные вызовы и обеспечить
безопасные переходы Off/Internal/External, program-storage modal и USB I/O.

## Реализация

1. Ввести один `TransportController`/coordinator на main core. Он принимает
   опубликованные internal ticks или USB realtime events, вызывает sequencer,
   обслуживает deadlines и передаёт готовые packets тонкому USB sink.
2. Удалить из loop одновременное использование `Sequencer::update`,
   `MidiClockTransmitter::emitDueClocks` и прямых `midiSendNoteOn/Off` как
   независимых планировщиков. После миграции старые классы удалить либо сузить до
   адаптеров; не оставлять неиспользуемый второй источник clock.
3. При смене режима атомарно для main loop: отменить pending deadlines, закрыть
   активную ноту по правилу, сбросить неактивный источник и начать новый источник
   с определённой фазой. `Internal → Off/External` отправляет `Stop` только если
   outgoing internal MIDI session был активен.
4. При открытии program storage modal остановить transport тем же контроллером и
   запретить любые LittleFS операции, пока transport ещё running. При закрытии не
   возобновлять transport самовольно без явного пользовательского/external события.
5. Оставить обработку кнопок, storage и UI вне realtime-критического участка.
   Проверить, что получение нескольких USB пакетов в одном loop сохраняет их
   timestamped порядок.

## Проверки

Сквозные native-тесты контроллера: все режимы, live switch, modal, Start/Stop,
external loss, очередь и deadline. Firmware build обязателен. На устройстве
проверить отсутствие двойного `F8`, двойных Note Off, stuck-notes и старого clock
после перехода режима.

## Вне объёма

Новые UI-экраны и другие MIDI-протоколы.

## Передача результата

Указать удалённые legacy-paths, новый порядок loop и device-проверки.
