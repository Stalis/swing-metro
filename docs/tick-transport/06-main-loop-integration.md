# Этап 6. Интеграция transport в приложение

Статус: реализовано. Зависит от этапов 1–5.

## Реализация

`TransportController` координирует режимы, reset и источники tick. Внутренний
`MidiDispatcher` — единственный владелец `MidiEventQueue` consumption, сравнения tick/phase
deadlines и USB-MIDI output. `Sequencer` только ставит события на two-tick horizon.

## Фактический порядок loop

1. `UsbMidiRealtimeReceiver` передаёт входные realtime-сообщения контроллеру.
2. `MidiDispatcher` принимает до четырёх опубликованных internal ticks за проход,
   обрабатывает due phase deadlines и отправляет пакеты; остаток backlog сбрасывается.
3. `Sequencer::scheduleThrough` пополняет будущий two-tick horizon.
4. Обычный input/UI/storage код остаётся вне realtime-участка. После input
   `PicoInternalTickAlarm` синхронизируется с состоянием контроллера.

Для Internal dispatcher отправляет `FA`, затем на каждом tick `F8`, затем queued phase-zero
Off/On. Off использует internal alarm только для sequencer и не отправляет transport realtime.
External не отправляет outgoing realtime bytes.

## Переходы

`ToggleTransport` теперь достигает `TransportController` из `AppInputCoordinator`: stopped
всегда Start, running всегда Stop; local Continue отсутствует. Stop/reset очищает queue, вызывает
`Sequencer::stop`, а dispatcher ровно один раз отправляет возвращённый actual Note Off. `FC`
отправляется только для active Internal output session.

External Start ставит tick 0, но ждёт первого F8 с measured period до note output; incoming F8
не echo. После loss первый F8 возвращает lock, Continue использует сохранённый period, а следующий
F8 открывает tick без второго sample. Loss очищает pending state и actual note, остаётся
stopped/lost; relock сам не запускает playback. `CapacityExceeded` или `TickQuotaExceeded`
останавливают transport, очищают queue и отправляют один actual Note Off. Смена режима
безопасно останавливает старый путь и не запускает новый автоматически. Открытие storage modal
синхронно останавливает transport перед LittleFS работой.

## Удалённый production path

`main.cpp` больше не использует `MidiClockTransmitter`, `midi_step_boundary`, direct
`midiSendNoteOn/Off`, `noteSent` или `lastNoteSent`. `MidiDispatcher` остаётся единственным
production owner tick lifecycle, queue consumption и packet notifications.

## Проверки

Native tests покрывают dispatcher order, phase deadline, Internal Start/F8/note order, external
Start period gate/no echo, local Toggle, mode switch и external loss Note Off. Выполнен
`make verify` (`format-check`, `tidy`, 210 native tests и firmware build). Tidy reports existing
repository warnings but exits successfully.

На устройстве проверить отсутствие double `F8`, double Note Off, stuck-notes и старого clock
после mode switch, а также timing alarm при длительной USB/LVGL нагрузке.

## Вне объёма

Swing, новые UI-экраны и другие MIDI-протоколы.
