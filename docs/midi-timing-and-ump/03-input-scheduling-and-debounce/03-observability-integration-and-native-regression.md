# Шаг 3.3. Наблюдаемость, интеграция и native regression

Статус: запланировано. Зависит от шагов 3.1–3.2.

## Цель

Сделать actual encoder polling interval и scheduler delay наблюдаемыми, завершить
firmware integration и доказать repository-side contracts deterministic native tests.
Этот шаг не принимает решение, достаточен ли polling на физическом устройстве.

## Контекст текущей реализации

`src/main.cpp` уже экспортирует transport diagnostics только после quiescent stop
(`exportInternalTimingDiagnostics()`), с versioned `swing_metro_diagnostics_v3` schema.
`TransportDiagnostics` измеряет MIDI service/process lateness, но не GPIO input interval.
Host tooling в `scripts/pico_run_protocol.py`, `scripts/pico_serial_run.py` и
`scripts/pico_midi_run.py` распознаёт текущую diagnostics schema; менять его следует
только при добавлении export fields. Native runner требует явной регистрации каждого
test entry point в `test/test_native/main.cpp`.

## Контракт observability

- Encoder actual sample interval — разность timestamps двух последовательных реальных
  encoder GPIO sample passes; это не назначенный period и не scheduler deadline lateness.
- Хранить saturating boot-cumulative maximum actual interval и count interval выше
  bound шага 3.1. Первый sample после boot не образует интервал.
- Отдельный missed-deadline counter допустим только с определённым semantics из шага
  3.1; его нельзя использовать как число потерянных physical transitions.
- Diagnostics state не выполняет Serial I/O, allocation или application callbacks в
  input timing path. Export только в existing safe/quiescent context.
- Существующая `swing_metro_diagnostics_v3` остаётся строгой и не расширяется. Input
  fields требуют новой версии всей строки либо отдельного versioned prefix с exact
  field count; parsers сохраняют support старых v2/v3 fixtures.

## Работа

1. Добавить маленький hardware-independent input diagnostics model и wrap-safe update
   API, пригодный native tests.
2. Инструментировать один scheduled encoder sample pass в `src/main.cpp`; не добавлять
   `Serial.print()` или timing calls внутрь `Encoder::update()`.
3. Интегрировать matrix/switch timestamps шага 3.2 через весь путь до
   `AppInputCoordinator`, включая `handleProgramStorageEvent()` и current context stack.
4. При необходимости добавить новую версию diagnostics либо отдельную input-строку и
   обновить Python parser/summary/tests; не менять field count v3 и не смешивать GPIO
   maxima с MIDI acceptance metrics.
5. Добавить и зарегистрировать native unit/integration tests для scheduler, debounce,
   diagnostics и regression existing input semantics.

## Детерминированные проверки

- Actual interval max, threshold crossing, first sample, saturation и wrap-around.
- Scheduler missed deadline, если реализован, отделён от actual interval.
- Full matrix path сохраняет physical order, click/long press, Shift, context capture,
  storage modal routing и one-gesture-one-edge invariant.
- Tempo/swing/volume quadrature direction и switch routes сохраняют current dispatch
  paths (`handleEncoderDirection()` и `handleButtonBatch()`).
- Добавленная serial schema, если есть, проходит exact parsing и не трактуется как v2/v3
  schema иной версии.

## Готовность шага

- Репозиторий измеряет defined input metrics, а integration и all deterministic tests
  готовы к hardware run.
- Все новые чистые branches доступны native suite и зарегистрированы в runner.
- Документ содержит точный serial schema либо явно фиксирует, что export не менялся.
- `make verify` проходит.
- Шаг не утверждает, что polling выдержал hardware encoder bound; это исключительно
  результат шага 3.4.

## Вне объёма

Аппаратные claims, завершение Stage 3, IRQ/PIO choice или implementation, USB hardware
fault testing и новые UX features.
