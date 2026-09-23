# Шаг 4.5. Аппаратная проверка Gate

Статус: запланировано. Зависит от шагов 4.1–4.4.

## Цель

На Pico 2 W подтвердить software contracts Gate по наблюдаемому MIDI order/balance и
интеракции UI, не выдавая USB stack acceptance или слуховой результат за host delivery proof.

## Контекст текущего кода

Stage 2 описывает baseline и serial diagnostics в
`docs/midi-timing-and-ump/02-events-and-usb-backpressure/05-hardware-validation.md`.
Stage 3 polling отклонён, но его IRQ/PIO follow-up явно отложен и не блокирует Stage 4
software validation. `make verify` строит firmware и native suite; `main.cpp` экспортирует
diagnostics только в safe/quiescent state. `data/` и `logs/` являются локальными artifacts
и не входят в этот этап документации.

## Зафиксированные решения и контракты

- Перед измеряемым run: `make verify`, upload firmware и reboot board. Использовать один
  Pico 2 W, стабильный USB path и MIDI capture host; не запускать параллельный monitor.
- Проверить internal clock 68 и 240 BPM, Gate 1/25/50/75/100 и swing 50/90. Сравнить
  assigned deadlines с captured ordered On/Off и Clock balance/timing.
- Обязательны wrap, disabled step, overlap, equal deadline и same-pitch scenarios. Проверить
  отсутствие duplicate/stale Off и порядок Clock -> Off -> On на equal position.
- Во время running transport не делать Save/Load и вообще LittleFS I/O. Отдельно в stopped
  состоянии проверить save/load/reboot persistence Gate через user slot/current program.
- External clock и disconnect tests отмечаются `unavailable`, если нет валидного setup для
  входного clock или воспроизводимого USB disconnect; не подменять их симуляцией.

## Файлы и API в scope

Firmware changes не предполагаются. Использовать existing build/upload commands, MIDI capture
tooling и diagnostics parser; после run обновить только этот документ с условиями, revision,
результатами и unavailable cases. Не изменять `data/`, `logs/`, firmware или test code.

## Последовательность работы

1. Выполнить verify, upload и reboot; зафиксировать firmware revision, board, host, USB/MIDI
   path, capture tool и длительность каждого run.
2. Для 68/240 BPM снять baseline Gate 100 и затем 1/25/50/75/100 при swing 50/90; сравнить
   Clock count, accepted/delivered On/Off, deadlines, jitter и ordered events.
3. Выполнить targeted pattern runs: transport/phase wrap, disabled, swung overlap, equal
   deadline и same pitch; проверить independent Off и stale-Off protection.
4. Во время active MIDI открыть Step Settings и менять Gate/step; подтвердить UI route и
   effect только на future unscheduled launches, без global Volume regression.
5. Stop transport, save/load user slot/current program, reboot и повторно load Gate. Отдельно
   зафиксировать external/disconnect outcome либо reason `unavailable`.

## Детерминированные проверки

Перед hardware run должны проходить native checks шагов 4.1–4.4: codec legacy/invalid cases,
phase formula, pair capacity/order, identity/backpressure lifecycle и input/UI snapshot routes.
В capture analysis детерминированно проверять MIDI balance, ordered On/Off sequence и expected
Gate deadline in transport units; не выводить физическую latency из одного слухового теста.

## Готовность шага

- Есть run records для internal 68/240, Gate 1/25/50/75/100 и swing 50/90 с MIDI balance/timing.
- Wrap/disabled/overlap/equal-deadline/same-pitch и UI interaction проверены, либо явный
  blocker записан с conditions и evidence.
- Stopped save/load/reboot подтверждает persistence; отсутствует LittleFS I/O в running run.
- External clock/disconnect имеют result или честную отметку `unavailable`.
- Документ обновлён, локальные artifacts не добавлены в Git; `make verify` прошёл до upload.

## Вне объёма

Реализация IRQ/PIO follow-up Stage 3, полифония/MPE/UMP, DAW latency guarantee, изменение
firmware/test code ради измерения и добавление capture artifacts в `data/` либо `logs/`.

## Передача следующему этапу

Передать Stage 5 measured MIDI balance, timing/order, setup, artifact locations вне Git и
все unresolved hardware limitations. Не переносить неподтверждённые external/disconnect claims.
