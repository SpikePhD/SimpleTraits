# Vendored SAL integration header

`SAL_API.h` is copied verbatim from Simple Alternate Levelling (SAL). Do not edit it here;
update it by copying a newer committed version from SAL and updating this record.

| Field | Value |
|---|---|
| Source file | `include/SAL_API.h` |
| SAL commit | `6933938e30eb9e125daf2d55a731c06231a3c179` (feat: integration API V4 with an XP reward multiplier, 2026-09-26) |
| SHA-256 | `0170e129e2bff349984b91822d126f8a5c1a5302c1c951254f61c7f6fc9001f2` |
| Interface | `SAL::SALInterfaceV4` (prefixes `SALInterfaceV3`, `SALInterfaceV2`, `SALInterfaceV1`), sender `SimpleAlternateLevelling`, message type `0x53414C00` |

The header has no dependencies beyond `<cstdint>` and uses only C types and function
pointers, so it is safe across separately built DLLs. Later SAL interface versions only
append members. Simple Traits accepts any `version >= 1` whose payload is at least
`sizeof(SALInterfaceV1)`, and uses the V2 pre-skill-menu step when `version >= 2` and the
payload is at least `sizeof(SALInterfaceV2)`, and the V4 XP multiplier (Intelligence) when
`version >= 4` and the payload is at least `sizeof(SALInterfaceV4)`. ST no longer uses the
V3 skill point bonus. The requests are `docs/SAL_API_V2.md`, `docs/SAL_API_V3.md` and
`docs/SAL_API_V4.md`.
