# Шаг 2.3. Просрочка и жизненный цикл сессии

Статус: запланировано. Зависит от шага 2.2.

## Цель

Определить bounded поведение pending messages при длительном backpressure, Stop,
mode switch, clock loss и disconnect/reconnect. Не допускать старого Clock burst,
запоздалого Note On после Stop и молчаливой потери Note Off.

## Контекст после шага 2.2

Typed due messages атомарно переходят в fixed delivery outbox. Sink различает
`Accepted`, `RetryLater` и достоверный `Disconnected`; head retry сохраняет identity,
deadline и FIFO order, а accepted note state обновляется только после acceptance.
Консервативное capacity exhaustion пока останавливает transport, type-specific
expiration и recovery ещё не определены.

## Политики сообщений

Политики должны использовать назначенный deadline, текущую transport position и
session generation, а не количество случайно выполненных loop iterations.

- **Clock:** просроченный Clock не отправляется неограниченной catch-up пачкой.
  При появлении более нового Clock старые непринятые Clock coalesce/drop по явно
  выбранному правилу; каждый пропуск учитывается. После восстановления выдаётся
  только актуальный поток, без replay всей паузы.
- **Note On:** сохраняется при кратком `RetryLater`, пока событие ещё относится к
  активной transport/session generation. Stop, mode switch, storage или session reset
  отменяют непринятый старый On. Он никогда не появляется после Stop.
- **Note Off:** краткий `RetryLater` не удаляет Off и не позволяет связанному On его
  обогнать. При подтверждённом disconnect физическая доставка невозможна; такой Off
  помечается отдельной причиной abandoned/remote-state-unknown, а не success/drop без
  объяснения.
- **Start/Continue:** образуют границу session generation. Clock/notes этой generation
  не обгоняют непринятый transport start.
- **Stop:** отменяет pending Clock и Note On текущей generation. Для локально accepted
  sounding note сначала ставится Note Off, затем Stop; порядок сохраняется при retry.

## Disconnect и восстановление

Без подтверждения удалённого состояния безопасная политика текущего monophonic
устройства: подтверждённый disconnect завершает outbound session и останавливает
локальный transport. Pending Clock/Note On инвалидируются; недоставимый Note Off и
Stop учитываются как session-abandoned. Reconnect не воспроизводит старые сообщения
и не возобновляет transport автоматически. Следующий явный Start создаёт новую
generation и чистое accepted-state предположение.

Если доступный TinyUSB API не даёт устойчивого edge disconnect/reconnect, adapter
фиксирует только наблюдаемое mounted state; тесты модели всё равно покрывают обе
ветви, а аппаратное ограничение передаётся в шаг 2.5.

## Capacity и safety reserve

Outbox должен сохранять место для завершающего Note Off и transport Stop либо иметь
эквивалентный priority/reservation механизм. Точные capacity/reserve оформляются
именованными константами и обосновываются максимальным batch текущего monophonic
sequencer-а. Clock/Note On не могут занять safety reserve.

Если sustained `RetryLater` исчерпал допустимое окно/ёмкость после coalescing,
transport выполняет контролируемый Stop. Нельзя продолжать планирование в
неограниченную очередь или ждать sink в цикле.

## Работа

1. Добавить session generation и причины invalidation pending events.
2. Реализовать Clock coalescing/expiration без изменения internal tick grid.
3. Реализовать Stop barrier и сохранение Off → Stop → отсутствие старого On.
4. Добавить safety reserve либо эквивалентную доказуемую capacity policy.
5. Реализовать disconnect stop и explicit-start recovery.
6. Согласовать external clock loss, mode switch и storage stop с теми же policy,
   не создавая отдельных неэквивалентных путей.

## Детерминированные проверки

- Длительный `RetryLater` на Clock не создаёт burst после recovery.
- Stop при pending Clock/Note On отменяет их; ни один старый On не accepted позже.
- Pending Note Off сохраняется через краткий отказ и precedes следующий Note On.
- Stop при accepted note выдаёт Off перед Stop; retry каждого не меняет порядок.
- Confirmed disconnect при звучащей accepted note отмечает remote state unknown,
  очищает session и останавливает transport.
- Reconnect ничего не replay; новый Start создаёт другую generation.
- Mode switch, storage open и external clock loss используют правильные причины.
- Queue pressure не занимает safety reserve обычными Clock/Note On.
- Все циклы и число попыток остаются bounded.

## Готовность шага

- Для каждого поддержанного message type записана и реализована overdue policy.
- Stop/disconnect/reconnect не оставляют replay старой session.
- Note Off не теряется молча, а невозможность доставки названа отдельно.
- Выполнен `make verify`.

## Вне объёма

All Notes Off для полифонии, автоматический resume после reconnect, UMP negotiation,
Gate и continuous-controller coalescing.
