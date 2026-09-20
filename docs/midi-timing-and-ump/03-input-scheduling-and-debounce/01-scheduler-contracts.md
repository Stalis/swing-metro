# Шаг 3.1. Расписания input polling

Статус: запланировано. Зависит от завершённого этапа 2.

## Цель

Разделить matrix/button polling и quadrature polling энкодеров на независимые,
wrap-safe расписания на core 0. Убрать зависимость частоты чтения от числа оборотов
`loop()`, не пытаясь фиктивными повторными scans компенсировать пропущенное время.

## Контекст текущей реализации

`loop()` в `src/main.cpp` вызывает `buttonMatrix.readButtons()` на каждом обороте,
обрабатывает edge/holding, затем вызывает `buttonMatrix.update()` и `update()` всех
трёх `Encoder` (строки 533–560). Поэтому matrix debounce в
`lib/ButtonMatrix/src/button_state.cpp` и switch debounce в
`lib/Encoder/src/encoder.cpp` сейчас зависят от load-dependent числа вызовов.
`ButtonMatrix::readButtons()` выполняет один полный 4x4 scan с задержкой 5 us на
строку (`lib/ButtonMatrix/src/button_matrix.ipp`), а `Encoder::update()` читает оба
quadrature pins и switch в одном pass (`lib/Encoder/src/encoder.cpp`). MIDI polling и
`transportController.process()` должны продолжать исполняться на каждом loop pass.

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

1. Добавить минимальный hardware-independent scheduler/elapsed helper в `src/input/`
   либо иной native-testable module; не добавлять framework, task queue или dynamic
   allocation.
2. Добавить отдельное состояние deadlines для matrix и encoder в `src/main.cpp` и
   инициализировать его в `setup()` без немедленной серии overdue passes.
3. Вынести текущие matrix handling и encoder `UPDATABLES` в отдельные одинарные
   scheduled passes, сохранив вызовы `handleButtonBatch()` и current routing.
4. Определить, нужны ли scheduler missed-deadline counters для шага 3.3; если да,
   counter отражает deadline lateness, а не выдуманное число восстановленных GPIO edges.
5. Обновить этот документ точными chosen periods, расчётом encoder bound и причиной
   выбора до завершения шага.

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
