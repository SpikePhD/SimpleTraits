# SAL integration API V4: XP reward multiplier

Request from Simple Traits (ST) for Simple Alternate Levelling (SAL), implemented in SAL
`6933938`. ST re-vendors `include/SAL_API.h` from that commit.

## Goal

Replace Intelligence's skill point bonus (V3) with an XP multiplier. Skill points are a
one-time grant, so a temporary Intelligence bonus (future potions, gear, spells) would have
nothing clean to take back when it ends. A multiplier read on every award only affects the
XP earned while it is in force. The XP needed per level (V1 threshold multiplier) is not
used: lowering it mid-level would make a potion cause an instant level-up.

## Header change (append-only)

```cpp
inline constexpr std::uint32_t kInterfaceVersion4 = 4;

// Stable, append-only; treat an unknown category like any other.
inline constexpr std::uint32_t kXPSourceQuest = 0;
inline constexpr std::uint32_t kXPSourceKill = 1;
inline constexpr std::uint32_t kXPSourceExploration = 2;
inline constexpr std::uint32_t kXPSourceLock = 3;
inline constexpr std::uint32_t kXPSourceBook = 4;
inline constexpr std::uint32_t kXPSourcePickpocket = 5;

struct SALInterfaceV4 {
    SALInterfaceV3 v3;  // v3.v2.v1.version is 4 in a V4 broadcast
    bool (*RegisterXPMultiplier)(float (*provider)(std::uint32_t sourceCategory));
};
```

## Semantics (as implemented)

- One registrant; null or second registration rejected (returns false) and logged.
- SAL calls `provider(sourceCategory)` on the main thread once per award in
  `XPManager::AwardXP`, with no caching: `amount = base * level scaling * multiplier`.
  Every XP source goes through `AwardXP`.
- Not rounded: the engine's XP bucket is a float, so fractional bonuses accumulate
  (5 x 1.1 = 5.5). Only the HUD notification rounds.
- Non-finite or <= 0 counts as 1.0; values in (0, 1) are allowed (future debuffs); above
  100 is clamped to 100 (garbage guard). A throwing provider counts as 1.0. Invalid or
  clamped values warn once per session.
- The notification and XP Log show the boosted amount; the XP Log adds "x1.50 from other
  mods" (`$SAL_LOG_BONUS`).
- The threshold, the V1 threshold multiplier, reward scaling and SAL's cosave are unchanged.
- The V3 skill point bonus stays in the API; ST no longer registers it.

## ST side

- Handshake: `HandshakeRules::HasXPMultiplier` (version >= 4, payload at least
  `sizeof(SALInterfaceV4)`, non-null `RegisterXPMultiplier`); without it Intelligence has no
  effect and the trait menu shows "No effect yet".
- Provider: `TraitState::XPMultiplier` returns `1 + points * intelligence_xp_percent`
  (default 0.10, 0-0.5), published to an atomic whenever the allocation or settings change,
  so SAL's per-award call never takes ST's mutex. The source category is ignored for now.
