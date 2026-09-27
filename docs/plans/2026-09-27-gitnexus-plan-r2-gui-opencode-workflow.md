# R2: GUI decomposition with OpenCode

Approved 2026-09-27. Base: f02409f (R1 completed).

## Contract
Preserve GUI appearance, input behavior, timing instrumentation, cross-core consistency. LVGL stays on core 1. No engine/input redesign, feature UI, flash, pushes or changes to R0 fixtures.

## Ownership
Each package: fresh GitNexus and impact checks; explicit OpenCode Plan prepares a read-only local plan; Codex validates it; separate OpenCode Build implements; Codex reviews, verifies, performs full graph gates and commits. Use configured model and compact handoffs. OpenCode never commits. If Build is blocked, diagnose and report; no silent Codex implementation takeover. Auto-approved OpenCode permissions require explicit consent.

## 7. Implementation sequence
1. R2.1: Extract ui_snapshot.h, keep UiSettings name; group existing fields into main (including external clock), editor, midiClock, storage; page remains top-level. Preserve exact packed atomics, defaults, seq_cst, duplicate suppression and common generation. Adapt producer, decorator, UI and tests mechanically. Test each field change and concurrent consistency across ALL fields. Snapshot builder/coordinator decomposition deferred to R3.
2. R2.2: Extract PicoDisplay owning bus, display, buffer, init/flush. Keep pins, rotation, actual 6400-color buffer, partial rendering and flush timing order. Add UiTheme and StepGrid under src/drivers/ui, preserving subject/observer mechanism, 16 objects, geometry and colors. Splash stays 500 ms. Callback owners stable and nonmovable.
3. R2.3: MainScreen and StepSettingsScreen own trees, subjects and caches; create(), root(), apply(group). Main owns StepGrid; keep formatting/guards. LvglUi reads once, loads screen only on page change. Keep timer handler -> runtime snapshot publication -> UI snapshot application.
4. R2.4: MidiClockDialog and ProgramStorageDialog own top-layer trees, visibility/caches with create()/apply(group). Preserve stacking, all states/cancel/reset/busy/success/error. LvglUi retains assembly, service and routing only. Components constructed once.

## Verification
Each package: format-check, native tests, firmware build and full detect_changes before atomic commit. Final make verify: formatting, tidy, native/host tests and production/off/fault firmware. Refresh index after relationship edits/commits. UNKNOWN graph results need source checks; warn before HIGH/CRITICAL edits. Do not import LVGL into native tests to fake UI coverage.
Report RAM/Flash and validation in docs/refactoring/r2-gui-decomposition.md. Manual device checklist: both screens/transitions, active/disabled steps, all modal states and live playback UI. Separate software completion from hardware validation; no claim of unperformed device checks.

## Evidence limitations
Schema-4 index current at f02409f, CLI 1.6.12. UiViewModel MEDIUM impact: five direct files, seven total; LvglUi LOW. UiSettings UNKNOWN, source usages confirmed. PDG controls yielded no results; ordering constraints are source-verified, not PDG-proven.

## 11. implementation_context
```json
{
  "files_to_modify": [
    "src/components/ui_view_model.h",
    "src/drivers/lvgl_ui.h",
    "src/drivers/lvgl_ui.cpp",
    "src/main.cpp",
    "src/input/app_input_coordinator.h",
    "test/test_native/components/test_ui_view_model.cpp",
    "Makefile",
    "platformio.ini"
  ],
  "acceptance_criteria": [
    "behavior preserved",
    "all snapshot fields tested",
    "four verified packages"
  ],
  "verification_commands": [
    "make format-check",
    "make test",
    "make build",
    "make verify"
  ],
  "avoid": [
    "renaming UiSettings",
    "removing observer mechanism",
    "silent Codex implementation takeover",
    "engine/input redesign"
  ],
  "evidence_provenance": {
    "schema_version": 2,
    "head_commit": "f02409f9ae71f61cfaa1638458b2ec123d9585b5",
    "generated_plan_path": "docs/plans/2026-09-27-gitnexus-plan-r2-gui-opencode-workflow.md",
    "global_dirty_digest": {
      "algorithm": "sha256",
      "canonicalization": "gitnexus-evidence-provenance-v2 NUL-framed UTF-8 records",
      "value": "0a9c85780067d9afcd0764f307b60891e3cee927ee11eaeb5ec7826d10fd82cd"
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
        "path": "platformio.ini",
        "object_kind": {
          "head": "regular",
          "index": "regular",
          "worktree": "regular",
          "untracked": "absent"
        },
        "state": "clean",
        "rename_from": null,
        "rename_to": null,
        "head_digest": "sha256:0c7c2292a38a8a07267d04af3848551d127eb62c75e64dca40234c5afe7f6f42",
        "index_digest": "sha256:0c7c2292a38a8a07267d04af3848551d127eb62c75e64dca40234c5afe7f6f42",
        "worktree_digest": "sha256:0c7c2292a38a8a07267d04af3848551d127eb62c75e64dca40234c5afe7f6f42",
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
        "state": "clean",
        "rename_from": null,
        "rename_to": null,
        "head_digest": "sha256:f209e95f3a857b40ea1879c2687aee0087867fbcefd8dc7b165f1adc7010c6c2",
        "index_digest": "sha256:f209e95f3a857b40ea1879c2687aee0087867fbcefd8dc7b165f1adc7010c6c2",
        "worktree_digest": "sha256:f209e95f3a857b40ea1879c2687aee0087867fbcefd8dc7b165f1adc7010c6c2",
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
        "head_digest": "sha256:13e2ccd1d42002a3bf120b9db4191bd8c2a6aaf3d813fde18157ac8ccc7c6f9a",
        "index_digest": "sha256:13e2ccd1d42002a3bf120b9db4191bd8c2a6aaf3d813fde18157ac8ccc7c6f9a",
        "worktree_digest": "sha256:13e2ccd1d42002a3bf120b9db4191bd8c2a6aaf3d813fde18157ac8ccc7c6f9a",
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
        "head_digest": "sha256:7c176ec88500bc9e0a913719ad80b9a9fbf83b499909607b385e1d9a7dc0e175",
        "index_digest": "sha256:7c176ec88500bc9e0a913719ad80b9a9fbf83b499909607b385e1d9a7dc0e175",
        "worktree_digest": "sha256:7c176ec88500bc9e0a913719ad80b9a9fbf83b499909607b385e1d9a7dc0e175",
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
        "head_digest": "sha256:0ad63d22a42014eddeee2fc51ac122c671ec01c61376953ebe7e06e79c23d6c7",
        "index_digest": "sha256:0ad63d22a42014eddeee2fc51ac122c671ec01c61376953ebe7e06e79c23d6c7",
        "worktree_digest": "sha256:0ad63d22a42014eddeee2fc51ac122c671ec01c61376953ebe7e06e79c23d6c7",
        "untracked_digest": "absent"
      },
      {
        "path": "src/main.cpp",
        "object_kind": {
          "head": "regular",
          "index": "regular",
          "worktree": "regular",
          "untracked": "absent"
        },
        "state": "clean",
        "rename_from": null,
        "rename_to": null,
        "head_digest": "sha256:705c19a4edf2f0842193496040bbf448e6d29bd95bd43f89fa7f98f11833e535",
        "index_digest": "sha256:705c19a4edf2f0842193496040bbf448e6d29bd95bd43f89fa7f98f11833e535",
        "worktree_digest": "sha256:705c19a4edf2f0842193496040bbf448e6d29bd95bd43f89fa7f98f11833e535",
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
        "head_digest": "sha256:781ad7abfefb8585eebc644cbe0509fbe5b481b6aaca81ef93340b5fd59617bf",
        "index_digest": "sha256:781ad7abfefb8585eebc644cbe0509fbe5b481b6aaca81ef93340b5fd59617bf",
        "worktree_digest": "sha256:781ad7abfefb8585eebc644cbe0509fbe5b481b6aaca81ef93340b5fd59617bf",
        "untracked_digest": "absent"
      }
    ]
  }
}
```
