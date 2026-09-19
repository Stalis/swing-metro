# Шаг 1.5. Аппаратное расследование и baseline

Статус: выполнено 2026-09-19. Consumer discard race исправлен политикой без
discard active backlog-а; аппаратная проверка на 68 и 240 BPM завершена.
Зависит от шагов 1.1–1.4.

## Цель

На реальном Pico 2 W проверить исправление ранней отправки и абсолютное расписание,
локализовать редкие двойные Clock-интервалы по счётчикам пути тика и получить
сопоставимый baseline перед этапом 2.

## Исходные данные

Доступны две host-side записи:

- `data/midi-test-free.mmon`: 234,63 с, 68 BPM, swing 50, без взаимодействия;
  6347 Clock, 33 интервала 73,35–73,68 мс. Обычный средний интервал 36,7783 мс
  против расчётных 36,7647 мс. Clock → Note On: медиана 331 мкс, максимум
  2,785 мс.
- `data/midi-test-loaded.mmon`: выбранный стационарный участок 33,17 с, около
  100 BPM, редактирование velocity/нот; 1321 Clock, 5 интервалов 50,14–50,37 мс,
  обычный средний интервал 25,0191 мс. Финальный swing не подтверждён.

Это timestamps приёма на host. Они могут показать наблюдаемый провал, но сами по
себе не различают поздний alarm, задержку main loop, очередь USB и поведение host.
Во всех выбранных участках соседние Note On разделены шестью записанными Clock, что
согласуется с паузой продвижения транспорта, но не доказывает конкретную причину.

В free отклонение сетки между первым и последним Clock около 1,300 с. 33
дополнительных периода объясняют около 1,213 с, остаток 86,5 мс нельзя полностью
приписывать прошивке без сопоставления часов.

## Диагностика прошивки

После предыдущих шагов и этого изменения:

- dispatcher не отправляет phase-события, если `nowUs` раньше начала тика;
- известны max service interval, tick processing latency и event attempt lateness;
- есть согласованные счётчики callback, publish, overflow, pop, discard и Clock
  attempt, callback interval/lateness, ошибки постановки alarm, backlog после budget
  и длительность `process()`;
- alarm следует абсолютной сетке и имеет bounded overdue policy.

`Serial` инициализируется как CDC на 115200 бод. После каждого перехода internal
timing из active в inactive `syncInternalAlarm()` сначала вызывает
`internalTickAlarm.stop(...)`, который отменяет/дренирует producer, и только затем
печатает snapshot на main core. Во время internal timing, IRQ и MIDI send path печати
нет. Header печатается один раз за boot, затем одна data-строка на такой Stop.

CSV schema v2, в порядке колонок:

```text
swing_metro_diagnostics_v2,alarm_callback_invocations,synchronous_start_publication_attempts,successful_publications,failed_publications,tick_queue_overflows,stop_discards,mode_switch_discards,storage_discards,alarm_arm_failures,stale_alarm_callbacks,stale_alarm_arm_failures,missed_scheduled_targets,out_of_horizon_alarm_callbacks,max_actual_callback_interval_us,max_callback_lateness_us,successful_consumer_pops,budget_discards,outgoing_internal_f8_attempts,max_service_interval_us,max_internal_tick_processing_lateness_us,max_external_tick_processing_lateness_us,max_f8_attempt_lateness_us,max_queued_event_attempt_lateness_us,max_internal_ticks_popped_per_process_pass,internal_tick_budget_reached_passes,max_remaining_internal_ticks_after_budget_pass,max_process_duration_us
```

Каждая data-строка начинается с `swing_metro_diagnostics_v2` и содержит значения в том
же порядке; v1 и v2 не смешивать в одной таблице. `failed_publications` и
`tick_queue_overflows` обозначают один и тот же
случай: producer не смог записать в полный tick queue, поэтому их значения должны
совпадать. `outgoing_internal_f8_attempts` и значения lateness относятся к попыткам
вызвать USB MIDI sink, не к принятию USB-стеком или доставке host.

`budget_discards` сохранён для сравнения старых запусков, но active internal path v2
не должен его увеличивать: backlog остаётся для следующего `process()` pass.
`max_remaining_internal_ticks_after_budget_pass` — consumer-side snapshot сразу после
pass с четырьмя pop; producer может опубликовать следующий tick сразу после снимка.
`max_process_duration_us` измеряет только вызов `TransportController::process()` по
двум timestamp, переданным main core, и не меняет смысл `max_service_interval_us`.
В internal output mode `outgoing_internal_f8_attempts == successful_consumer_pops`.

Все counters cumulative с boot. Перед каждым измеряемым прогоном нужно reboot Pico;
не сравнивать строки, полученные после нескольких запусков без reboot.

В quiescent snapshot после Stop проверяется точный баланс:

```text
successful_publications == successful_consumer_pops + budget_discards + stop_discards + mode_switch_discards + storage_discards
```

`successful_publications` включает успешные publication из callback и синхронный
Start. Не выводить число публикаций из `alarm_callback_invocations`: stale и
out-of-horizon callbacks не публикуют tick.

## Ограничения hardware alarm

Сравнение 32-bit timestamp определено только для расстояния `< 2^31` мкс. Драйвер
привязывает ближайший будущий modulo deadline к 64-bit Pico boot time; запрос вне
этого горизонта деактивируется и требует явного Start вместо догадки о направлении
wrap. Отмена alarm не отменяет уже начавшийся IRQ, поэтому id и generation всегда
проверяются; stale callback не может публиковать тик или поставить новый alarm.
Pico alarm не гарантирует отсутствие callback latency, но latency не сдвигает
музыкальную сетку и измеряется отдельным diagnostics counter.

Если какого-то сигнала нет, не подменять его предположением по host MIDI log:
вернуться к соответствующему шагу и добавить минимальное измерение.

## Захват и A/B протокол

Перед каждым прогоном выполнить `make verify`, загрузить собранную прошивку и
reboot Pico. Открыть CDC serial monitor на 115200, начать host MIDI capture и
зафиксировать условия. Запустить internal clock, а после длительности прогона
выполнить обычный Stop. Сохранить header и единственную следующую data-строку CSV;
эта строка уже получена после остановки/drain producer-а. Не делать serial print,
снимки или LittleFS операции во время работающего transport.

Для автоматического free-прогона остановленная firmware принимает одну строку
`RUN <duration_ms> <bpm> <swing>`. Перед Start она печатает acknowledgement, затем до
автоматического Stop не читает и не печатает Serial. После Stop выводятся diagnostics
v2 и `swing_metro_control_v1,run_complete`. Host helper сам находит единственный
`/dev/cu.usbmodem*` и сохраняет только CSV header/data:

```sh
~/.platformio/penv/bin/python scripts/pico_serial_run.py \
  --duration-seconds 244 --bpm 68 --swing 50 \
  --output data/midi-stage-1-5-free-diagnostics-v2.csv
```

Скрипт отказывается перезаписывать существующий файл. Если нужен только diagnostics
snapshot, MIDI host capture запускается отдельно до этой команды.

Рекомендуемый автоматический прогон одновременно открывает CDC Serial и USB MIDI,
записывает все принятые MIDI-сообщения с host monotonic timestamps и сопоставляет
число принятых Clock с firmware F8 attempts. Установить host-зависимости и запустить:

```sh
~/.platformio/penv/bin/pip install -r scripts/requirements-hardware.txt
~/.platformio/penv/bin/python scripts/pico_midi_run.py \
  --duration-seconds 244 --bpm 68 --swing 50 \
  --output-prefix data/midi-stage-1-5-free
```

Перед измерением всё ещё требуется reboot Pico: firmware counters cumulative с boot.
Скрипт открывает MIDI до отправки `RUN` и больше не посылает данные по Serial во время
active transport; firmware в это время не читает и не печатает Serial. После
`run_complete` скрипт создаёт три файла:

- `*-midi.csv`: сырые MIDI-сообщения, host timestamp и Clock interval;
- `*-diagnostics.csv`: firmware diagnostics v2;
- `*-summary.csv`: interval statistics, подозрительные короткие/длинные интервалы и
  разница между firmware F8 attempts и принятыми host Clock.

При нескольких MIDI-входах нужный порт задаётся индексом или уникальной частью имени
через `--midi-port`; список показывает `--list-midi-ports`. Host timestamps включают
USB и scheduling latency macOS и не заменяют Pico-side lateness diagnostics.

Для каждого прогона записать:

- git revision и параметры сборки;
- плату и частоты, host/OS и программу записи MIDI;
- способ USB-подключения без молчаливой замены кабеля/хаба;
- BPM, swing, clock mode, сценарий нагрузки и длительность;
- момент/способ получения diagnostics snapshot;
- какие timestamps относятся к Pico, а какие к host.

Точный A/B протокол: оба прогона используют internal clock, 68 BPM, swing 50,
длительность не менее 234,63 с и одинаковые board/frequency, host/OS, MIDI recorder,
USB cable/hub и capture setup. В A нет interaction. В B меняется только письменно
описанная interaction load (например, конкретные edits velocity/notes); все остальные
условия сохраняются. Старая loaded запись не является строгим A/B: у неё другой BPM и
меньшая длительность.

Для локализации полезны GPIO-маркеры/логический анализатор в точках callback и
попытки F8, если они доступны. Маркеры должны иметь фиксированную малую стоимость;
не использовать serial print из IRQ или MIDI path. Host capture вести одновременно,
но не вычитать Pico и host timestamps без синхронизации часов. Связывать аномалии по
ordinal: пронумеровать F8/Clock в host capture, сопоставить позицию длинного
host-интервала с порядком Clock attempt и суммарными CSV counters на завершении того
же run; Pico и host timestamps не вычитать.

Итоговый CSV локализует проблему только на уровне всего run: maxima и counters не
содержат ordinal отдельной аномалии. Если в run есть несколько возможных причин или
суммарный snapshot не объясняет конкретный длинный интервал, повторить ограниченный
участок с фиксированными GPIO-маркерами callback/F8 либо разбить сценарий на более
короткие прогоны. Не приписывать aggregate maximum конкретному host-интервалу без
такого дополнительного сигнала.

## Проверяемые ветви и решения

1. **Поздний или пропущенный alarm:** ненулевые/аномальные
   `max_actual_callback_interval_us`, `max_callback_lateness_us`,
   `alarm_arm_failures` или `missed_scheduled_targets`. Исправлять причину в alarm
   path этапа 1.
2. **Overflow producer-а:** `failed_publications`/`tick_queue_overflows` растёт.
   Устранить переполнение producer queue в границах этапа 1.
3. **Задержка main loop:** publication успешны, но растут
   `max_service_interval_us` и `max_internal_tick_processing_lateness_us`; в v2 это
   увеличивает lateness/backlog, но не `budget_discards`.
4. **Ограничение consumer-а:** `internal_tick_budget_reached_passes` и
   `max_remaining_internal_ticks_after_budget_pass` показывают накопление более четырёх
   тиков. В v2 backlog не отбрасывается; оценивать его вместе с
   `max_process_duration_us` и lateness, не по `budget_discards`.
5. **После Clock attempt:** balance проходит, alarm/producer/consumer counters не
   объясняют пропуск и Clock attempts по ordinal ровные, но длинный интервал есть
   только в host capture. Передать в этап 2 конкретную гипотезу USB
   acceptance/backpressure; не называть это потерей F8 и не объявлять USB доказанным.

Для каждого Stop также проверить transition: следующая CSV строка появляется только
после остановки internal timing; нет строк во время run; повторный Stop без нового
internal run не создаёт строку; Stop, mode switch и storage имеют соответствующую
причину discard в cumulative counters. Отдельно проверить Start/Stop, смену BPM и
длительный стационарный прогон: они не должны оставлять старые alarm или создавать
Clock burst.

## Анализ и отчёт

- Не делать вывод только по среднему периоду: привести количество, максимум и
  позиции длинных интервалов, общий дрейф и соответствующие diagnostics snapshots.
- Сопоставить каждый аномальный участок с балансом callback → publish → pop → Clock
  attempt.
- Clock → Note On сравнивать с baseline только при одинаковых BPM/swing и сценарии.
- Попытку F8 не называть принятием USB-стеком. Ровный GPIO до USB при провале host
  capture — основание для следующей гипотезы, а не доказательство причины.

### Free-прогон до исправления consumer race

Прогон firmware `3306729` сохранён в `data/midi-stage-1-5-free.mmon` и
`data/midi-stage-1-5-free-diagnostics.csv`. Диагностика относится ко всему run, но
MIDI Monitor сохранил только последние 10 000 сообщений (`maxMessageCount = 10000`),
поэтому `.mmon` содержит хвост длительностью 277,5766 с, а не весь run.

Firmware snapshot:

- 19 319 alarm callbacks и одна synchronous Start publication;
- 19 320 successful publications, ноль publication overflow/arm failure/missed
  target/stale/out-of-horizon;
- max callback interval 36 775 мкс, max callback lateness 14 мкс;
- 19 201 consumer pops и F8 attempts, 119 `budgetDiscards`;
- max service interval 618 мкс, max tick/F8 lateness 324 мкс, max event lateness
  182 мкс;
- quiescent balance проходит: `19320 == 19201 + 119`.

В сохранённом host-хвосте 7 498 Clock: 51 интервал около двух периодов и один
интервал 110,288 мс около трёх периодов. Это ровно 53 пропущенных периода. Сырой
drift относительно сетки 68 BPM равен 1 949,430 мс; после вычитания этих 53 периодов
остаётся 0,901 мс. Медиана Clock interval 36 761,542 мкс, среднее без длинных
интервалов 36 764,930 мкс. Между каждой парой соседних Note On остаётся ровно шесть
записанных Clock; Clock → Note On: медиана 266,750 мкс, максимум 2 722,500 мкс.

Alarm, producer overflow и main-loop stall исключаются измерениями. Причина находится
в `TransportController::process()`: после выхода из tick-pop loop при пустой очереди
код безусловно вызывал `ticks.discard()`. Callback мог опубликовать новый tick между
неудачным `pop()` и `discard()`, после чего свежий tick удалялся и ошибочно считался
budget discard.

### Free-прогон после условного discard

Firmware `e729b92`, полный run 243,858 с, internal clock 68 BPM. Proven facts:

- 6 633 successful publications, 6 602 consumer pops и 6 602 F8 attempts;
- 31 `budgetDiscards` и ровно 31 double host Clock interval;
- producer overflow, explicit stop/mode/storage discard равны нулю;
- max service interval 650 мкс, callback lateness 18 мкс, tick/F8 lateness 306 мкс.

Это подтверждает, что условный active-path discard остаётся источником потерь: разница
между publications и pops ровно равна `budgetDiscards` и числу double intervals.
Непосредственная причина, почему budget достигался при малом service interval, всё ещё
не объяснена aggregate snapshot-ом: он не даёт ordinal backlog-а. Не приписывать эти
31 apparent budget hit конкретному callback или host-интервалу.

Новая политика сохраняет предел четырёх обработанных tick за один `process()` pass,
но никогда не отбрасывает остаток active queue. Цена — backlog может временно
сохраниться и увеличить processing lateness; v2 измеряет его observed depth, число
budget-limited passes, максимум pops/pass и duration `process()`. Stop, mode switch и
storage по-прежнему явно drain/discard producer queue.

### Аппаратная проверка исправления

Проверка выполнена на Pico 2 W с firmware после `cfd3f36` и host monitor из
`213e445`. Перед каждым измеряемым прогоном firmware загружалась заново, поэтому
diagnostics относятся к одному run. Все прогоны: internal clock, swing 50%, 244 с;
host capture использует CoreMIDI через `python-rtmidi` и `time.monotonic_ns()`.

| Run | Условия | Host/Firmware Clock | Длинные интервалы / потери | Host abs jitter p95/p99 | Callback interval/lateness | Tick/F8/event lateness | Max process | Queue / discard |
| --- | --- | ---: | --- | --- | --- | --- | ---: | --- |
| Free 68 A | без interaction | 6 637 / 6 637 | 0 / 0 | 303 / 952 мкс | 36 807 / 49 мкс | 324 / 324 / 203 мкс | 373 мкс | max 1 pop; 0 budget/overflow |
| Free 68 B | повторный free | 6 637 / 6 637 | 0 / 0 | 286 / 773 мкс | 36 807 / 49 мкс | 277 / 277 / 141 мкс | 361 мкс | max 1 pop; 0 budget/overflow |
| Free 240 | без interaction | 23 424 / 23 424 | 0 / 0 | 274 / 639 мкс | 10 449 / 41 мкс | 495 / 495 / 147 мкс | 562 мкс | max 1 pop; 0 budget/overflow |
| Loaded 240 | активное редактирование note/velocity | 23 424 / 23 424 | 0 / 0 | 338 / 789 мкс | 10 461 / 55 мкс | 602 / 602 / 501 мкс | 713 мкс | max 1 pop; 0 budget/overflow |

Итого в четырёх полных прогонах host получил 60 122 из 60 122 F8 attempts. Средняя
ошибка периода находилась в диапазоне 0,019–0,144 мкс, end-to-end drift за каждый
244-секундный run — 128–956 мкс. Редкие host-выбросы до примерно 4 мс приходят
компенсирующими длинной/короткой парой; ни один не достиг порога 1,5 периода и не
соответствует потере Clock. Это host arrival timestamps, поэтому они включают USB и
планирование macOS.

В loaded 240 snapshot `max_service_interval_us` равен 114 903 мкс, но этот cumulative
с boot максимум не локализован внутри active transport. Он не может представлять
active consumer stall такой длительности: max tick lateness равен 602 мкс, max
callback lateness 55 мкс, queue не накопилась, overflow и missed target равны нулю.
Для последующих этапов service maximum следует сбрасывать при старте измеряемого run
или маркировать active-only, если потребуется связывать его с конкретной нагрузкой.

Сырые локальные артефакты проверки:

- `data/midi-stage-1-5-monitor-{midi,diagnostics,summary}.csv` — Free 68 A;
- `data/midi-stage-1-5-loaded-monitor-{midi,diagnostics,summary}.csv` — Free 68 B
  (имя историческое; interaction в MIDI-потоке не зафиксирован);
- `data/midi-stage-1-5-free-240bpm-monitor-{midi,diagnostics,summary}.csv` — Free 240;
- `data/midi-stage-1-5-loaded-240bpm-monitor-{midi,diagnostics,summary}.csv` — Loaded 240.

Дополнительный loaded 68 capture был вручную остановлен через Save на 130,47 с. До
Stop host получил 3 549 из 3 549 Clock; он подтверждает работу под interaction, но не
включён в четыре полных прогона и итог 60 122. Файлы имеют префикс
`data/midi-stage-1-5-loaded-monitor-actual`.

Команды проверки:

```sh
make verify
pio device list
pio run -e rpipico2 -t upload
pio device monitor -e rpipico2
```

Если используется другой serial monitor, он должен быть подключён к тому же CDC port
на 115200. Не запускать upload из этой процедуры без отдельного явного решения.

## Готовность шага

- Выполнен указанный в Pending Hardware free-прогон либо явно записана недоступность
  устройства/инструментов и оставшиеся команды/условия.
- Для двойных интервалов определён участок пути, где возникает потеря/задержка, и
  подтверждённая причина исправлена в границах этапа 1.
- Если причина находится после попытки USB-отправки, сформулирована конкретная задача
  этапа 2 с наблюдениями; это не выдаётся за завершённое USB-расследование.
- Опубликована таблица до/после: double intervals, drift, max service interval,
  callback lateness, tick/event lateness, overflow/discard и Clock attempts.
- Выполнен `make verify`; аппаратные результаты содержат все условия прогона.

## Hardware validation result

Pending Hardware закрыт четырьмя полными прогонами выше. Для каждого run выполняется
quiescent balance, `successful_publications == successful_consumer_pops ==
outgoing_internal_f8_attempts == host Clock count`; все discard/overflow counters
равны нулю. Источник старых двойных интервалов локализован в active consumer discard
race и устранён. Этап 1 не оставляет незакрытой hardware-гипотезы о потере internal
Clock.

## Вне объёма

Изменение USB sink-контракта, retries и доказательство доставки до DAW. Полная
нагрузочная матрица с LVGL, input и Gate выполняется на этапе 5.
