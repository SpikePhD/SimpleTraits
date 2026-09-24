# Vendored SAL integration header

`SAL_API.h` is copied verbatim from Simple Alternate Levelling (SAL). Do not edit it here;
update it by copying a newer committed version from SAL and updating this record.

| Field | Value |
|---|---|
| Source file | `include/SAL_API.h` |
| SAL commit | `817e418e87d71d304c6e9963b91525a36958863c` (feat: integration API V2 with a pre-skill-menu level-up step, 2026-09-24) |
| SHA-256 | `37584c71c321b29095a2d66aa9a66cac938625a4442b6fa3a0c62c16302e11dc` |
| Interface | `SAL::SALInterfaceV2` (prefix `SALInterfaceV1`), sender `SimpleAlternateLevelling`, message type `0x53414C00` |

The header has no dependencies beyond `<cstdint>` and uses only C types and function
pointers, so it is safe across separately built DLLs. Later SAL interface versions only
append members. Simple Traits accepts any `version >= 1` whose payload is at least
`sizeof(SALInterfaceV1)`, and uses the V2 pre-skill-menu step when `version >= 2` and the
payload is at least `sizeof(SALInterfaceV2)`. The API V2 request is `docs/SAL_API_V2.md`.
