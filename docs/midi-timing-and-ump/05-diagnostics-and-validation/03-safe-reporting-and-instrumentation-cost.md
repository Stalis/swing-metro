# 5.3 Safe reporting и стоимость instrumentation

Статус: выполнено 2026-09-26. Software-инфраструктура и трёхпарный аппаратный A/B
проверены на Pico 2 W.

## Безопасный отчёт

`pico_midi_run.py` и `pico_serial_run.py` создают
`swing_metro_timed_run_report_v1`. Существующие firmware-схемы v2/v3/v4, input v1,
runtime v1 и их CSV не меняются.

Host принимает только цельную последовательность: один совпадающий `run_started`, затем
ровно по одной строке diagnostics/input/runtime и один `run_complete`. Несовпадающие
параметры, дубликаты, пропуски и неверный порядок завершают capture ошибкой. Все пути,
включая `*-report.json`, проверяются до запуска; существующие файлы не перезаписываются.

Device-local значения и host MIDI лежат в разных разделах отчёта и имеют явные clock
domain. Их timestamp нельзя вычитать друг из друга. Firmware v4 и input v1 являются
boot-cumulative; runtime v1 относится к окну timed run. Значение
`synchronous_start_publication_attempts == 1` означает только `fresh_boot_candidate`, а не
доказывает физическую перезагрузку.

`--metadata` необязателен для совместимости со старыми командами. Без полного набора
metadata capture сохраняется, но получает `reproducibility_complete: false` и
`comparison_eligibility: insufficient_metadata`.

Пример metadata:

```json
{
  "run_id": "stage5-3-on-r01",
  "scenario": {"id": "baseline-68", "interaction": "none"},
  "instrumentation": {"stage5": "enabled"},
  "firmware": {
    "source_revision": "<git-commit>",
    "binary_sha256": "<64-hex-digits>",
    "platformio_environment": "rpipico2",
    "build_flags": ["<effective PlatformIO build flags>"],
    "toolchain": "arm-none-eabi 16.1.0",
    "dependencies": ["lvgl 9.6.0", "GFX Library for Arduino 1.6.8"]
  },
  "hardware": {
    "board": "Raspberry Pi Pico 2 W",
    "cpu_frequency_hz": 150000000,
    "usb_topology": "direct"
  },
  "host": {"notes": "same Mac and CoreMIDI input for every pair"}
}
```

## A/B seam

Обычная среда `rpipico2` оставляет Stage 5 instrumentation включённой. Среда
`rpipico2-stage5-instrumentation-off` отличается одним macro и отключает только добавленные
в 5.1/5.2 probes: LVGL/flush timing, runtime window, MIDI histograms, scheduled high-water и
observed IRQ queue depth. Старые transport/input counters и MIDI behavior остаются включены.
Off firmware печатает те же строгие строки, но Stage 5 поля равны нулю.

```sh
make build
make build-stage5-off
shasum -a 256 .pio/build/rpipico2/firmware.uf2 \
  .pio/build/rpipico2-stage5-instrumentation-off/firmware.uf2
pio run -e rpipico2 -t size
pio run -e rpipico2-stage5-instrumentation-off -t size
pio pkg list -e rpipico2
pio pkg list -e rpipico2-stage5-instrumentation-off
```

Hash, flash/RAM size и отсутствие Stage 5 symbols/probes в off ELF являются build evidence,
но не измерением времени, CPU load, IRQ interference или MIDI jitter. Сравнимые сборки
используют один commit, toolchain, dependencies, board clock и flags; различается только
instrumentation macro.

## Парное аппаратное измерение

Перед каждым run загрузить соответствующий UF2 и перезапустить Pico. Использовать одинаковые
board, display state, cable/hub, host, MIDI port, BPM, swing, duration и interaction. Выполнить
не менее трёх пар, чередуя порядок `on/off`, затем `off/on`, чтобы порядок не стал скрытым
фактором.

```sh
python3 scripts/pico_midi_run.py --duration-seconds 244 --bpm 68 --swing 50 \
  --metadata data/stage5-3-on-r01-metadata.json \
  --output-prefix data/stage5-3-on-r01
```

Host MIDI distributions сравниваются только с host MIDI; device-local метрики — только в
своём clock domain. `lv_timer_handler` уже включает время вложенного flush, поэтому эти totals
не складываются. Количество вызовов `loop1` или handler не является CPU utilization и не
доказывает 100% полезную загрузку.

## Аппаратный результат 2026-09-26

На commit `3f3d4f0` выполнены три пары по 244 секунды при 68 BPM, swing 50 и без
взаимодействия с органами управления. Порядок чередовался: ON→OFF, OFF→ON, ON→OFF.
Каждый run начинался после загрузки соответствующей прошивки и перезагрузки Pico.
Serial и CoreMIDI записывались одновременно через проектное `.venv`.

Во всех шести прогонах host и firmware получили по 6 637 Clock; суммарно
39 822/39 822. Пропущенных, коротких и длинных host-интервалов, failed publications,
tick queue overflows и missed scheduled targets не было.

Медианные результаты трёх прогонов каждого варианта:

| Метрика | ON | OFF | Медианная парная разница ON−OFF |
| --- | ---: | ---: | ---: |
| Host absolute jitter p95 | 386,5 мкс | 367,2 мкс | +19,3 мкс |
| Host absolute jitter p99 | 1 312,4 мкс | 926,5 мкс | +294,3 мкс |
| Device Clock max acceptance lateness | 427 мкс | 407 мкс | +20 мкс |
| Device Note max acceptance lateness | 306 мкс | 267 мкс | +39 мкс |
| Max service interval | 2 039 мкс | 1 551 мкс | +482 мкс |

Наблюдается небольшой tail overhead, особенно в max service interval, но он не привёл
к потере или нарушению непрерывности MIDI в baseline. Это не распространяется на ещё не
выполненные load/fault scenarios. После серии на устройство возвращена ON-прошивка.

Fault/load scenarios относятся к 5.4, широкая аппаратная валидация и
GPIO/logic-analyzer conclusions — к 5.5.
