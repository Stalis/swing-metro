# Этап 4 — старт, Current Program и валидация

Статус: реализовано.

## Цель

Восстанавливать рабочую программу при старте и сохранять её автоматически без
риска для воспроизведения.

## Работа

1. На старте загрузить `Current Program`; при пустой или невалидной ячейке
   применить обычные defaults.
2. После успешного Save или Load синхронно обновлять Current Program, пока
   транспорт уже остановлен.
3. Изменения Program помечают dirty-состояние. Когда transport остановлен,
   сравнить CRC canonical payload с Current Program и записать только при отличии.
4. Не выполнять filesystem I/O во время Internal или External playback. Если
   питание выключено в этот период, изменения после последнего autosave могут
   быть потеряны; это осознанный компромисс ради MIDI-тайминга.
5. `ProgramStorageController::restoreCurrentProgram()` восстанавливает Current Program
   или применяет `Program{}` defaults без записи во flash. `perform()` после успешных
   Save/Load синхронно сохраняет Current Program, а `syncCurrentProgramIfChanged()`
   записывает его только при изменении CRC canonical payload и stopped transport.
6. После failed mount LittleFS первично форматируется только когда raw-byte проверка всего
   раздела подтверждает erased state (`0xFF` в каждом байте). Corruption, неверная конфигурация
   и transient mount failures возвращают `MountFailed` без форматирования; Save/Load/autosave
   не имеют format path.

## Проверка

- изменение → остановка → power cycle восстанавливает программу;
- неизменённое состояние не создаёт лишней flash-записи;
- Save/Load и autosave не выполняются при работающем транспорте;
- отключение питания во время записи оставляет доступной предыдущую A/B-копию;
- на устройстве проверить все текущие параметры шага, а затем добавить
  совместимый fixture старой версии перед расширением шага gate/repeat-полями.

## Результаты

- Native tests покрывают restore defaults/current, sync Save/Load, отсутствие redundant
  writes и отказ autosave при running transport.
- Native test проверяет prerequisite безопасного format fallback: все bytes должны быть erased;
  один programmed byte запрещает форматирование.
