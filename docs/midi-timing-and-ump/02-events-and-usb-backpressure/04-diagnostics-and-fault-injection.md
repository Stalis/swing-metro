# Шаг 2.4. Диагностика и fault injection

Статус: выполнено. Зависит от шага 2.3.

## Цель

Сделать каждый исход delivery pipeline наблюдаемым и доказать policies этапа 2
детерминированным fake sink без изменения аппаратного timing path.

## Контекст после шага 2.3

Outbound messages типизированы, USB encoding изолирован, delivery использует
fixed-capacity outbox и ограниченный budget, а session policy определяет retry,
coalescing/cancel, Stop и disconnect recovery. Метрики этапа 1 всё ещё описывают
alarm, tick producer/consumer и попытки F8, но не acceptance/outbox.

## Diagnostics contract

Добавить saturating counters/maxima как минимум для:

- всех send attempts и `Accepted` по классам Clock, realtime transport и notes;
- `RetryLater` и `Disconnected` результатов;
- событий, accepted после одного или нескольких retry;
- Clock coalesced/expired;
- Note On cancelled по Stop/mode/storage/session reset;
- Note Off/Stop, которые невозможно доставить из-за confirmed disconnect;
- outbox capacity failure и safety-stop;
- current/max outbox depth и max attempts per process pass;
- max lateness deadline → первая attempt и deadline → acceptance;
- session generations/resets, если это помогает проверить recovery без log spam.

Названия должны отличать attempt, stack acceptance и host observation. Счётчики не
пишутся из Serial/IRQ и не используют dynamic allocation.

## Serial schema и host tools

Текущая `swing_metro_diagnostics_v2` фиксирована данными этапа 1. Новые колонки не
добавлять молча в v2: ввести versioned schema v3 либо отдельную delivery-строку с
явным prefix. Обновить `scripts/pico_run_protocol.py`, `pico_serial_run.py` и
`pico_midi_run.py`, сохранив чтение старых v2 fixtures там, где оно требуется.

Hardware summary должна отдельно показывать:

- firmware Clock attempts;
- firmware stack-accepted Clock;
- host-received Clock;
- разницу attempt → accepted и accepted → host.

Последняя разница является сравнением счётчиков одного run, а не доказательством
конкретной причины без ordinal trace.

## Fault-injection model

Fake sink получает заранее заданную либо stateful последовательность результатов и
записывает attempt identity/order. Он должен моделировать:

- immediate success;
- единичные и повторные `RetryLater`;
- sustained backpressure до coalescing/capacity policy;
- disconnect до/после accepted Note On;
- recovery/new Start;
- отказ конкретного Off, On, Clock, Start и Stop.

Тестовые часы передаются явно. Нельзя использовать wall clock, sleep или вероятностные
сценарии в native tests.

## Работа

1. Добавить diagnostics state и saturating helpers вне driver-specific кода.
2. Инструментировать единственную точку result handling и все policy drops/cancels.
3. Версионировать Serial schema и обновить Python parsing/summary/tests.
4. Расширить fake sink и написать таблицу fault-injection сценариев.
5. Проверить invariants через balance equations для каждой session, учитывая
   accepted, pending и каждую именованную причину удаления.
6. Задокументировать, какие maxima cumulative с boot и какие reset/scoped к run.

## Детерминированные проверки

- Каждый scripted sink result увеличивает ровно свой counter.
- Retry → accepted сохраняет identity и увеличивает accepted ровно один раз.
- Coalesced Clock, cancelled On и abandoned Off попадают в разные причины.
- High-water mark и attempts/pass достигают ожидаемых границ, но не превышают их.
- Acceptance lateness считается от исходного deadline через wrap-around.
- Diagnostics balance проходит для success, retry recovery, Stop и disconnect.
- Старые v2 строки читаются как v2; новые v3 не ошибочно интерпретируются как v2.
- Python monitor summary различает attempt, accepted и host Clock.

## Готовность шага

- Нет удаления/отмены outbound event без наблюдаемой причины.
- Fake sink покрывает все result и lifecycle branches этапа 2.
- Serial export не выполняется при active transport и versioned однозначно.
- `make verify` включает host-side Python tests и проходит полностью.

## Вне объёма

GPIO ordinal trace, статистика UI/input, Gate-specific Off deadlines и UMP counters.

## Реализация и проверка

- `TransportDiagnostics` хранит boot-cumulative saturating delivery counters по Clock,
  Transport и Note: attempts/Accepted/RetryLater/Disconnected/retry-recovered и maxima
  deadline-to-first-attempt/acceptance. `currentOutboxDepth` и current generation — snapshots;
  остальные delivery maxima/counters не сбрасываются при Start/Stop.
- `MidiDeliveryAttempt` передаёт sink только immutable message, delivery identity, generation,
  target/deadline и ordinal; очередные internals не выходят за границу sink. Один result path
  сохраняет `attempts = accepted + retryLater + disconnected` для каждого класса.
- Очереди остаются generic и возвращают fixed class summaries. Diagnostics различает successful
  scheduled creation, scheduled-to-outbox transfer, named scheduled/pending invalidation и
  acceptance. Проверяемые балансы: `created = scheduledDepth + transferred + scheduledRemoved`
  и `outboxInserted = outboxDepth + accepted + pendingRemoved` (coalesce/expiry учитываются
  отдельными named counters).
- Firmware экспортирует только strict `swing_metro_diagnostics_v3` после полной остановки
  transport; v3 начинает payload с неизменённых v2 columns и добавляет deterministic flattened
  delivery/session fields. Host parsers принимают exact v2/v3 prefix+field count и никогда не
  трактуют v2 как v3. Hardware capture и claims остаются шагом 2.5.
- Проверка: native fake sink покрывает identity retry, class result counters, lateness, pass
  budget и local balance; Python tests покрывают v2/v3 schema/header/summary compatibility.
