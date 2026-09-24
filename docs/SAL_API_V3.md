# SAL integration API V3: skill point bonus

Request from Simple Traits (ST) for Simple Alternate Levelling (SAL). Implement in the SAL
repository; ST re-vendors `include/SAL_API.h` from the SAL commit that ships it.

## Goal

Let a companion plugin add whole skill points to a level-up. ST uses it for Intelligence:
each Intelligence point is worth a configurable fraction of a skill point per level,
retroactively. ST keeps the fractions and the history in its own cosave and only ever
hands SAL whole points, so SAL's cosave does not change.

Lowering SAL's own points per level (for example to 7) stays SAL's
`skill_allocation.points_per_level` setting; no API is needed for that.

## Header change (append-only)

```cpp
inline constexpr std::uint32_t kInterfaceVersion3 = 3;

struct SALInterfaceV3 {
    // Layout-compatible prefix. v2.v1.version is 3 in a V3 broadcast.
    SALInterfaceV2 v2;

    // Skill point bonus. SAL calls bonus(level) exactly once per level-up it
    // intercepts, after the pre-skill-menu step has finished (or right away
    // when there is none) and before it computes the points for its skill
    // menu. `level` is the player's current level. The return value is added
    // to that level-up's grant: total = pending + points_per_level + bonus.
    bool (*RegisterSkillPointBonus)(std::int32_t (*bonus)(std::uint32_t level));
};
```

- SAL broadcasts one `SALInterfaceV3` (version field 3, `dataLen = sizeof(SALInterfaceV3)`).
  V1 and V2 consumers keep working unchanged.
- Update the header comments and the level-up sequence description.

## Semantics

- **Registration:** one registrant; null or second registration rejected (returns false)
  and logged, like the other slots. Register during the kPostPostLoad listener or later.
- **Exactly once per level-up:** the provider is called once for each intercepted level-up,
  never for re-opened or stray menus, and not again when the skill menu is re-shown after a
  failure. Callers rely on this to track what they have granted.
- **When:** after the V2 pre-skill-menu step, so a companion's own level-up screen (ST's
  trait menu) is already committed; before `CheckedPointTotal`, so a level with a bonus
  but zero base points still opens the skill menu.
- **Values:** negative or throwing providers count as 0; values above 1000 are clamped to
  1000 and logged. The sum goes through the existing overflow check.
- **Failure paths:** if the skill menu cannot open or a commit is rejected, the bonus is
  part of the preserved pending points, exactly like the base grant.
- **Display (optional):** the skill menu may show the bonus separately, for example
  "+2 from traits", via a translation key.
- **Lifecycle:** nothing persisted by SAL; load, revert and new game need no extra state.

## Tests and docs (SAL side)

- UIRules: total with and without a bonus, negative and oversized bonus, overflow, bonus
  with zero base points, provider called exactly once per level-up in the flow sequencer
  (including the pre-step and the no-points path).
- AGENTS.md skill allocation flow, README integration section, `SAL_API.h` comments.
- Log line per level-up: `skill point bonus from integration: N (level L)`.

## ST side once V3 ships

- Re-vendor `SAL_API.h`; record the SAL commit and SHA-256 in `extern/SAL/README.md`.
- Handshake: use the bonus hook when `version >= 3` and `dataLen >= sizeof(SALInterfaceV3)`;
  without it Intelligence has no effect (logged).
- Provider: `owed = floor(intelligencePoints * rate * (level - 1)) - granted`, clamped at
  0; `granted` (cosave) increases by the returned amount. `rate` is the setting
  `per_point.intelligence_skill_points` (default 0.25).
