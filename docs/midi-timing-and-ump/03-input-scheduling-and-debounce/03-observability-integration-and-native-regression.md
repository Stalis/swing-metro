# Шаг 3.3. Наблюдаемость, интеграция и native regression

Статус: выполнено 2026-09-20. Зависит от шагов 3.1–3.2.

## Цель

Сделать actual encoder polling interval и scheduler delay наблюдаемыми, завершить
firmware integration и доказать repository-side contracts deterministic native tests.
Этот шаг не принимает решение, достаточен ли polling на физическом устройстве.

## Контекст текущей реализации

`src/main.cpp` уже экспортирует transport diagnostics только после quiescent stop
(`exportInternalTimingDiagnostics()`), с versioned `swing_metro_diagnostics_v3` schema.
`TransportDiagnostics` измеряет MIDI service/process lateness, но не GPIO input interval.
Input diagnostics хранятся отдельно от него. Native runner требует явной регистрации
каждого test entry point в `test/test_native/main.cpp`.

## Контракт observability

- Encoder actual sample interval — разность timestamps двух последовательных реальных
  encoder GPIO sample passes; это не назначенный period и не scheduler deadline lateness.
- Хранить saturating boot-cumulative maximum actual interval и count interval выше
  bound шага 3.1. Первый sample после boot не образует интервал.
- Missed-deadline metric не экспортируется. Interval count не означает число потерянных
  physical transitions.
- Diagnostics state не выполняет Serial I/O, allocation или application callbacks в
  input timing path. Export только в existing safe/quiescent context.
- Существующая `swing_metro_diagnostics_v3` остаётся строгой и не расширяется. В safe
  stopped/quiescent export после каждой v3 data row печатается отдельная strict schema:
  `swing_metro_input_diagnostics_v1,max_actual_encoder_sample_interval_us,encoder_sample_intervals_above_1250_us`.
  Header и data row имеют ровно три поля; data row имеет вид
  `swing_metro_input_diagnostics_v1,<uint32>,<uint32>`. Header печатается один раз,
  metrics boot-cumulative. Parsers сохраняют support старых v2/v3 fixtures.

## Работа

1. Добавлена header-only Arduino-independent model `EncoderSampleDiagnostics`. Первый
   sample, включая timestamp `0`, не создаёт interval; unsigned subtraction сохраняет
   wrap-safe actual interval. Maximum boot-cumulative, count saturating и увеличивается
   только при interval строго больше `1,250 us`.
2. Model вызывается ровно раз непосредственно перед `pollEncoderInputs(encoderNowUs)` в
   scheduled pass. Нет дополнительных clock reads, Serial, allocation или callbacks в
   input timing path и нет изменений scheduler-а либо `Encoder`.
3. `handleButtonBatch()` принимает явный `nowUs` и передаёт одно значение каждому событию
   batch, `AppInputCoordinator::dispatch()` и `handleProgramStorageEvent()`. Matrix
   передаёт scan timestamp `nowMs * 1'000U` modulo `uint32_t`; encoder switches передают
   scheduled confirmation timestamp. Quadrature direction сохраняет `micros()` route.
4. Host protocol имеет отдельные exact parser/header/is-data helpers. `pico_serial_run`
   пишет рядом `<stem>-input<suffix>`, а `pico_midi_run` пишет
   `<prefix>-input-diagnostics.csv` и добавляет только явно prefixed firmware input
   metrics в summary.
5. Native tests покрывают first sample, timestamp zero, max progression, strict bound,
   repeated crossings, saturation seam, `UINT32_MAX` и wrap-around; existing input
   integration regressions остаются зарегистрированными.

## Детерминированные проверки

- Actual interval max, threshold crossing, first sample, saturation и wrap-around.
- No missed-deadline metric exported; actual interval remains separate from scheduler policy.
- Full matrix path сохраняет physical order, click/long press, Shift, context capture,
  storage modal routing и one-gesture-one-edge invariant.
- Tempo/swing/volume quadrature direction и switch routes сохраняют current dispatch
  paths (`handleEncoderDirection()` и `handleButtonBatch()`).
- Input serial schema проходит exact parsing и не трактуется как v2/v3 schema иной версии.

## Готовность шага

- Репозиторий измеряет defined boot-cumulative input metrics, а integration и all
  deterministic tests готовы к hardware run.
- Все новые чистые branches доступны native suite и зарегистрированы в runner.
- Документ содержит exact separate serial schema; v3 header, data order, count, prefix
  и semantics не менялись.
- `make verify` проходит.
- Шаг не утверждает, что polling выдержал hardware encoder bound или hardware success;
  это исключительно результат шага 3.4.

## Вне объёма

Аппаратные claims, завершение Stage 3, IRQ/PIO choice или implementation, USB hardware
fault testing и новые UX features.
