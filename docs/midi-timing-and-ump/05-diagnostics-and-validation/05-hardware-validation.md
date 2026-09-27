# 5.5 Итоговая аппаратная валидация

Статус: завершён 2026-09-27; 5.5.1–5.5.5 выполнены.
Выполняется на production-прошивке после завершённых 5.1–5.4.

## Цель и границы

Шаг закрывает оставшиеся аппаратные утверждения этапа 5 и выпускает единый decision report.
Он не повторяет уже принятые данные 5.3 и 5.4 без причины: instrumentation A/B и девять
load/fault-ячеек входят в итоговый пакет ссылками на исходные отчёты. Новая проверка сосредоточена
на музыкальной нагрузке с Gate, external Clock, одновременном UI/input и безопасном storage.

GPIO/логический анализатор остаётся отдельным дополнительным источником локальных времён. Его
отсутствие не подменяется host timestamps и явно отмечается среди неизмеренных величин. MPE и UMP
не входят в этот шаг.

Для каждого нового прогона обязательны revision, SHA-256 ELF, PlatformIO environment, плата и
частота CPU, host/OS/MIDI API, USB topology, режим Clock, BPM, swing, Gate/pattern, вид нагрузки,
длительность и участие оператора. Raw CSV/JSON не коммитятся; в репозиторий попадают сценарий,
агрегированные результаты, выводы и команды воспроизведения.

## 5.5.1 Production internal/Gate matrix

Подготовить воспроизводимый capture на production-прошивке для двух фиксированных программ:

- free baseline: 68 BPM, swing 50, Gate 100%;
- musical load: все 16 шагов включены, Gate циклически 1/25/50/75/100%, swing 50 и 90,
  затем 240 BPM как верхняя граница.

Для каждой ячейки записать host MIDI, diagnostics v4, input и runtime snapshots. Проверить точное
совпадение принятого device Clock с host Clock, ровно один Start/Stop, отсутствие delivery errors и
queue overflow, корректное количество Note On/Off, отсутствие зависших нот и bounded queue depth.
Host interval heuristics и device lateness приводятся раздельно; часы не вычитаются друг из друга.

Автоматизация должна создавать timestamped result directory, metadata, manifest и сводный JSON,
отказываться от перезаписи и возвращать production-прошивку. Конфигурация музыкального паттерна,
если она выполняется оператором, фиксируется как явный gate перед capture и подтверждается
snapshot/чек-листом.

Software harness реализован двумя командами. Сначала включить все 16 шагов, выставить Gate 100 на
каждом и подтвердить подготовку явно:

```sh
make stage5-internal-gate100 STAGE5_PATTERN_CONFIRMED=1
```

Затем выставить Gate шагов в цикле `1/25/50/75/100` (последовательность полностью записывается в
metadata) и запустить четыре ячейки 68/240 BPM × swing 50/90:

```sh
make stage5-internal-mixed-gate STAGE5_PATTERN_CONFIRMED=1
```

Порты обычно определяются автоматически; при необходимости используются `STAGE5_SERIAL_PORT` и
`STAGE5_MIDI_PORT`. Результаты создаются под `data/stage5-5-internal-runs/<timestamp>/`. Для
продолжения указать напечатанный каталог через `STAGE5_OUTPUT_DIR` и `STAGE5_RESUME=1`, не меняя
прошивку или pattern. Без `STAGE5_PATTERN_CONFIRMED=1` hardware run не начинается.

### Фактический результат — выполнено 2026-09-27

Пять production-прогонов на revision `d707092` и одном ELF SHA-256
`2af15b376e59953b343b02d6ed59ab599f7702cccba1056276b162fa8d7e1542` завершились
`pass`: один профиль Gate 100 при 68 BPM/swing 50 и четыре mixed-Gate ячейки при
68/240 BPM × swing 50/90. Во всех профилях были включены 16 шагов; mixed-Gate использовал
цикл `1/25/50/75/100`. Raw evidence сохранён локально в
`data/stage5-5-internal-runs/20260927-002750/` и
`data/stage5-5-internal-runs/20260927-004323/` и не коммитится.

| Проверка | Результат |
| --- | --- |
| Clock | device и host получили ровно по 66 759 Clock |
| Note lifecycle | 11 129 Note On и 11 129 Note Off; 0 unmatched/dangling |
| Delivery | 66 759/66 759 Clock и 22 258/22 258 note events; retry/disconnect/overflow/missed targets равны 0 |
| Очереди | финальный outbox пуст во всех ячейках; max outbox и scheduled depth равны 3 |
| Device timing | max Clock acceptance lateness 739 us; max note acceptance lateness 1 565 us |
| Host intervals | суммарно одна long/short batching-пара в ячейке 240 BPM/swing 50; counts сохранены, absolute drift 174 us |

Все отчёты имеют полные metadata и статус `paired_capture_candidate`, однако metadata честно
фиксирует dirty worktree; фактические байты прошивки привязаны ELF-хешем. Профиль `gate100` по
manifest содержит 16 активных шагов и потому не является note-free baseline; отдельные baseline
данные без операторской нагрузки уже приняты в 5.3 и 5.4. Runtime-window polling во всех пяти
ячейках превысил границу 1 250 us (max 1 804–2 502 us), что учитывается как отдельный `fail` в
5.5.5 и не меняет MIDI correctness verdict этих прогонов.

## 5.5.2 External Clock, loss и relock

Host harness открывает MIDI output устройства и генерирует заранее описанную последовательность:

1. Start и ровный 24 PPQN при 120 BPM;
2. ограниченный deterministic jitter без потери Clock;
3. Stop, затем Continue;
4. пауза, достаточная для loss;
5. восстановление Clock и relock;
6. финальный Stop.

Одновременно Serial capture сохраняет diagnostics. Acceptance проверяет отсутствие echo входного
Clock, корректные Waiting/Locked/Lost переходы, один transport transition на команду, отсутствие
зависшей ноты при Stop/loss и восстановление без локального Clock burst. Генератор хранит фактические
host send timestamps, но не выдаёт их за device-local interrupt latency.

Software harness реализован одной командой. Перед запуском выбрать на устройстве External Clock,
включить все 16 шагов, выставить каждому Gate 100% и явно подтвердить подготовку:

```sh
make stage5-external-clock STAGE5_PATTERN_CONFIRMED=1
```

Команда собирает и загружает production-прошивку, затем автоматически выполняет фиксированный
сценарий при 120 BPM: по 96 ровных и jittered Clock, Stop/Continue, 48 Clock, паузу 400 ms,
один Clock для relock, Continue, ещё 96 Clock и финальный Stop. Она записывает отдельно фактические
host send timestamps, MIDI output устройства, diagnostics v4, input/runtime snapshots, manifest,
metadata, полный report и краткий summary. Результаты создаются в
`data/stage5-5-external-runs/<timestamp>/`; существующие capture-файлы не перезаписываются.

Обычно порты выбираются автоматически. При неоднозначности задать
`STAGE5_SERIAL_PORT`, `STAGE5_MIDI_INPUT_PORT` и `STAGE5_MIDI_OUTPUT_PORT`; прежний
`STAGE5_MIDI_PORT` служит общим fallback для обоих MIDI-направлений. Список MIDI endpoints можно
посмотреть командой `.venv/bin/python scripts/stage5_external_clock_validation.py --list-midi-ports`.

Run считается успешным только при отсутствии realtime echo, ровно одном intentional Clock loss,
двух отправленных Stop и одном Start с двумя Continue, наличии нот до loss и после relock,
сбалансированном Note On/Off lifecycle, точном совпадении device/host note counts, пустом outbox и
нулевых delivery/queue ошибках. Device `session_ends_stop` при этом равен трём: кроме двух команд
Stop он учитывает безопасную очистку исходной сессии перед Start. Переходы Waiting/Locked/Lost
подтверждаются поведением транспорта и device counters;
покадрового device-local trace состояний и измерения interrupt latency этот сценарий не заявляет.

### Фактический результат — выполнено 2026-09-27

Production-прогон `stage5-5-external-clock` на revision `de20b04` и ELF SHA-256
`9aa75ac5ea599e774396253af0bb20af3a4825439209ee0302b0f155b8b2a59e` завершился `pass`.
Условия: Raspberry Pi Pico 2 W, 150 MHz, прямое USB-подключение, CoreMIDI на arm64 macOS,
External Clock 120 BPM, swing 50, все 16 шагов включены с Gate 100%, без действий оператора во
время capture. Воспроизводимый raw evidence сохранён локально в
`data/stage5-5-external-runs/20260927-110825/` и не коммитится.

| Проверка | Результат |
| --- | --- |
| Входной transport/Clock stimulus | 1 Start, 2 Continue, 2 Stop и 337 Clock; intentional loss pause 400.806 ms |
| Realtime echo | 0 исходящих Clock/Start/Continue/Stop; device Clock и transport delivery attempts равны 0 |
| Note lifecycle | 54 Note On и 54 Note Off; 0 unmatched/dangling; 39 Note On до loss и 15 после relock |
| Loss/relock | ровно 1 `session_ends_external_clock_lost`; после relock ноты восстановились |
| Delivery | 108 из 108 note events приняты с первой попытки; retry/disconnect/overflow/safety-stop равны 0 |
| Очереди | финальный outbox пуст; max outbox depth 2, max scheduled depth 3 |
| Device timing | max external tick processing lateness 11 us; max note first/acceptance lateness 8 us |
| Host stimulus | max absolute scheduling error 3.790 ms; это host scheduling, не device interrupt latency |

Все 14 автоматических acceptance checks прошли. Значение `session_ends_stop = 3` соответствует двум
входным Stop и безопасной очистке исходной сессии перед Start; `session_generation_advances = 3`
соответствует Start и двум Continue. Тем самым подтверждены отсутствие realtime echo и локального
Clock burst, корректные loss/relock, transport lifecycle и отсутствие зависших нот.

Input polling остаётся отдельным известным ограничением: максимум encoder sample interval составил
2,064 us при границе 1,250 us, а runtime window зафиксировал 511 превышений. Это не нарушило MIDI
correctness данного прогона и не переименовывается в `pass`; результат переносится как evidence в
5.5.3 и в итоговый decision report 5.5.5. Максимумы LVGL handler и display flush составили
42,238 us и 2,204 us соответственно; из-за inclusive/nested измерения они не суммируются как
независимая CPU-нагрузка.

## 5.5.3 UI и input stress

Оператор выполняет один фиксированный сценарий во время internal playback: переключение Main/Step,
вращение всех трёх энкодеров, изменение Tempo, swing, note, velocity и Gate, открытие и отмена MIDI
Clock и Save/Load модалок. Порядок и временные окна задаются чек-листом; случайные действия не
считаются воспроизводимым сценарием.

Capture проверяет непрерывность Clock и Note lifecycle, а runtime/input snapshots — max encoder
sample interval, crossings 1,250 us, inclusive LVGL handler и flush. Результат должен отдельно
ответить, подтверждён ли текущий polling bound; уже известное аппаратное превышение этапа 3 не
переименовывается в pass. Решение о shift-register/PIO остаётся отложенным до новой платы.

### Фактический результат — выполнено 2026-09-27

На production-прошивке revision `779a5f7` выполнен 120-секундный internal playback capture с
одновременной работой оператора. Во время прогона Tempo изменялся примерно 120 → 240 → 68 BPM,
редактировались note, velocity и Gate в Step Settings, изменялись swing и volume, выполнялись
переходы Main/Step и отмена MIDI Clock chooser. Save/Load не открывался: этот сценарий намеренно
оставлен для специализированной проверки 5.5.4, где storage сначала безопасно останавливает
transport. Raw evidence сохранён локально в
`data/stage5-5-ui-input-runs/20260927-150925/` и не коммитится.

| Проверка | Результат |
| --- | --- |
| Transport и Clock | 1 Start, 1 Stop; device и host получили ровно по 5 853 Clock |
| Note lifecycle | 976 Note On и 976 Note Off; 0 unmatched и 0 dangling |
| Delivery и очереди | 0 retry/disconnect/overflow/missed targets; финальный outbox пуст; max outbox и scheduled depth равны 3 |
| Device lateness | max internal tick, F8 и queued-event lateness — 931 us |
| Input polling | max interval в окне прогона 3 097 us; 6 549 интервалов выше границы 1 250 us — `fail` |
| UI diagnostics | inclusive max LVGL handler 99 049 us; display flush 6 320 us |

Единый fixed-BPM анализ всего capture для такого сценария неприменим: смена Tempo закономерно
создаёт интервалы, которые он ошибочно классифицирует как missing/long/short. Поэтому отдельно
проверены три стационарных окна; p95/p99 ниже — nearest-rank абсолютное отклонение от периода
соответствующего Tempo.

| Окно | Clock intervals | Mean / median | p95 / p99 abs jitter | Min / max | Long / short |
| --- | ---: | ---: | ---: | ---: | ---: |
| 120 BPM, 0–15 s | 720 | 20 826.723 / 20 825.062 us | 513.459 / 669.209 us | 16 528.208 / 21 613.083 us | 0 / 0 |
| 240 BPM, 35–50 s | 1 439 | 10 424.040 / 10 403.917 us | 663.750 / 934.916 us | 8 923.750 / 11 904.000 us | 0 / 0 |
| 68 BPM, 60–120 s | 1 632 | 36 764.883 / 36 760.313 us | 678.377 / 1 041.003 us | 30 208.083 / 43 108.000 us | 0 / 0 |

Итог: MIDI correctness при live Tempo и UI/input-нагрузке подтверждён, но заявленный polling bound
не выдержан. Прогон имеет ограниченную воспроизводимость: у него нет полного metadata/manifest и
точных timestamp-маркеров действий оператора; boot-cumulative diagnostics также включают
предыдущий external capture на том же boot. Поэтому для MIDI используются только согласованные
host counts и timed-run window, а cumulative max service/input interval не приписываются этому
прогону. Ограничение polling переносится в decision report 5.5.5; encoder shift-register/PIO
остаётся deferred до новой платы.

## 5.5.4 Storage и Reset program safety

На остановленном транспорте проверить Save, Load, Cancel и `Reset program`: confirmation открывается
с `No`, `No` не меняет программу, `Yes` возвращает tempo/swing/volume/MIDI mode и все 16 шагов к
`Program{}` и сохраняет current program. После reboot состояние должно восстановиться как initial.
Пользовательские слоты не должны измениться.

Во время playback попытка storage не должна выполнять LittleFS I/O; открытие host-scoped модалки
сначала безопасно останавливает transport. Capture подтверждает финальный Note Off/Stop и отсутствие
Clock во время записи flash. Проверка включает Cancel из action и slot chooser.

### Фактический результат — выполнено 2026-09-27

На остановленном transport слот 0 использован как явно разрешённый тестовый слот. В него сохранена
контрольная программа 137 BPM, swing 63, volume 77, Internal Clock и включённый Step 1 с C4,
velocity 91 и Gate 42. Ручной сценарий дал следующие результаты:

- `Cancel` из action chooser закрыл модалку без изменения программы;
- `Cancel` из Load slot chooser закрыл модалку без изменения программы;
- после изменения текущей программы `Load → Slot 0` точно восстановил контрольные значения;
- confirmation `Reset program` открылся с выбранным `No`; подтверждение `No` сохранило программу;
- `Reset program → Yes` установил `Program{}`: 120 BPM, swing 50, volume 100, MIDI Clock Off,
  все 16 шагов выключены, параметры шагов C2 / velocity 127 / Gate 100;
- последующий `Load → Slot 0` восстановил контрольную программу, то есть Reset не изменил
  пользовательский слот;
- повторный Reset и аппаратный reboot восстановили initial-состояние из current program.

Playback safety проверен отдельным clean-boot capture с Internal Clock 120 BPM, swing 50,
всеми 16 шагами и Gate 100%. Во время активного нотного потока оператор открыл Save/Load и после
остановки выполнил `Load → Slot 0`; UI показал `Complete` и восстановил контрольную программу.
Raw evidence сохранён локально в
`data/stage5-5-storage-runs/20260927-playback-storage/` и не коммитится.

| Проверка | Результат |
| --- | --- |
| Transport и Clock | 1 Start, 1 Stop; device и host получили ровно по 1 024 Clock; 0 long/short и 0 Clock после Stop |
| Terminal note lifecycle | 171 Note On и 171 Note Off; последний Note Off непосредственно перед Stop; 0 unmatched/dangling |
| Storage safety | `session_ends_storage = 1`, `storage_discards = 1`, одна активная scheduled note удалена причиной storage |
| Delivery | Clock 1 024/1 024, notes 342/342, transport 2/2; retry/disconnect/overflow равны 0 |
| Terminal delivery | `terminal_note_off_abandoned_count = 0`, `terminal_stop_abandoned_count = 0` |
| Очереди | финальный outbox пуст; max outbox и scheduled depth равны 3 |

Тем самым подтверждено, что открытие storage во время playback сначала завершает MIDI session с
Note Off и Stop, а LittleFS-операция выполняется уже без последующих Clock. Direct capture не имеет
полного metadata/manifest и поэтому помечен `insufficient_metadata`; он используется как аппаратное
correctness evidence, но не как сравнимый performance run. Первая проба с одним активным шагом
также прошла без зависших нот, однако остановка пришлась между нотами; в decision report входит
только clean-boot повтор, намеренно поймавший активный scheduled Note Off.

## 5.5.5 Decision report

Свести новые результаты с принятыми отчётами 5.3 и 5.4 в одну таблицу. Для каждого утверждения
указать `pass`, `fail`, `not measured` или `deferred`, путь к evidence и ограничение метода.
Обязательно дать отдельные ответы:

- сохранились ли Clock loss, двойные интервалы или накопительный drift после этапа 1;
- выдерживает ли текущая delivery/queue модель Gate-нагрузку;
- достаточен ли polling ввода на текущей плате;
- есть ли измеримое основание вводить DMA дисплея или менять планирование;
- какие проверки требуют логического анализатора, новой encoder-платы или DAW.

Найденные оптимизации не реализуются внутри отчёта. Correctness-дефект блокирует завершение 5.5 и
получает отдельный фикс с регрессией; performance-возможность оформляется отдельной задачей.

### Итоговый decision report — выполнено 2026-09-27

Вердикты ниже относятся только к явно проверенным условиям. `pass` означает, что заявленный
correctness-критерий выдержан; `fail` сохраняется как обнаруженное ограничение, а `not measured` и
`deferred` не переименовываются в успех.

| Утверждение | Вердикт | Evidence | Ограничение метода |
| --- | --- | --- | --- |
| Instrumentation не нарушает baseline MIDI correctness | `pass` | [5.3 A/B](03-safe-reporting-and-instrumentation-cost.md#аппаратный-результат-2026-09-26): 39 822/39 822 Clock | Есть небольшой tail overhead: median paired ON−OFF +20 us Clock, +39 us Note, +482 us service interval; это не CPU utilization |
| Internal Clock не теряется в baseline 40/120/240 BPM, swing 50/90 | `pass` | [5.4 matrix](04-load-and-fault-scenarios.md#hardware-result): все шесть ячеек с точным device/host count | CoreMIDI timestamps не являются device-local временем |
| Retry, sustained backpressure и disconnect завершаются предсказуемо | `pass` | [5.4 matrix](04-load-and-fault-scenarios.md#hardware-result): одна recovery, один safety stop, один disconnect | Disconnect детерминированно инжектирован; физическое отключение кабеля не измерено |
| Production delivery/queue выдерживает Gate 1–100% до 240 BPM | `pass` | [5.5.1](#551-production-internalgate-matrix): 66 759/66 759 Clock, 22 258/22 258 note events, depth ≤ 3 | Один host long/short batching pair без потери count; профиль Gate 100 не note-free |
| External Clock loss/relock не создаёт echo, burst или зависшие ноты | `pass` | [5.5.2](#552-external-clock-loss-и-relock): 14/14 checks, 337 входных Clock, 54/54 notes | Нет покадрового device-local state trace и interrupt-latency measurement |
| Live Tempo и UI/input-нагрузка сохраняют MIDI lifecycle | `pass` | [5.5.3](#553-ui-и-input-stress): 5 853/5 853 Clock, 976/976 notes | Ручной прогон без полного metadata и точных action timestamps; fixed-BPM анализ применён только к стационарным окнам |
| Storage/Reset безопасны для transport, current program и user slots | `pass` | [5.5.4](#554-storage-и-reset-program-safety): terminal Note Off → Stop, 1 024/1 024 Clock, reboot restore | Ручные state-проверки; direct MIDI capture имеет `insufficient_metadata` |
| Encoder polling выдерживает границу 1 250 us на текущей плате | `fail` | [5.5.1](#551-production-internalgate-matrix), [5.5.2](#552-external-clock-loss-и-relock), [5.5.3](#553-ui-и-input-stress): max timed-window 3 097 us и 6 549 crossings в UI stress | Не найдено нарушения MIDI correctness, но сам polling bound не выполнен |
| DMA дисплея необходим для MIDI correctness | `deferred` | В проверенных нагрузках MIDI loss отсутствует; flush max 1 159 us в 5.5.1 и 6 320 us в UI stress | Inclusive LVGL/flush метрики не дают causal attribution и не измеряют CPU utilization |
| GPIO/device-local edge timing | `not measured` | Host и firmware counters/timestamps сохранены раздельно | Нужны GPIO-маркеры и логический анализатор для ISR-to-edge, scheduler-to-edge и связи display stall с MIDI edge |
| End-to-end доставка до музыкального приложения | `not measured` | CoreMIDI capture подтверждает приём хостом | Нужен выбранный DAW и метод сопоставления его clock domain; USB-wire и audio latency здесь не измерены |
| Encoder shift-register/PIO устраняет polling fail | `deferred` | Текущая плата стабильно превышает bound | Нужна новая encoder-плата, затем повтор input/runtime и MIDI stress с тем же acceptance bound |
| MPE/UMP и per-note expressive поток | `deferred` | Не входит в этап 5 | Отдельный ограниченный прототип этапа 6 |

Ответы на обязательные вопросы:

- **Clock loss, двойные интервалы и drift:** потери device/host Clock после этапа 1 не обнаружены.
  Одна long/short пара при 240 BPM сохранила точный count и дала всего 174 us absolute drift, что
  соответствует host batching, а не пропуску или накопительному drift. External intentional loss
  завершился ровно одним loss и корректным relock без локального burst.
- **Gate delivery/queue:** текущая модель выдержала проверенные Gate 1–100%, swing 50/90 и
  68/240 BPM без retry, disconnect, overflow или зависших нот; max depth равен 3.
- **Input polling:** на текущей прямой разводке энкодеров недостаточен относительно заявленной
  границы 1 250 us. Это известное performance/interaction-ограничение, а не обнаруженный MIDI
  correctness-дефект.
- **DMA и планирование:** измеримого основания менять MIDI scheduling нет. DMA дисплея также не
  требуется для доказанной MIDI correctness, но остаётся возможной отдельной оптимизацией после
  causal GPIO/logic-analyzer измерения; одних inclusive максимумов LVGL/flush недостаточно.
- **Недостающая аппаратура:** логический анализатор нужен для device-local edge timing; новая
  shift-register/PIO encoder-плата — для закрытия polling bound; DAW — для end-to-end consumer
  latency и sync. Эти проверки не блокируют принятый MIDI correctness scope этапа 5.

Этап 5 завершён с одним известным ограничением: encoder polling `fail`, а соответствующее
аппаратное изменение остаётся deferred до новой платы. Блокирующих MIDI correctness-дефектов в
проверенном scope не осталось; оптимизации DMA/PIO не реализуются внутри этого отчёта.

## Порядок выполнения

Шаги выполняются по одному: 5.5.1 → 5.5.2 → 5.5.3 → 5.5.4 → 5.5.5. Автоматические 5.5.1 и
5.5.2 готовятся и software-тестируются до аппаратного прогона. 5.5.3 и 5.5.4 начинаются только когда
оператор находится рядом с устройством. После каждого изменения firmware выполняется `make verify`.
