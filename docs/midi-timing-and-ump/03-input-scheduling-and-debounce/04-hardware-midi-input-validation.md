# Шаг 3.4. Аппаратная проверка input и решение polling-vs-IRQ/PIO

Статус: выполнено 2026-09-20; polling отклонён, требуется условный IRQ/PIO follow-up.
Зависит от шагов 3.1–3.3.

## Цель

На Pico 2 W измерить реальные matrix/encoder polling limits под MIDI/UI нагрузкой,
сравнить MIDI timing с Stage 2 baseline и принять evidence-based решение: polling
достаточен либо требуется bounded IRQ/PIO capture implementation.

## Предпосылки и baseline

Stage 2 hardware baseline записан в
`docs/midi-timing-and-ump/02-events-and-usb-backpressure/05-hardware-validation.md`:
free 68 BPM и stress 240 BPM runs сравнивают firmware Clock attempts, stack acceptance,
host capture, jitter, lateness и max process duration. Эти значения являются baseline,
а не обещанием постоянной latency. Input metrics и encoder bound определены шагами
3.1 и 3.3; без них аппаратный run не является валидным polling decision.

## Условия

Перед каждым run выполнить `make verify`, загрузить текущую firmware и reboot board.
Использовать тот же Pico 2 W, USB cable/path, macOS host и MIDI backend, что и Stage 2,
если они доступны. Не запускать MIDI Monitor параллельно. Сохранить MIDI capture,
serial diagnostics и summary под новым локальным prefix; не добавлять data/log artifacts
в Git.

## Обязательные сценарии

1. **Idle baseline:** internal clock 68 BPM и 240 BPM, swing 50, без interaction.
2. **Matrix:** медленные и быстрые press/release, long press, Shift и одновременные
   pads при active MIDI; проверять один press/release на физический gesture.
3. **Encoders:** каждый encoder медленно и на максимальной practically repeatable
   скорости в обе стороны, с switch press/release, во время active MIDI и screen changes.
4. **Mixed stress:** matrix, все encoders и active MIDI; не выполнять Save/Load и другие
   LittleFS operations при работающем transport.

## Критерий решения

Polling считается достаточным только если во всех обязательных сценариях:

- max actual encoder sample interval не превышает bound, рассчитанный в шаге 3.1;
- count interval above bound и defined scheduler misses не выявляют breach contract;
- fast-rotation physical check не показывает потерянных, duplicated или inverted detents;
- matrix сохраняет one-gesture-one-edge semantics;
- Stage 2 MIDI balances не имеют необъяснённого gap, а jitter/lateness/max process duration
  не показывают длительной input-induced blocking regression.

Если любой polling criterion не проходит, не объявлять Stage 3 complete. Внести в этот
документ capture evidence, exact failing bound/scenario и conditional follow-up:
bounded IRQ либо PIO transition producer, который не вызывает application code, USB,
Serial или LVGL. Выбор IRQ/PIO и implementation scope оформляются отдельным следующим
шагом; текущий этап не маскирует failure повторными scans или изменением критериев после
измерения.

## Анализ и отчёт

- Отдельно привести назначенные periods, actual interval max, threshold crossings и
  scheduler counters; не называть aggregate counter доказательством конкретного edge.
- Сравнить Clock attempt/accepted/host, long/short intervals, host jitter p95/p99,
  drift, tick/F8/event acceptance lateness и max process duration с Stage 2.
- Описать physical procedure, rotation rate estimate, screen/MIDI load и ограничения
  измерения. Не выдавать native model или USB stack acceptance за GPIO proof.
- В конце документа зафиксировать outcome: `polling accepted` либо
  `polling rejected — conditional IRQ/PIO follow-up required`.

## Результат

Проверка выполнена на Pico 2 W и macOS/CoreMIDI path с firmware revision `a7fa4ed`.
Перед каждым измеряемым run выполнены `make verify`, upload и reboot; MIDI Monitor
параллельно не запускался. Оба run использовали internal clock, swing 50, длились
244 с и выполнялись без interaction. Диагностика input накоплена с момента reboot,
поэтому включает также короткий интервал подготовки host capture.

Host monitor дополнен метриками `host_abs_jitter_p95_us`,
`host_abs_jitter_p99_us` и `absolute_clock_drift_us`; для их расчёта добавлены два
Python regression tests. Формулы совпадают с использованными для Stage 2: абсолютное
отклонение каждого межтактового интервала от ideal interval и абсолютное отклонение
полной длительности последовательности Clock от идеальной.

| Run | Clock attempt / accepted / host | Long / short / estimated missing | Host abs jitter p95/p99 | Drift | Tick/F8 / event acceptance lateness | Max process | Encoder interval max / > 1 250 мкс |
| --- | ---: | --- | --- | ---: | --- | ---: | ---: |
| Idle 68 | 6 637 / 6 637 / 6 637 | 0 / 0 / 0 | 205 / 284 мкс | 825 мкс | 354 / 205 мкс | 1 138 мкс | 1 643 мкс / 1 216 |
| Idle 240 | 23 424 / 23 424 / 23 424 | 0 / 0 / 0 | 436 / 582 мкс | 709 мкс | 451 / 167 мкс | 1 156 мкс | 1 926 мкс / 8 402 |

Clock `RetryLater`, disconnect, failed publication, queue overflow, missed target,
coalescing, expiry, capacity failure и safety-stop counters равны нулю. Firmware
stack-accepted Clock полностью совпадает с host capture. По сравнению со Stage 2
idle/free baseline нет необъяснённой MIDI-регрессии: ни один Clock не потерян и не
дублирован, long/short intervals отсутствуют, drift остаётся меньше 1 мс, p95/p99,
lateness и max process duration не превышают соответствующие Stage 2 результаты.

Тем не менее polling contract нарушен уже в первом обязательном idle scenario:
максимальный фактический интервал sampling равен 1 643 мкс при опубликованном bound
1 250 мкс, а число строгих превышений равно 1 216. Повторный idle run на 240 BPM
подтвердил нарушение: 1 926 мкс и 8 402 превышения. Хороший MIDI balance не отменяет
это нарушение input sampling contract.

После первого decisive failure matrix, encoder physical и mixed stress scenarios
остановлены досрочно: они не могут изменить обязательное решение, по которому одного
нарушенного polling criterion достаточно для отклонения polling. Поэтому этот run не
даёт нового аппаратного доказательства one-gesture-one-edge semantics или отсутствия
потерянных/duplicated/inverted detents; соответствующие проверки должны войти в
валидацию следующей реализации capture.

Локальные артефакты, намеренно не добавленные в Git:

- `data/midi-stage-3-4-idle-68-run1-{midi,diagnostics,input-diagnostics,summary}.csv`;
- `data/midi-stage-3-4-idle-240-run1-{midi,diagnostics,input-diagnostics,summary}.csv`.

## Решение и условный следующий шаг

Outcome: `polling rejected — conditional IRQ/PIO follow-up required`.

Этап 3 не завершён. Следующий implementation step должен заменить polling энкодеров
ограниченным producer на GPIO IRQ либо PIO, который только timestamp-ит/буферизует
переходы и не вызывает application code, USB, Serial или LVGL. Выбор IRQ или PIO,
размер и overflow policy очереди, consumer budget и diagnostics необходимо оформить
отдельным планом. После реализации повторить оба idle run, physical matrix/encoder
scenarios и mixed stress по исходным критериям; bound 1 250 мкс задним числом не
ослаблять.

## Готовность шага и этапа 3

- Два idle run и их artifact locations записаны; physical/mixed runs намеренно
  остановлены после decisive polling failure и не считаются пройденными.
- Решение polling-vs-IRQ/PIO следует опубликованному criterion, а не предположению.
- При `polling accepted` нет необъяснённой MIDI regression и этап 3 может быть отмечен
  завершённым в index/README.
- При `polling rejected` этап 3 остаётся запланированным/незавершённым; conditional
  follow-up implementation описан, но не выполнен этим документом.
- После изменений host monitor выполнен полный `make verify`: format, tidy, 296 native
  tests, 14 Python tests и firmware build прошли.

## Вне объёма

Реализация IRQ/PIO capture, перенос input на core 1, межъядерная queue, автоматический
GPIO rig, USB protocol analyser и DAW-specific latency guarantee.
