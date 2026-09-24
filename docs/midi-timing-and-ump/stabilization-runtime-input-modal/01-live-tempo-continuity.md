# Шаг S.1. Непрерывная смена Tempo

Статус: выполнено программно 2026-09-24; аппаратная приёмка отложена до S.4. Зависит от
завершённых этапов 1, 2 и 4.

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
- Update не меняет generation и не переустанавливает alarm: pending request остаётся
  действительным, поскольку его deadline не меняется. Это исключает отдельную re-arm race около
  уже наступившей границы.
- Fractional remainder новой частоты начинается заново. Погрешность такого перехода меньше
  1 us и не накапливается при неизменном BPM.
- Если source не активен, update только запоминает BPM и не создаёт request.
- Повторная установка того же BPM является no-op и не меняет request.
- Update и alarm callback сериализованы producer critical section. Если update получает его
  первым, новый период применяется на ближайшей границе; если callback уже обработал границу —
  на следующей. Ни один вариант не отменяет Clock и не создаёт burst.

## Работа

1. Заменить `setBpmAt()` boundary-preserving update без параметра времени и обновить call sites.
2. При active update сохранить pending deadline и generation, оставив существующий аппаратный
   alarm действительным.
3. В `PicoInternalTickAlarm::setBpm()` только обновить source под существующим critical section;
   не cancel/re-arm alarm и не выполнять I/O.
4. Сохранить existing diagnostics. Если для наблюдения tempo updates нужен новый counter,
   добавить bounded atomic counter и включить его в существующий snapshot/export; не печатать
   из callback.
5. Обновить unit tests и комментарии, которые сейчас закрепляют `now + period`.

## Детерминированные тесты

- Start 120 BPM назначает первый deadline как раньше.
- Несколько updates 121→180→240 до первого callback оставляют один и тот же deadline.
- Request остаётся тем же и действительным после всей серии updates.
- Его callback публикует один Clock; следующий deadline использует период
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

## Фактический результат

`InternalTickSource::setBpmAt(bpm, appliedAtUs)` заменён на `updateBpm(bpm)`. При реальном
изменении обновляются BPM и fractional remainder, но текущие `generation` и
`_nextDeadlineUs` остаются неизменными. `PicoInternalTickAlarm::setBpm()` больше не отменяет и
не переустанавливает Pico alarm: update и callback используют прежний producer critical
section, поэтому на границе безопасно побеждает одна из двух операций, без потерянного Clock.

Native regressions подтверждают серию 121→180→240 BPM до одного callback, применение периода
последнего BPM, no-op одинакового значения, inactive update, wraparound и прежний учёт stale
requests/arm failures. `make verify` проходит: 318 native tests, 14 script tests, clang-format,
clang-tidy и firmware build для `rpipico2`.

## Вне объёма

Сглаживание BPM, encoder acceleration, внешний MIDI Clock, изменение PPQN, swing, display
refresh, transport start/stop и полный аппаратный прогон.
