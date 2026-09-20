# Шаг 3.4. Аппаратная проверка input и решение polling-vs-IRQ/PIO

Статус: запланировано. Зависит от шагов 3.1–3.3.

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

## Готовность шага и этапа 3

- Все обязательные runs, artifacts locations и численные результаты записаны.
- Решение polling-vs-IRQ/PIO следует опубликованному criterion, а не предположению.
- При `polling accepted` нет необъяснённой MIDI regression и этап 3 может быть отмечен
  завершённым в index/README.
- При `polling rejected` этап 3 остаётся запланированным/незавершённым; conditional
  follow-up implementation описан, но не выполнен этим документом.

## Вне объёма

Реализация IRQ/PIO capture, перенос input на core 1, межъядерная queue, автоматический
GPIO rig, USB protocol analyser и DAW-specific latency guarantee.
