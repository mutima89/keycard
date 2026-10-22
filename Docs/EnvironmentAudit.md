# Environment audit (Phase 0)

## Cloud session (2026-10-22)
| Check | Result |
|---|---|
| OS | Linux x86_64 (cloud container), not Windows |
| C++ toolchain | GCC 13.3, Clang 18.1.3, CMake 3.28.3; used for the core |
| Python | 3.13 |
| Unreal Engine, MSVC, Inno Setup | Not available here (Windows PC only) |
| Riskbound | github.com/mutima89/riskbound cloned read-only (public), v1.1.0 |
| Keycard repo | Not created yet: the owner creates or approves it |

## Windows PC (to run through Remote Control, owner's go-ahead needed)
Riskbound's audit of the same PC (2026-10-20): Windows 11 Home, UE 5.8.3 at `C:\Program Files\Epic Games\UE_5.8`, VS 2022 Build Tools 17.14 (MSVC 14.44), Windows SDK 10.0.22621, git and gh signed in as mutima89, CMake 3.27.7, GTX 1650 4 GB. To confirm for Keycard: same versions, Inno Setup installed (Riskbound later used it), free disk, `C:\Downloads\Keycard` created, core tests on MSVC, editor build of the Keycard shell module with the DX11/SM5 config.
