# Этап 5. Миграция секвенсора на ticks

Статус: реализовано для engine/native tests. Интеграция `main.cpp` остаётся этапом 6.

## Задача

Убрать из `Sequencer` `_lastStepAt`, `_stepPeriodUs`, `sync(micros)` и
`update(micros)`. Секвенсор принимает transport-команды, заранее планирует
шаги на небольшом горизонте ticks и кладёт MIDI-события в очередь, но не
отправляет их сам.

## Контракт

- Один владелец считает шесть ticks на шестнадцатую — transport/adapter, не
  `ExternalMidiClock` и не скрытая копия в `Sequencer`.
- Секвенсор поддерживает постоянный небольшой горизонт планирования
  (`SCHEDULING_LOOKAHEAD_TICKS`, первоначально два ticks). При обработке
  transport-позиции он рассчитывает все границы шестнадцатых до этого горизонта,
  ставит события в `MidiEventQueue` и больше не владеет их MIDI-пакетами.
  Очередь остаётся единственным владельцем поставленных пакетов до отправки или
  очистки transport-командой.
- `MidiDispatcher` (реализация transport tick consumer и USB sink) —
  единственный компонент, который читает `MidiEventQueue`, сравнивает текущую
  transport-позицию с позициями событий и отправляет USB-MIDI. Секвенсор не
  читает очередь, не знает о дедлайнах фаз и не вызывает USB API.
- На каждой запланированной boundary `T`: сначала поставить Note Off активной
  ноты с `(T, 0)`, затем Note On включённого шага с `(T, configuredPhase)`.
  Для zero swing обе ноты имеют фазу 0.
- Пара Off/On должна добавляться атомарно: до изменения музыкального состояния
  очередь резервирует места для всех пакетов boundary. При нехватке места пара
  не добавляется, состояние секвенсора не продвигается и ошибка доступна
  диагностике. Текущую ёмкость очереди не оптимизировать преждевременно;
  расширение остаётся отдельным решением, если появится реальное переполнение.
- Outgoing clock уже поставлен или отправлен раньше тех же событий: фактический
  порядок на `(T, 0)` — `F8`, Note Off, Note On.
- Reset — это не API `Sequencer`: coordinator этапа 6 очищает очередь, вызывает
  `stop()` и отправляет возвращённый actual Note Off, затем вызывает `start()`.
  Start начинает с step 0 ровно один раз.
- Секвенсор владеет только музыкальным состоянием: фактически звучащей нотой
  (она обновляется при достижении реальной boundary), прогнозом активной ноты
  для уже запланированных границ и следующей непоставленной boundary. Это не
  владение адресами и жизненным циклом конкретных MIDI-пакетов. Разделение
  фактического и прогнозного состояния позволяет Stop после очистки очереди
  отправить Off именно реально звучащей ноте. Уже принятый очередью пакет
  неизменяем: правка шага влияет только на ещё не рассчитанные границы.
- Редактирование шагов меняет будущие решения; не должно изменять уже отправленный
  пакет или вызывать запись в flash из transport-пути.

## Реализация

1. Добавлен `Sequencer::start`, `continuePlayback`, `stop`,
   `scheduleThrough(position, queue)` и `notifyBoundaryReached(tick)`.
   `scheduleThrough` планирует до `position.tick + SCHEDULING_LOOKAHEAD_TICKS`.
   `sync(micros)`, `update(micros)` и старые external-методы пока сохранены
   только для совместимости с runtime `main.cpp` до переключения этапа 6.
2. Вынести из `main.cpp` фактически звучащую ноту, прогноз активной ноты и
   следующую непоставленную boundary в `Sequencer`. Нельзя оставить два пути,
   которые оба посылают Note Off; конкретные пакеты после enqueue принадлежат
   только очереди.
3. В очередь добавлен `enqueueBatch(requests, count)`. Он проверяет общую
   ёмкость и tick quota до первой мутации, затем ставит ноль, один или два
   пакета в обычном порядке приоритетов. При отказе sequencer не меняет
   projected state и повторит ту же boundary после освобождения очереди.
4. Сохранить UI snapshot текущего шага: он обновляется только при достижении
   реальной transport boundary, а не при её предварительной постановке в
   очередь. LVGL по-прежнему только читает `UiViewModel`.
5. На Start coordinator этапа 6 должен до первого прохода диспетчера заполнить начальный горизонт,
   включающий tick 0; так первый Note On не может появиться после того, как
   диспетчер уже обслужил этот tick. Затем обычный путь остаётся строгим:
    dispatcher обслуживает due-события, а секвенсор пополняет только будущий
    горизонт.
6. Отказ `CapacityExceeded` или `TickQuotaExceeded` не повторяется в loop:
   `TransportController` останавливает transport, очищает queued future events
   и через dispatcher отправляет ровно один actual Note Off, если нота звучит.

## Проверки

Native Unity покрывает Start/tick 0, horizon 2, шесть ticks на шаг, wrapping 16
шагов, bytes/order Off/On, отказ/retry capacity и quota batch, immutable packet,
actual против projected Stop, Continue и legacy timing compatibility. Dispatcher
и порядок `F8 → Off → On` остаются частью интеграции этапа 6.

### Фактический API

```cpp
sequencer.start();
sequencer.scheduleThrough(transport.position(), queue);
// Dispatcher sends due queue packets, then acknowledges a real sixteenth boundary:
sequencer.notifyBoundaryReached(transport.position().tick);
// Dispatcher clears future packets and sends this optional actual Note Off exactly once:
std::optional<MIDI_Note> actual = sequencer.stop();
```

`stop()` не трогает очередь: dispatcher сначала очищает её, затем отправляет
возвращённый actual Note Off. Это сохраняет единственного consumer/USB owner.
Для reset coordinator затем вызывает `start()`; отдельного `Sequencer::reset()`
нет, поэтому actual Note Off нельзя случайно отбросить.

## Вне объёма

Настройка swing пользователем и изменения UI, кроме сохранения существующего
отображения позиции.

## Передача результата

Заполнить фактический API, владельца ноты, удалённый time API и тесты.
