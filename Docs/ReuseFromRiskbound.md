# Reuse from Riskbound

Source: github.com/mutima89/riskbound at its default branch, 2026-10-22 (v1.1.0, MIT, same owner). Rename rule: `Riskbound` -> `Keycard`, `RiskboundSim` -> `KeycardSim`, `rb::` -> `kc::`, `Rb*` -> `Kc*`, `RB_API` -> `KC_API`, `-RbSelfTest` -> `-KcSelfTest`, `.rbsave` -> `.kcsave`.

## Copied in Phase 0
| Keycard | From | Change |
|---|---|---|
| `KcApi.h`, `KcRng.h`, `KcJson.h/.cpp`, `KcVoice.h/.cpp` | `RbApi`, `RbRng`, `RbJson`, `RbVoice` | Renamed only |
| `KcSave.h/.cpp` | `RbSave` | Same file format, safe write, backup rotation, `.corrupt` handling, checksum; game sections rewritten for Keycard; version reset to 1 with a migration hook |
| `KcGameState` settings | `PlayerSettings`, `ClampSettings` | Same fields and limits |
| `KcCareer` | `RbCareer` | Same gate logic; LP ladder, competencies, backgrounds, crisis gate, ceremony stage |
| `Tests/Core/KcTest.h`, `TestMain.cpp`, `TestVoice.cpp` | test harness | Renamed only |
| `CMakeLists.txt`, `Scripts/run_core_tests.sh`, `RunCoreTests.bat`, `RunUnrealTests.bat`, `BuildWindows.bat` | build scripts | Renamed |
| `Scripts/BuildVoices.py`, `ImportAudio.py` | voice pipeline | Renamed; Kokoro model SHA-256 check kept |
| `Scripts/FetchCharacters.py`, `ImportCharacters.py` | Rocketbox cast pipeline | Renamed; avatar list still Riskbound's office cast (Phase 1 picks hotel staff and guests) |
| `Scripts/MakePortableZip.ps1`, `BuildInstaller.bat`, `Installer/Keycard.iss`, `RunWalkthrough.ps1` | release pipeline | Renamed; new installer AppId |
| `.claude/skills/unreal-project-audit` | skill | Renamed |

## Not copied yet (phase that needs it)
- Phase 1: UE module code (`RbCharacter`, interaction component and interactables, `RbPerson` faces/lip-sync/gestures/walkers, `RbCast`, HUD and Slate screens, save and game subsystems, self-test, walkthrough driver, audio subsystem, credits), `RbMissionData` + `RbJsonReader` (strict content readers), `FetchAssets.py`/`ImportAssets.py`/`GenerateTextures.py`, `CreateMap`.
- Phase 2: `RbCampaign` (availability, hidden-rubric scoring, consequences), `RbWorld` (relationships, inbox, stress), `RbLearning` (glossary, field manual), achievements.
- Phase 3+: `RbGrowth` (endings, modes, leaderboard), `RbAiPrompt` (opt-in AI colleagues).
- Not reused: risk-specific modules (`RbRiskModel`, `RbQuant`, `RbKri`, `RbActions`, `RbReport`, `RbMission001`).

## Known Riskbound bugs to avoid
- Rocketbox bones import as `Bip01-Pelvis`, not `Bip01 Pelvis`.
- Music and ambience volume must not be squared (fade level times component volume).
- Long text must fit fixed-width panels (walkthrough checks text fit).
- Reduced motion must cut to the ceremony camera, never skip the ceremony.
- DX12/SM6 crashed the NVIDIA driver on the GTX 1650; Keycard config is DX11/SM5 (`Config/DefaultEngine.ini`).
- MetaHuman Creator crashes on the 4 GB GPU; not used.
