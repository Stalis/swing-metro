# Стабилизация S. Live Tempo и модальный ввод

Статус: S.1 выполнен программно 2026-09-24; S.2–S.4 запланированы. Выполняется после
завершённого этапа 4 и до этапа 5.

## Цель

Устранить три обнаруженные на устройстве регрессии без изменения музыкальной модели:

- вращение Tempo во время internal playback не должно откладывать MIDI Clock и визуальный
  переход шага до остановки ручки;
- удержание Shift на Volume Encoder в Step Settings не должно открывать Save/Load;
- пользователь должен иметь явный безопасный способ закрыть Save/Load до запуска операции.

## Подтверждённые причины

`AdjustTempo` немедленно меняет BPM секвенсора, после чего `syncInternalAlarm()` вызывает
`PicoInternalTickAlarm::setBpm()`. Драйвер отменяет активный alarm, а
`InternalTickSource::setBpmAt()` назначает новый deadline как `now + newPeriod`. Каждый новый
detent снова отодвигает deadline; непрерывное вращение способно полностью лишить transport
тиков.

`GlobalContext` использует один Volume Encoder switch для двух фаз одного жеста: `Pressed`
активирует Shift, а `LongPressed` без учёта текущей страницы открывает Program Storage.
В Step Settings активный `ShiftContext` не забирает этот long-press, поэтому событие доходит
до global route.

Program Storage хранит только `Save`/`Load`. В состояниях `Action` и `Slot` контекст поглощает
весь посторонний ввод, а `CloseProgramStorage` создаётся только кликом после `Success` или
`Error`. Таким образом, до выполнения операции пути закрытия нет.

## Общие контракты

- Изменение BPM применяется на ближайшей безопасной Clock-границе и не переносит уже
  назначенный следующий тик на `now + period`.
- Live Tempo не создаёт burst догоняющих Clock, ранний Clock, сброс transport position или
  Start/Stop/Continue.
- Shift остаётся удерживаемым действием: `Pressed` активирует, `Released`/`Clicked`
  деактивирует. В Step Settings его `LongPressed` не имеет второго действия.
- Save/Load по long-press остаётся доступен на Main Display.
- Cancel является UI/navigation choice, а не `ProgramStorageAction`; он никогда не передаётся
  в `ProgramStorageController::perform()` и не вызывает LittleFS.
- Cancel доступен и при выборе действия, и при выборе слота. Во время `Busy` отмены нет;
  synchronous storage operation должна завершиться.
- Отмена закрывает modal с теми же transport semantics, что обычное закрытие: остановленный
  при входе transport самопроизвольно не запускается.
- Не читать и не писать LittleFS при работающем transport; не обращаться к LVGL с core 0.

## Последовательность

| Шаг | Документ | Результат |
| --- | --- | --- |
| S.1 | [Непрерывная смена Tempo](stabilization-runtime-input-modal/01-live-tempo-continuity.md) | **Выполнено программно:** BPM update сохраняет действующий alarm request и pending deadline без cancel/re-arm. |
| S.2 | [Арбитрация Shift long-press](stabilization-runtime-input-modal/02-shift-long-press-arbitration.md) | Step Settings удерживает Shift без открытия Save/Load; Main Display сохраняет shortcut. |
| S.3 | [Cancel в Program Storage](stabilization-runtime-input-modal/03-program-storage-cancel.md) | Action и Slot имеют безопасный Cancel без storage I/O. |
| S.4 | [Интеграционная и аппаратная приёмка](stabilization-runtime-input-modal/04-integrated-validation.md) | Все три пользовательских сценария и MIDI continuity подтверждены на устройстве. |

Шаги выполняются последовательно и по одному на ветку/коммит. S.1 меняет timing contract и
проверяется раньше UI-исправлений. S.2 фиксирует конфликт жестов до расширения модалки в S.3.
S.4 не меняет product semantics; допустимы только тестовый протокол, диагностика и малые
исправления, необходимые для воспроизводимой проверки.

## Общая проверка

- Для каждого software-шага выполнить его targeted native tests и `make verify`.
- Для S.1 добавить regression, моделирующий несколько BPM updates между двумя alarm
  callbacks: абсолютный pending deadline не меняется, а следующий период использует последний
  BPM.
- Для S.2/S.3 тестировать полные последовательности фаз кнопки через
  `AppInputCoordinator`, а не только отдельный context handler.
- В S.4 записать USB MIDI и internal timing diagnostics при вращении Tempo; отдельно вручную
  подтвердить экран шага, Shift и оба Cancel path.

## Вне объёма

IRQ/PIO follow-up энкодеров этапа 3, изменение debounce threshold, новый физический Back,
асинхронный LittleFS, возобновление transport после Cancel, redesign модалок, изменение swing,
Gate, UMP или MIDI 2.0.
