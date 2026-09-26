# 5.5 Итоговая аппаратная валидация

Статус: запланировано. Выполняется на production-прошивке после завершённых 5.1–5.4.

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

## 5.5.3 UI и input stress

Оператор выполняет один фиксированный сценарий во время internal playback: переключение Main/Step,
вращение всех трёх энкодеров, изменение Tempo, swing, note, velocity и Gate, открытие и отмена MIDI
Clock и Save/Load модалок. Порядок и временные окна задаются чек-листом; случайные действия не
считаются воспроизводимым сценарием.

Capture проверяет непрерывность Clock и Note lifecycle, а runtime/input snapshots — max encoder
sample interval, crossings 1,250 us, inclusive LVGL handler и flush. Результат должен отдельно
ответить, подтверждён ли текущий polling bound; уже известное аппаратное превышение этапа 3 не
переименовывается в pass. Решение о shift-register/PIO остаётся отложенным до новой платы.

## 5.5.4 Storage и Reset program safety

На остановленном транспорте проверить Save, Load, Cancel и `Reset program`: confirmation открывается
с `No`, `No` не меняет программу, `Yes` возвращает tempo/swing/volume/MIDI mode и все 16 шагов к
`Program{}` и сохраняет current program. После reboot состояние должно восстановиться как initial.
Пользовательские слоты не должны измениться.

Во время playback попытка storage не должна выполнять LittleFS I/O; открытие host-scoped модалки
сначала безопасно останавливает transport. Capture подтверждает финальный Note Off/Stop и отсутствие
Clock во время записи flash. Проверка включает Cancel из action и slot chooser.

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

## Порядок выполнения

Шаги выполняются по одному: 5.5.1 → 5.5.2 → 5.5.3 → 5.5.4 → 5.5.5. Автоматические 5.5.1 и
5.5.2 готовятся и software-тестируются до аппаратного прогона. 5.5.3 и 5.5.4 начинаются только когда
оператор находится рядом с устройством. После каждого изменения firmware выполняется `make verify`.
