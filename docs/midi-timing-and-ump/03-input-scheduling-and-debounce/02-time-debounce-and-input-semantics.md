# Шаг 3.2. Debounce по времени и семантика input timestamps

Статус: запланировано. Зависит от шага 3.1.

## Цель

Заменить debounce по числу samples на wrap-safe подтверждение устойчивого кандидата за
заданное время. Сохранить press/release, click, long press, Shift, context capture и
порядок событий.

## Контекст текущей реализации

`ButtonState::newState()` в `lib/ButtonMatrix/src/button_state.cpp` подтверждает state
после `debouncing + 1` identical samples. `Encoder::updateSwitch()` использует такой же
`_currentSwitchDebouncing` counter. В `src/main.cpp` matrix edge передаётся в
`StepButtonInputs::{onPressed,onReleased,update}` с `millis()`; switches вызывают
`ButtonInputAdapter` из encoder callbacks, также с `millis()`.

`StepButtonInputs` задаёт 500 ms threshold (`src/input/step_button_inputs.h`). Native
integration tests в `test/test_native/input/test_step_button_integration.cpp` фиксируют
click, long press, independent held-button timers и late release без промежуточного
`update()`. `ContextInput::ButtonInputAdapter` является владельцем текущей semantics
batch/event ordering; её callers и tests нужно прочитать перед изменением API.

## Точный временной контракт

- Raw observation timestamp — момент физического GPIO чтения matrix row или encoder
  switch. Он используется только для начала/перезапуска candidate stability interval.
- При новом raw state debounce state machine запоминает candidate и его observation
  timestamp. Пока raw state совпадает с candidate, elapsed считается unsigned
  subtraction в явно заданной единице.
- Stable press/release подтверждается первым observation, для которого candidate
  непрерывен не менее выбранного debounce duration. Event timestamp — timestamp этого
  confirmation observation, а не timestamp первого noisy edge.
- Если raw state вернулся к current stable state до confirmation, candidate отменяется;
  одно физическое действие не порождает лишние stable edges.
- Matrix и encoder switch имеют отдельно именованные debounce durations и единицы;
  их значения, rationale и relation к periods шага 3.1 должны быть внесены сюда.
- Long press отсчитывается от timestamp подтверждённого press. При release после
  threshold, когда periodic holding update отсутствовал, release path обязан сохранить
  существующий результат: сгенерировать long press и не сгенерировать click.
- Одновременные matrix events сохраняют existing physical-index dispatch order. Context
  routing, Shift и capture получают только подтверждённые input events.

## Работа

1. Вынести маленькую чистую timestamped debounce state machine в native-testable code;
   state не зависит от Arduino GPIO, allocation или wall-clock calls.
2. Перевести `ButtonState` и `ButtonMatrix` на неё, передавая timestamp каждого scan.
   Сохранить current matrix public edge/holding queries либо заменить их минимальным
   эквивалентом в одном изменении callers.
3. Перевести encoder switch path в `Encoder` на тот же contract, передавая timestamp из
   scheduled encoder pass; не менять active-low wiring, quadrature handler context или
   order switch/quadrature без тестового обоснования.
4. Передавать confirmation timestamps в `StepButtonInputs` и encoder switch adapters;
   обновить long-press path без смены threshold или UX policy.
5. Удалить obsolete 4-bit sample counters/settings после перевода всех callers.

## Детерминированные проверки

- Bounce press/release, устойчивые press/release, короткий импульс, нерегулярные calls,
  delayed confirmation и `uint32_t` wrap-around.
- Candidate reset при возврате raw state; нет multiple press/release для одного gesture.
- Long press ровно на threshold, до threshold, после threshold и поздний release без
  holding update.
- Existing `StepButtonInputs` cases: click toggles once, long press opens/reselects
  settings without toggle, two held steps have independent timers, Shift/context capture
  сохраняют outcomes.
- Encoder switch press/release исполняются один раз и идут в прежний adapters route.

## Готовность шага

- Debounce duration зависит от elapsed time, не от loop/sample count.
- Observation и confirmation timestamps, units и long-press basis однозначно
  документированы и покрыты native tests.
- Чистая logic зарегистрирована в `test/test_native/main.cpp`.
- `make verify` проходит.

## Вне объёма

Изменение long-press threshold, scheduler periods, physical encoder capture, IRQ/PIO,
новые input gestures и UI redesign.
