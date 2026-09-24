# Шаг 4.2. Deadline Gate и независимый Note Off

Статус: выполнено 2026-09-24. Зависит от шага 4.1.

## Цель

Вычислять точную назначенную позицию Gate Off от swung Note On и помещать независимые
On/Off в расписание. Это промежуточный scheduling scope, не финальная спецификация
overlap, cancellation или backpressure lifecycle.

## Контекст текущего кода

`src/engine/transport.h` задаёт `PHASE_MAX=65535`, `TransportPosition{tick,phase}` и
`swingPhase`; `phaseFromPercent(100)` возвращает `PHASE_MAX`, то есть не полную tick
duration. `Sequencer::scheduleThrough()` в `src/engine/sequencer.cpp` формирует swung
step-boundary requests с lookahead `SCHEDULING_LOOKAHEAD_TICKS=2`.

`MidiEventQueue`, `MidiPendingDeliveryQueue` и `TransportController` доставляют typed
MIDI events. Сейчас `MidiEventQueue::eventPrecedes()` применяет class priority Clock,
Off, On только при phase `0`, а на одинаковой ненулевой phase сохраняет insertion order.
Шаг обязан распространить этот priority contract на любую равную `TransportPosition`.
Сейчас boundary path также связан с состоянием одной ноты; его прежнее безусловное
завершение на прямой границе нельзя оставлять как источник Gate Off.

## Зафиксированные решения и контракты

- В phase units `PHASE_COUNT=PHASE_MAX+1=65536`. Для Gate вычислить в `uint64_t`:
  `durationUnits=floor(6*PHASE_COUNT*gate/100)`.
- Преобразовать назначенный `onPosition` в absolute units, прибавить `durationUnits`,
  затем нормализовать обратно в `tick` и `phase`. Gate 100 даёт ровно `+6 ticks`;
  Gate 1 всё ещё даёт положительную duration. Не использовать `phaseFromPercent(100)`.
- On и Off фиксируют note, velocity, gate/deadline и immutable `launchId`. На этом шаге
  `launchId` только correlation metadata пары; его cancellation, stale invalidation и
  lifecycle semantics принадлежат исключительно шагу 4.3.
- При одинаковой позиции dispatcher сохраняет Clock -> Off -> On. Duration всегда
  начинается от assigned swung On, не от time USB acceptance.
- Пересчитать queue capacity, reserved quotas и atomic enqueue для двух связанных events,
  включая wrap и lookahead. Нельзя enqueue только On либо только Off при capacity failure.

## Файлы и API в scope

`src/engine/transport.h`, `src/engine/sequencer.{h,cpp}`, `src/engine/midi_event*.h`,
`src/engine/midi_event_queue.h`, `src/engine/transport_controller.h` и связанные existing
native engine tests. Из 4.1 используется `SequencerStep::gate`; persistence, UI и final
pending-delivery policy не меняются в этом шаге.

## Последовательность работы

1. Добавить маленькую pure helper-функцию position arithmetic рядом с transport types,
   с явным `uint64_t` intermediate и normalisation tick/phase.
2. Snapshot-ить шаг и назначенный swung On position при launch, вычислять Off независимо
   от следующей step boundary и прикреплять один immutable launchId к обоим events.
3. Заменить boundary-only insertion на pair insertion; пересчитать maximum scheduled
   events/lookahead quota и сделать reservation/enqueue атомарными для пары.
4. Изменить ordering одинаковой `TransportPosition` так, чтобы Clock -> Off -> On
   выполнялся и при ненулевой phase; проверить wrap tick/phase без overflow.
5. Не добавлять итоговые cancellation/backpressure branches: передать наблюдаемые случаи
   и capacity assumptions шагу 4.3.

## Детерминированные проверки

- Formula для gate 1/25/50/75/100; 100 точно `on.tick+6, phase=on.phase`, включая swung On.
- Нормализация phase carry, tick wrap/lookahead boundary и отсутствие truncation `uint64_t`.
- Off может лежать между Clock ticks и после последнего шага перед переходом такта.
- Pair enqueue атомарен при каждом capacity/quota boundary; нет orphan On/Off.
- Equal deadline даёт Clock, Off, On; launchId совпадает в созданной паре.

## Готовность шага

- Все Gate deadlines используют зафиксированную формулу и independent scheduled Off.
- Capacity и reservation соответствуют двум событиям на launch, включая wrap/lookahead.
- Native suite и `make verify` проходят. Документ не заявляет final overlap/backpressure
  correctness до шага 4.3.

## Вне объёма

Identity-based stale cancellation, accepted/projected state, RetryLater ordering, terminal
policy, UI, filesystem и hardware validation.

## Передача шагу 4.3

Передать immutable launchId пары, absolute On/Off deadlines, atomic pair enqueue и точные
capacity/reserve значения. Шаг 4.3 становится единственным владельцем lifecycle и решает,
какие scheduled/pending events являются stale.

Фактический контракт после реализации: scheduled queue сохраняет capacity 16 и максимум
8 packets на tick, причём допускает не более 7 non-Clock packets, резервируя одно место
для Clock. Sequencer добавляет On/Off только атомарной парой с общим ненулевым `launchId`;
при отказе capacity либо tick quota его boundary остаётся retryable. Cancellation и stale
identity намеренно не реализованы до шага 4.3.
