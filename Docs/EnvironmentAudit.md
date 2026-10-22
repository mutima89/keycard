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

## Windows PC (2026-10-22, via Remote Control, owner approved)
| Check | Result |
|---|---|
| OS | Windows 10.0.26300.9711 |
| Unreal Engine | **5.8.4** (CL 58902052, ++UE5+Release-5.8) at `C:\Program Files\Epic Games\UE_5.8`. The master prompt said 5.8.3; `EngineAssociation` 5.8 covers both |
| Compiler | VS 2022 Build Tools 17.14 (MSVC 14.44.35207, used by UBT); also VS 2019 Build Tools (14.29) and VS 2022 Community 17.13 without C++ tools |
| Windows SDK | 10.0.19041.0, 10.0.22621.0 (UBT used 22621) |
| Inno Setup | Per-user install at `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`, not on PATH. `Scripts\BuildInstaller.bat` already looks there first |
| CMake / git / gh | 3.27.7 / 2.53.0 / gh signed in as mutima89 (repo scope) |
| GPU | GeForce GTX 1650, driver 610.62, 4096 MiB (plus Intel UHD 630) |
| Disk | C: 70.4 GB free |
| Working folder | `C:\Downloads\Keycard`, fresh clone at 8c524cd |
| Core tests (MSVC, `Scripts\RunCoreTests.bat`) | `27 test cases, 0 failed, 0 failed checks`, exit 0 |
| Editor build (`KeycardEditor Win64 Development`) | `Result: Succeeded`, 18 actions, 160.58 s; KeycardSim and Keycard compiled and linked |

Not checked yet: the DX11/SM5 Config at runtime (needs the editor or a package to open; Phase 1).
