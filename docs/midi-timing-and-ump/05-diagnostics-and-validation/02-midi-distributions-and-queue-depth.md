# 5.2 MIDI distributions and queue depth

Статус: выполнено программно 2026-09-25. Аппаратная проверка не выполнена.

`swing_metro_diagnostics_v4` append-only расширяет v3; v2, v3, input v1 и runtime v1
не менялись. Host scripts строго проверяют prefix и точное число CSV полей.

## Lateness

Есть четыре cumulative-since-boot fixed-size distribution: Clock attempt, Clock accepted,
Note attempt и Note accepted. Attempt записывается непосредственно перед каждым
`sink.send()`, включая retry; accepted — только при `SendResult::Accepted`. Обе используют
исходный pending deadline. Transport messages не включены.

Каждое distribution содержит `early`, `on_time`, `unordered` и positive-us buckets:
`1..10`, `11..50`, `51..100`, `101..250`, `251..500`, `501..1000`, `1001..5000`,
`5001..20000`, `20001..100000`, `>=100001`. Классификация использует
`timestampReached`; modulo-2^32 wrap поддержан, расстояние ровно 2^31 учитывается как
`unordered`. Все counters saturate на `uint32_t`.

## Queue depth

v4 добавляет total current scheduled depth и scheduled high-water; существующие outbox
current/max fields повторно используются, новых дублирующих outbox counters нет. Producer
snapshot добавляет `observed_internal_tick_queue_depth`, observed high-water и explicit
overflow count. «Observed» означает bounded producer observation, а не race-free global
instantaneous maximum. IRQ work остаётся bounded и allocation-free.

Start/stop не сбрасывают эти diagnostics. В MIDI/IRQ paths нет Serial, filesystem,
allocation, locks, waits или logging.

## Excluded work

5.3 instrumentation-cost/reporting work, 5.4 load/fault scenarios и 5.5 hardware validation
явно не входят в 5.2.
