# R1 — один live-протокол и отдельные компоненты диагностики

Исходная точка: R0, commit `5ee39b9e0cbdf6c05139715b42f5e5deb96880b3`.
Этап выполняет строку R1 [одобренного плана](../plans/2026-09-27-gitnexus-plan-refactor-feature-foundation.md).

## Изменения

- Удалены live v2/v3, их определения колонок, старые aliases и fallback
  `unavailable_v2`. Разборщик и firmware summary принимают только v4.
- Колонки v4 собраны непосредственно из tick-pipeline и delivery-групп с прежним
  порядком. Существующий `TimedRunCapture` уже направляет все diagnostics-префиксы
  в общий разборщик: старые версии отклоняются сразу, не игнорируются.
- C++ serializer вынесен из main в `DiagnosticsSerializer`; `DiagnosticsCapture`
  владеет pending export и подтверждением снимка второго ядра.
- `SerialRunController` владеет RUN/EXTERNAL_RUN, подготовкой/завершением прогона и
  опциональным fault-портом. `ArduinoDiagnosticConsole` подключает Serial и часы.
- Main сократился с 822 до 358 строк. Он сохраняет порядок обслуживания, связывает
  объекты и уведомляет capture при переключении alarm; дальнейшее разделение main
  относится к последующим этапам.
- Native source filter включает новый serializer `.cpp`; новый набор сценариев
  зарегистрирован в единственном native runner. Native включает fault-флаг для
  проверки опциональной команды; production/off её состояние и parser не компилируют.
- Добавлена актуальная [документация диагностики](../diagnostics.md).

## Проверки

Шесть новых native-тестов проверяют эталонные headers и несколько различимых значений
serializer, однократное завершение после snapshot/alarm, подготовку EXTERNAL_RUN без
старта транспорта, ошибки режима, запрет выгрузки во время playback, блокировку новой
команды при pending export, fault acknowledgement и переполнение millis.

Старые positive-тесты v2/v3 заменены negative-тестами полных исторических строк
(27 и 163 data fields), старых префиксов с длиной v4, неизвестных версий, неверного числа
полей и типа данных. Сохранены проверки доставки/сводки и все неизменённые R0 fixtures.
Тесты сначала запущены против старого parser: ожидаемый провал (6 failures, 4 errors),
затем успешный запуск после очистки. Число Python test methods стало 45 вместо 46:
старые проверки сводки объединены, а v2/v3 rejection покрывает несколько subtests.

Полный `make verify` после исправления review прошёл: **345 native, 45 host**,
format-check, clang-tidy и production/off/fault firmware. Существующие предупреждения
сохраняются; это не warning-free сборка. Проверено также отсутствие строки fault-протокола
в production/off бинарниках и её наличие в fault. Точные размеры и SHA-256 —
в [r1-verification.json](r1-verification.json). Production: RAM 169956 байт (+68
к R0), Flash 731472 байта (+1344).

## Граф и границы

Перед правками проверены parser, summary, перемещаемые функции main и тестовые entry
points. `diagnostics_columns_for_row`: CRITICAL, 20 зависимостей, 3 прямых потребителя,
6 процессов. Прямые потребители — header helper, parser и `TimedRunCapture.consume`;
транзитивно затронуты serial/MIDI collectors и external-clock validation.
`add_firmware_summary`: LOW, 8 зависимостей. Перемещаемые функции main: LOW, обычно
прямой потребитель loop. UNKNOWN у framework entry points и тестов подтверждён
исходниками: Arduino вызывает loop, Unity/main регистрирует native tests, Python
unittest discovery запускается из Makefile. UNKNOWN не трактовался как отсутствие вызовов.

OpenCode использован для анализа и чтения реализации. Его команды и правки были
заблокированы настройками permissions; автоматические разрешения не включались.
Изменения и проверки выполнены инструментами Codex. В read-only review OpenCode
нашёл лишний fault-parser в production/off после переноса. Замечание подтверждено:
fault includes, параметр, состояние и обработка возвращены под прежний compile-time
флаг; повторная полная проверка после исправления прошла.

Не менялись GUI, музыкальная семантика, storage, схемы актуальных v1-протоколов,
исторические captures, текст плана и R0 artifacts. Новая прошивка не загружалась на
плату; физический MIDI timing/опрос энкодеров повторно не измерялись. Известные
аппаратные ограничения R0 остаются открытыми. R2–R6 и будущие фичи не выполнялись.
