# Шаг 1.2. Метрики обслуживания и событий

Статус: выполнено. Зависит от шага 1.1.

## Цель

Заменить неоднозначные `lateTicks`/`lateEvents` метриками с измеримым смыслом:
насколько редко обслуживается MIDI и насколько поздно код пытается отправить событие
относительно его deadline.

## Контекст текущей реализации

`TransportDiagnostics` содержит максимумы в микросекундах:
`maxServiceIntervalUs`, `maxInternalTickProcessingLatenessUs`,
`maxExternalTickProcessingLatenessUs`, `maxClockAttemptLatenessUs` и
`maxQueuedEventAttemptLatenessUs`. `droppedTicks` сохраняет число internal records,
discard-нутых после лимита обработки одного прохода; его прибавление насыщающее.

`TransportController::process()` получает один timestamp на весь вызов, извлекает
до `MAX_INTERNAL_TICKS_PER_PASS`, вызывает dispatcher, планирует новые события и
проверяет external clock. `MidiDispatcher::sendDue()` отправляет все события,
которые очередь считает наступившими, но явный deadline в микросекундах сейчас не
передаётся в sink.

На этом этапе `MidiPacketSink::send()` возвращает `void`, а
`UsbMidiPacketSink` игнорирует результат `writePacket()`. Поэтому здесь можно
измерить только назначенное время и локальный момент попытки. Успешное принятие
USB-стеком появится после изменения контракта на этапе 2.

## Определения

- **Начало обслуживания MIDI** — свежий `micros()` непосредственно после
  `midiClockReceiver.poll()` и перед `TransportController::process()`. Именно этот
  timestamp передаётся как `nowUs` и участвует в метриках одного прохода.
- **Интервал обслуживания** — wrap-safe разность между началами любых двух
  последовательных вызовов `process()`. Первый вызов после загрузки или явного
  сброса диагностики только устанавливает baseline; Start/Stop и clock mode его
  сами по себе не сбрасывают.
- **Назначенное время (deadline)** — начало транспортного тика плюс смещение,
  соответствующее сохранённой phase и периоду тика.
- **Опоздание попытки** — `max(0, attemptAtUs - deadlineUs)` в пределах временного
  горизонта шага 1.1.
- **Попытка отправки** — вызов текущего `MidiPacketSink::send()`. Это не принятие
  стеком и тем более не доставка хосту.

Нужно явно решить и записать, включаются ли Start/Stop/Continue и Clock в общую
метрику event lateness. Рекомендуемый минимум шага: отдельные максимумы для Clock и
очередных музыкальных событий; мгновенные transport-команды считать попытками без
искусственного deadline.

## Реализация

`process(nowUs, ticks)` фиксирует service baseline до любых возвратов, включая Stop,
режим без internal timing и storage. Пары за строгим горизонтом timestamp заново
устанавливают baseline. `handleExternal(event, observedAtUs)` принимает отдельное
локальное наблюдение; `main.cpp` получает его свежим `micros()` на каждый callback.

`MidiDispatcher` получает diagnostics от controller. Он сохраняет музыкальный порядок,
но различает `dueAtUs` (для catch-up) и `attemptAtUs` — локальное наблюдение времени
для batch, полученное в начале соответствующего `process()` или external callback.
Deadline очередного события вычисляется перед `send()` как `tickStartUs +
ceil(phase * periodUs / (PHASE_MAX + 1))`; абсолютный deadline в `MidiEvent` не хранится.
Direct internal F8 и queued F8 учитываются как Clock, остальные queued packets как
музыкальные события. Start/Stop/Continue и stop Note Off не имеют искусственного deadline.

Метрика сопоставляет deadline с ближайшим доступным локальным наблюдением для batch,
в котором вызывается `MidiPacketSink::send()`. Она не измеряет продолжительность
самого batch и не означает принятие USB-стеком, retry, backpressure или доставку
компьютеру. Более точное разделение попытки и принятия выполняется после изменения
sink-контракта на этапе 2.

## Детерминированные проверки

- Native tests покрывают service `1000, 1100, 1500`, включая Stop и storage.
- Internal и external record с разницей local observation `125` мкс дают точный максимум.
- До, на и после ceil phase deadline дают `0`, `0` и точное положительное значение;
  отдельная нецелая фаза отличает округление вверх от ошибочного округления вниз.
- Service, tick и queued Clock/event проверены через wrap-around; queued F8 остаётся
  перед Note On. Расстояние ровно `2^31` переустанавливает service baseline.

## Ограничения

USB acceptance, retries, backpressure, IRQ/alarm metrics, histogram and diagnostics
export/reset UI остаются вне этого шага.

## Проверки

- `make test`: 222/222 native-теста пройдено.
- `make verify`: пройдены format-check, clang-tidy, native-тесты и сборка `rpipico2`.
- Аппаратный прогон не выполнялся; численные граничные случаи проверены на
  детерминированных последовательностях времени.

## Вне объёма

Результат `writePacket()`, retries и USB backpressure относятся к этапу 2.
Гистограммы, LVGL/flush/input метрики и механизм выгрузки полной диагностики относятся
к этапу 5.
