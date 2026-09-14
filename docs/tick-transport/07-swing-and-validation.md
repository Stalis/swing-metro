# Этап 7. Swing через фазовый offset и сквозная проверка

Статус: реализовано в firmware и native-тестах. Зависит от этапов 1–6.

## Задача

Использовать уже готовый процентный `phase` для swing. Swing не создаёт вторую
сетку времени, не сдвигает MIDI Clock и не меняет transport tick/позицию шага.

## Контракт

- UI, runtime и новые программы принимают только `50..90`; `50` означает straight.
  `100` не выбирается и не планируется, потому что слился бы со следующим tick.
- Чистая функция `swingPhase(step, swing)` не получает BPM или время:

  ```text
  phase = 0,                                      если step чётный или swing = 50
  phase = floor(swing * 65536 / 100),             если step нечётный и swing = 51..90
  ```

  Поэтому `51`, `75` и `90` означают буквально 51%, 75% и 90% текущего tick-интервала.
  Шаги `1, 3, ..., 15` задерживаются; `0, 2, ..., 14` всегда имеют фазу 0.
- Только Note On получает swing phase. Note Off всегда остаётся в `(tick, 0)`.
  Для internal clock порядок общей границы остаётся `F8 -> Note Off -> Note On`;
  задержанный Note On отправляется позже в том же tick. Swing не изменяет F8,
  transport position, BPM или сетку 24 PPQN.
- `(tick, phase)` является музыкальной координатой. Изменение BPM или отфильтрованного
  external period меняет только физический deadline открытого tick; target уже
  поставленного в очередь пакета не меняется. Очередь immutable после enqueue.
- Sequencer только планирует look-ahead. `MidiDispatcher` остаётся единственным
  владельцем очереди, deadline и USB передачи.

## Владение звучащей нотой

- Dispatcher подтверждает Sequencer только после фактической передачи MIDI-пакета.
  Отправленный Note Off очищает совпадающую actual sounding note; отправленный Note On
  устанавливает её. Boundary сам по себе не подтверждает delayed Note On.
- Stop, reset, mode switch и external-clock loss очищают будущие queued события. Они
  отправляют ровно один Off только для ноты, уже подтверждённой как фактически звучащая.
  Если stop случился до delayed On, Off для неё не отправляется.

## Совместимость программ

- Кодек декодирует старые `Swing 91..100` и мягко нормализует их до `90`; такие
  программы не отклоняются и при следующем сохранении становятся каноническими.
- Новое кодирование и runtime validation принимают только `50..90`. Прямой вызов
  `applyProgram` также нормализует legacy `91..100` до `90` до применения.

## Полная матрица приёмки

- Native: покрыты mapping `50/51/75/90`, odd/even steps, phase-zero ordering,
  actual-state before/after delayed On, stop, legacy clamp, immutable target и
  retiming deadline при изменении периода. Тесты зарегистрированы в
  `test/test_native/main.cpp`.
- Hardware: ещё требуется подтвердить MIDI monitor/DAW и, если доступно, логическим
  анализатором на internal 40/120/240 BPM и с внешним master. Проверить literal 51/75/90%
  интервала, отсутствие outgoing F8 в External и безопасный clock loss.

## Вне объёма

Ratchet, независимый gate-length, humanize, per-step microtiming, DIN MIDI и
Song Position Pointer. Они могут переиспользовать `(tick, phase)` позже, но не
добавляются этой задачей.

## Результат

Кривая линейная и literal: значение UI является процентом текущего tick-интервала.
Верхняя граница `90%` оставляет 10% до следующего tick; hardware-измерения остаются
открытым пунктом приёмки.
