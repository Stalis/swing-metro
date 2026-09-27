# Legacy inventory перед R1

Исходники проверены на `6317513ff61d2de3673c2dc3927b85b2987603b5`.
Статус этого списка — кандидаты и границы будущих изменений; R0 ничего из него не удаляет.

## Удалить из live-пути в R1

| Элемент | Место | Потребители / действие |
| --- | --- | --- |
| Diagnostics v2/v3 | `scripts/pico_run_protocol.py`: `V2_DIAGNOSTICS_PREFIX`, `V3_DIAGNOSTICS_PREFIX`, `V2_COLUMNS`, `V3_COLUMNS` | Сейчас V4_COLUMNS разворачивает V3, а V3 — V2. Сначала развернуть текущую схему в смысловые группы, сохранив порядок v4 |
| Старые aliases | Там же: `DIAGNOSTICS_PREFIX/COLUMNS/HEADER` | Указывают на v2. Удалить после проверки импортов во всём дереве, не переименовать молча в v4 |
| Выбор схемы 2/3/4 | `diagnostics_columns_for_row` | Его прямые потребители: `diagnostics_header_for_row`, `parse_diagnostics_row`, `TimedRunCapture.consume`; оставить строгий v4 и понятную ошибку старого формата |
| Ветка неполных v2 метрик | `scripts/pico_midi_run.py`: `add_firmware_summary`, `diagnostics_version == 2`, `unavailable_v2` | Удаляется вместе с live v2, а не с метриками принятия/доставки |
| Тесты старых схем | `scripts/tests/test_pico_midi_run.py`: v2/v3 parsing и зависимость порядка v3 от V2_COLUMNS | Заменить проверками отказа старых live-версий. Оставить полноценные v4 tests и новый независимый fixture |

GitNexus `context(diagnostics_columns_for_row)` подтверждает три прямых потребителя.
В макроплане его upstream impact — **CRITICAL**, 19 символов на глубине 3; перед
редактированием R1 повторить impact на актуальном индексе. Транзитивные сценарии:
`pico_midi_run.py`, `pico_serial_run.py`, `pico_run_report.py` и внешние clock-прогоны.
Нельзя считать `pico_serial_run.py` старой версией `pico_midi_run.py`: первый собирает
serial-only, второй добавляет host MIDI.

## Сохранить: действующие отдельные протоколы

| Элемент | Назначение |
| --- | --- |
| `swing_metro_diagnostics_v4` | Текущая firmware-диагностика; эталон закреплён в R0 |
| `swing_metro_control_v1` | RUN/EXTERNAL_RUN, начало/завершение прогона, ошибки |
| `swing_metro_input_diagnostics_v1` | Метрики sampling ввода |
| `swing_metro_runtime_diagnostics_v1` | Окно измерений LVGL/display/input |
| `swing_metro_fault_v1` | Подтверждение fault-сценария тестовой прошивки |
| `swing_metro_timed_run_report_v1` / `swing_metro_fault_run_report_v1` | JSON-отчёты, не старые версии diagnostics |

Суффикс v1 сам по себе не является признаком устаревшего кода. Сохранить distinctions
attempted/accepted/host-observed, reasons удаления, session generation и terminal Off.

## Вынести, а не удалить

- `src/main.cpp`: форматирование headers/строк диагностики, RUN state machine,
  ожидание runtime snapshot. После выноса сохранить stopped-only export и порядок
  завершения `run_complete` относительно всех диагностических строк.
- `src/engine/transport_controller.h`: `TransportDiagnostics`, counters и timing
  distributions. Это используемая диагностика, не версия старого сборщика.
- `src/engine/stage5_instrumentation.h`, `SWING_METRO_STAGE5_*`, environments и
  Makefile `stage5-*`: историческое именование актуальных режимов. Переименование
  выполнять согласованно со scripts/docs; production/off/fault не сливать.
- `scripts/stage5_*`: orchestration разных аппаратных сценариев. Не объявлять
  дубликатами до проверки различий acceptance и восстановления production.

## Отдельный аудит после R4

| Кандидат | Почему не входит в очистку логов |
| --- | --- |
| `src/engine/midi_step_boundary.h`: `processMidiStepBoundary`, `processExternalMidiClock` | Старый способ исполнения границ; `processMidiStepBoundary` всё ещё используется тестами `test_midi_clock_transmitter.cpp` |
| `src/engine/midi_clock_transmitter.h`: `MidiClockTransmitter` | Старый clock-путь; его тестовый набор явно зарегистрирован в native runner |
| `Sequencer::update/sync/toggleRunning/externalStart/externalContinue/externalStop/advanceExternal` | Совместимость старого timing API; `sync` ещё вызывается main, `toggleRunning` — fallback координатора; есть тест legacy timing |
| `ProgramStep` / `SequencerStep` и capture/apply | Дублирование доменной модели, не поддержка старых логов; разрешается при разделении Program/Playback |

Граф query нашёл legacy flow `ProcessMidiStepBoundary → ClampBpm/Send`.
Дополнительная текстовая проверка подтверждает тестовые потребители и регистрацию.
Пустой/UNKNOWN caller set в C++ не доказывает безопасность удаления: проверить
декларацию, определение, callbacks и template instantiations перед каждой правкой.

## Совместимость и данные, которые не удалять

- Импорт старых сохранённых программ без gate и нормализацию legacy swing:
  `program_codec.cpp`, `program_runtime.cpp`; тесты `testCodecMigratesLegacyPayloadWithoutGate`
  и `testCodecClampsLegacySwingOnDecode`. Это пользовательские данные, не telemetry.
- Исторические raw captures под `data/`, отчёты и stage-документацию с реальными
  результатами. Для чтения v2/v3 после R1 доступна исходная ревизия утилит; отдельный
  converter не входит в R1 без установленной потребности.
- Скопированный в R0 v4 образец и его первоначальные metadata, в том числе
  `working_tree_dirty=true` и известные ограничения измерений.

Этот список достаточен для начала R1; он не разрешает попутное удаление всех
старых APIs, переписывание storage или изменение timing.
