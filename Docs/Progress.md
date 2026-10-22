# Progress

## Status: Phase 1 in progress (core and content done; UE layer next)
Last update: 2026-10-22

- 2026-10-22 Phase 1 core, cloud session. The owner said to carry on through the phases without stopping.
  - Core: `KcProperty`, `KcPeople`, `KcMission` (on-foot casework over a multi-floor hotel: 22 locations from B2 to floor 14, travel time between floors, case clock, expiring steps, chain of custody, hidden 7-dimension rubric, critical breaches, what-if replays, latent consequences that name their cause), `KcMissionData`, `KcWorld`/`KcWorldData`, `KcLearning`. `KcLpMath::EvaluateFormula` (13 named formulas, every input required) and `FormatMoney`.
  - Save v2 with the v1 migration (Docs/SaveFormat.md).
  - Content: M001 Night Shift: Room 1412, M002 Locked Out, M003 The 2 a.m. Noise Complaint (sources in Scripts/content), 23-person cast, Field Manual (4 cards), story, events and fallout, glossary, endings. Cast names checked (Docs/FictionalNames.md).
  - Tests: `Scripts/run_core_tests.sh` (GCC, ASan + UBSan): `42 test cases, 0 failed, 0 failed checks`. Clang Release: `42 test cases, 0 failed, 0 failed checks`, no warnings. New tests cover content validation, best play scoring 100 on every mission, M001-M003 promoting to level 2 with the ceremony pending, critical breaches, expiry, travel, what-if, authority caps, save v2 round trip mid-case and the v1 migration.
  - Not yet done: the UE layer, PC build and package.

- 2026-10-22 Phase 0 PC audit (Remote Control, Docs/EnvironmentAudit.md): UE 5.8.4, MSVC 14.44, SDK 10.0.22621, GTX 1650 4 GB, 70 GB free. Core tests on MSVC: `27 test cases, 0 failed, 0 failed checks`. `KeycardEditor Win64 Development`: `Result: Succeeded` (160.58 s). Nothing changed in C:\Downloads\Riskbound.
- 2026-10-22 Phase 0, cloud session.
  - Read Riskbound v1.1.0 (architecture, core, scripts, docs). Reuse plan and what was copied: Docs/ReuseFromRiskbound.md.
  - Skeleton: `Keycard.uproject` (UE 5.8), `KeycardSim` and `Keycard` modules (UE side is a shell until Phase 1), targets, Config (DX11/SM5, no virtual shadow maps, no Nanite, Lumen reflections to roughness 0.35, TSR), scripts, installer script with a new AppId, MIT licence.
  - Core: `KcRng`, `KcJson`, `KcVoice`, `KcSave` (copied and renamed), `KcCareer` (LP ladder, ten competencies, four backgrounds, crisis gate at levels 6-9), `KcLpMath` (every LP formula), `KcGameState`.
  - Docs: CLAUDE.md, README.md, Docs/GameDesign.md, LpMethodology.md, CareerModel.md, FictionalNames.md (two names replaced: Ostrander -> Tessmore Guard Services, Halbrook -> Tarrowby Mutual), Architecture.md, SaveFormat.md, AssetCredits.md, EnvironmentAudit.md.
  - Tests (this session): `Scripts/run_core_tests.sh` with GCC 13.3, ASan + UBSan: `27 test cases, 0 failed, 0 failed checks`, no compiler warnings. Clang 18.1.3 Release: `27 test cases, 0 failed, 0 failed checks`, no warnings.
  - Not verified: the DX11/SM5 Config cvars at runtime (Phase 1).
  - Pushed to github.com/mutima89/keycard `main` (repo created by the owner); core tests re-run after the rebase onto the owner's initial commit: `27 test cases, 0 failed, 0 failed checks`.

### Next action
Phase 1 UE layer: port the Riskbound UE code, build the multi-floor hotel (B1 LP office and CCTV room, ground floor, floor 14) with lifts and stairs, cast, screens and the promotion ceremony; then build, test and package on the PC.
