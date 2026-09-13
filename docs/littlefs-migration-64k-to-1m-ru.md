# Миграция LittleFS 64 KB -> 1 MB

Эта процедура переносит все 17 слотов (16 пользовательских и текущий), обе сырые
копии A/B каждого слота, признак наличия и байты файлов. Она не декодирует программы:
поврежденная или незавершенная резервная копия также переносится буквально.

Обычная прошивка `rpipico2` остается с LittleFS 64 KB и не содержит протокол миграции
или CDC/MIDI-изменений. Используйте только два отдельных окружения ниже. В прошивках
миграции нет MIDI, дисплея, ввода или секвенсора; USB является только CDC.

## Требования

Нужны `pio`, Python 3 и USB CDC-порт платы. Скрипт использует только стандартную
библиотеку Python и работает на macOS/Linux (например, `/dev/cu.usbmodem...` или
`/dev/ttyACM0`). Остановите транспорт и отключите все MIDI-приложения.

## Экспорт с 64 KB

Сначала соберите и загрузите именно экспортную прошивку. Она монтирует существующий
64 KB LittleFS без автоматического форматирования.

```sh
pio run -e migrate-export-64k -t upload
python3 scripts/littlefs_migrate.py --port /dev/cu.usbmodemXXXX --export swing-metro-64k.smbk --yes-export
python3 scripts/littlefs_migrate.py --validate swing-metro-64k.smbk
```

`--validate` проверяет SMBK без CDC-порта и печатает SHA-256 для explicit destructive
confirmation.

Храните `swing-metro-64k.smbk` вне платы до полного завершения и проверки отката.
SMBK v1 содержит CRC заголовка и CRC payload; каждый USB-кадр также имеет версию,
длину, номер и CRC. Команды и кадры записи повторяются до трех раз.

## Опасная граница `uploadfs`

Не запускайте `pio run -t uploadfs` до экспорта. `uploadfs` записывает образ файловой
системы и является разрушительной границей: он может стереть 64 KB данные, которые
нужно сохранить. Только после successful host backup `uploadfs` — единственный
документированный способ подготовить пустой 1 MB target; migration firmware не вызывает
`LittleFS.format()` или raw erase.

## Восстановление в 1 MB

После успешного экспорта соберите и загрузите прошивку с 1 MB layout. Загрузка
прошивки меняет таблицу/границу файловой системы; поэтому резервная копия уже должна
быть на компьютере. Восстановление допускается только в пустой целевой LittleFS.

```sh
pio run -e migrate-restore-1m -t upload
# DESTRUCTIVE: очищает весь новый 1 MB filesystem, включая старые 64 KB в его конце.
pio run -e migrate-restore-1m -t uploadfs
# Подставьте SHA-256, напечатанный --validate, как отдельное подтверждение.
python3 scripts/littlefs_migrate.py --port /dev/cu.usbmodemXXXX --restore swing-metro-64k.smbk --yes-restore --prepared-target <SHA256>
```

Скрипт сначала проверяет весь SMBK (размер, версия, CRC заголовка и payload). Плата
получает весь payload, проверяет его CRC и все записи до первой записи в LittleFS.
Затем она проверяет отсутствие всех 34 целевых файлов. Только после этого пишет
имеющиеся сырые A/B файлы и после каждой записи читает их обратно для побайтной
проверки. При `TargetNotEmpty`, `InvalidBackup` или ошибке CRC ничего не записывается.
При ошибке записи не продолжайте: целевой набор может быть частично записан.

## Завершение и проверка

После успешного restore-1m загрузите обычную 1 MB прошивку `rpipico2-1m`.
Не загружайте `rpipico2`: это отдельный 64 KB profile.

```sh
pio run -e rpipico2-1m -t upload
```

Проверьте загрузку сохраненных программ вручную. Не используйте `uploadfs` в этой
проверке и не меняйте `board_build.filesystem_size` у `rpipico2`.

## Откат

Rollback снова пересекает destructive boundary: подготовьте пустой 64 KB target и
используйте строго `migrate-restore-64k`, затем верните обычный `rpipico2`.

```sh
pio run -e migrate-restore-64k -t upload
# DESTRUCTIVE: очищает 64 KB target.
pio run -e migrate-restore-64k -t uploadfs
python3 scripts/littlefs_migrate.py --port /dev/cu.usbmodemXXXX --restore swing-metro-64k.smbk --yes-restore --prepared-target <SHA256>
pio run -e rpipico2 -t upload
```

Если восстановление 1 MB не завершилось, не форматируйте плату: сохраните SMBK,
устраните причину и повторите восстановление на пустом target.
