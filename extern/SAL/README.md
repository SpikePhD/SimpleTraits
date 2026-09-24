# Vendored SAL integration header

`SAL_API.h` is copied verbatim from Simple Alternate Levelling (SAL). Do not edit it here;
update it by copying a newer committed version from SAL and updating this record.

| Field | Value |
|---|---|
| Source file | `include/SAL_API.h` |
| SAL commit | `64cb117f34d42c030689c2b189f17830a4d4ea1b` (feat: public integration API for companion plugins, 2026-09-24) |
| SHA-256 | `88b88c58c493b6a6671786d2e5c8906cc443b7e336bb017d33b294d5e9c78bc4` |
| Interface | `SAL::SALInterfaceV1`, sender `SimpleAlternateLevelling`, message type `0x53414C00` |

The header has no dependencies beyond `<cstdint>` and uses only C types and function
pointers, so it is safe across separately built DLLs. Later SAL interface versions only
append members, so Simple Traits accepts any `version >= 1` whose payload is at least
`sizeof(SALInterfaceV1)`.
