# Этап 4. External источник tick

Статус: реализовано. Зависит от этапов 1–3.

## Задача

Подключить входящие `0xF8` к тому же transport API, сохранив существующие
Start/Continue/Stop и loss/relock. Уточнить период для phase-deadlines без
превращения BPM в источник музыкской позиции.

## Контракт

- Каждый валидный внешний `0xF8` публикует ровно один transport tick, если
  transport running. Он также обновляет фильтр `periodUs` независимо от running.
- Оценка периода — сглаженная, допускает дробный BPM в UI. BPM не округляется и
  не используется для продвижения внешнего транспорта.
- После фактической границы `T` phase deadline использует актуальную оценку
  следующего интервала. Новый входящий `0xF8` не может ретроспективно исправить
  уже отправленное событие; он влияет на будущие deadlines.
- Если следующий `0xF8` пришёл раньше ожидаемого phase-deadline, обработать его
  сначала как новую границу и не дать старому событию перескочить через неё.
  Эта race-политика должна быть реализована в одном владельце deadline.
- Потеря clock останавливает transport, отменяет pending phase-events и закрывает
  активную ноту. Возврат требует обычного external Start/Continue согласно
  существующей выбранной семантике.

## Реализация

- `ExternalMidiClock::handle()` возвращает timestamped `tickRecord` для каждого
  принятого `F8` при running transport, а также Start/Continue/Stop и status.
  `periodUs()` и fixed-point `bpmMilli()` доступны без округления; `bpm()` оставлен
  для legacy UI.
- `MidiDispatcher` внутри `TransportController` владеет общими phase deadlines,
  queue consumption и packet notifications. `Internal` посылает `F8` и
  `FA`/`FC`; `External` не посылает master ни clock, ни transport-команды.
- USB receive остаётся в `UsbMidiRealtimeReceiver::poll()` на main core. Отдельная
  receive SPSC очередь не нужна до wiring этапа 6.
- `main.cpp` передаёт realtime events в `TransportController`; direct MIDI path
  не участвует в production transport.

## Policy

- Период сглаживается на четверть разницы между sample и текущей оценкой. Samples
  вне диапазона 40–240 BPM (`10'416..62'500` мкс на `F8`) не меняют фильтр, но сами
  принятые `F8` остаются границами transport и обновляют timeout.
- Timeout `250'000` мкс переводит status в `Lost`, останавливает transport и
  очищает pending phase events через dispatcher. Только timeout теряет lock.
  Первый `F8` после loss сразу возвращает `Locked` и сохраняет фильтрованную
  оценку периода; последующий Continue запускает transport, а следующий `F8`
  открывает tick без ожидания второго fresh sample.
- Start не сбрасывает текущую external tempo estimate: она сохраняется до timeout.
- Владелец deadline один: если external `F8` обработан до pending deadline, он
  удаляет недоставленные события предыдущего tick перед новой границей. Если
  deadline обработан раньше, событие уже отправлено и не отменяется.
- Bounded catch-up (не более четырёх records за проход), `lateTicks`,
  `droppedTicks` и существующая queue capacity/quota сохранены.

## Проверки

Native Unity покрывает lock/loss/relock и wraparound, smoothing с джиттером,
outlier filter, fixed-point fractional BPM, Start/Stop/Continue, внешний consumer
без echo `F8`/`FA`/`FB`/`FC`, early/late race для phase 50%, а также прежние
internal deadlines, bounded catch-up и diagnostics. Проверка на устройстве ещё
нужна с DAW и hardware-master в диапазоне 40–240 BPM.

## Вне объёма

Генерация секвенсорных нот и swing-параметр.

## Передача результата

Фильтр, timeout, race-policy и API описаны выше. Аппаратные измерения остаются
за интеграцией этапа 6.
