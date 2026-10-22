# Progress

## Status: Phase 1 built and packaged on the PC (self-test play, resume and tour pass); voices and UE automation tests missing
Last update: 2026-10-22

- 2026-10-22 Phase 1 PC build, PC session (Remote Control, UE 5.8.4, MSVC). Nothing changed in C:\Downloads\Riskbound.
  - Branch `claude/project-thread-mjihsh` at b53fa7b.
  - `FetchAssets.py --resolve`: all 22 Poly Haven ids resolved, no MISSING slot (`white_bathroom_tiles` and `bathroom_tiles` 404, fell back to `square_tiles_02`); Inter fonts installed; AssetManifest.json, AssetCredits.md and KcAssetCredits.inl written. `GenerateTextures.py` and `BuildAudio.py` exit 0. `FetchCharacters.py` done with one gap: `missing anim f_cell_phone_listen_01`.
  - `BuildVoices.py` FAILED: `FileNotFoundError: Data\VoiceCast.json` - the voice cast file was never written. No voice clips in this build.
  - Logo check: contact sheet of the 22 `*_diff_1k.jpg` maps, no brand text or logo (one white mark on sofa_02 checked at full size: UV islands).
  - `KeycardEditor Win64 Development`: first compile of the UE layer. Fixed C4458 (`Role` local in KcHUD and `bHidden` parameter of `AKcHotel::AddInstance` hid AActor members; renamed `CallerRole`, `bHide`), C2397 (double Yaw narrowed to float in `AKcHotel::SpotFor`), LNK2019 `GGameThreadTime`/`GRenderThreadTime` (added private `RenderCore` dependency, as Riskbound does). Third pass: `Result: Succeeded`, no warnings.
  - Core tests (MSVC, `Scripts\RunCoreTests.bat`): `42 test cases, 0 failed, 0 failed checks`.
  - Imports, headless: `KcImport: done: 8 meshes, 7 materials, 0 errors`; Saved/KcImportReport.json `errors: []`; `KcAudio: imported 28 sounds, 0 errors`. ImportCharacters: 32 avatars, 49 anims, exit 1 from one editor error on an upstream stray file `m003_head_normal - wr.png` (spaces in the name; the real `m003_head_normal.png` imported). CreateMap: exit 0, `Content/Maps/L_Harbourfront.umap` written.
  - `RunUnrealTests.bat`: `No automation tests matched 'Keycard.'`, exit 255 - the Keycard module has no UE automation tests yet.
  - `BuildWindows.bat Development`: `BUILD SUCCESSFUL`, `AutomationTool exiting with ExitCode=0 (Success)`.
  - Packaged `Keycard.exe -KcSelfTest=play`: `PASS` (M001, M002, M003 each score 100; ceremony Senior LP Officer; level 2; 4 cards; 28 sounds). `-KcSelfTest=resume`: `PASS` (M001-M003 passed, level Senior LP Officer, ceremony not pending, 3 records). `-KcSelfTest=tour`: `PASS`, 17 places at 1280x720, `average_frame_ms=17.09 average_fps=58.5`; lowest LP office 41.8 fps (first place, warm-up); Floor 14 corridor (east) render 30.14 ms.
  - Tour screenshots looked at: lobby ceiling blown out to white, a potted plant in the lobby renders as spheres, LP office ceiling pure black and room flat, wardrobe veneer in 1412 stretched. People at the front desk render.
  - Not done: voice cast and clips, UE automation tests, 1920x1080 High performance check, hands-on playtest.

- 2026-10-22 Phase 1 UE layer, cloud session (not compiled yet: no UE in the container).
  - Hotel (`AKcHotel`): one building, B1 back of house (LP office, CCTV room, dock), ground floor (lobby, front desk, lounge, bar, street doors), floor 14 (corridor, rooms incl. 1412), stairs A and B, two lifts; floors 1-13 listed in the lift but not fitted. People and case points placed per mission step.
  - HUD and screens rewritten for hotel casework: talk to people, case points, terminal, report, debrief, inbox (needs-reply first), Field Manual, career, lift ride with fade, radio case calls, promotion ceremony in the lobby.
  - Self-test modes play (M001-M003 through the HUD, checks the first promotion and ceremony), resume, tour, ui.
  - Asset scripts: `GenerateTextures.py` (18 original textures, checked by eye on a contact sheet; fixed the exit arrow over the T, the struck-through "YOU ARE HERE" and the whiteboard tag over the first note), `ImportAssets.py` (M_Kc masters, every MI the hotel uses, night HDRI, 8 models; flat-tint fallbacks when a texture is missing), `FetchAssets.py` (hotel list with fallback ids; Poly Haven API is blocked from the container, so it runs on the PC), `BuildAudio.py` (+5 ambiences: fluorescent, lobby at night, harbour, stair echo, music through a door; synthesised and checked for length, peak and NaN here).
  - Docs: ArtDirection.md (new), AssetCredits.md (audio, original artwork, generated block).
  - Core tests (GCC, ASan + UBSan): `42 test cases, 0 failed, 0 failed checks`.

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
Write Data/VoiceCast.json and run BuildVoices; add UE automation tests; fix the lighting and plant issues seen in the tour; check 1920x1080 High; hands-on playtest.
