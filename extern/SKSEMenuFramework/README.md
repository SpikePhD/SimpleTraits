# Vendored SKSE Menu Framework header

`SKSEMenuFramework.h` (MIT, see `LICENSE`) is copied verbatim from Simple Alternate
Levelling, which vendors it from QTR-Modding/SKSE-Menu-Framework-3-Example @ `aa8effa`.
Do not edit it here.

| Field | Value |
|---|---|
| Copied from | SAL `extern/SKSEMenuFramework/SKSEMenuFramework.h` at SAL commit `751ebcda2ec6481a5689be4d370c1990940f2046` |
| Upstream | QTR-Modding/SKSE-Menu-Framework-3-Example @ `aa8effa` |
| SHA-256 | `8f5b8be01b60ee2c258a2b3c2c97ccc4118cf7ab9fe17775a8375bf69378683e` |

The header resolves every function from `SKSEMenuFramework.dll` at runtime, so there is no
link dependency: when the framework is not installed, `SKSEMenuFramework::IsInstalled()`
returns false and Simple Traits skips its pages.
