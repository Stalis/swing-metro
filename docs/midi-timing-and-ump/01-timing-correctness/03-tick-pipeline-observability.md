# Шаг 1.3. Наблюдаемость пути тиков

Статус: выполнено. Зависит от шага 1.2.

## Цель

Сделать путь внутреннего тика от аппаратного callback до попытки отправки Clock
проверяемым по раздельным счётчикам и задержкам, не утяжеляя IRQ.

## Реализованная модель

`InternalTickDiagnostics` описывает producer-часть пути:

- вызовы alarm callback;
- попытки синхронной публикации первого тика при Start;
- успешные и неуспешные публикации;
- discard при Stop, смене clock mode и открытии storage;
- ошибки постановки alarm;
- максимальный интервал между фактическими callback;
- максимальное опоздание callback относительно deadline текущего alarm.

`TransportDiagnostics` сохраняет метрики шага 1.2 и дополнительно считает успешные
consumer pop и прямые попытки исходящего internal F8. `droppedTicks` по-прежнему
означает только остаток, отброшенный после лимита
`MAX_INTERNAL_TICKS_PER_PASS == 4`. `TickPipelineDiagnostics` объединяет согласованный
producer snapshot с этими полями, принадлежащими ядру 0.

`InternalTickDiscardReason` передаётся из `TransportController` через
`syncInternalAlarm()` в `PicoInternalTickAlarm::start()`/`stop()`. Поэтому очистка
SPSC-очереди в критической секции alarm получает ровно одну причину: Stop,
ModeSwitch или Storage. Перезапуск также очищает оставшиеся старые записи с явно
переданной причиной до синхронной публикации нового первого тика.

## Время callback после шага 1.4

Драйвер передаёт `InternalTickAlarmRequest` с generation и назначенной modulo-`2^32`
целью. Он преобразует её в ближайшую будущую 64-bit Pico boot-time цель, проверяет id
callback и не допускает синхронный reentrant callback при постановке alarm.

`TransportTickRecord::timestampUs` — назначенное время музыкальной сетки. Поэтому
`maxInternalTickProcessingLatenessUs` шага 1.2 измеряет задержку main loop от
запланированного тика, а `maxCallbackLatenessUs` отдельно показывает задержку IRQ.
`staleAlarmCallbacks`, `missedScheduledTargets` и ошибки arm различают устаревший
запрос, пропущенные цели и отказ hardware alarm.

Интервалы и lateness используют modulo-`2^32` арифметику и сравнение только на
строгом горизонте `< 2^31` мкс. Неопределённая или слишком далёкая пара не попадает
в максимум и переустанавливает baseline. Start также переустанавливает callback
baseline, чтобы пауза транспорта не считалась интервалом callback.

## Согласованный snapshot и стоимость IRQ

Producer изменяется одним писателем за раз под существующей critical section
`PicoInternalTickAlarm`. Перед группой изменений version становится нечётной, после
неё — чётной. `InternalTickSource::diagnostics()` на ядре 0 повторяет чтение, если
version нечётная или изменилась. Consumer-счётчики не входят в этот seqlock: ими
владеет ядро 0, а `pipelineDiagnostics()` добавляет их после устойчивого producer
snapshot. Таким образом, multi-writer seqlock между IRQ и consumer не используется.

В IRQ выполняются только ограниченное число атомарных операций, вычисления
максимумов, одна попытка публикации и постановка следующей абсолютной цели. Там нет
печати, выделения памяти, блокирующего вывода или retry-цикла. Retry находится только
в snapshot API вне IRQ.

## Инварианты

Для snapshot без конкурентного изменения очереди и до насыщения счётчиков:

- `successfulPublications = successfulConsumerPops + budgetDiscards +
  stopDiscards + modeSwitchDiscards + storageDiscards + queuedRecords`;
- потерянная при overflow запись увеличивает callback и failed publication, но не
  pop, discard или Clock attempt;
- в спокойном internal-прогоне после Start и N callback:
  `successfulPublications = successfulConsumerPops =
  outgoingInternalClockAttempts = N + 1`, а callback count равен N;
- каждый извлечённый record учитывается как pop до передачи dispatcher;
- outgoing internal Clock учитывается непосредственно перед `sink.send(F8)` и пока
  означает попытку, а не принятие USB-стеком;
- каждый очищенный опубликованный record получает одну причину discard.

Счётчики насыщаются на `UINT32_MAX`, чтобы переполнение не превращало накопленное
значение в ноль. Встроенный `TransportTickStore::overflowCount()` сохранён и должен
совпадать с failed publications producer-а для текущего единственного publisher.

## Детерминированные проверки

- Start и три callback дают четыре публикации, четыре pop и четыре F8 attempt.
- Заполненное кольцо даёт один callback publication failure/overflow, а сообщение об
  arm failure не создаёт дополнительной записи.
- Шесть накопленных records дают четыре pop/F8 и два budget discard.
- Stop, mode switch и storage относят по две записи к своей причине и не увеличивают
  F8 attempts.
- Последовательные grid callback с lateness `10` и `50` мкс подтверждают, что
  callback interval использует фактические времена, а lateness — назначенный
  deadline.
- Stale callback также входит в физический callback interval, но не в lateness
  актуального deadline; отдельно проверены wrap-around и переустановка baseline на
  горизонте `2^31`.

Native-тесты проверяют аппаратно-независимое сообщение об arm failure. Реальный
неположительный результат `add_alarm_at()` и точность Pico alarm требуют аппаратного
прогона; firmware build проверяет интеграцию с Pico SDK.

## Проверки

- `make test`: 233/233 native-теста пройдены после шага 1.4.
- `make verify`: пройдены format-check, clang-tidy, native-тесты и сборка `rpipico2`.
- Аппаратный прогон не выполнялся.

## Вне объёма

USB acceptance/retry, гистограммы и вывод статистики остаются вне шага.
