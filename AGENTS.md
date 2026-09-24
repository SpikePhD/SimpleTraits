# Simple Traits - AGENTS.md

`AGENTS.md` is the single source of truth for this project. There is intentionally no `CLAUDE.md`.

## What this mod does

Simple Traits (ST) is an SKSE plugin that adds six player traits. The player spends trait
points on them; each point gives one bonus:

| Trait | Bonus per point | Setting (`per_point.*`) | Default |
|---|---|---|---|
| Strength | +Stamina | `stamina` | 5 |
| Resilience | +Health | `health` | 5 |
| Agility | +Critical hit chance (mechanism to decide; see Rule details) | `critical_chance` | 1 (= +1%) |
| Intelligence | Less XP needed per level, via SAL's threshold multiplier | `intelligence_threshold_reduction` | 0.02 per point |
| Wisdom | +Magicka | `magicka` | 5 |
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
- Phase 2 (current): cosave, Strength/Resilience/Wisdom bonuses on the permanent modifier
  layer, and a temporary SKSE Menu Framework debug page for spending points. Agility,
  Intelligence and Charisma can be allocated but have no effect yet. No SAL callbacks are
  registered yet.

## Architecture

```text
SKSEPluginLoad()
├── Config::Load()                - reads SimpleTraits.json + SimpleTraits.user.json (no logging)
├── InitializeLog()               - timestamped spdlog in the standard SKSE log directory,
│                                   retention from debug.max_log_files
├── Config::LogReport()           - logs config errors/warnings and effective values
├── SKSE::Init(.log = false)
├── TraitState::RegisterSerialization() - cosave 'SMTR' / record 'TRTS' v1
├── MessagingInterface (SKSE)
│   ├── kPostPostLoad             - logs handshake state (SAL broadcasts from its own handler,
│   │                               which may run before or after ours)
│   ├── kDataLoaded               - SALBridge::Finalize(): still pending -> Missing;
│   │                               DiagnosticSinks::Register(); DebugPage::Register()
│   ├── kPostLoadGame             - DiagnosticSinks::Reset(); TraitState::Reconcile()
│   └── kNewGame                  - DiagnosticSinks::Reset(); TraitState::OnNewGame()
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

### Trait state and bonuses

`TraitState` owns the six allocations and the amount of each actor-value bonus ST has
applied (`TraitRules::AppliedBonuses`). Both are saved in the cosave; unspent points are
never stored.

- **Reconcile** (`kPostLoadGame`, after each spend): `TraitRules::PlanReconcile` computes
  `target = points * per_point` for Stamina (Strength), Health (Resilience) and Magicka
  (Wisdom), applies `target - applied` with
  `ModActorValue(ACTOR_VALUE_MODIFIER::kPermanent, ...)` and records `target` as applied.
  Base values are never written. Running it again changes nothing; a lowered per-point
  setting removes only ST's own excess. Each change is logged with base/permanent/current
  before and after.
- The engine stores the permanent modifier in the main save, so after a load the bonus is
  already present and the reconcile delta is 0 unless settings changed.
- **Spend** (`SpendPoint`, main thread): +1 on a trait when `ValidateAllocation` allows it,
  then reconcile. Refused when no game is loaded.
- New game and revert clear the state without touching actor values.

Known limits: if the cosave is lost while the main save keeps ST's modifiers, ST sees
"applied 0" and applies the bonus again. Uninstalling ST leaves its modifiers in the save
(a cleanup option can come later).

### Cosave

- Unique ID `SMTR`, record `TRTS`, version 1 (`TraitSave`, pure codec, portable tests).
- Payload, little-endian: `uint32 allocation[6]`, `uint32 count` (max 8), then `count` x
  `{ uint32 RE::ActorValue id, float32 applied }`. v1 writes Stamina, Health, Magicka.
  Entries are keyed by actor value so later bonuses (CriticalChance) extend the whitelist
  without a new layout.
- Rejected: other versions, wrong lengths, allocations above `kMaxPointsPerTrait`, ids
  outside the whitelist, duplicate ids, non-finite amounts. The first valid record wins; a
  missing or rejected record means no allocations and nothing applied.

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
| `src/TraitRules.cpp` / `include/TraitRules.h` | Pure rules: points, allocation validation, bonuses, reconcile delta, Intelligence multiplier, barter adjustment |
| `src/TraitSave.cpp` / `include/TraitSave.h` | Pure cosave codec: encode, decode with validation, first-valid adoption |
| `src/TraitState.cpp` / `include/TraitState.h` | Allocations and applied bonuses, cosave callbacks, reconcile, spend, snapshots |
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
| `per_point.stamina` / `health` / `magicka` | number | 0-100 | 5 |
| `per_point.critical_chance` | number | 0-10 | 1 |
| `per_point.intelligence_threshold_reduction` | number | 0-0.5 | 0.02 |
| `per_point.charisma_price_improvement` | number | 0-0.05 | 0.01 |

No settings page yet.

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
  tempering (weapon health above 1.0) and the weapon's crit mult scale it. For Agility,
  +1% per point means +10 CriticalChance per point (weapon-dependent), or a perk using
  the Calculate My Critical Hit Chance entry point for an exact percentage. Decide before
  implementing Agility; the shipped `per_point.critical_chance` comment and bounds follow
  that decision.
- **Intelligence.** `multiplier = clamp(1 - points * reduction, 0.5, 1)`. ST clamps to 0.5
  itself even though SAL also clamps with `integration.threshold_multiplier_floor`.
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
(`SKSE/Plugins/SimpleTraits.dll` and `SimpleTraits.json`) and its SHA-256 file into the
ignored `Deployed/` folder.

For local MO2 deployment, set `ST_DEPLOY_DIR` in an ignored `CMakeUserPresets.json`; the
post-build step copies the DLL and shipped JSON there and never touches
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
