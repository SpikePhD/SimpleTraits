# Simple Traits - AGENTS.md

`AGENTS.md` is the single source of truth for this project. There is intentionally no `CLAUDE.md`.

## What this mod does

Simple Traits (ST) is an SKSE plugin that adds six player traits. The player spends trait
points on them; each point gives one bonus:

| Trait | Bonus per point | Setting (`per_point.*`) | Default |
|---|---|---|---|
| Strength | +% of base Stamina | `stamina_percent` | 0.05 (5%) |
| Resilience | +% of base Health | `health_percent` | 0.05 (5%) |
| Agility | +Critical hit chance, as the CriticalChance actor value | `critical_chance` | 1 (= +1%) |
| Intelligence | +% XP on every SAL XP award (SAL API V4) | `intelligence_xp_percent` | 0.10 (10 points = 2x XP) |
| Wisdom | +% of base Magicka | `magicka_percent` | 0.05 (5%) |
| Charisma | Better buy and sell prices (fBarterMin/fBarterMax) | `charisma_price_improvement` | 0.01 per point |

ST requires Simple Alternate Levelling (SAL). SAL's repository is a separate project and
is reference only: never edit it from here.

## Core rules (every change must follow these)

1. **Never write actor base values.** Actor-value bonuses are applied as the plugin's own
   delta on the permanent modifier layer. The cosave records exactly how much ST has
   applied, so reconciling (`TraitRules::ReconcileDelta(target, applied)`) is idempotent.
2. **Unspent points are derived, never stored:**
   `earned(level) = starting_points + floor(level / levels_per_point)`,
   `unspent = max(0, earned - sum(allocations))`. Allocations are never removed
   automatically, even if settings reduce earned below spent.
3. **Unspent points carry forward** when the player closes the menu without spending.
   There is no respec in v1: `TraitRules::ValidateAllocation` rejects any decrease.

## Status

- Phase 1 (done): scaffold, SAL handshake, config, pure rules, diagnostics.
- Phase 2 (done): cosave, Strength/Resilience/Wisdom bonuses on the permanent modifier
  layer, temporary SKSE Menu Framework debug page. Verified in game.
- Phase 3 (done): trait allocation menu (`ST_TraitMenu.swf`) opened as SAL's
  pre-skill-menu level-up step (SAL API V2). Verified in game.
- Phase 4 (done): Strength/Resilience/Wisdom as a percentage of the base value;
  Intelligence grants retroactive SAL skill points (SAL API V3); settings page in SKSE
  Menu Framework; cosave v2. Verified in game.
- Phase 5 (done): Agility (CriticalChance actor value) and Charisma (barter game
  settings). All six traits now have an effect. Verified in game except the items in
  "Deferred in-game checks".
- Phase 6 (built, not yet tested in game): Intelligence multiplies SAL XP awards (SAL API
  V4) instead of granting skill points, so a future temporary trait bonus only affects XP
  earned while it lasts. Skill points granted by the old Intelligence stay.

## Architecture

```text
SKSEPluginLoad()
├── Config::Load()                - reads SimpleTraits.json + SimpleTraits.user.json (no logging)
├── InitializeLog()               - timestamped spdlog in the standard SKSE log directory,
│                                   retention from debug.max_log_files
├── Config::LogReport()           - logs config errors/warnings and effective values
├── TraitState::SetSettings()     - copy of the validated trait settings
├── SKSE::Init(.log = false)
├── TraitState::RegisterSerialization() - cosave 'SMTR' / record 'TRTS' v1
├── MessagingInterface (SKSE)
│   ├── kPostPostLoad             - logs handshake state (SAL broadcasts from its own handler,
│   │                               which may run before or after ours)
│   ├── kDataLoaded               - SALBridge::Finalize(): still pending -> Missing;
│   │                               TraitState::CaptureGameSettings() (barter originals);
│   │                               DiagnosticSinks::Register(); TraitMenu::Register()
│   │                               (menu + SAL level-up step); SAL XP multiplier
│   │                               (Intelligence); SettingsPage::Register();
│   │                               DebugPage::Register()
│   ├── kPreLoadGame              - TraitMenu::ResetState()
│   ├── kPostLoadGame             - DiagnosticSinks::Reset(); TraitState::Reconcile()
│   └── kNewGame                  - TraitMenu::ResetState(); DiagnosticSinks::Reset();
│                                   TraitState::OnNewGame()
└── SALBridge::RegisterListener() - listener for sender "SimpleAlternateLevelling";
                                    SKSE refuses it when SAL is not loaded -> Missing
```

### SAL handshake

SAL broadcasts `SAL::kMessageInterface` with a pointer to a static `SALInterfaceV1` at its
`kPostPostLoad`. `HandshakeRules::ClassifyInterfaceMessage` (pure) accepts it when the
type matches, data is non-null, `version >= 1`, `dataLen >= sizeof(SALInterfaceV1)` (later
versions only append members) and every V1 function pointer is non-null.

`SALBridge` states: `Pending -> Ready | Unsupported | Missing`. SAL is a hard requirement
of ST, so `Missing`/`Unsupported` only guard a broken install: on kDataLoaded ST logs an
error and shows a message box ("Simple Alternate Levelling NOT LOADED!" or "... NOT
SUPPORTED!"), and SAL-dependent features (Intelligence) stay disabled. That path is not
tested in game. No SAL callbacks are registered yet.

`extern/SAL/SAL_API.h` is vendored verbatim; `extern/SAL/README.md` records the SAL commit
and SHA-256. Update both together, from a committed SAL version only.

### Level-up flow and trait menu

```text
vanilla LevelUp Menu opens -> SAL hides it
  -> SAL V2 pre-skill-menu step: TraitMenu::WantsStep(level)
       unspent trait points > 0 ? open ST_TraitMenu, SAL waits : continue
  -> SAL skill menu -> (SAL V1 step, unused by ST) -> vanilla LevelUp Menu once
```

- ST registers exactly one step on kDataLoaded: SAL V2's pre-skill-menu step when the
  handshake reports it (`HandshakeRules::HasPreSkillMenuStep`), otherwise V1's
  post-skill-menu step (then the order is SAL -> ST -> vanilla).
- The menu opens on any level-up with unspent points (including carried-over and starting
  points); the starting points are therefore spent at the first level-up.
- `ContinueLevelUp` is shared by SAL's steps, so ST calls it exactly once per wait:
  `TraitMenuRules::ContinuationGuard` is armed when `WantsStep` returns true and every exit
  path (confirm, unexpected close, SWF failure, nothing to allocate) goes through
  `FinishStep`. SAL continues on its own after 10 s of unpaused play; the menu pauses.
- The menu state machine is `Idle -> Opening -> Active -> Committing -> Closing`
  (`TraitMenuRules::Session`, pure, portable tests). -/+ only change a preview on top of
  the committed allocation; Deallocate never goes below it (no respec). Confirm re-checks
  that the committed allocation did not change while open, then commits once through
  `TraitState::CommitAllocation` (ValidateAllocation + reconcile). Unspent points are kept.
- Every Scaleform callback must come from the active movie with the exact argument count;
  trait ids must be integral 0-5 (`ParseTraitIndex`). An unexpected close commits the
  preview, like SAL's skill menu. Load, revert and new game drop the session.
- Text comes from `Interface/Translations/SimpleTraits_ENGLISH.txt` (`$ST_*`, generated by
  `tools/generate_translation.py`; English fallbacks in `src/TraitMenu.cpp`).

### Trait menu SWF

`data/Interface/ST_TraitMenu.swf` is drawn entirely by
`assets/swf_src/scripts/frame_1/DoAction.as` (AS2, SAL's visual language: colours, font,
backdrop, row highlight, footer buttons). Its container was SAL's `EA_SkillMenu.swf`
(1280x720 stage, `gfx.io.GameDelegate`), with the script replaced. Rebuild or verify it
with Java 17 and FFDec 25.1.3 via `ST_JAVA_EXECUTABLE` / `ST_FFDEC_JAR` and the
`rebuild_trait_menu` / `verify_trait_menu` targets; normal builds do not need FFDec.
`tools/validate_ui_assets.py` (ctest `ui_asset_contract`) checks that function names and
argument shapes match between `src/TraitMenu.cpp` and the ActionScript, that every `$ST_*`
key exists, and that the SWF has no static stage objects or preview harness.

A browser preview (Ruffle) lives in the ignored `out/ui-preview/`: a copy of the SWF with
an ExternalInterface shim, driven by `index.html`. Keyboard input works there; confirm
layouts in game.

### Trait state and bonuses

`TraitState` owns the six allocations and the amount of each actor-value bonus ST has
applied (`TraitRules::AppliedBonuses`). Both are saved in the cosave; unspent points are
never stored.

- **Reconcile** (`kPostLoadGame`, after each commit, after the vanilla LevelUp Menu
  closes, after a settings change): `TraitRules::PlanReconcile` computes
  `target = base * percent * points` for Stamina (Strength), Health (Resilience) and
  Magicka (Wisdom), where `base` is `GetBaseActorValue` only (never other modifiers, so
  ST's own bonus cannot compound). It applies `target - applied` with
  `ModActorValue(ACTOR_VALUE_MODIFIER::kPermanent, ...)` and records `target` as applied.
  Base values are never written. The bonus follows the base retroactively (level-up
  attribute choice raises it); a lowered percentage removes only ST's own excess; Phase 2
  flat bonuses migrate on the first load. Each change is logged with
  base/permanent/current before and after.
- **Agility** is reconciled with the others as a flat CriticalChance bonus:
  `points * critical_chance * (1 / fWeaponConditionCriticalChanceMult)`, the game setting
  read on every reconcile (10 CriticalChance per 1% in vanilla). The cosave's applied
  entry for actor value 33 tracks it; records written before Agility simply lack it.
- **Charisma** changes the global game settings `fBarterMin`/`fBarterMax`, which are not
  saved per character. `CaptureGameSettings` stores the originals on kDataLoaded. Every
  reconcile writes `ApplyCharisma(originals, points, charisma_price_improvement)` and
  remembers the written values; if the current values differ from what ST wrote, another
  mod changed them, so ST logs a warning and adopts them as the new originals. Revert
  (main menu, new game, before a load) writes the originals back. The reconcile after
  `kPostLoadGame` applies the loaded character's Charisma.
- **Intelligence** (SAL API V4 `RegisterXPMultiplier`, `docs/SAL_API_V4.md`): SAL calls
  `TraitState::XPMultiplier(sourceCategory)` on every XP award and multiplies the award by
  `TraitRules::XPMultiplier = 1 + points * intelligence_xp_percent` (linear, uncapped; 1.0
  for a bad setting). The value is published to an atomic whenever the allocation or
  settings change (commit, load, revert, new game, settings), so the per-award call never
  takes ST's mutex. The source category is ignored for now. Nothing is granted or stored;
  SAL keeps XP as a float, so small bonuses accumulate even when the notification rounds.
  Design rule for future temporary trait bonuses (potions, gear): every effect is a live
  value computed from the trait's effective points, never a one-time grant; only the
  greatest temporary bonus will count, no hard cap. The old V3 skill point bonus is no
  longer registered; `skillPointsGranted` in the cosave is legacy, read and written back
  unchanged.
- The engine stores the permanent modifier in the main save, so after a load the bonus is
  already present and the reconcile delta is 0 unless settings changed.
- **Spend** (`SpendPoint`, main thread): +1 on a trait when `ValidateAllocation` allows it,
  then reconcile. Refused when no game is loaded.
- New game and revert clear the state without touching actor values.

Known limits: if the cosave is lost while the main save keeps ST's modifiers, ST sees
"applied 0" and applies the bonus again. Uninstalling ST leaves its modifiers in the save
(a cleanup option can come later).

### Cosave

- Unique ID `SMTR`, record `TRTS`, version 2 (`TraitSave`, pure codec, portable tests).
- Payload, little-endian: `uint32 allocation[6]`, `uint32 skillPointsGranted` (legacy),
  `uint32 count` (max 8), then `count` x `{ uint32 RE::ActorValue id, float32 applied }`.
  v2 writes Stamina, Health, Magicka and CriticalChance (33). v1 records (no
  `skillPointsGranted`) and v2 records without the CriticalChance entry still load.
  Entries are keyed by actor value so later bonuses extend the whitelist
  without a new layout.
- Rejected: other versions, wrong lengths, allocations above `kMaxPointsPerTrait`, ids
  outside the whitelist, duplicate ids, non-finite amounts. The first valid record wins; a
  missing or rejected record means no allocations and nothing applied.

### Settings page

"Simple Traits / Settings" in SKSE Menu Framework (optional; without it players edit
`SimpleTraits.user.json`). Settings are listed in registry order under Trait points,
Bonuses per point and Debug, with labels and descriptions from the translation file
(`$ST_SETTING_*`, `$ST_DESC_*`, cached on the main thread at registration) and the default
in each tooltip. Values that differ from the default are highlighted. Like SAL's page,
each committed edit (Enter, focus loss, +/- click, checkbox, reset all) is validated by
`SettingsModel::Set`, saved atomically (`Config::SaveAndApply`, only differences from the
shipped defaults) and applied on the main thread: `TraitState::SetSettings` then
`ReconcileIfActive`. Partial typing is never saved. Rendering may run off the main thread;
every model access holds the page's mutex.

### Debug page (temporary)

With `debug.allocation_page=true` and SKSE Menu Framework installed, "Simple Traits /
Debug" shows level, earned/spent/unspent, each trait's points and applied bonus, and a +1
button per trait. Rendering may run off the main thread: the page reads
`TraitState::GetSnapshot()` (mutex-guarded) and queues spending with
`SKSE::GetTaskInterface()->AddTask`. It goes away when the real allocation menu exists.

### Key files

| File | Role |
|---|---|
| `src/main.cpp` | Plugin entry, log init and rotation, SKSE messaging |
| `src/Config.cpp` / `include/Config.h` | Reads both JSON files next to the DLL, fills `Config::traits` |
| `src/SettingsModel.cpp` / `include/SettingsModel.h` | Fixed registry with bounds and built-in defaults, layering, schema version, per-value validation |
| `src/TraitRules.cpp` / `include/TraitRules.h` | Pure rules: points, allocation validation, bonuses, reconcile delta, Intelligence XP multiplier, barter adjustment |
| `src/TraitSave.cpp` / `include/TraitSave.h` | Pure cosave codec: encode, decode with validation, first-valid adoption |
| `src/TraitState.cpp` / `include/TraitState.h` | Allocations and applied bonuses, cosave callbacks, reconcile, spend, snapshots |
| `src/TraitMenuRules.cpp` / `include/TraitMenuRules.h` | Pure menu session (preview/commit) and the exactly-once continuation guard |
| `src/TraitMenu.cpp` / `include/TraitMenu.h` | Scaleform trait menu, SAL level-up step, validated callbacks, commit |
| `assets/swf_src/scripts/frame_1/DoAction.as` | Trait menu ActionScript; `data/Interface/ST_TraitMenu.swf` is built from it |
| `data/Interface/Translations/SimpleTraits_ENGLISH.txt` | Menu text (UTF-16 LE), generated by `tools/generate_translation.py` |
| `docs/SAL_API_V2.md` | The SAL API V2 request (pre-skill-menu step), implemented in SAL `817e418` |
| `src/SettingsPage.cpp` / `include/SettingsPage.h` | Settings page in SKSE Menu Framework |
| `docs/SAL_API_V3.md` | The SAL API V3 request (skill point bonus), implemented in SAL `d53dc5b`; no longer used |
| `docs/SAL_API_V4.md` | The SAL API V4 request (XP multiplier), implemented in SAL `6933938` |
| `src/DebugPage.cpp` / `include/DebugPage.h` | Temporary SKSE Menu Framework page for spending points |
| `extern/SKSEMenuFramework/SKSEMenuFramework.h` | Vendored MIT header (via SAL `751ebcd`, upstream `aa8effa`); runtime-resolved, no link dependency |
| `src/HandshakeRules.cpp` / `include/HandshakeRules.h` | Pure SAL interface message validation |
| `src/SALBridge.cpp` / `include/SALBridge.h` | SAL listener, handshake state, received interface |
| `src/DiagnosticSinks.cpp` / `include/DiagnosticSinks.h` | Log-only sinks: `CriticalHit::Event` (player crits at info with CriticalChance current/permanent/base and weapon CRDT; others at debug) and a `TESHitEvent` counter of player weapon swings, split into normal and power attacks, for the observed crit rates (totals logged on the first swing, every 10 swings, and on load/new game). An enchanted weapon raises two hit events per swing (the enchantment's has no attack flags); events for the same target and weapon within 100 ms merge into one swing |
| `src/LogPolicy.cpp` / `include/LogPolicy.h` | Pure log-retention policy (`SimpleTraits_<timestamp>.log`) |
| `include/PCH.h` | Precompiled header: RE/Skyrim.h, SKSE, spdlog |
| `extern/SAL/SAL_API.h` | Vendored SAL consumer header (C types and function pointers only) |
| `data/SKSE/Plugins/SimpleTraits.json` | Shipped defaults, packaged verbatim |

`TraitRules`, `TraitSave`, `HandshakeRules` and `LogPolicy` must not include CommonLib; they are built
by the portable tests on any OS.

### Config

- `SimpleTraits.json` holds the shipped defaults. `SimpleTraits.user.json` holds only the
  keys the user changes, plus `"config_version": 1`. Both files live next to the DLL.
- The registry in `SettingsModel.cpp` defines every key, its type, inclusive bounds and
  built-in default. The built-in defaults, the shipped JSON and `TraitRules::TraitSettings`
  must agree; the settings test enforces this.
- Each value is validated on its own. A missing shipped value falls back to the built-in
  default silently; an invalid shipped value falls back with a warning; an invalid or
  unknown override is ignored with a warning. A user file with a non-integer or newer
  `config_version` is ignored entirely. Integers written as `6.0` are accepted and stored
  as integers.

| Key | Type | Range | Default |
|---|---|---|---|
| `debug.verbose` | bool | | false |
| `debug.max_log_files` | int | 0-1000 (0 keeps all) | 10 |
| `debug.allocation_page` | bool | | false (temporary debug page for spending points) |
| `points.starting_points` | int | 0-100 | 4 |
| `points.levels_per_point` | int | 1-100 | 3 |
| `per_point.stamina_percent` / `health_percent` / `magicka_percent` | number | 0-1 | 0.05 |
| `per_point.critical_chance` | number | 0-10 | 1 |
| `per_point.intelligence_xp_percent` | number | 0-0.5 (two decimals on the page) | 0.10 |
| `per_point.charisma_price_improvement` | number | 0-0.05 | 0.01 |

All of these are editable on the settings page.

### Rule details

- **CriticalChance units.** UESP's Skyrim Mod:Actor Value Indices lists `CritChance`
  (index 33) with "(%)", but that is not the effective crit chance. **In-game measurement contradicts 1 = 1%** (2026-09-24, Imperial Sword
  of Long-Embers, crit mult 1.0, no crit perks): CriticalChance 100 gave 3 crits in 20 power
  attacks (15%); CriticalChance 0 gave 0 in 20. The actor value matters but is not a direct
  percentage; the "Base Critical Hit Chance" mod grants crit chance through the
  *Calculate My Critical Hit Chance* perk entry point instead. A second run at
  CriticalChance 100 gave 5/47 normal attacks (10.6%) and 5/40 power attacks (12.5%):
  crits are not limited to power attacks, and CriticalChance 100 yields roughly 10-15%
  (pooled 13/107). At kDataLoaded the diagnostics list every game setting whose name
  contains "crit"; vanilla has `fWeaponConditionCriticalChanceMult = 0.1`. The engine
  formula documented for Fallout 3 (https://fallout.wiki/wiki/Critical_Hit_Chance_Formula)
  is `crit% = PerkModifiers(fWeaponConditionCriticalChanceMult * WeaponCondition *
  CriticalChance * WeaponCritMult)`; with an untempered weapon (health 1.0) that predicts
  10% at CriticalChance 100, matching the measurement. **Confirmed in game** (2026-09-24):
  with `setgs fWeaponConditionCriticalChanceMult 1` and CriticalChance 100, 83 of 83
  swings crit, then 27 of 27 after a phase with a lower setting (0 of 23). So in vanilla
  **10 CriticalChance = 1% crit chance** on an untempered weapon with crit mult 1.0;
  tempering (weapon health above 1.0) and the weapon's crit mult scale it. Decided
  (2026-09-24): Agility adds CriticalChance on the permanent modifier layer, the amount
  per 1% read from `fWeaponConditionCriticalChanceMult` at runtime.
- **Charisma.** From UESP Skyrim:Speech:
  `factor = fBarterMax - (fBarterMax - fBarterMin) * min(Speech, 100) / 100`,
  `buy = round(value * buyMod * factor)`, `sell = round(value * sellMod / factor)`,
  defaults fBarterMax 3.3 and fBarterMin 2.0. `ApplyCharisma` scales both settings by
  `m = 1 - points * improvement`, which scales the factor by exactly `m` at every Speech
  level (buy x m, sell x 1/m). Each value is floored at 1.0 (sell never exceeds buy); a
  value already below 1.0 is left unchanged. Always compute from the captured original
  values, never from the currently written ones.

## Deferred in-game checks (run before release)

Phase 2 was verified in game on 2026-09-24 (spend, save, reload after restart, reload in
session: no double bonus, base values untouched). Still to run at the end of development:

- Change `per_point.health` in the user JSON, restart, load: only ST's part of Health
  changes (`applied 5.00 -> 10.00`), everything else `unchanged`.
- Load a save made before Phase 2: no trait record, nothing applied, no errors.
- Load a save with an allocation after deleting its `.skse` cosave: documents the known
  double-apply limit.
- Charisma buy price (2026-09-24): with 4 points, moving 0.01 -> 0.05 per point (price
  factor x0.96 -> x0.80, written correctly) changed a hunting bow's buy price 151 -> 131;
  UESP's formula predicts ~126. Selling matched (16 -> 19). Re-check with the same
  merchant and item, closing and reopening the barter menu between settings, to see
  whether the buy formula differs from UESP's or the first trade differed.
- Agility crit rate end to end (critical_chance 10 with 1 point -> CriticalChance 100,
  confirmed via getav; the observed rate should be ~10%).
- Intelligence XP (Phase 6, needs SAL `6933938` or later): the ST log shows "XP multiplier
  registered"; with 10 points (or 1 point at 0.50) SAL's XP Log shows "x2.00 from other
  mods" and a kill or quest gives double the XP; spending a point or changing the setting
  takes effect on the next award; the trait menu shows "+10% XP per point". A save made with
  the old Intelligence keeps its skill points and loads without errors.

## Build

```powershell
cmake --preset windows-release-tests
cmake --build --preset windows-release-tests
ctest --preset windows-release-tests
```

Set `VCPKG_ROOT` before configuring. Clone with `--recurse-submodules` or run
`git submodule update --init --recursive` before the first plugin build. CommonLibSSE-NG is
pinned to `5decf47` (v9.0.0), the same commit SAL uses.

Critical build/runtime notes:

- Use `x64-windows-static-md`, so `spdlog` and `fmt` link statically and the plugin does
  not import `spdlog.dll` or `fmt.dll`. Do not use `x64-windows`.
- Keep `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"` before
  `project()` so the plugin matches the triplet's `/MD` runtime.
- The plugin targets AE 1.7.104 with the post-1.6.629 structure layout (patched into the
  generated plugin metadata; `tools/verify_plugin_metadata.py` checks it). Non-VR SE/AE
  runtimes at 1.6.629 or later with matching SKSE and Address Library are supported;
  earlier runtimes and VR are not.
- Call `SKSE::Init(a_skse, { .log = false })`; the default `InitInfo` replaces the
  timestamped logger.

Dependency-free tests on any supported development OS:

```sh
cmake --preset portable-tests
cmake --build --preset portable-tests
ctest --preset portable-tests
```

Portable tests use a non-`assert` `Check()` so they run in Release. The Windows-only
settings test uses `assert` with `NDEBUG` undefined at the top of the file.

`cmake --build --preset windows-package` writes a deterministic, mod-manager-ready ZIP
(`SKSE/Plugins/SimpleTraits.dll`, `SimpleTraits.json`, `Interface/ST_TraitMenu.swf`,
`Interface/Translations/SimpleTraits_ENGLISH.txt`) and its SHA-256 file into the ignored
`Deployed/` folder.

For local MO2 deployment, set `ST_DEPLOY_DIR` in an ignored `CMakeUserPresets.json`; the
post-build step copies the DLL, shipped JSON, SWF and translation there and never touches
`SimpleTraits.user.json`. Never put local absolute paths in tracked files.

Logs go to the standard SKSE log directory as `SimpleTraits_<timestamp>.log`, prefixed
`[ST]`.

## Json for testing

The shipped `data/SKSE/Plugins/SimpleTraits.json` must keep release defaults
(`tools/check_release_defaults.py` enforces this). Put test values in
`SKSE/Plugins/SimpleTraits.user.json` inside the deploy directory, for example:

```json
{
  "config_version": 1,
  "debug": { "verbose": true },
  "points": { "starting_points": 20, "levels_per_point": 1 }
}
```
