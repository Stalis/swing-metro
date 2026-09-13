# Этап 3. Internal источник tick и фазовые deadlines

Статус: реализовано. Зависит от этапов 1–2.

## Задача

На основе установленного BPM генерировать точные transport ticks и переводить
фазу очереди в фактический deadline. Внутренний период управляет только этим
низкоуровневым источником, не `Sequencer`.

## Контракт

- Период tick: `60'000'000 / (BPM * 24)` мкс с накоплением дробного остатка,
  чтобы не накапливался дрейф.
- Предпочтительная реализация — hardware timer/alarm: IRQ публикует счётчик
  новых ticks, а основной loop забирает их и вызывает engine. Если API платы не
  позволяет безопасно применить alarm на этом этапе, допустим временный poller
  `micros()`, но его интерфейс обязан быть идентичен и иметь явную задачу замены.
- После границы `T` deadline события вычисляется как
  `tickStartUs + periodUs * phase / 65536`. Ширина промежуточного умножения —
  минимум 64 bit.
- При изменении BPM следующий ещё не открытый tick и его phase-deadline используют
  новый период. Уже отправленные события не переигрываются; заданный deadline
  текущего открытого tick пересчитывается до отправки, если это возможно.
- При Start внутренний генератор фиксирует новую фазу и публикует первый tick;
  Stop отменяет ожидающие phase deadlines и больше не создаёт ticks.

## Фактическая реализация

- `src/engine/internal_tick_source.h`: header-only `InternalTickSource` и
  фиксированное SPSC `InternalTickStore`. Период `60'000'000 / (BPM * 24)`
  распределяет остаток целыми микросекундами. Запись tick содержит timestamp и
  период открытого интервала.
- `src/engine/internal_tick_consumer.h`: header-only consumer владеет только
  логикой `Transport` и `MidiEventQueue`; USB sink передаётся вызывающим кодом.
  Он отправляет `F8` и phase-0 события при открытии tick, затем вычисляет
  ближайший deadline через `periodUs * phase / 65536` с 64-bit произведением.
- `MidiEventQueue::nextPosition()` добавлен как read-only доступ к ближайшей
  позиции. Для `F8` зарезервирован один из восьми пакетов tick: не-clock события
  ограничены семью, clock допускается восьмым. Поэтому note scheduling не может
  вытеснить clock.
- `src/drivers/pico_internal_tick_alarm.{h,cpp}`: one-shot adapter RP2350/Pico
  SDK на `pico/time.h`. IRQ только публикует timestamped tick и переармливает
  `add_alarm_in_us`; USB, очередь и Transport остаются на main core. Критическая
  секция сериализует callback с `cancel_alarm`/rearm при Stop и смене BPM.
- Новый источник намеренно не подключён в `main.cpp`; legacy `Sequencer::update`,
  `MidiClockTransmitter`, `midi_step_boundary` и прямая отправка нот не менялись.

## Policy

- `InternalTickConsumer` обрабатывает максимум 4 накопленных tick за проход,
  сохраняет их временной порядок, считает late delivery и сбрасывает остаток с
  `droppedTicks`. Неограниченный burst запрещён.
- `InternalTickSource::setBpm()` сбрасывает дробный остаток: следующий ещё не
  открытый tick использует новый период. `InternalTickConsumer::setBpm()`
  пересчитывает deadline только неотправленного phase-события открытого tick;
  уже отправленные события не переигрываются.
- При будущем wiring изменение BPM вызывает оба API на main core: сначала
  `PicoInternalTickAlarm::setBpm()` для следующей границы, затем
  `InternalTickConsumer::setBpm()` для открытого tick.
- `start()` consumer посылает Start и ждёт опубликованный первый tick; `stop()`
  посылает Stop, очищает очередь и отменяет phase deadline; `continuePlayback()`
  посылает Continue.

## Проверки

Native Unity: 40/120/240 BPM и дробный остаток, deadlines 0/50/75%, порядок
`F8 -> Off -> On`, Start/Stop/Continue, BPM change, wraparound `micros()`,
ограниченный catch-up и диагностика, future queue events, read-only ближайшая
позиция и резерв clock slot. На устройстве всё ещё нужно измерить 24 PPQN,
отсутствие дрейфа на нескольких минутах и phase-offset на MIDI monitor/логическом
анализаторе.

## Вне объёма

External clock и миграция нот секвенсора.

## Передача результата

Выбран `pico/time.h` one-shot alarm adapter; API и catch-up policy описаны выше.
Аппаратные замеры остаются задачей при подключении источника в отдельном этапе.
