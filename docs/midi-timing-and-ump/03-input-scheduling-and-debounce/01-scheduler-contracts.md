# Шаг 3.1. Расписания input polling

Статус: выполнено 2026-09-20. Зависит от завершённого этапа 2.

## Цель

Разделить matrix/button polling и quadrature polling энкодеров на независимые,
wrap-safe расписания на core 0. Убрать зависимость частоты чтения от числа оборотов
`loop()`, не пытаясь фиктивными повторными scans компенсировать пропущенное время.

## Контекст текущей реализации

До этого шага `loop()` в `src/main.cpp` вызывал `buttonMatrix.readButtons()`,
обрабатывал edge/holding, затем вызывал `buttonMatrix.update()` и `update()` всех
трёх `Encoder` на каждом обороте. Это делало matrix debounce в
`lib/ButtonMatrix/src/button_state.cpp` и switch debounce в
`lib/Encoder/src/encoder.cpp` зависимыми от load-dependent числа вызовов.
`ButtonMatrix::readButtons()` выполняет один полный 4x4 scan с задержкой 5 us на
строку (`lib/ButtonMatrix/src/button_matrix.ipp`), а `Encoder::update()` читает оба
quadrature pins и switch в одном pass (`lib/Encoder/src/encoder.cpp`). MIDI polling и
`transportController.process()` продолжают исполняться на каждом loop pass; Step 3.1
period-gates только GPIO input passes.

## Принятые periods и encoder bound

- `MATRIX_SCAN_PERIOD_MS = 5`: полный matrix scan имеет четыре intentional 5 us
  row-settle delays, а 5 ms сохраняет отзывчивость UX. До шага 3.2 существующий
  four-sample debounce временно требует примерно 15 ms стабильности от первого
  observation и даёт до 20 ms общей input latency с учётом фазы scan; шаг 3.2 заменит
  эту зависимость от samples на time-based contract.
- `ENCODER_SAMPLE_PERIOD_US = 1_000`: для design bound предполагается максимум
  100 practically repeatable detents/s. Current decoder требует 4 quadrature
  transitions на detent, поэтому худший dwell одного состояния равен
  `1_000_000 / (100 * 4) = 2_500 us`.
- Допустимый actual encoder interval установлен в `1_250 us`: это запас 2x внутри
  2_500 us dwell. Nominal 1_000 us period оставляет ещё 250 us scheduling margin
  до bound. Это непроверенная design hypothesis, а не hardware claim: шаг 3.3 измерит
  actual interval, а шаг 3.4 примет или отклонит polling по hardware evidence.

Scheduler создаётся с compile-time period; valid period обязан быть nonzero и строго
меньше `2^31` единиц соответствующих часов. До `start(now)` он inert. `start()` ставит
первый deadline в `now + period`, поэтому setup не создаёт immediate или overdue pass.
`poll(now)` сравнивает deadline через unsigned subtraction в half-range `2^31`. При
late pass он выполняет один read и продвигает deadline на
`((now - deadline) / period + 1) * period` к первому будущему target; пропущенные
targets не вызывают повторных GPIO reads. Missed-deadline counter намеренно не добавлен:
его defined observability semantics относится к шагу 3.3 и не может означать lost edges.

## Точный контракт scheduler-а

- Matrix scan и encoder sample имеют отдельные именованные constants с явными
  единицами (`..._PERIOD_MS` либо `..._PERIOD_US`); один общий input period запрещён.
- Matrix period выбирается из UX/debounce requirements. Encoder period выбирается из
  документированного худшего физического сценария: максимальных detent/s, переходов
  quadrature на detent и запаса на один sample interval. До кода зафиксировать расчёт,
  выбранные значения и допустимый actual encoder interval в этом документе.
- Каждый scheduler получает `now` из соответствующих часов ровно один раз за pass.
  Проверка elapsed/deadline использует unsigned subtraction и корректна при wrap-around.
- Когда pass due, он выполняет ровно один актуальный физический scan/sample. При
  задержке N периодов scheduler учитывает пропуск, продвигает deadline к следующему
  будущему target и не запускает N чтений подряд.
- Нельзя переносить `transportController.process()`, USB MIDI polling или Serial command
  polling внутрь period-gated input pass; они остаются на каждом loop iteration.
- Matrix pass сохраняет текущий порядок: scan, dispatch press/release/holding для
  physical indices в порядке matrix, затем publication состояния. Encoder pass сохраняет
  порядок `tempo`, `swing`, `volume` из `UPDATABLES`.

## Работа

1. Добавлен минимальный hardware-independent scheduler в `src/input/` без framework,
   task queue или dynamic allocation.
2. Добавлены независимые matrix и encoder deadlines, инициализируемые в `setup()`.
3. Matrix handling и encoder `UPDATABLES` вынесены в отдельные single scheduled passes,
   сохраняя `handleButtonBatch()` и current routing.
4. Counter отложен до шага 3.3; scheduler resynchronizes deadline, но не заявляет
   восстановление GPIO transitions.
5. Этот документ фиксирует выбранные periods, calculation и design bound.

## Детерминированные проверки

- Due/not-due, first pass, delayed pass и wrap-around для каждого clock unit.
- Delayed pass вызывает один scan/sample и resynchronizes к будущему deadline без
  burst catch-up.
- Matrix и encoder имеют независимые deadlines: due одного не делает due другой.
- Кодовая проверка/integration test доказывает, что MIDI poll/process не period-gated.

## Готовность шага

- В коде есть два независимых scheduler-а с явными units и documented periods.
- Нет count-based предположения, что повторное чтение одного состояния восстанавливает
  пропущенные quadrature transitions.
- Выбранный encoder bound и метод его аппаратной проверки переданы шагу 3.4.
- `make verify` проходит.

## Вне объёма

Time-based debounce, input diagnostics export, IRQ/PIO implementation, изменение decoder
quadrature, перенос ввода между cores и изменение UX.
