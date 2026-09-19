# Шаг 2.5. Аппаратная проверка и baseline

Статус: запланировано. Зависит от шагов 2.1–2.4.

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
