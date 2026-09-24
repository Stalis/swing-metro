# Этап 4. Gate Percent для ноты

Статус: запланировано. Зависит от этапов 1–3; незавершённый IRQ/PIO follow-up этапа 3 не блокирует software work этапа 4.

## Цель

Добавить сохраняемый Gate 1–100% для каждого шага, независимый Note Off по его
музыкальному deadline и его редактирование в Step Settings. Этап сохраняет режим
одной активной ноты и делает её lifecycle корректным при очереди и USB backpressure.

## Общие контракты

- Gate по умолчанию равен 100%; `0` не является mute, поскольку его задаёт `enabled`.
- Длительность начинается в назначенной swung позиции Note On, а не при USB acceptance.
  Изменение Gate влияет только на будущие, ещё не запланированные launches.
- Gate 100% равен ровно шести Clock ticks. Gate не меняет Clock, swing или позицию On.
- При равной позиции порядок событий: Clock, затем Note Off, затем Note On.
- Один владелец transport/sequencer отвечает за accepted и projected sounding state.
  Идентичность launch, а не только pitch, защищает новую ноту от старого Off.
- LittleFS нельзя читать или писать при работающем transport. UI получает только
  согласованный снимок и не вызывает LVGL с core 0.

## Последовательность

| Шаг | Документ | Результат |
| --- | --- | --- |
| 4.1 | [Домен Gate и persistence программы](04-note-gate/01-gate-domain-and-program-persistence.md) | Выполнено: Gate в модели и совместимый TLV codec/storage. |
| 4.2 | [Deadline Gate и независимый Note Off](04-note-gate/02-gate-deadline-and-independent-note-off.md) | Выполнено: точная phase-математика и атомарное парное scheduling. |
| 4.3 | [Monophonic identity и delivery lifecycle](04-note-gate/03-monophonic-identity-and-delivery-lifecycle.md) | Выполнено: финальные overlap, backpressure и session semantics. |
| 4.4 | [Ввод и UI](04-note-gate/04-input-and-ui.md) | `AdjustGate`, снимок и экран Step Settings. |
| 4.5 | [Аппаратная проверка](04-note-gate/05-hardware-validation.md) | Измерения на устройстве и MIDI host capture. |

Шаги выполняются строго последовательно; каждый должен оставлять repository build и
native suite зелёными. Шаг 4.2 намеренно является промежуточным scope: он не объявляет
окончательные overlap/backpressure semantics до шага 4.3.

## Зависимости

- 4.1 предоставляет валидный Gate и round-trip программы для 4.2 и 4.4.
- 4.2 предоставляет absolute deadline, correlation metadata и расчёт capacity для 4.3.
- 4.3 фиксирует delivery lifecycle, необходимый UI-интеграции и hardware assertions.
- 4.4 предоставляет пользовательский путь для 4.5; 4.5 проверяет весь software result.

## Общие ограничения

- Не менять container version, CRC contract, лимит payload 255 bytes или старый
  трёхбайтный `TAG_STEPS` без отдельной совместимой миграции.
- Не делать файловых операций в running transport и не подменять native proof
  аппаратным измерением либо USB acceptance host delivery.
- Новая hardware-independent logic должна быть доступна native tests; новый test module
  требует явной регистрации entry point в `test/test_native/main.cpp`.
- Для firmware-изменений выполнять `make verify`; hardware результат фиксировать с
  условиями run и без добавления `data/` или `logs/` artifacts в Git.

## Вне объёма

Полифония, MPE, tie, Gate больше 100%, изменение swing-формулы, UMP, DAW latency
guarantee, IRQ/PIO реализация Stage 3 и filesystem operations во время playback.
