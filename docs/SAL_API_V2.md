# SAL integration API V2: pre-skill-menu level-up step

Request from Simple Traits (ST) for Simple Alternate Levelling (SAL). Implement in the SAL
repository; ST re-vendors `include/SAL_API.h` from the SAL commit that ships it.

## Goal

On a level-up, let a companion plugin show its own screen **before** SAL's skill menu:

```text
vanilla LevelUp Menu opens -> SAL hides it (unchanged)
  -> [NEW] pre-skill-menu step: wantsStep(level)? wait for ContinueLevelUp
  -> SAL skill menu (unchanged; "no points" still skips it)
  -> existing V1 level-up step (post-skill-menu), unchanged
  -> vanilla LevelUp Menu opens once (attribute choice), then normal play
```

ST uses the pre-step for its trait menu, so the player allocates traits, then skills, then
the vanilla attribute.

## Header change (append-only)

```cpp
inline constexpr std::uint32_t kInterfaceVersion2 = 2;

struct SALInterfaceV2 {
    // Layout-compatible prefix. v1.version is 2 in a V2 broadcast.
    SALInterfaceV1 v1;

    // Pre-skill-menu step. When SAL has intercepted the vanilla LevelUp
    // Menu and is about to open its skill menu, it calls wantsStep(level)
    // with the player's current level. Returning true makes SAL wait; the
    // step owner must call v1.ContinueLevelUp on every exit path, after
    // which SAL opens its skill menu (or continues as it does today when
    // there are no skill points). Returning false continues immediately.
    bool (*RegisterPreSkillMenuStep)(bool (*wantsStep)(std::uint32_t level));
};
```

- SAL broadcasts one `SALInterfaceV2` (version field 2, `dataLen = sizeof(SALInterfaceV2)`)
  with the same sender name and message type. V1 consumers keep working: they check
  `version >= 1` and `dataLen >= sizeof(SALInterfaceV1)`.
- Update the header comments (handshake example, contract) to describe V2.

## Semantics

- **Registration:** one registrant; a null or second registration is rejected (returns
  false) and logged, like the other slots. Register during the kPostPostLoad listener or
  later.
- **When it runs:** on every level-up SAL intercepts, before `Open()` of the skill menu,
  regardless of how many skill points SAL grants. `level` is the player's current level.
- **Continuation:** `ContinueLevelUp` resumes whichever step is waiting. From the pre-step
  it proceeds to the skill menu; from the V1 step it opens the vanilla menu, as today.
  Idempotent; ignored when nothing waits. Callable from any thread; the work runs on the
  main thread.
- **Fail-safe:** the same rule as V1: if the game stays unpaused for 10 s while the
  pre-step waits, SAL continues on the owner's behalf (to the skill menu) and logs a warning.
- **Both steps registered:** pre-step -> skill menu -> V1 step -> vanilla. Each waits
  independently.
- **Threshold refresh:** `IsDeferringVanillaLevelUp()` stays true while the pre-step waits;
  the threshold is still refreshed only when the final vanilla LevelUp Menu closes.
- **Lifecycle:** load, revert and new game reset a pending pre-step wait with the rest of
  the SkillMenu state. Nothing is persisted.
- **Robustness:** a throwing `wantsStep` is caught and treated as false; a vanilla LevelUp
  Menu opened by something else while the pre-step waits ends the wait (as for V1).

## Tests and docs (SAL side)

- UIRules: pre-step hand-off begin/continue/fail-safe, and the full sequence with both
  steps registered, one of them, and neither.
- AGENTS.md skill allocation flow, README integration section, `SAL_API.h` comments.
- Log lines matching the V1 step (`waiting for integration pre-skill-menu step at level N`,
  `pre-skill-menu step finished (reason=...)`).

## ST side once V2 ships

- Re-vendor `SAL_API.h`; record the SAL commit and SHA-256 in `extern/SAL/README.md`.
- Handshake: accept V2 when `version >= 2` and `dataLen >= sizeof(SALInterfaceV2)`; with a
  V1-only SAL, fall back to the V1 (post-skill-menu) step.
- `wantsStep(level)` returns true when the player has unspent trait points.
