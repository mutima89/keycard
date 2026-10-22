# Progress

## Status: Phase 0 (cloud part done and pushed to github.com/mutima89/keycard; PC audit waits for the owner's go-ahead)
Last update: 2026-10-22

- 2026-10-22 Phase 0, cloud session.
  - Read Riskbound v1.1.0 (architecture, core, scripts, docs). Reuse plan and what was copied: Docs/ReuseFromRiskbound.md.
  - Skeleton: `Keycard.uproject` (UE 5.8), `KeycardSim` and `Keycard` modules (UE side is a shell until Phase 1), targets, Config (DX11/SM5, no virtual shadow maps, no Nanite, Lumen reflections to roughness 0.35, TSR), scripts, installer script with a new AppId, MIT licence.
  - Core: `KcRng`, `KcJson`, `KcVoice`, `KcSave` (copied and renamed), `KcCareer` (LP ladder, ten competencies, four backgrounds, crisis gate at levels 6-9), `KcLpMath` (every LP formula), `KcGameState`.
  - Docs: CLAUDE.md, README.md, Docs/GameDesign.md, LpMethodology.md, CareerModel.md, FictionalNames.md (two names replaced: Ostrander -> Tessmore Guard Services, Halbrook -> Tarrowby Mutual), Architecture.md, SaveFormat.md, AssetCredits.md, EnvironmentAudit.md.
  - Tests (this session): `Scripts/run_core_tests.sh` with GCC 13.3, ASan + UBSan: `27 test cases, 0 failed, 0 failed checks`, no compiler warnings. Clang 18.1.3 Release: `27 test cases, 0 failed, 0 failed checks`, no warnings.
  - Not verified: anything on Windows (MSVC core build, UE editor build of the module shell, the Config cvars).
  - Pushed to github.com/mutima89/keycard `main` (repo created by the owner); core tests re-run after the rebase onto the owner's initial commit: `27 test cases, 0 failed, 0 failed checks`.

### Next action
Owner: go-ahead for the PC audit through Remote Control. Then Phase 1 on the owner's word.
