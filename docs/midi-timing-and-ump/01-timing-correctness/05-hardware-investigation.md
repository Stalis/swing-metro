# Шаг 1.5. Аппаратное расследование и baseline

Статус: ожидает аппаратных прогонов. Зависит от шагов 1.1–1.4.

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
  attempt, callback interval/lateness и ошибки постановки alarm;
- alarm следует абсолютной сетке и имеет bounded overdue policy.

`Serial` инициализируется как CDC на 115200 бод. После каждого перехода internal
timing из active в inactive `syncInternalAlarm()` сначала вызывает
`internalTickAlarm.stop(...)`, который отменяет/дренирует producer, и только затем
печатает snapshot на main core. Во время internal timing, IRQ и MIDI send path печати
нет. Header печатается один раз за boot, затем одна data-строка на такой Stop.

CSV schema, в порядке колонок:

```text
swing_metro_diagnostics,alarm_callback_invocations,synchronous_start_publication_attempts,successful_publications,failed_publications,tick_queue_overflows,stop_discards,mode_switch_discards,storage_discards,alarm_arm_failures,stale_alarm_callbacks,stale_alarm_arm_failures,missed_scheduled_targets,out_of_horizon_alarm_callbacks,max_actual_callback_interval_us,max_callback_lateness_us,successful_consumer_pops,budget_discards,outgoing_internal_f8_attempts,max_service_interval_us,max_internal_tick_processing_lateness_us,max_external_tick_processing_lateness_us,max_f8_attempt_lateness_us,max_queued_event_attempt_lateness_us
```

Каждая data-строка начинается с `swing_metro_diagnostics` и содержит значения в том
же порядке. `failed_publications` и `tick_queue_overflows` обозначают один и тот же
случай: producer не смог записать в полный tick queue, поэтому их значения должны
совпадать. `outgoing_internal_f8_attempts` и значения lateness относятся к попыткам
вызвать USB MIDI sink, не к принятию USB-стеком или доставке host.

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
   `max_service_interval_us` и `max_internal_tick_processing_lateness_us`; затем
   возможен `budget_discards`.
4. **Ограничение consumer-а:** `successful_consumer_pops` и `budget_discards`
   показывают накопление более четырёх тиков. Исправлять consumer/service path этапа 1.
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

Шаблон таблицы до/после:

| Run | Load | Duration s | Clock count | Double intervals (count/max/ordinals) | Drift us | Max service us | Max callback interval/lateness us | Max tick/F8/event lateness us | Overflow | Budget discard | Stop/mode/storage discard | Consumer pops | F8 attempts | Balance | Decision |
| --- | --- | ---: | ---: | --- | ---: | ---: | --- | --- | ---: | ---: | --- | ---: | ---: | --- | --- |
| A | none |  |  |  |  |  |  |  |  |  |  |  |  | pass/fail |  |
| B | written interaction only |  |  |  |  |  |  |  |  |  |  |  |  | pass/fail |  |

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

- Выполнены сопоставимые free и loaded прогоны либо явно записана недоступность
  устройства/инструментов и оставшиеся команды/условия.
- Для двойных интервалов определён участок пути, где возникает потеря/задержка, и
  подтверждённая причина исправлена в границах этапа 1.
- Если причина находится после попытки USB-отправки, сформулирована конкретная задача
  этапа 2 с наблюдениями; это не выдаётся за завершённое USB-расследование.
- Опубликована таблица до/после: double intervals, drift, max service interval,
  callback lateness, tick/event lateness, overflow/discard и Clock attempts.
- Выполнен `make verify`; аппаратные результаты содержат все условия прогона.

## Pending Hardware

16 сентября 2026 года `pio device list` на машине выполнения не обнаружил Pico/CDC:
доступны только системные `/dev/cu.wlan-debug`, `/dev/cu.debug-console` и
`/dev/cu.Bluetooth-Incoming-Port`. Поэтому firmware не загружалась, аппаратные A и B
прогоны, host MIDI capture и возможные GPIO/logic-analyzer измерения не выполнены.

Чтобы снять блокировку, подключить Pico 2 W к фиксированному USB cable/hub, повторить
`pio device list`, загрузить firmware командой выше и выполнить A, B и transition
checks по протоколу этого документа, сохранив CDC CSV и host captures. До этого нет
заявленных hardware results, локализованной причины или завершения stage 1.

## Вне объёма

Изменение USB sink-контракта, retries и доказательство доставки до DAW. Полная
нагрузочная матрица с LVGL, input и Gate выполняется на этапе 5.
