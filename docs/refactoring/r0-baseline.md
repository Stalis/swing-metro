# R0 — исходная точка рефакторинга

Этап R0 выбранного варианта A: зафиксировать поведение и проверяемую исходную точку
перед очисткой логов R1. Исходный commit — `6317513ff61d2de3673c2dc3927b85b2987603b5`.
[Одобренный макроплан](../plans/2026-09-27-gitnexus-plan-refactor-feature-foundation.md)
зафиксирован отдельным commit `08551e7020aa76db14bcc335c92980b3b2b45dcb`.
Статус выполнения этапов ведётся здесь; исторический текст плана не переписывается.

## Результат

- Исходный полный `make verify` прошёл: 337 native-тестов, 43 host-теста,
  formatting, clang-tidy и три firmware-сборки.
- Добавлены две независимые эталонные трассы музыкального расписания и три проверки
  сохранённых v4 wire-данных/отчёта. Финальные количества: 339 native и 46 host.
- Сохранён небольшой реальный аппаратный образец CSV, отчёта и metadata с хешами.
- Составлен [список legacy и границ удаления](legacy-inventory.md) для R1 и более поздних этапов.
- Production-код, протоколы, настройки сборки и поведение устройства не изменены.

Точные результаты и SHA-256 бинарников — в [r0-verification.json](r0-verification.json).
Успешный verify не означает отсутствие предупреждений: clang-tidy выдаёт существующие
диагностики, а compiler предупреждает о deprecated LVGL API; native-сборка предупреждает
о старых проигнорированных `nodiscard` в тестах.

## Эталон музыкальных сценариев

Две новые проверки в
[test_sequencer.cpp](../../test/test_native/engine/test_sequencer.cpp)
используют числовые позиции, записанные независимо от timing helper-функций:

- `test_baseline_straight_cycle_preserves_events_through_wrap` — swing 50;
- `test_baseline_swung_cycle_preserves_events_through_wrap` — swing 75.

Обе проходят ticks 0–96 включительно, то есть полный 16-шаговый цикл и начало
следующего. Включены шаги 0/1/4/15 с note 60/61/64/75, velocity 100/101/80/90,
gate 100/25/1/100; остальные шаги выключены. Проверяются все 9 событий, порядок,
точная phase, channel 0, launch identity, generation 7 и оставшийся Off на tick 102.

| Контракт | Прямой ритм | Swing 75 |
| --- | --- | --- |
| Старт | On 60 в `(0, 0)` | То же |
| Следующий шаг | Off 60 перед On 61 в `(6, 0)` | Off 60 в `(6, 0)`, On 61 в `(6, 49152)` |
| Gate 25 | Off 61 в `(7, 32768)` | Off 61 в `(8, 16384)` |
| Gate 1 | On 64 в `(24, 0)`, Off в `(24, 3932)` | То же |
| Wrap | Off 75 перед новым On 60 в `(96, 0)` | On 60 в `(96, 0)` раньше планового Off 75 в `(96, 49152)` |

Это **scheduled events**, а не принятые USB-сообщения. При swing=75 предыдущий
плановый Off выходит за следующую границу; dispatcher применяет свою монофоническую
replacement-политику. Не использовать эту трассу как утверждение о перекрытии
реальных звучащих нот или времени доставки на компьютер.

Сохраняемые сценарии доставки уже покрыты
[test_transport_controller.cpp](../../test/test_native/engine/test_transport_controller.cpp):

| Сценарий | Существующий тест |
| --- | --- |
| Внешний Start ждёт измеренный tick, не эхо Clock | `test_external_start_waits_for_measured_tick_and_never_echoes_clock` |
| Continue без повторной атаки | `test_external_continue_waits_for_next_tick_without_retriggering` |
| Clock loss/relock | `test_external_loss_relocks_with_one_clock_before_continue` |
| Stop до/после отложенного On | `test_stop_before_delayed_on_does_not_send_an_off`, `test_stop_after_delayed_on_sends_its_actual_off` |
| Retry сохраняет identity и одно принятие ноты | `test_retry_keeps_delivery_identity_and_commits_note_once` |
| Монофоническое замещение с задержанным On | `test_retrying_swung_projection_gets_early_off_before_same_pitch_replacement` |
| Повторный Stop не дублирует terminal Off | `test_repeated_stop_retries_one_off_before_one_stop` |
| Disconnect требует явного нового Start | `test_disconnect_requires_explicit_start_and_never_replays` |

## Эталон v4 и отчёта

Файлы находятся в
[scripts/tests/fixtures/refactor_baseline_v4](../../scripts/tests/fixtures/refactor_baseline_v4/README.md).
Это byte-for-byte копии части исторического прогона `stage5-5-internal-gate100-68bpm-swing50`:
244 секунды, все 16 шагов включены, gate 100, 68 BPM, swing 50.

Сохранены `diagnostics.csv`, `input-diagnostics.csv`, `runtime-diagnostics.csv`,
`metadata.json`, `report.json`; происхождение каждого файла и SHA-256 — в
`provenance.json`. CSV v4 сохраняет порядок колонок независимо от текущего определения
`V4_COLUMNS`. Исходные `data/` остаются неизменными и исключены из Git.

Три теста в [test_refactor_baseline.py](../../scripts/tests/test_refactor_baseline.py):

1. Сверяют байты исторических файлов с зафиксированными хешами.
2. Сравнивают все три wire-header и расшифрованные метрики с сохранённым отчётом.
3. Повторно строят весь отчёт из CSV и metadata и сравнивают с историческим JSON.
   Подменяются только время создания и значения среды host; смысловые поля не удаляются.

Проверена чувствительность эталона: перестановка двух колонок v4 только в памяти
тестового процесса вызывает ожидаемый провал проверки. Файлы реализации не изменялись.
Новые тесты автоматически входят в `make test-scripts`; native-тесты зарегистрированы
в уже вызываемом `test_sequencer_main`.

Ограничения образца:

- Он снят с firmware revision `d70709299eae627554f9569ed90647421c381647`,
  `working_tree_dirty=true`. Это не новая аппаратная проверка HEAD рефакторинга.
- Raw serial transcript в исходном пакете отсутствует. Для теста восстанавливаются
  только control-рамки `run_started/run_complete`; измеренные CSV остаются исходными.
- Большой raw MIDI CSV не добавлен в Git: его путь и хеш сохранены. Host aggregate
  в тесте берётся из исходного report; повторный анализ raw MIDI не заявляется.
- Firmware acceptance и host timestamps относятся к разным clock domains.
  JSON equality подтверждает отсутствие изменения reporter, а не точность физического MIDI timing.

## Память и известные аппаратные ограничения

| Сборка | Статическая RAM, байт | Flash, байт |
| --- | ---: | ---: |
| production (`rpipico2`) | 169888 | 730128 |
| instrumentation-off | 169588 | 729288 |
| fault scenarios | 173032 | 730904 |

Это отчёт linker/PlatformIO, не замер stack/heap high-water во время работы.

[Принятый аппаратный decision report](../midi-timing-and-ump/05-diagnostics-and-validation/05-hardware-validation.md#итоговый-decision-report--выполнено-2026-09-27)
сохраняет `fail` по границе опроса энкодеров 1250 us; в UI stress наблюдались
3097 us и 6549 превышений. MIDI correctness прошёл только в перечисленных там
нагрузках. Device-local edge timing, end-to-end DAW latency и физический disconnect
не превращаются в выполненные проверки из-за успешного native-набора.

В R0 устройство не прошивалось и аппаратные прогоны не повторялись. Измерительный
spike многоканальной нагрузки остаётся задачей R5 до F3: нынешняя очередь на 16
элементов и quota 8 packets/tick не доказывают возможность 16×16 маршрутов.
Политика совпадающих нот остаётся продуктовым решением до реализации F3.

## Воспроизведение и переход к R1

Из корня проекта:

```sh
make test
make test-scripts
make verify
```

В R1 удаляется поддержка старых live diagnostics v2/v3, но эти эталоны текущего v4
должны оставаться зелёными. Не регенерировать fixture из новой реализации для устранения
регрессии. Намеренное изменение протокола требует отдельной версии и нового эталона,
а изменение музыкальной семантики — соответствующего feature-этапа и явных новых ожиданий.

R0 завершает подготовку исходной точки. R1 и остальные этапы ещё не выполнялись.
