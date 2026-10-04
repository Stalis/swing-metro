# План реализации интерфейса секвенсора на LVGL

> Ветка: codex/plan-lvgl-interface. Источник разметки: assets/Figma Export/README.md и локальные SVG.
> Evidence verified at commit bfbf3f2fb9373e5d27cb73cf78805bfaf3aa9e3a; GitNexus index refreshed with --index-only --pdg for this working tree.
> Evidence provenance schema 2; global dirty digest 785e2723aff645cb7780839114906453e6cc03caa8c49f3fcea797c3b2e19b7c; cited-path manifest 32 sorted entries; exact generated plan path excluded.

## 1. Цель

[verified] Перенести экран 160×128 и модалки из экспорта Silkscreen Flat study в прошивку LVGL: основной экран, настройку шага, MIDI Clock, меню Save/Load/Cancel/Reset, прокручиваемые слоты 01–16 и подтверждение Reset (assets/Figma Export/README.md:1-105). На этом этапе Mode, Repeat Count и дополнительные параметры последовательности допустимо отдавать через mock-функции устройства и UiViewModel; логика воспроизведения и хранения остаётся отдельной задачей.

## 2. Текущее состояние

- [verified] В рабочей копии уже есть палитра, Silkscreen Flat, сетка 4×4, дополнительные поля UiSettings и UiMockData, но экран и модалки ещё смешивают новый и старый стиль (src/drivers/ui/ui_theme.h:9-17; src/drivers/ui/step_grid.cpp:7-105; src/input/ui_mock_data.h:9-45).
- [verified] Основной экран пока показывает старые Tempo/Swing/Volume/Clock строки; StepSettings загружается как отдельный экран, а обе существующие модалки используют старую геометрию (src/drivers/ui/main_screen.cpp:18-52; src/drivers/lvgl_ui.cpp:28-43; src/drivers/ui/step_settings_screen.cpp:6-61).
- [verified] Проверка make build на текущей копии завершилась ошибкой: отсутствуют GREEN/BLUE/ORANGE/DARK_GRAY, MainScreen вызывает удалённые StepGrid::setSteps/draw, modal_backdrop.cpp не компилирует std::uint8_t. Это первый блокирующий этап, а не будущая регрессия.
- [verified] Хранение уже имеет реальные состояния Action, Slot, ResetConfirmation и диапазон слотов 0–15 с отдельным Cancel sentinel (src/program/program_storage_modal.cpp:7-106).

## 3. Архитектура и границы

[verified] Application::loop собирает состояние движка, декорирует UI-снимок и публикует его; Application::loop1 читает ViewModel и обновляет LVGL (src/application.cpp:170-196). Все вызовы LVGL должны остаться на UI-ядре. UiViewModel переносит снимок атомарными полями с generation/retry, поэтому новое поле необходимо добавить во все четыре места: publish, read, sameSettings и тесты (src/components/ui_view_model.h:13-181). Экранные классы живут в src/drivers/ui; ввод и mock-поля — в src/input.

## 4. GitNexus: зависимости и влияние

- [graph] impact(UiViewModel, upstream, depth=2): risk LOW; прямые зависимые — src/application.h, src/drivers/lvgl_ui.h, test/test_native/components/test_ui_view_model.cpp и test/test_native/input/test_app_input_coordinator.cpp. Поэтому изменения снимка требуют проверки обоих входов и тестов.
- [graph] impact(AppUiSnapshotBuilder, upstream, depth=2): risk LOW; прямые зависимые — src/input/app_input_coordinator.h и test/test_native/input/test_app_ui_snapshot_builder.cpp.
- [graph] impact(StepGrid, upstream, depth=2): risk LOW; прямые зависимые — src/drivers/ui/main_screen.h, src/drivers/ui/step_grid.cpp и src/drivers/ui/step_settings_screen.cpp.
- [graph] impact(MainScreen, upstream, depth=2): risk LOW; прямые зависимые — src/drivers/lvgl_ui.h и src/drivers/ui/main_screen.cpp.
- [graph] impact(UiSettings, upstream, depth=2) вернул risk UNKNOWN и ноль связей; [verified] текстовый поиск подтверждает его использование в Application, ViewModel, SnapshotBuilder, UI и native-тестах. Ноль в графе не означает безопасное изменение.
- [graph] detect-changes(scope=all): текущие незакоммиченные 9 файлов затрагивают 64 символа, пять потоков выполнения, общий риск medium. Индекс после обновления покрывает текущую рабочую копию; анализ потоков сообщает о пропусках из-за внутренних лимитов обхода.

## 5. Ограничения на уровне операторов

- [graph] pdg_query(controls, src/components/ui_view_model.h): проверка sameSettings на строке 14 управляет ранним выходом publish; проверки generation на строках 74 и 99 управляют повторным чтением. [verified] код подтверждает атомарную запись всех полей между двумя изменениями generation (src/components/ui_view_model.h:13-67,71-147). Новое поле без sameSettings или unpack даёт устаревший/несогласованный UI.
- [graph] pdg_query(controls, src/drivers/lvgl_ui.cpp): условие page==StepSettings управляет apply редактора, а page!=_currentPage — lv_screen_load (строки 31–38). [verified] код подтверждает отдельный root шага. При переводе в модалку основной экран должен оставаться host, а подложка и панель переключаются только на верхнем слое (src/drivers/lvgl_ui.cpp:28-43).

## 6. Предлагаемые изменения

| Узел | Ответственность |
|---|---|
| src/drivers/ui/pico_display.cpp, ui_fonts.cpp, ui_theme.h/.cpp | Проверить фактическое разрешение после rotation=1; инициализировать Silkscreen Flat после lv_init, согласовать палитру с README (#00B8B0 для Teal), базовые стили и 1 px рамки. |
| src/drivers/ui/main_screen.cpp и step_grid.cpp | Собрать верхнюю строку BPM/CLK, разделитель x=3/y=11/154×1, строку SEQ/MIDI CH и SWING справа, затем сетку 4×4 с карточками 36×23 и шагом 39×26. Исправить вызов StepGrid::apply. |
| src/drivers/ui/modal_backdrop.cpp, src/drivers/lvgl_ui.cpp | Создать общий дизеринг 1 px/50% и панель 148×112; обеспечить один активный модальный слой, порядок backdrop → panel и сохранение основного экрана под ним. |
| src/drivers/ui/step_settings_screen.cpp | Заменить отдельный экран модалкой: Normal — Note/Vel/Gate; Legato — Mode/Gate; Repeat — все поля. Недоступные строки остаются серыми. |
| src/drivers/ui/midi_clock_dialog.cpp | Показать ACTIVE отдельно от текущего фокуса OFF/INTERNAL/EXTERNAL/CANCEL; визуализация не меняет существующую семантику CANCEL. |
| src/drivers/ui/program_storage_dialog.cpp | Отрисовать меню действий, окно шести слотов, scrollbar, переход к слоту 16 и Reset confirmation с NO по умолчанию. В модели оставить 0–15; на экране показывать 01–16. |
| src/components/ui_snapshot.h, ui_view_model.h, src/input/app_ui_snapshot_builder.h, ui_mock_data.h | Добавить только недостающие поля для отображения и preview-наборы, включая маски MIDI каналов на 7, 13 и 15 выбранных каналов. Реальные значения не затирать в production. |

[verified] В текущем StepGrid красная рамка уже замещает другую, но жёлтая назначается всем выключенным шагам внутри длины; для макета жёлтая должна обозначать выбранный шаг. Приоритет рамки: active > focused > enabled > disabled. Если active и focused совпадут, одна красная рамка сохраняется, без второй обводки (src/drivers/ui/step_grid.cpp:42-66; assets/Figma Export/README.md:28-39).

## 7. Порядок работ

1. **Стабилизировать основу.** Исправить ошибки текущей сборки и привести вызовы MainScreen ↔ StepGrid к одному API. Критерий этапа: make build проходит до включения новой разметки.
2. **Подготовить пиксельную базу.** Проверить runtime-размер LVGL; включить шрифт, цвета, общий стиль панели и дизеринг. Проверить символы C#2/Bb0, размеры 4/5/6/7 px и расход памяти/время кадра.
3. **Собрать основной экран.** Верхняя строка, разделитель, статус, маски каналов ALL/один/несколько, SWING справа; карточки 01–16, OFF/--, velocity/gate, рамка active/focus. Проверить длинные маски 7/13/15 каналов в одной строке.
4. **Перевести Step Settings в модалку.** Оставить MainScreen загруженным, добавить overlay и правила доступности Normal/Legato/Repeat. Новые режимы пока только представление и mock-значения.
5. **Обновить MIDI Clock.** Подключить те же базовые стили, различить ACTIVE и выделенную строку; проверить OFF/INTERNAL/EXTERNAL/CANCEL.
6. **Обновить хранение.** Меню действий, шесть видимых слотов со сдвигом окна и индикатором прокрутки, NO/YES для Reset; существующие команды/flash-операции не менять.
7. **Финальная проверка.** Дополнить native-тесты снимка и чистых форматтеров, собрать обычную и preview-прошивки, проверить 1:1 все состояния по локальным SVG на реальном дисплее или воспроизводимом LVGL-кадре; затем make verify.

Каждый этап оставляет собираемую рабочую копию. Файлы дизайна и шрифта сейчас untracked: перед переносом ветки в другое рабочее дерево их потребуется включить в ветку.

## 8. Проверки

- [verified] make format-check, make tidy, make test, make build и make verify определены в Makefile:101-120; preview-окружение rpipico2-ui-preview определено в platformio.ini:86-90.
- [verified] Native runner регистрирует тесты вручную (test/test_native/main.cpp:52-95). Изменить существующие test_ui_view_model.cpp и test_app_ui_snapshot_builder.cpp: сейчас их comparator/ассерты не охватывают все новые поля (test/test_native/components/test_ui_view_model.cpp:86-105; test/test_native/input/test_app_ui_snapshot_builder.cpp:34-63).
- Тесты UI-адаптеров: маска 0xFFFF → ALL; один канал → номер; наборы 7/13/15 → короткая подпись без перекрытия SWING; слот 0/15 → 01/16; scrolling в начале/середине/конце; Reset NO без команды, MIDI CANCEL без изменения режима.
- Визуальная матрица: главный экран, три режима Step, MIDI Clock, Storage Actions, слоты в начале/середине/конце, Reset NO/YES. Проверить 160×128, отсутствие обрезки и артефактов, видимость выбора на дизеринге и разницу RGB565-цветов на устройстве.
- Не выполнять запись LittleFS во время работающего транспорта; проверять модальные сценарии без изменения существующего контроллера хранения.

## 9. Риски и совместимость

- [verified] PicoDisplay задаёт физические 128×160 и rotation=1, а LVGL берёт gfx.width()/height() во время setup (src/drivers/ui/pico_display.h:28-38; src/drivers/ui/pico_display.cpp:6-23). [inferred] Устройство может уже выдавать логические 160×128; менять rotation до измерения нельзя.
- [verified] UiFonts::initialize использует tiny_ttf, но LvglUi::setup её сейчас не вызывает (src/drivers/ui/ui_fonts.cpp:19-30; src/drivers/lvgl_ui.cpp:5-13). [inferred] Для 160×128 нужны проверка реальных метрик и памяти на устройстве, особенно строчной b.
- [verified] Цепь storage уже проходит через ProgramStorageModal и запрос к контроллеру (src/input/app_input_coordinator.h:77-109,143-150). Визуальные правки не должны обходить этот путь и запускать flash из UI-ядра.
- [graph] Прямые зависимые UiViewModel, SnapshotBuilder, StepGrid и MainScreen перечислены в §4; они покрыты этапами и тестами. Для UiSettings граф даёт UNKNOWN, поэтому контроль всех текстовых употреблений обязателен перед его изменением.

## 10. Ожидаемые файлы

| Группа | Файлы |
|---|---|
| Разметка и стиль | src/drivers/ui/main_screen.cpp, step_grid.cpp, ui_theme.h/.cpp, ui_fonts.cpp, pico_display.cpp |
| Модалки и композиция | src/drivers/lvgl_ui.cpp, src/drivers/ui/modal_backdrop.cpp, step_settings_screen.cpp, midi_clock_dialog.cpp, program_storage_dialog.cpp |
| Данные для представления | src/components/ui_snapshot.h, ui_view_model.h, src/input/app_ui_snapshot_builder.h, ui_mock_data.h |
| Проверки | test/test_native/components/test_ui_view_model.cpp, test/test_native/input/test_app_ui_snapshot_builder.cpp, test/test_native/input/test_midi_clock_modal.cpp, test/test_native/program/test_program_storage_modal.cpp |

## 11. Reusable Implementation Context

~~~json
{
  "implementation_context": {
    "task_summary": "Визуально реализовать экспортированный интерфейс секвенсора 160×128 на LVGL в ветке codex/plan-lvgl-interface; отсутствующую функциональность подавать через mock-данные и UiViewModel.",
    "acceptance_criteria": [
      "Основной экран и все модальные состояния соответствуют локальным SVG/README при масштабе 1:1.",
      "Существующие MIDI/transport/storage операции не меняют поведение ради визуального этапа.",
      "Данные проходят main core → UiViewModel → UI core; LVGL вызывается только UI core.",
      "make verify и сборка preview-окружения проходят, все 16 слотов достижимы."
    ],
    "evidence_provenance": {
      "schema_version": 2,
      "head_commit": "bfbf3f2fb9373e5d27cb73cf78805bfaf3aa9e3a",
      "generated_plan_path": "docs/plans/2026-10-04-gitnexus-plan-lvgl-sequencer-interface.md",
      "global_dirty_digest": {
        "algorithm": "sha256",
        "canonicalization": "gitnexus-evidence-provenance-v2 NUL-framed UTF-8 records",
        "value": "785e2723aff645cb7780839114906453e6cc03caa8c49f3fcea797c3b2e19b7c"
      },
      "cited_path_manifest": [
        {
          "path": "Makefile",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:810775f170f9383fd6dac50199ea53d26cfd61cba62e6ad04b68de4300ee7b89",
          "index_digest": "sha256:810775f170f9383fd6dac50199ea53d26cfd61cba62e6ad04b68de4300ee7b89",
          "worktree_digest": "sha256:810775f170f9383fd6dac50199ea53d26cfd61cba62e6ad04b68de4300ee7b89",
          "untracked_digest": "absent"
        },
        {
          "path": "assets/Figma Export/Monophonic step sequencer — Silkscreen Flat.svg",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:2768010e769689a1c69d874510288acc3032aaf036ceef862cda045811a01a84"
        },
        {
          "path": "assets/Figma Export/README.md",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:704081eaebfd767b716d29d79d68215624156aff3772b193399e68a76fe18b5e"
        },
        {
          "path": "assets/Figma Export/Sequencer Modals.svg",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:0cdb2f1aee7720488e72c9e25f8fb3f339a34f323dde25d8da0ad6a02c306ff8"
        },
        {
          "path": "platformio.ini",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:f22997417019ff91d355249d64585d5e5fcf2c7116d4b8212a1f5b2bfeef92c7",
          "index_digest": "sha256:f22997417019ff91d355249d64585d5e5fcf2c7116d4b8212a1f5b2bfeef92c7",
          "worktree_digest": "sha256:24b6a48903411fa428517c93222fa1ae09568b43f20e950676d234d12c53d573",
          "untracked_digest": "absent"
        },
        {
          "path": "src/application.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:3a42d1de81ea22ae9b9fb033d322c47b901ac650b7a9f2b617bc9bb85c35104e",
          "index_digest": "sha256:3a42d1de81ea22ae9b9fb033d322c47b901ac650b7a9f2b617bc9bb85c35104e",
          "worktree_digest": "sha256:19f9dd976517b4bd012289e05536f537f82e5fb98362fc284ae8ee13a3dad746",
          "untracked_digest": "absent"
        },
        {
          "path": "src/components/ui_snapshot.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:d7d67dcc568e19e881800a264955f473c308754b83e06d1bde0de1449d5fbb01",
          "index_digest": "sha256:d7d67dcc568e19e881800a264955f473c308754b83e06d1bde0de1449d5fbb01",
          "worktree_digest": "sha256:2e63dd805d4d5422fea94de1ae1125658630e88eec450d02894169c049b15559",
          "untracked_digest": "absent"
        },
        {
          "path": "src/components/ui_view_model.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:bd851dc2221a552fd8d9c30a5cfac76b6c7fdee67da73af98515b9efcecd43ad",
          "index_digest": "sha256:bd851dc2221a552fd8d9c30a5cfac76b6c7fdee67da73af98515b9efcecd43ad",
          "worktree_digest": "sha256:c9621e42893c89f39d6cff6d972e6049b45181fdfd865846fded93e4a4eae95d",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/lvgl_ui.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:b4543e900d976fbaeb18f77ee6c7a7067fb8490205b1e2b7230f90606e1507d7",
          "index_digest": "sha256:b4543e900d976fbaeb18f77ee6c7a7067fb8490205b1e2b7230f90606e1507d7",
          "worktree_digest": "sha256:b4543e900d976fbaeb18f77ee6c7a7067fb8490205b1e2b7230f90606e1507d7",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/lvgl_ui.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:2812831125c0e349d59f036234583cd8dda4d94cdb3c876c50d75e7bcd7d7a94",
          "index_digest": "sha256:2812831125c0e349d59f036234583cd8dda4d94cdb3c876c50d75e7bcd7d7a94",
          "worktree_digest": "sha256:2812831125c0e349d59f036234583cd8dda4d94cdb3c876c50d75e7bcd7d7a94",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/main_screen.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:ab300c952907173e7d72cbae6afab2af88a937b0965a55018ff03b157ee20a9e",
          "index_digest": "sha256:ab300c952907173e7d72cbae6afab2af88a937b0965a55018ff03b157ee20a9e",
          "worktree_digest": "sha256:ab300c952907173e7d72cbae6afab2af88a937b0965a55018ff03b157ee20a9e",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/main_screen.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:9d2288fd10fae7c9a83b867689f74f1495dab3b4fb6231b28e3e75ba5398c49b",
          "index_digest": "sha256:9d2288fd10fae7c9a83b867689f74f1495dab3b4fb6231b28e3e75ba5398c49b",
          "worktree_digest": "sha256:9d2288fd10fae7c9a83b867689f74f1495dab3b4fb6231b28e3e75ba5398c49b",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/midi_clock_dialog.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:093b306b05dface679f831629ee654251c7e160f1d0192b44f487081983b04e3",
          "index_digest": "sha256:093b306b05dface679f831629ee654251c7e160f1d0192b44f487081983b04e3",
          "worktree_digest": "sha256:093b306b05dface679f831629ee654251c7e160f1d0192b44f487081983b04e3",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/modal_backdrop.cpp",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:f518109d589c12c7040a31af21ad4e74eebe259889c70f827808e21e6764995d"
        },
        {
          "path": "src/drivers/ui/pico_display.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:b5d3d869ed118444b087836072db799fc208410d31a6189587d7686aeed821c6",
          "index_digest": "sha256:b5d3d869ed118444b087836072db799fc208410d31a6189587d7686aeed821c6",
          "worktree_digest": "sha256:b5d3d869ed118444b087836072db799fc208410d31a6189587d7686aeed821c6",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/pico_display.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:3399e143e486f5c07416ed4e938a5945d19d7c4cd6802a91a2fbcbae24bb9a48",
          "index_digest": "sha256:3399e143e486f5c07416ed4e938a5945d19d7c4cd6802a91a2fbcbae24bb9a48",
          "worktree_digest": "sha256:3399e143e486f5c07416ed4e938a5945d19d7c4cd6802a91a2fbcbae24bb9a48",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/program_storage_dialog.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:b13d20509cee5d0e3769d594603cf1cda806e477c3db438cd41d7de1d7d6b4b5",
          "index_digest": "sha256:b13d20509cee5d0e3769d594603cf1cda806e477c3db438cd41d7de1d7d6b4b5",
          "worktree_digest": "sha256:b13d20509cee5d0e3769d594603cf1cda806e477c3db438cd41d7de1d7d6b4b5",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/step_grid.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:bd286b8234d17e578dfa71e97da99cf2599c32a33125421fcfac45a62d78a6e5",
          "index_digest": "sha256:bd286b8234d17e578dfa71e97da99cf2599c32a33125421fcfac45a62d78a6e5",
          "worktree_digest": "sha256:47e2d3da88e899e55e8be0ed392e97908186ac9c952bd89bb464ce5126cdea41",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/step_grid.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:99388cbf6326b4db57906027b03b315369e34cf543738dc698f3f1de4de06bfa",
          "index_digest": "sha256:99388cbf6326b4db57906027b03b315369e34cf543738dc698f3f1de4de06bfa",
          "worktree_digest": "sha256:04000154a6157fae60ad6e35dbf0e5df581574e50183138b1b5ecfb44e241f5b",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/step_settings_screen.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:14b007d83505e81efb0992891fdf7f7c71948af7fc1796869d37ac0d36b461c1",
          "index_digest": "sha256:14b007d83505e81efb0992891fdf7f7c71948af7fc1796869d37ac0d36b461c1",
          "worktree_digest": "sha256:14b007d83505e81efb0992891fdf7f7c71948af7fc1796869d37ac0d36b461c1",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/ui_fonts.cpp",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:9b79bc6e13087fb848b9eccc0811e12a501c6c4f307f6d6f58e970d44af7cb91"
        },
        {
          "path": "src/drivers/ui/ui_theme.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:451c30f97888f7a9ec8d44b1f203e576085562c363374997c4f14bdad3bdcd84",
          "index_digest": "sha256:451c30f97888f7a9ec8d44b1f203e576085562c363374997c4f14bdad3bdcd84",
          "worktree_digest": "sha256:3a9a9390045e7dc17c0526d4aefff622d4a4bd7626e9edc550d2b43293617952",
          "untracked_digest": "absent"
        },
        {
          "path": "src/drivers/ui/ui_theme.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:41601cfd69533c6354c5e741f744bbba2fd6edff3e9313e9b931dbc74d4df98b",
          "index_digest": "sha256:41601cfd69533c6354c5e741f744bbba2fd6edff3e9313e9b931dbc74d4df98b",
          "worktree_digest": "sha256:f1dcbcdf49ad40c0602a4e7e94a278e691d3eeca1fd36e6b81f58c30d9aa5fc6",
          "untracked_digest": "absent"
        },
        {
          "path": "src/input/app_input_coordinator.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:0e4f203e7f2482b165b70d710b2314fbb539aa13a9639450fd24130eacd95ef6",
          "index_digest": "sha256:0e4f203e7f2482b165b70d710b2314fbb539aa13a9639450fd24130eacd95ef6",
          "worktree_digest": "sha256:0e4f203e7f2482b165b70d710b2314fbb539aa13a9639450fd24130eacd95ef6",
          "untracked_digest": "absent"
        },
        {
          "path": "src/input/app_ui_snapshot_builder.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "unstaged",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:4d7ba5dd7afdd8635a82ff2900191a61eb3fbb1c372787b33cd401568704f1e4",
          "index_digest": "sha256:4d7ba5dd7afdd8635a82ff2900191a61eb3fbb1c372787b33cd401568704f1e4",
          "worktree_digest": "sha256:da36663bce689ba2c7dcfe9506320d31397347034a092ce08f9b3cbd91704dd5",
          "untracked_digest": "absent"
        },
        {
          "path": "src/input/ui_mock_data.h",
          "object_kind": {
            "head": "absent",
            "index": "absent",
            "worktree": "absent",
            "untracked": "regular"
          },
          "state": "untracked",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "absent",
          "index_digest": "absent",
          "worktree_digest": "absent",
          "untracked_digest": "sha256:b5bbd29c8256143a29451b87f189a180a3c2f40e576fe44704355b228e0df11e"
        },
        {
          "path": "src/program/program_storage_modal.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:18ec2483884613013de0d6b6c36164d01da2621b045cb7bae56b5a318d61aac6",
          "index_digest": "sha256:18ec2483884613013de0d6b6c36164d01da2621b045cb7bae56b5a318d61aac6",
          "worktree_digest": "sha256:18ec2483884613013de0d6b6c36164d01da2621b045cb7bae56b5a318d61aac6",
          "untracked_digest": "absent"
        },
        {
          "path": "test/test_native/components/test_ui_view_model.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:d98eba5867b5062947c37a331a9add28250aeaefb6a77d1cbf50cb9d84c7b257",
          "index_digest": "sha256:d98eba5867b5062947c37a331a9add28250aeaefb6a77d1cbf50cb9d84c7b257",
          "worktree_digest": "sha256:d98eba5867b5062947c37a331a9add28250aeaefb6a77d1cbf50cb9d84c7b257",
          "untracked_digest": "absent"
        },
        {
          "path": "test/test_native/input/test_app_ui_snapshot_builder.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:41cfdf6cea5869c4cbad2fa6a43ef84cf8b5f8f7dfce8b14e70ca7c3a2a16c9e",
          "index_digest": "sha256:41cfdf6cea5869c4cbad2fa6a43ef84cf8b5f8f7dfce8b14e70ca7c3a2a16c9e",
          "worktree_digest": "sha256:41cfdf6cea5869c4cbad2fa6a43ef84cf8b5f8f7dfce8b14e70ca7c3a2a16c9e",
          "untracked_digest": "absent"
        },
        {
          "path": "test/test_native/input/test_midi_clock_modal.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:969a4c7e73c321165bae826a7b5238b78b4a189c0e4cf0ead3dc6486777a8d52",
          "index_digest": "sha256:969a4c7e73c321165bae826a7b5238b78b4a189c0e4cf0ead3dc6486777a8d52",
          "worktree_digest": "sha256:969a4c7e73c321165bae826a7b5238b78b4a189c0e4cf0ead3dc6486777a8d52",
          "untracked_digest": "absent"
        },
        {
          "path": "test/test_native/main.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:ca360b7acd921ed03aa6bafbe97890b0920ab2cfc09a9cf790f2f02f6d1378b4",
          "index_digest": "sha256:ca360b7acd921ed03aa6bafbe97890b0920ab2cfc09a9cf790f2f02f6d1378b4",
          "worktree_digest": "sha256:ca360b7acd921ed03aa6bafbe97890b0920ab2cfc09a9cf790f2f02f6d1378b4",
          "untracked_digest": "absent"
        },
        {
          "path": "test/test_native/program/test_program_storage_modal.cpp",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:47364a220827480ebd2b3c410b58cdf34d048de8874e95d27f950e8456fd5170",
          "index_digest": "sha256:47364a220827480ebd2b3c410b58cdf34d048de8874e95d27f950e8456fd5170",
          "worktree_digest": "sha256:47364a220827480ebd2b3c410b58cdf34d048de8874e95d27f950e8456fd5170",
          "untracked_digest": "absent"
        }
      ]
    },
    "primary_symbols": [
      {
        "symbol": "UiViewModel.publish/read",
        "file": "src/components/ui_view_model.h",
        "lines": "13-147",
        "role": "атомарная передача полного UI-снимка между ядрами"
      },
      {
        "symbol": "AppUiSnapshotBuilder.decorate",
        "file": "src/input/app_ui_snapshot_builder.h",
        "lines": "24-45",
        "role": "сборка UI-состояний и preview/mocks"
      },
      {
        "symbol": "LvglUi.setup/readViewModel",
        "file": "src/drivers/lvgl_ui.cpp",
        "lines": "5-43",
        "role": "создание экранов, применение состояния и модальный хост"
      },
      {
        "symbol": "MainScreen.create/apply",
        "file": "src/drivers/ui/main_screen.cpp",
        "lines": "5-73",
        "role": "глобальная строка и статус секвенции"
      },
      {
        "symbol": "StepGrid.init/apply",
        "file": "src/drivers/ui/step_grid.cpp",
        "lines": "7-105",
        "role": "геометрия и состояние 16 карточек"
      }
    ],
    "related_symbols": [
      {
        "symbol": "ProgramStorageModal",
        "relationship": "источник Action/Slot/ResetConfirmation и слота 0–15",
        "relevance": "отрисовка меню, списка и подтверждения"
      },
      {
        "symbol": "MidiClockModal",
        "relationship": "источник active/selection",
        "relevance": "отделить текущий режим от фокуса"
      },
      {
        "symbol": "PicoDisplay.setup",
        "relationship": "размер LVGL берёт из gfx.width()/height()",
        "relevance": "проверка фактической ориентации"
      }
    ],
    "execution_path": [
      "Application::loop собирает реальное состояние и вызывает AppInputCoordinator::decorateUiSettings.",
      "AppUiSnapshotBuilder добавляет текущий modal/input state и только недостающие mock-поля.",
      "UiViewModel::publish переносит согласованный снимок между ядрами.",
      "Application::loop1 вызывает LvglUi::readViewModel; тот обновляет основной экран и видимый модальный слой."
    ],
    "pdg_constraints": [
      {
        "description": "Если sameSettings считает снимок прежним, publish завершается без обновления",
        "affected_statements": [
          "src/components/ui_view_model.h:14",
          "src/components/ui_view_model.h:15"
        ],
        "implementation_consequence": "Каждое добавленное поле должно участвовать в sameSettings, pack и unpack."
      },
      {
        "description": "read повторяется при нечётной или изменившейся generation",
        "affected_statements": [
          "src/components/ui_view_model.h:73",
          "src/components/ui_view_model.h:74",
          "src/components/ui_view_model.h:98",
          "src/components/ui_view_model.h:99"
        ],
        "implementation_consequence": "Не читать отдельные atomics из UI в обход read()."
      },
      {
        "description": "Сейчас UiPage::StepSettings вызывает загрузку отдельного экрана",
        "affected_statements": [
          "src/drivers/lvgl_ui.cpp:31",
          "src/drivers/lvgl_ui.cpp:35",
          "src/drivers/lvgl_ui.cpp:37"
        ],
        "implementation_consequence": "Для модалки сохранить основной экран как host и переключать только верхний слой."
      }
    ],
    "architectural_patterns": [
      {
        "pattern": "main core публикует, UI core читает",
        "example_location": "src/application.cpp:170-196",
        "usage_guidance": "LVGL создавать и менять только на UI core"
      },
      {
        "pattern": "UI-демо отделено через SWING_METRO_UI_PREVIEW",
        "example_location": "src/input/ui_mock_data.h:14-45",
        "usage_guidance": "мок-значения использовать только для недостающих полей или preview-фикстур"
      },
      {
        "pattern": "storage modal уже хранит 0–15 и Cancel sentinel",
        "example_location": "src/program/program_storage_modal.cpp:33-44",
        "usage_guidance": "отрисовывать 01–16, не менять нумерацию модели"
      }
    ],
    "files_to_modify": [
      {
        "file": "src/drivers/lvgl_ui.cpp",
        "symbols": [
          "LvglUi::setup",
          "LvglUi::readViewModel"
        ],
        "intended_change": "инициализация шрифта, хост основного экрана, один активный модальный слой"
      },
      {
        "file": "src/drivers/ui/main_screen.cpp",
        "symbols": [
          "MainScreen::create",
          "MainScreen::apply"
        ],
        "intended_change": "строки BPM/CLK и SEQ/MIDI CH/SWING, разделитель, новая сетка"
      },
      {
        "file": "src/drivers/ui/step_grid.cpp",
        "symbols": [
          "StepGrid::init",
          "StepGrid::apply"
        ],
        "intended_change": "сетка 4×4, текст и единая рамка состояний"
      },
      {
        "file": "src/drivers/ui/step_settings_screen.cpp",
        "symbols": [
          "StepSettingsScreen::create",
          "StepSettingsScreen::apply"
        ],
        "intended_change": "модалка Normal/Legato/Repeat вместо отдельного экрана"
      },
      {
        "file": "src/drivers/ui/midi_clock_dialog.cpp",
        "symbols": [
          "MidiClockDialog::create",
          "MidiClockDialog::apply"
        ],
        "intended_change": "панель выбора и отдельная индикация активного режима"
      },
      {
        "file": "src/drivers/ui/program_storage_dialog.cpp",
        "symbols": [
          "ProgramStorageDialog::create",
          "ProgramStorageDialog::apply"
        ],
        "intended_change": "меню, шесть видимых слотов, scrollbar и reset confirmation"
      },
      {
        "file": "src/drivers/ui/ui_theme.cpp",
        "symbols": [
          "createModalPanel",
          "createLabel",
          "setMenuItemStyle"
        ],
        "intended_change": "общие пиксельные стили"
      },
      {
        "file": "src/components/ui_view_model.h",
        "symbols": [
          "UiViewModel::publish",
          "UiViewModel::read",
          "UiViewModel::sameSettings"
        ],
        "intended_change": "дополнить только поля, реально требуемые представлению"
      },
      {
        "file": "src/input/app_ui_snapshot_builder.h",
        "symbols": [
          "AppUiSnapshotBuilder::decorate"
        ],
        "intended_change": "подать visual-only состояния и mock-поля"
      }
    ],
    "tests": [
      {
        "file": "test/test_native/components/test_ui_view_model.cpp",
        "scenarios": [
          "каждое новое поле меняется → read возвращает изменение",
          "два чередующихся полных снимка между ядрами → невозможна смесь полей"
        ]
      },
      {
        "file": "test/test_native/input/test_app_ui_snapshot_builder.cpp",
        "scenarios": [
          "выбранный шаг → modal state и правильные значения",
          "preview и обычный режим → реальные поля не затираются mock-данными"
        ]
      },
      {
        "file": "test/test_native/input/test_midi_clock_modal.cpp",
        "scenarios": [
          "CANCEL → режим не меняется",
          "активный режим отличается от строки фокуса"
        ]
      },
      {
        "file": "test/test_native/program/test_program_storage_modal.cpp",
        "scenarios": [
          "0 и 15 → подписи 01 и 16 в UI-адаптере",
          "NO → команда Reset не появляется",
          "переход по всем 16 слотам → видимое окно корректно сдвигается"
        ]
      }
    ],
    "verification_commands": [
      "make format-check",
      "make tidy",
      "make test",
      "make build",
      "pio run -e rpipico2-ui-preview",
      "make verify"
    ],
    "risks": [
      "Текущая рабочая копия имеет незакоммиченные UI-изменения и исходно не собирается.",
      "Реестр и SVG пока untracked; при новом checkout их нужно включить в ветку.",
      "Физические размеры дисплея 128×160 и rotation=1 требуют фактической проверки 160×128 LVGL.",
      "TinyTTF на устройстве может расходовать RAM и давать отличающиеся пиксельные метрики.",
      "Смена UiSettings затрагивает publish/read и tests; граф для Struct UiSettings дал UNKNOWN."
    ],
    "assumptions": [
      "Визуальный этап не добавляет режимы в движок и формат хранения; проверить, что Mode/Repeat Count берутся из UiMockData до реализации поведения.",
      "Figma README и локальные SVG соответствуют последнему согласованному макету; сверить дату экспорта до финального пиксельного QA.",
      "Если gfx.width()/height() уже равны 160×128 после rotation=1, не менять драйвер дисплея."
    ],
    "open_questions": [
      "В макете не определён случай совпадения фокуса и активного шага; в плане применяется приоритет active > focus.",
      "Для очень плотных масок MIDI каналов способ сокращённой подписи нужно утвердить по 1:1 снимку, сохраняя SWING справа."
    ],
    "avoid": [
      "Не вызывать LVGL из main core.",
      "Не обращаться к LittleFS во время работающего транспорта.",
      "Не менять код MIDI playback, legato/repeat или формат программы в визуальной фазе.",
      "Не подменять реальные tempo/swing/step значения preview-значениями в production."
    ]
  }
}
~~~

## 12. Допущения и открытые вопросы

- [assumed] Текущий экспорт README/SVG является последней согласованной версией; перед пиксельным QA проверить даты файлов. Источник геометрии — SVG, PNG используют только как превью.
- [assumed] Визуальная фаза не меняет движок, сериализацию программы или правила Gate/Legato/Repeat; реализация поведения будет отдельным этапом.
- [assumed] При совпадении active и focused красная рамка имеет приоритет; если потребуется другое поведение, меняется только функция выбора стиля рамки.
- [inferred] Формат строки для плотной маски MIDI каналов потребуется проверить на реальном шрифте 1:1; ограничение — SEQ слева и SWING справа без переноса.
- Отложено: реализация MIDI channel routing, playback Legato/Repeat и сохранение их в слотах.

## 13. Готово, когда

- Ветка содержит рабочий план и доступные локальные референсы; этапы выполняются в указанном порядке.
- Обычная и preview-прошивки собираются; make verify проходит.
- Все указанные состояния показаны на экране 160×128 без обрезки, а сравнение 1:1 с локальными SVG зафиксировано снимками.
- UI получает согласованный снимок через ViewModel; во время работающего транспорта не появляется flash I/O.
- 16 слотов достижимы, Cancel/NO сохраняют существующую семантику, новые mock-поля не меняют playback.
