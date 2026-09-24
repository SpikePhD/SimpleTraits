# Vendored SAL integration header

`SAL_API.h` is copied verbatim from Simple Alternate Levelling (SAL). Do not edit it here;
update it by copying a newer committed version from SAL and updating this record.

| Field | Value |
|---|---|
| Source file | `include/SAL_API.h` |
| SAL commit | `d53dc5b69311f6084253a6aef5ecc9a0ea3d0c32` (feat: integration API V3 with a skill point bonus, 2026-09-24) |
| SHA-256 | `18fc7e8083f33470f3eff0be9361bea51755b80ff2df579f2ac7f95e6105765c` |
| Interface | `SAL::SALInterfaceV3` (prefixes `SALInterfaceV2`, `SALInterfaceV1`), sender `SimpleAlternateLevelling`, message type `0x53414C00` |

The header has no dependencies beyond `<cstdint>` and uses only C types and function
pointers, so it is safe across separately built DLLs. Later SAL interface versions only
append members. Simple Traits accepts any `version >= 1` whose payload is at least
`sizeof(SALInterfaceV1)`, and uses the V2 pre-skill-menu step when `version >= 2` and the
payload is at least `sizeof(SALInterfaceV2)`, and the V3 skill point bonus (Intelligence)
when `version >= 3` and the payload is at least `sizeof(SALInterfaceV3)`. The requests are
`docs/SAL_API_V2.md` and `docs/SAL_API_V3.md`.
