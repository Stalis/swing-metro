# Шаг S.1. Непрерывная смена Tempo

Статус: запланировано. Зависит от завершённых этапов 1, 2 и 4.

## Цель

Сохранить непрерывную сетку internal MIDI Clock при вращении Tempo. Быстрая серия изменений
BPM не должна бесконечно откладывать следующий tick, останавливать шаги секвенсора или
замораживать их отображение.

## Контекст текущего кода

`MainDisplayContext` создаёт `AdjustTempo`; `AppEventHandler` применяет весь encoder delta к
tempo counter и вызывает `Sequencer::setBpm()`. В конце каждого loop `syncInternalAlarm()`
сравнивает BPM и вызывает `PicoInternalTickAlarm::setBpm()`.

При активном источнике драйвер под critical section отменяет текущий Pico alarm, вызывает
`InternalTickSource::setBpmAt(bpm, time_us_32())` и ставит alarm заново. `setBpmAt()` меняет
generation и вычисляет `_nextDeadlineUs = appliedAtUs + periodUs`. Поэтому каждый detent до
callback переносит границу ещё на один полный период от нового `now`.

Источник уже умеет публиковать по абсолютной сетке, учитывать stale requests, пропускать
просроченные targets без burst и работать через wraparound `uint32_t`. Эти свойства должны
сохраниться.

## Зафиксированная семантика

- Если source активен, BPM update сохраняет текущий `_nextDeadlineUs`. Уже назначенный
  ближайший Clock публикуется в прежней абсолютной позиции.
- Новый BPM применяется к периоду, возвращаемому при этом callback, и ко всем последующим
  deadlines. Это boundary-safe изменение без частичного масштабирования текущего периода.
- Update меняет generation, чтобы отменённый request нельзя было принять как актуальный;
  драйвер переустанавливает alarm на тот же абсолютный deadline.
- Fractional remainder новой частоты начинается заново. Погрешность такого перехода меньше
  1 us и не накапливается при неизменном BPM.
- Если source не активен, update только запоминает BPM и не создаёт request.
- Повторная установка того же BPM является no-op на app/driver boundary и не должна создавать
  лишнюю generation.
- Update, пришедший после достижения deadline, не синтезирует Clock синхронно и не создаёт
  burst. Existing overdue callback policy остаётся единственным владельцем skip accounting.

## Работа

1. Заменить контракт `setBpmAt()` на явно названный boundary-preserving update. Удалить
   параметр времени, если он больше не нужен, и обновить driver call sites.
2. При active update сохранить pending deadline, инвалидировать старый request и вернуть
   актуальный request для повторного arm.
3. В `PicoInternalTickAlarm::setBpm()` отменять/re-arm только когда BPM действительно изменён;
   не расширять critical section и не выполнять внутри него I/O.
4. Сохранить existing diagnostics. Если для наблюдения tempo updates нужен новый counter,
   добавить bounded atomic counter и включить его в существующий snapshot/export; не печатать
   из callback.
5. Обновить unit tests и комментарии, которые сейчас закрепляют `now + period`.

## Детерминированные тесты

- Start 120 BPM назначает первый deadline как раньше.
- Несколько updates 121→180→240 до первого callback оставляют один и тот же deadline.
- Старые requests после каждой generation считаются stale и не публикуют Clock.
- Callback актуального request публикует один Clock; следующий deadline использует период
  последнего BPM, без burst.
- Update после нескольких callbacks сохраняет текущую pending boundary.
- Inactive update не создаёт alarm request; последующий start использует новый BPM.
- Wraparound deadline остаётся валидным.
- Overdue/missed-target и alarm-arm-failure tests сохраняют прежнюю семантику.

## Файлы в scope

`src/engine/internal_tick_source.h`, `src/drivers/pico_internal_tick_alarm.{h,cpp}`,
`src/main.cpp` только при необходимости адаптации API, `test/test_native/engine/test_internal_tick.cpp`
и отдельные driver seams/tests, если re-arm нельзя доказать существующим source test.

## Готовность

- Regression с серией tempo updates доказывает, что pending Clock нельзя отодвигать.
- Native suite, format, tidy и firmware build проходят через `make verify`.
- Hardware claim ещё не делается: он относится к S.4.

## Вне объёма

Сглаживание BPM, encoder acceleration, внешний MIDI Clock, изменение PPQN, swing, display
refresh, transport start/stop и полный аппаратный прогон.

