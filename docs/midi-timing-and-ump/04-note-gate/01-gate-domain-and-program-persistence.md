# Шаг 4.1. Домен Gate и persistence программы

Статус: запланировано. Зависит от завершённых этапов 1–2; Stage 3 IRQ/PIO follow-up не является блокером.

## Цель

Добавить Gate в domain model шага и сохранить его в Program без потери совместимости
со старыми слотами и current program. Gate — целое значение от 1 до 100, default 100.

## Контекст текущего кода

`src/engine/sequencer.h` содержит `SequencerStep` с enabled/note/velocity и API
note/velocity. `src/program/program.h` содержит зеркальный `ProgramStep`, `Program`,
constants и `isValid`. `src/program/program_runtime.{h,cpp}` переносит состояние через
`captureProgram()` и `applyProgram()` между sequencer и `Program`; `src/main.cpp`
подключает этот path через `ProgramStorageController` для user slots и current program.

`src/program/program_codec.cpp` использует container version 1, CRC и TLV payload.
`TAG_STEPS=16` кодирует ровно 16 шагов по три bytes. `TAG_STEP_GATE=17` уже зарезервирован;
`PROGRAM_CURRENT_PAYLOAD_SIZE` сейчас отражает старый payload, а максимальный payload
остаётся 255 bytes. `ProgramSlotStore`, `ProgramStorageController` и storage backend
обслуживают user slots и two-copy current-program recovery.

## Зафиксированные решения и контракты

- Добавить `gate` в `ProgramStep` и `SequencerStep`, default 100, с named min/max/default
  constants в program/domain boundary. `isValid(ProgramStep)` отвергает 0 и 101.
- Добавить clamp/read/adjust API sequencer-а, аналогичный текущему velocity API; public
  mutation не должна выпускать значение вне 1..100. Capture/apply копирует Gate полностью.
- `TAG_STEP_GATE=17` — отдельный TLV длиной 16 bytes, один Gate на каждый шаг. `TAG_STEPS`
  остаётся 3 bytes/step и не меняет layout.
- При отсутствии tag 17 decoder задаёт всем шагам 100. Duplicate tag, length не 16, Gate
  0 либо 101 возвращают существующую codec error (`MalformedTlv` для структуры, `InvalidField`
  для значения), не частично загруженную программу.
- Новый payload увеличивается с 62 до 80 bytes: 4 scalar TLV по 3, steps TLV 50 и gate TLV
  18. Version, header, CRC и лимит 255 не меняются.
- Кодек обязан round-trip Gate для default/current program, пользовательских slots и обоих
  copy paths; старый valid blob без tag 17 остаётся valid и получает 100.

## Файлы и API в scope

`src/engine/sequencer.{h,cpp}`, `src/program/program.h`, `src/program/program_codec.{h,cpp}`,
`src/program/program_runtime.{h,cpp}`, `src/program/program_slot_store.{h,cpp}`,
`src/program/program_storage_controller.{h,cpp}` и соответствующие существующие
`test/test_native/engine/` и `test/test_native/program/` files. `src/main.cpp` меняется
только при необходимости wiring, которой нельзя выразить через существующий
runtime/controller path. Если создаётся новый test module, добавить его entry point в
`test/test_native/main.cpp`.

## Последовательность работы

1. Ввести constants, поле и validation в Program/Sequencer; добавить минимальные
   `getStepGate`/`adjustStepGate` и включить значение в capture/apply.
2. Сформировать 16-byte gate array и append `TAG_STEP_GATE`; обновить current payload
   assertion до 80, сохранив порядок старых TLV и container format.
3. В decoder отслеживать один gate tag; при его отсутствии оставить defaults конструктора,
   при наличии валидировать длину и все значения до assignment результата.
4. Пройти load/save current program, user slots и двухкопийный recovery, чтобы Gate не
   потерялся в обход codec-а.
5. Расширить существующие program/sequencer tests; новый runner entry добавлять только
   при создании отдельного test file.

## Детерминированные проверки

- Default, clamp/read/adjust и invalid 0/101 в ProgramStep и SequencerStep.
- Codec round-trip 1/25/50/75/100, exact 80-byte payload и неизменный 3-byte layout steps.
- Legacy 62-byte payload без gate декодируется с 100 на всех 16 шагах.
- Duplicate gate tag, длина 0/15/17, 0 и 101 дают ожидаемый отказ; CRC, unsupported version
  и payload limit 255 сохраняют прежнее поведение.
- Save/load user slot, current program и выбор обеих copies восстанавливают Gate.

## Готовность шага

- Gate валиден, редактируем и переносится между Program и Sequencer.
- Старые программы совместимы, новые round-trip через все storage paths.
- Existing native suite и `make verify` проходят без изменения firmware/test semantics вне scope.

## Вне объёма

Scheduling, Note Off, launch identity, UI, изменение navigation packing и hardware run.

## Передача шагу 4.2

Передать named Gate contract 1..100/default 100, сохранённое значение на каждом
`SequencerStep` и подтверждённый codec/storage round-trip. Шаг 4.2 использует Gate только
для immutable launch snapshot, не меняя persistence format.
