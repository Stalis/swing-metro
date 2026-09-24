# Шаг S.2. Арбитрация Shift long-press

Статус: выполнено программно 2026-09-24; аппаратная приёмка отложена до S.4. Зависит от S.1
только по порядку поставки; логически независимо.

## Цель

При удержании Volume Encoder в Step Settings Shift должен оставаться активным и никогда не
открывать Program Storage. На Main Display прежний long-press продолжает открывать Save/Load.

## Контекст текущего кода

`GlobalContext` обрабатывает Volume Encoder button: `Pressed` создаёт `ActivateShift`,
`Clicked`/`Released` — `DeactivateShift`, `LongPressed` — `OpenProgramStorage`. После Pressed
координатор добавляет `ShiftContext` поверх `StepSettingsContext`, но `ShiftContext` обрабатывает
только вращение Tempo Encoder и пропускает button phases. Поэтому LongPressed проваливается в
GlobalContext и открывает модалку даже при выбранном шаге.

`ShiftContext` уже получает ссылку на `selectedStep`, поэтому может решить конфликт, не добавляя
page state в GlobalContext и не меняя универсальный Router.

## Зафиксированная семантика

- При выбранном шаге и активном Shift `VolumeEncoder + LongPressed` поглощается
  `ShiftContext` без AppEvent.
- `Released` и `Clicked` продолжают проходить к GlobalContext, чтобы снять Shift.
- Вращение Tempo с активным Shift в Step Settings продолжает менять ноту октавами.
- На Main Display `selectedStep` отсутствует, поэтому тот же LongPressed проходит в GlobalContext
  и открывает Program Storage.
- Короткий Shift не открывает модалку и не меняет Gate/Volume сам по себе.
- Исправление не меняет capture semantics жеста, приоритет MIDI Clock modal и Program Storage
  modal или поведение физического `ShiftSwitch`, если он будет подключён позднее.

## Работа

1. Добавить узкое правило button arbitration в верхний контекст, владеющий активным Shift.
2. Не переносить весь Program Storage shortcut в Step Settings и не давать GlobalContext знание
   UI page.
3. Дополнить coordinator tests полной цепочкой Pressed → LongPressed → Released в двух режимах.
4. Проверить, что после поглощённого LongPressed Shift снимается release-событием и context stack
   возвращается к ожидаемому размеру.

## Детерминированные тесты

- Step Settings: Pressed активирует Shift; LongPressed не открывает storage; Released снимает
  Shift и оставляет Step Settings открытым.
- Во время этого удержания Tempo rotation создаёт octave `AdjustNote`, после release — обычный
  semitone `AdjustNote`.
- Main Display: Pressed активирует Shift; LongPressed открывает Program Storage по существующему
  contract и не оставляет ShiftContext в стеке.
- Program Storage/MIDI Clock modal продолжают consume-ить ввод согласно приоритету.
- Другие encoder buttons и step buttons не меняют поведение.

## Файлы в scope

`src/input/app_contexts.h`, при необходимости минимальная адаптация
`src/input/app_input_coordinator.h`, и `test/test_native/input/test_app_input_coordinator.cpp`.
Новые универсальные Router/Button APIs не требуются.

## Готовность

- Все обе последовательности жеста покрыты coordinator regression tests.
- `make verify` проходит.
- Ручная аппаратная проверка отложена до S.4.

## Фактический результат

`ShiftContext` при выбранном шаге поглощает только фазу `LongPressed` кнопки Volume Encoder.
`Pressed` по-прежнему активирует Shift через `GlobalContext`, а `Released` проходит туда же и
деактивирует его. При отсутствии выбранного шага событие LongPressed также проходит вниз и
сохраняет существующий shortcut открытия Program Storage.

Coordinator regressions воспроизводят настоящие batch-последовательности
`Pressed → LongPressed → Released`: в Step Settings модалка не открывается, октавный Shift
работает до release и затем возвращается semitone edit; на Main Display Save/Load открывается,
Shift снимается, а остаток жеста подавляется capture-механизмом. `make verify` проходит: 320
native tests, 14 script tests, clang-format, clang-tidy и firmware build для `rpipico2`.

## Вне объёма

Отдельная физическая кнопка Shift, изменение long-press threshold, перенос энкодеров на 74HC165,
новые Shift mappings и redesign context stack.
