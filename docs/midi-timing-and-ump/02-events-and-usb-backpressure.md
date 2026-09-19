# Этап 2. Музыкальные события и надёжная USB-отправка

Статус: запланировано. Зависит от завершённого этапа 1.

## Цель этапа

Отделить музыкальные события от USB MIDI 1.0 кодирования и перестать считать вызов
`writePacket()` безусловно успешной отправкой. Добавить bounded delivery pipeline,
явные политики retry/просрочки/сброса и диагностику, сохранив музыкальную сетку,
порядок событий и отсутствие динамического выделения памяти в MIDI-пути.

Этап разбит на автономные последовательные шаги. Каждый документ содержит текущий
контекст, требуемую семантику, проверки, критерии готовности и границы задачи.

## Текущее состояние

- `MidiEventQueue` хранит `MidiUsbPacket`, а порядок Note Off/Note On/Clock определяет
  разбором USB status bytes.
- `Sequencer::scheduleThrough()` создаёт четырёхбайтовые USB-пакеты напрямую.
- `MidiPacketSink::send()` возвращает `void`; `UsbMidiPacketSink` игнорирует результат
  `Adafruit_USBD_MIDI::writePacket()`.
- `MidiDispatcher` удаляет due-события до вызова sink и сразу обновляет
  `actualSoundingNote`, даже если USB-стек не принял пакет.
- Start, Stop, Clock и аварийный Note Off отправляются напрямую, минуя единый
  delivery/backpressure path.
- Метрики этапа 1 описывают попытку F8, но не принятие сообщения USB-стеком.

## Шаги

| Шаг | Документ | Результат | Зависит от |
| --- | --- | --- | --- |
| 2.1 | [Типизированные события и USB-кодировщик](02-events-and-usb-backpressure/01-typed-events-and-usb-encoding.md) | Секвенсор и очередь больше не хранят USB-пакеты; прежние MIDI 1.0 bytes создаёт отдельный кодировщик. | Этап 1 |
| 2.2 | [Принятие, pending delivery и состояние нот](02-events-and-usb-backpressure/02-acceptance-and-pending-delivery.md) | Выполнено: Sink возвращает явный результат; непринятое событие остаётся pending, а состояние ноты меняется только после acceptance. | 2.1 |
| 2.3 | [Просрочка и жизненный цикл сессии](02-events-and-usb-backpressure/03-overdue-and-session-lifecycle.md) | Выполнено: bounded Clock/Note policies, idempotent Stop barrier, reserve и disconnect recovery. | 2.2 |
| 2.4 | [Диагностика и fault injection](02-events-and-usb-backpressure/04-diagnostics-and-fault-injection.md) | Все исходы наблюдаемы; fake sink детерминированно проверяет backpressure и бюджеты. | 2.3 |
| 2.5 | [Аппаратная проверка и baseline](02-events-and-usb-backpressure/05-hardware-validation.md) | Подтверждено отсутствие timing-регрессии и сопоставлены acceptance counters с host capture. | 2.1–2.4 |

## Общие инварианты

- Назначенная позиция события остаётся `TransportPosition {tick, phase}`. Deadline и
  sequence identity переживают retry и не пересчитываются от времени новой попытки.
- Для одинаковой позиции сохраняется порядок Clock → Note Off → Note On; связанный
  Note Off никогда не обгоняется следующим Note On.
- `Accepted` означает только принятие локальным USB-стеком. Это не подтверждение
  доставки хосту, DAW или синтезатору.
- Retry выполняется с фиксированным бюджетом попыток за `process()` pass. Нет
  busy-wait, блокирующего flush, динамической памяти, Serial или LittleFS в пути.
- Планируемое состояние секвенсора, accepted-состояние локального USB sender и
  неизвестное состояние удалённого синтезатора называются и учитываются раздельно.
- Confirmed disconnect не маскируется как временная занятость, но adapter не
  выдумывает причину, которой TinyUSB не позволяет различить.
- Входящий USB MIDI realtime path остаётся типизированным и не должен регрессировать.
- Stage 2 не меняет tick grid, swing, Gate или monophonic музыкальную семантику.

## Готовность всего этапа

- Все пять шагов выполнены, документы дополнены реализацией и проверками.
- Engine не зависит от CIN/USB packet layout; MIDI 1.0 encoder проверен отдельно.
- Fake sink покрывает success, RetryLater, sustained backpressure и disconnect.
- Непринятые сообщения не подтверждаются, accepted-события не дублируются, а
  связанные Off/On не переставляются.
- Clock не уходит неограниченной catch-up пачкой; Note Off не теряется молча; старый
  Note On не появляется после Stop или смены сессии.
- Все попытки, acceptance, retry, drop/cancel причины, high-water marks и acceptance
  lateness доступны в diagnostics.
- Выполнен `make verify` и аппаратный host capture без timing-регрессии относительно
  baseline этапа 1. Недоступные disconnect-проверки отмечены отдельно.

## Вне объёма

Gate Percent, регуляризация input polling, UMP transport, JR timestamps, MPE,
полифония и новые expressive-контроллеры.
