# Этап 3. Регулярный ввод и debounce по времени

Статус: запланировано. Зависит от завершённых этапов 1–2.

## Цель

Сделать считывание кнопок и энкодеров предсказуемым и независимым от числа оборотов
основного цикла, сохранив ввод на core 0. Решение оставить polling либо перейти к
IRQ/PIO принимается только после аппаратных измерений шага 3.4.

## Последовательность

| Шаг | Документ | Результат |
| --- | --- | --- |
| 3.1 | [Расписания input polling](03-input-scheduling-and-debounce/01-scheduler-contracts.md) | Независимые периоды matrix и encoder, wrap-safe scheduler и no-fake-catch-up policy. |
| 3.2 | [Debounce и временная семантика](03-input-scheduling-and-debounce/02-time-debounce-and-input-semantics.md) | Time-based debounce с определёнными observation/confirmation timestamps и long-press basis. |
| 3.3 | [Наблюдаемость и интеграция](03-input-scheduling-and-debounce/03-observability-integration-and-native-regression.md) | Repository-side diagnostics, firmware integration и deterministic native regression tests. |
| 3.4 | [Аппаратная проверка и решение capture](03-input-scheduling-and-debounce/04-hardware-midi-input-validation.md) | MIDI/input measurements и evidence-based polling-vs-IRQ/PIO decision. |

Шаги выполняются строго последовательно. Завершение шага 3.3 означает только готовность
репозитория к измерению; оно не утверждает, что polling выдерживает encoder bound. Если
polling не проходит критерии шага 3.4, этап 3 остаётся незавершённым, а документируется
условный следующий implementation step для bounded IRQ/PIO capture.

## Общие ограничения

- MIDI service и применение команд остаются на core 0; LVGL остаётся на core 1.
- Не выполнять несколько чтений одного GPIO состояния для компенсации пропущенного
  deadline: это не восстанавливает пропущенные физические переходы.
- IRQ/PIO, если потребуется, не вызывает application code, USB, Serial или LVGL из
  interrupt/producer context.
- Не читать и не писать LittleFS при работающем transport.
- Чистую временную логику размещать в native-testable module и регистрировать её в
  `test/test_native/main.cpp`.
- Для firmware changes выполнять `make verify`; native tests не заменяют аппаратную
  проверку GPIO, USB или MIDI timing.

## Вне объёма этапа

Перенос ввода на core 1, межъядерная очередь команд, изменение UX органов управления,
автоматизация физических жестов и реализация IRQ/PIO до результата шага 3.4.
