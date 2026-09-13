# Этап 6. Интеграция transport в приложение

Статус: запланировано. Зависит от этапов 1–5.

## Задача

Собрать новый путь в `main.cpp`, удалить старые параллельные вызовы и обеспечить
безопасные переходы Off/Internal/External, program-storage modal и USB I/O.

## Реализация

1. Ввести на main core один `TransportController`/coordinator с явным
   `MidiDispatcher`. Dispatcher — единственный владелец очереди и USB sink: он
   принимает опубликованные internal ticks или USB realtime events, первым в
   каждом проходе `loop()` открывает текущий tick, выпускает все due-пакеты и
   обслуживает процентные phase deadlines между ticks. Только после этого
   `Sequencer` пополняет очередь событиями на будущий горизонт. Он не отправляет
   MIDI и не извлекает пакеты из очереди.
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
6. Start — единственное bootstrap-исключение: coordinator просит секвенсор
   предварительно поставить горизонт, включающий tick 0, до первого обслуживания
   этого tick dispatcher'ом. Во всех остальных проходах сначала работает
   dispatcher, затем только пополнение будущих событий.
7. Reset coordinator выполняет строго как `queue.clear()`, затем `sequencer.stop()`
   с отправкой возвращённого actual Note Off, затем `sequencer.start()` и bootstrap
   tick 0. Отдельного `Sequencer::reset()` нет.

## Проверки

Сквозные native-тесты контроллера: dispatcher первым в loop, заполнение только
будущего горизонта, bootstrap Start, все режимы, live switch, modal, Start/Stop,
external loss, очередь и deadline. Firmware build обязателен. На устройстве
проверить отсутствие двойного `F8`, двойных Note Off, stuck-notes и старого clock
после перехода режима.

## Вне объёма

Новые UI-экраны и другие MIDI-протоколы.

## Передача результата

Указать удалённые legacy-paths, новый порядок loop и device-проверки.
