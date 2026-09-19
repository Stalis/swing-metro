# Шаг 2.2. Принятие, pending delivery и состояние нот

Статус: запланировано. Зависит от шага 2.1.

## Цель

Сделать результат попытки отправки явным, не удалять временно непринятое событие и
обновлять accepted-состояние ноты только после принятия сообщения USB-стеком.

## Контекст после шага 2.1

Sequencer и `MidiEventQueue` создают typed messages с target/sequence, а USB adapter
кодирует их непосредственно перед `writePacket()`. При этом sink всё ещё возвращает
`void`, due-события удаляются до попытки, Start/Stop/Clock отправляются как fire and
forget, а `Sequencer::notifyNoteOnSent()`/`notifyNoteOffSent()` вызываются сразу.

Этап 1 различает deadline и attempt timestamp. Этот шаг должен сохранить оба и
добавить момент acceptance, не переименовывая старую попытку в доставку.

## Контракт отправки

Sink возвращает один из явно названных результатов:

- `Accepted` — локальный USB stack принял packet;
- `RetryLater` — сейчас packet не принят, но подтверждённого disconnect нет;
- `Disconnected` — adapter имеет отдельное достоверное свидетельство отсутствия
  USB connection/mount.

Ложь запрещена в обе стороны: false/zero от `writePacket()` сам по себе не называется
disconnect, если TinyUSB не даёт такого различения; отсутствие mount не называется
временным backpressure. `Accepted` не означает host delivery.

## Pending delivery

Ввести fixed-capacity pending/outbox path, общий для realtime и note messages.
Событие получает стабильные target, deadline и sequence identity до первой попытки.

- На `Accepted` head удаляется ровно один раз и вызывается semantic commit hook.
- На `RetryLater` head остаётся без изменения; pass прекращает попытки, чтобы не
  busy-wait и не переставить последующие сообщения.
- На `Disconnected` событие остаётся до политики шага 2.3 либо консервативно
  завершает текущую outbound session; результат не трактуется как acceptance.
- На одном `process()` pass выполняется не более восьми send attempts. Константа
  именуется и проверяется; accepted FIFO может использовать весь бюджет.
- Capacity фиксирована и документирована. До type-specific policies шага 2.3
  исчерпание capacity должно безопасно остановить transport/session, а не молча
  потерять событие или выделить память.

Scheduled queue и delivery outbox имеют разные роли: первая хранит ещё не due
музыкальные события, вторая — due, но ещё не accepted сообщения. Перемещение между
ними атомарно относительно capacity: нельзя удалить scheduled event, если outbox его
не принял.

## Accepted-состояние

`actualSoundingNote` означает локально accepted Note On, а не предполагаемое звучание
на удалённом синтезаторе. Оно меняется только после `Accepted` соответствующего
Note On/Off. Planned/projected state sequencer-а остаётся отдельным.

Start, Stop и Clock проходят через тот же result-aware sender. Диагностический
`outgoingInternalClockAttempts` этапа 1 сохраняет смысл попыток; новый acceptance
counter появится в шаге 2.4.

## Работа

1. Заменить `MidiPacketSink::send(void)` result-returning typed sink contract.
2. Реализовать TinyUSB mapping mount/write result без выдуманных причин.
3. Добавить fixed pending delivery с FIFO identity и бюджетом восемь попыток/pass.
4. Изменить due-drain на peek/transfer/commit семантику без потери при отказе.
5. Перенести note state notifications в `Accepted` commit path.
6. Провести Start/Stop/Clock через единый pending sender.
7. Добавить fake sink с программируемой последовательностью результатов.

## Детерминированные проверки

- `RetryLater, Accepted` даёт две попытки одного identity и один commit.
- Несколько `Accepted` обрабатываются FIFO, но не больше восьми за pass.
- `RetryLater` head блокирует следующий Off/On и не создаёт duplicate acceptance.
- Scheduled event не удаляется при невозможности переноса в полный outbox.
- Note On/Off меняют accepted note state только после `Accepted`.
- Rejected Start не позволяет более позднему Clock обогнать его; rejected Note Off
  не позволяет следующему Note On обогнать Off.
- Capacity exhaustion приводит к определённой безопасной остановке без heap/busy-wait.
- Успешный sink сохраняет bytes и ordering шага 2.1.

## Готовность шага

- Ни одно сообщение не считается accepted по самому факту попытки.
- Retry не дублирует accepted messages и сохраняет deadline/identity/order.
- Работа за pass и память строго ограничены.
- Выполнен `make verify`.

## Вне объёма

Окончательные type-specific overdue/drop/reconnect policies и полная diagnostics
schema выполняются в шагах 2.3–2.4. Hardware host capture — шаг 2.5.
