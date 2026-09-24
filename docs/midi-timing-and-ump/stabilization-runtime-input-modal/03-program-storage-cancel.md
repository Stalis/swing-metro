# Шаг S.3. Cancel в Program Storage

Статус: запланировано. Зависит от S.2 по порядку изменения общего input coordinator.

## Цель

Добавить явный Cancel, который закрывает Save/Load до запуска операции как с экрана выбора
действия, так и с экрана выбора слота, не обращаясь к LittleFS.

## Контекст текущего кода

`ProgramStorageAction` содержит только `Save` и `Load` и является аргументом
`ProgramStorageController::perform()`. Coordinator переключает action между двумя значениями,
после подтверждения переходит к Slot, а подтверждение слота ставит `Busy/pending`. Контекст
создаёт `CloseProgramStorage` только кликом в `Success`/`Error`; остальные события consume.
LVGL создаёт массив ровно из двух labels.

Расширять persistence action значением Cancel опасно: controller сейчас ветвится как
`Save`, иначе `Load`. UI navigation не должна становиться допустимой storage operation.

## Зафиксированная модель

- Ввести отдельный UI selection type для меню `Save`, `Load`, `Cancel` либо эквивалентную
  типобезопасную модель. `ProgramStorageAction` остаётся только `Save`/`Load`.
- Confirm `Cancel` в состоянии Action немедленно вызывает существующий close path.
- В состоянии Slot вращением доступна отдельная позиция `Cancel` наряду с 16 слотами.
  Confirm Cancel закрывает modal; confirm реального слота единственный создаёт pending operation.
- Ни один Cancel path не вызывает `ProgramStorageController::perform()`, backend read/write,
  autosave или изменение выбранной программы.
- В Busy ввод остаётся заблокированным. Success/Error закрываются существующим кликом.
- После Cancel transport остаётся остановленным, external clock state не возобновляется и
  modal/context/UI snapshot согласованно переходят в Closed.
- Повторное открытие начинает с `Save` и слота 0, независимо от прошлой отмены.

## Работа

1. Разделить UI menu selection и исполняемый storage action в domain/UI settings так, чтобы
   Cancel был непредставим для `perform()`.
2. Реализовать bounded/wrap либо clamp navigation для трёх action items и 17 slot choices;
   зафиксировать выбранную политику в тестах. Предпочтение: clamp и порядок `Cancel, 0..15`,
   чтобы отмена была достижима одним шагом назад от slot 0.
3. При подтверждении Save/Load сохранить executable action и перейти в Slot; при Cancel вызвать
   единый `closeProgramStorage()`.
4. Обновить UI snapshot и LVGL labels/layout. Cancel должен помещаться на дисплее без scroll и
   быть визуально выбран тем же способом, что Save/Load.
5. Дополнить coordinator, view-model и rendering/model tests; backend spy должен доказывать
   ноль операций при отмене.

## Детерминированные тесты

- Action navigation проходит Save, Load, Cancel с согласованным selection и highlight.
- Confirm Action Cancel закрывает modal и не вызывает controller.
- Save/Load переходят в Slot с правильным executable action.
- Из Slot 0 один шаг назад выбирает Cancel; confirm закрывает modal без pending/perform.
- Confirm реального слота по-прежнему выполняет ровно одну Save либо Load.
- Busy нельзя закрыть Cancel; Success/Error закрываются как раньше.
- Повторное открытие сбрасывает selection; capture источника не протекает в Main Display.
- UiViewModel публикует согласованный modal snapshot, LVGL не индексирует label arrays вне
  границ.

## Файлы в scope

`src/program/program_storage_modal.h`, `src/input/app_contexts.h`,
`src/input/app_input_coordinator.h`, `src/components/ui_view_model.h`,
`src/drivers/lvgl_ui.{h,cpp}`, relevant coordinator/UI/storage tests. Сам codec и slot store не
меняются.

## Готовность

- Cancel видим и достижим в обоих pre-operation states.
- Типы не позволяют передать Cancel в storage controller.
- `make verify` проходит; аппаратное UX-подтверждение относится к S.4.

## Вне объёма

Отмена уже начатой synchronous flash operation, undo Load/Save, autosave redesign, новый Back
button, восстановление playback и изменение формата программы.

