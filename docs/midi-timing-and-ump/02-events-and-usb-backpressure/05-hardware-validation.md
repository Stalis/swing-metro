# Шаг 2.5. Аппаратная проверка и baseline

Статус: выполнено 2026-09-20. Зависит от шагов 2.1–2.4.

## Цель

На Pico 2 W подтвердить, что typed events и result-aware delivery не внесли timing
регрессию, а firmware stack-acceptance counters согласуются с host MIDI capture при
нормальном USB-соединении. Отдельно зафиксировать доступность реальной проверки
disconnect/backpressure.

## Базовый контекст

Этап 1 завершён четырьмя полными 244-секундными capture:

- два free run на 68 BPM: по 6 637 из 6 637 Clock;
- free и loaded run на 240 BPM: по 23 424 из 23 424 Clock;
- во всех run нулевые overflow/discard/missed target и длинные интервалы;
- host abs jitter p95 274–338 мкс, p99 639–952 мкс;
- максимальная Pico tick/F8 lateness 602 мкс, `process()` 713 мкс.

Локальные baseline CSV перечислены в
`../01-timing-correctness/05-hardware-investigation.md`. Host monitor
`scripts/pico_midi_run.py` одновременно захватывает CoreMIDI и Serial diagnostics.

## Условия сравнения

Перед каждым измеряемым run выполнить `make verify`, загрузить текущую firmware и
reboot Pico. Использовать тот же Pico 2 W, USB cable/path, macOS host и Python MIDI
backend. Не запускать MIDI Monitor параллельно.

Обязательные run после этапа 2:

1. Free: internal clock, 68 BPM, swing 50, 244 с, без interaction.
2. Stress: internal clock, 240 BPM, swing 50, 244 с, активное редактирование
   note/velocity без Save/Load и ручного Stop.

Gate ещё отсутствует, поэтому не включать его в сценарий. Каждый output prefix новый;
скрипт не перезаписывает baseline.

## Проверяемые балансы

Для нормального соединения после quiescent Stop:

```text
firmware Clock attempts
    == firmware stack-accepted Clock + Clock policy drops

firmware stack-accepted Clock
    == host-received Clock
```

В нормальном free/stress run ожидается zero policy drops и полное равенство всех трёх
счётчиков. Note On/Off сравниваются по ordered message stream; Stop должен быть
последним transport realtime сообщением, без старого On после него.

Acceptance означает результат TinyUSB write, а второе равенство — наблюдение данного
конкретного host capture. Ни одно из них не объявляется общей гарантией доставки DAW.

## Disconnect/backpressure hardware branch

Физическое отключение единственного USB кабеля одновременно убирает питание Pico и
не является валидной проверкой session recovery без отдельного питания/debug path.
Реальный disconnect выполнять только если устройство остаётся запитанным и Serial
diagnostics доступны независимым каналом либо firmware имеет заранее согласованный
детерминированный test hook.

Не добавлять production backdoor или timing-path Serial print только ради теста.
Если подходящего setup нет, отметить hardware disconnect как недоступный; exhaustive
fake-sink tests шага 2.4 остаются обязательными, но не называются аппаратной проверкой.

## Анализ

- Сравнить Clock count, long/short intervals, abs jitter p95/p99 и drift с этапом 1.
- Сравнить callback/tick/event/acceptance lateness и max process duration.
- Проверить outbox high-water, attempts/pass, RetryLater/disconnect/policy counters.
- Любой host gap сначала сопоставить с attempt/accepted balance. Aggregate maxima не
  приписывать конкретному ordinal без дополнительного trace.
- Если attempts > accepted, дефект находится до/в stack acceptance path этапа 2.
- Если accepted > host при чистом capture, сохранить артефакты и передать конкретную
  host/USB гипотезу; не объявлять adapter причиной без сигнала.

## Результат

Проверка выполнена на том же Pico 2 W и macOS/CoreMIDI path с firmware revision
`7c3ba26`. Перед каждым измеряемым run выполнены `make verify`, upload и reboot;
MIDI Monitor параллельно не запускался. Оба run использовали internal clock, swing 50
и длились 244 с. Во втором run активно редактировались note и velocity разных шагов
без Save/Load и ручного Stop.

| Run | Clock attempt / accepted / host | Long / short / estimated missing | Host abs jitter p95/p99 | Drift | Tick/F8 / event acceptance lateness | Max process |
| --- | ---: | --- | --- | ---: | --- | ---: |
| Free 68 | 6 637 / 6 637 / 6 637 | 0 / 0 / 0 | 569 / 716 мкс | 716 мкс | 512 / 318 мкс | 1 234 мкс |
| Stress 240 | 23 424 / 23 424 / 23 424 | 0 / 0 / 0 | 502 / 690 мкс | 673 мкс | 885 / 779 мкс | 1 829 мкс |

В обоих run Clock `RetryLater`, disconnect, coalescing, expiry, capacity failure и
safety-stop counters равны нулю. Максимальная глубина outbox равна 3, максимум
попыток за один publication pass — 3; после Stop scheduled и outbox пусты. Transport
имеет 2 из 2 accepted сообщения. Free run имеет 2 214 из 2 214 accepted note events;
stress — 7 800 из 7 800, то есть 3 900 упорядоченных пар Note On/Off. Stress capture
содержит 51 различную ноту и 27 velocity, поэтому interaction действительно попало в
MIDI stream. Stop является последним MIDI событием, после него нет старого Note On.
Две scheduled note были отменены финальным Stop; terminal abandonment равен нулю.

Относительно этапа 1 correctness-level timing не регрессировал: Clock не потерян и не
дублирован, длинных/коротких интервалов нет, drift остаётся меньше 1 мс, а p99 лежит
в прежнем диапазоне 639–952 мкс. При этом p95 host jitter вырос с прежних 274–338 до
502–569 мкс, максимальная tick/F8 lateness — с 602 до 885 мкс, а `process()` — с 713
до 1 829 мкс. Эти сдвиги зафиксированы как ограничение нового baseline: они не
создали balance gap или deadline miss, но их нельзя трактовать как полное отсутствие
изменений распределения lateness.

Локальные артефакты, намеренно не добавленные в Git:

- `data/midi-stage-2-5-free-68-20260920-0944-{midi,diagnostics,summary}.csv`;
- `data/midi-stage-2-5-stress-240-20260920-0952-{midi,diagnostics,summary}.csv`.

Физическая disconnect/backpressure проверка недоступна: единственный USB cable
одновременно питает Pico и несёт Serial/MIDI. Fake-sink тесты шага 2.4 покрывают эти
политики детерминированно, но не считаются аппаратным доказательством recovery.

## Готовность шага

- Оба обязательных run завершены с сохранёнными MIDI, diagnostics и summary CSV.
- Нет timing-регрессии, потерянных/двойных Clock или необъяснённого balance gap.
- Stack acceptance и host observation названы раздельно.
- Hardware disconnect либо выполнен в валидном setup, либо явно отмечен недоступным.
- Документы этапа 2 содержат реализацию, численные результаты и ограничения.
- Выполнен `make verify`; этап 2 отмечен завершённым.

## Вне объёма

DAW-specific latency guarantee, USB protocol analyzer, Gate/load matrix этапа 5,
UMP transport и автоматическое восстановление внешнего синтезатора.
