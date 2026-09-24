# Шаг 4.3. Monophonic identity и delivery lifecycle

Статус: выполнено 2026-09-24. Зависит от шага 4.2.

## Цель

Сделать transport/sequencer единственным владельцем monophonic состояния при overlap,
USB RetryLater, expiry и завершении session. Старый Off не должен выключать новую ноту
того же pitch; принятый On должен завершаться согласно своему Gate deadline.

## Контекст текущего кода

`Sequencer` хранит `_actualSoundingNote`, `_requestedNoteOff`, `_projectedSoundingNote` и
`RemoteNoteState`; текущие callbacks `notifyNoteOnAccepted`, `notifyNoteOffAccepted`,
`cancelRequestedNoteOff`, `abandonRemoteNoteState` работают только с note. `TransportController`
обрабатывает scheduled queue и `MidiPendingDeliveryQueue`, различает `RetryLater`, expiry,
terminal Note Off и session generation. Pending queue умеет искать Off по note/generation.

Шаг 4.2 уже создаёт On/Off с общим immutable launchId, independent deadlines и atomic pair
enqueue. Поэтому сравнение только pitch недостаточно для same-pitch adjacent launches.

## Зафиксированные решения и контракты

- Единственный owner принимает transitions accepted/projected state; UI и USB adapter не
  изменяют музыкальный lifecycle напрямую.
- Scheduled и pending Gate Off считаются stale по identity: `(sessionGeneration, launchId)`,
  а не по note. При досрочном Off перед replacement On старый planned/pending Gate Off
  инвалидируется в обоих местах.
- Новый On заменяет только реально/проектируемо активный launch при overlap. Пустой или
  disabled следующий шаг сам не обрывает ноту. Off at own Gate deadline не заменяется
  прямой step boundary.
- `RetryLater` сохраняет logical order Off -> On. Если On не принят до собственного Gate
  deadline, он expires и позднее не становится новой нотой. Если On принят, его Off остаётся
  обязательным до accepted, terminal policy либо явного session end.
- Stop, mode switch, external loss/disconnect и clean/new session очищают future scheduled
  events, invalidируют pending stale launches и завершают accepted current launch ровно
  один раз либо переводят remote state в documented Unknown при terminal failure.
- Пересчитать scheduled/pending capacities, terminal reserves и diagnostics для pair,
  stale invalidation, On expiry, terminal Off abandonment и session cleanup; counters имеют
  saturating/defined semantics.

## Файлы и API в scope

`src/engine/sequencer.{h,cpp}`, `src/engine/midi_event*.h`, `src/engine/midi_event_queue.h`,
`src/engine/midi_pending_delivery_queue.h`, `src/engine/transport_controller.h`,
`src/main.cpp` только для existing diagnostics export, и существующие engine native tests.
Шаг не меняет Program codec, input routing или LVGL.

## Последовательность работы

1. Пронести `(sessionGeneration, launchId)` через scheduled и pending event representation;
   заменить note-only lookup/invalidation в relevant paths.
2. Сформулировать accepted и projected transition table, затем применить её в enqueue,
   acceptance, expiry, replacement и cancellation path без дублирующих owners.
3. Обеспечить Off-before-On при retry, expired unaccepted On и own-deadline Off accepted On.
4. Свести stop/mode/loss/disconnect/session cleanup к общему lifecycle path, определить
   terminal fallback и diagnostics/capacity reserves.
5. Добавить native regressions для всех transitions и только после этого передать stable
   software contract UI шагу 4.4.

## Детерминированные проверки

- Non-overlap, swing overlap, disabled/empty successor и Gate crossing straight boundary.
- Same pitch: stale scheduled и pending Off старого launch не выключает новый launch.
- Equal Off/On deadline, `RetryLater` Off-before-On, full queues/reserves и atomic cleanup.
- On expiry на собственном Gate deadline до acceptance; accepted On получает один Off.
- Stop до/после Gate, mode switch, external loss, disconnect, terminal failure и новая
  session: нет duplicate Off, stale retry или неверного projected/actual state.
- Diagnostics отражают defined causes без counter wrap; existing normal delivery regressions сохраняются.

## Готовность шага

- Lifecycle имеет одного owner и identity-safe на scheduled и pending уровнях.
- Final monophonic overlap/backpressure/session semantics покрыты deterministic native tests.
- Capacity/reserves и diagnostics обновлены; `make verify` проходит.

## Вне объёма

Полифония, MPE, UI controls, codec layout, LittleFS operation в running transport и
hardware claim о host delivery.

## Передача шагу 4.4

Передать final правило: редактирование Gate не меняет уже scheduled/accepted launch,
а затрагивает только future unscheduled launches. UI отображает текущую настройку шага,
не состояние pending ноты и не участвует в lifecycle.

Фактический контракт после реализации: `(sessionGeneration, launchId)` сохраняется от
scheduled event через pending delivery до accepted/projected состояния sequencer-а.
Replacement удаляет старый scheduled Gate Off и ставит обязательный identity-bound Off
перед новым On; это действует и когда старый On ещё находится в `RetryLater`. Pending
queue сохраняет capacity 16, normal capacity 14 и два terminal-reserve места для
replacement Off и Stop. Stale Gate Off и On expiry имеют отдельные diagnostics/removal
reasons; session cleanup не использует pitch как identity.
