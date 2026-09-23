# Шаг 4.4. Ввод и UI

Статус: запланировано. Зависит от шагов 4.1–4.3.

## Цель

Дать пользователю изменение и просмотр Gate выбранного шага без изменения глобальной
Volume semantics, Shift/modal priority или согласованности cross-core UI snapshot.

## Контекст текущего кода

`src/input/app_input.h` содержит `AdjustNote` и `AdjustVelocity`, но не `AdjustGate`.
`StepSettingsContext` в `src/input/app_contexts.h` направляет TempoEncoder в note,
SwingEncoder в velocity и сейчас consume-ит VolumeEncoder. В MainDisplay VolumeEncoder
остаётся global volume control; Shift, MIDI-clock modal и program-storage modal уже имеют
свои contexts/priorities в `AppInputCoordinator`.

`UiSettings` в `src/components/ui_view_model.h` несёт selected note/velocity. Его
`navigationPacked` использует все 32 бита: selected step/note, page, transport, shift,
selected velocity и MIDI-clock fields. `LvglUi::initStepSettingsScreen()` и update path
в `src/drivers/lvgl_ui.cpp` показывают selected step settings. Публикация использует
generation seqlock и `sameSettings` suppression.

## Зафиксированные решения и контракты

- Добавить `AdjustGate{int8_t delta}`. Только `StepSettingsContext` направляет encoder
  VolumeEncoder в это событие; в main display этот encoder по-прежнему меняет global Volume.
- Существующие Shift и modal contexts сохраняют приоритет над StepSettings: новый route не
  обходит capture и не меняет действия encoder switches/buttons.
- Добавить `selectedGate` в `UiSettings`. Нельзя расширять `navigationPacked`: он заполнен.
  Использовать отдельное atomic storage field, считываемое и записываемое внутри уже
  существующего generation seqlock, и включить его в `sameSettings` equality.
- LVGL Step Settings отображает явный Gate label/value в процентах и обновляет его только
  из snapshot. Main screen не получает новый Gate control.
- Команда обновляет `Sequencer::adjustStepGate(selectedStep, delta)`. По lifecycle contract
  шага 4.3 это меняет только future unscheduled launches; scheduled/pending/accepted launch
  не ретаймится.

## Файлы и API в scope

`src/input/app_input.h`, `src/input/app_contexts.h`, `src/input/app_input_coordinator.h`,
`src/main.cpp` event application и UI publication, `src/components/ui_view_model.h`,
`src/drivers/lvgl_ui.{h,cpp}`, а также relevant input, UI view-model и LVGL native tests.
Использовать существующий `Sequencer::adjustStepGate` из 4.1 и lifecycle contract 4.3.

## Последовательность работы

1. Добавить AppEvent и route VolumeEncoder только в StepSettingsContext; сохранить
   existing context stack/priorities без нового global shortcut.
2. Применить событие к selected sequencer step, получить Gate для UI publication и
   проверить, что main-display volume handler не меняется.
3. Добавить `selectedGate`, отдельный atomic и seqlock/equality/read/write integration;
   не трогать packed navigation layout.
4. Добавить Gate label/value в существующий Step Settings layout, сохранив mobile/board
   screen constraints и LVGL-only-on-UI-core ownership.
5. Дополнить existing tests routing, snapshot consistency и rendering/model updates.

## Детерминированные проверки

- VolumeEncoder в Step Settings выдаёт `AdjustGate`; Tempo/Swing продолжают note/velocity.
- VolumeEncoder в MainDisplay продолжает `AdjustVolume`; Shift и оба modal contexts имеют
  прежний priority/capture и не испускают Gate.
- Gate clamp 1..100 через UI path, selected-step switch и отсутствие mutation другого step.
- UiViewModel round-trip/`sameSettings` включает selectedGate; concurrent publication reader
  не получает смешанный snapshot в seqlock regression.
- LVGL model/screen показывает Gate label и значение; изменение Gate не ретаймит уже
  scheduled launch в integration test.

## Готовность шага

- Gate редактируется только в Step Settings, global Volume и context priorities не регрессируют.
- UI snapshot согласован и navigation word не переполнен; экран показывает Gate.
- Native suite и `make verify` проходят.

## Вне объёма

Новый физический control, UI redesign, live retiming, MIDI lifecycle changes, persistence
format и LittleFS operation во время playback.

## Передача шагу 4.5

Передать exact interaction: открыть Step Settings, VolumeEncoder меняет Gate 1..100%,
Tempo/Swing меняют note/velocity, main display сохраняет Volume. Hardware run проверяет
только future launches после edit и использует lifecycle expectations шага 4.3.
