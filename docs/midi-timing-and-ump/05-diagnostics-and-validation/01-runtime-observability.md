# 5.1 Runtime observability

Статус: выполнено программно 2026-09-25. Аппаратная проверка не выполнена.

## Назначение

После каждого internal timed run прошивка выдаёт bounded snapshot runtime-окна:

- `lv_timer_handler_count`, `lv_timer_handler_inclusive_total_us` и
  `lv_timer_handler_inclusive_max_us`;
- `display_flush_count`, `display_flush_inclusive_total_us` и
  `display_flush_inclusive_max_us`;
- window max и число превышений bound для выборки encoder.

`display_flush_*_inclusive_*` вложены в `lv_timer_handler_*_inclusive_*`.
Их totals нельзя складывать как независимую CPU work: flush уже входит в handler.
Длительности вычисляются unsigned subtraction `micros() - startedAtUs`, поэтому
безопасны при 32-bit wrap. Count и totals saturate на `uint32_t` max.

## Владение и публикация

`RuntimeTimingDiagnostics` не зависит от Arduino hardware и хранит только
32-bit atomics для request generation, acknowledgement, snapshot version и полей
snapshot. Core 1 является единственным writer/resetter локального окна. Он измеряет
handler и flush, а после завершённого handler pass публикует запрошенный snapshot и
сбрасывает локальное окно.

Core 0 при старте internal run запрашивает reset окна. После остановки он запрашивает
финальный snapshot; в последующих loop passes делает один неблокирующий read. После
ack печатаются diagnostics и только затем `swing_metro_control_v1,run_complete`.
В UI/flush critical path нет mutex, spin, busy wait, allocation или Serial. Export
запрещён при активном transport.

`EncoderSampleDiagnostics` сохраняет boot-cumulative `maxActualIntervalUs()` и
`intervalsAboveBound()` для `swing_metro_input_diagnostics_v1` без изменения.
Отдельный core-0 window snapshot-and-reset экспортируется в runtime row; sampling
остается непосредственно перед `pollEncoderInputs`.

## Export

Новая строгая строка CSV: `swing_metro_runtime_diagnostics_v1`. `pico_midi_run.py`
пишет её в `*-runtime-diagnostics.csv` и переносит значения в summary. Legacy
`swing_metro_diagnostics_v3` и `swing_metro_input_diagnostics_v1` не менялись.
`pico_serial_run.py` также сохраняет `*-runtime.csv`.

## Проверки

- Native Unity покрывает aggregation, nested/inclusive semantics, saturation,
  unsigned wrap, request/publish/read/reset и отсутствие double count.
- Native encoder tests покрывают window reset при сохранении cumulative counters.
- Python tests валидируют строгую runtime CSV schema и output path.
- `make verify`: 325 native Unity tests, 15 Python tests, clang-format, clang-tidy и firmware
  build для `rpipico2` прошли успешно.

## Связанные шаги

- 5.2 добавил MIDI lateness distributions и queue depth.
- 5.3 добавил safe reporting и A/B seam. Runtime totals являются inclusive measurements,
  не CPU utilization; фактическая instrumentation cost всё ещё требует парных Pico captures.
- 5.4: load/fault scenarios.
- 5.5: hardware validation.
