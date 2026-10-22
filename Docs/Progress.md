# Progress

## Status: Phase 3 content written (levels 4-7: M012-M026, crises C001-C002, floors 1 and 4); PC round 4 build pending
Last update: 2026-10-22

- 2026-10-22 Phase 3 (levels 4-7), cloud session (UE parts after commit 6cfc6d1 not compiled here; PC round 3 compiles part of it).
  - Missions M012-M026 (sources in Scripts/content): level 4 fire alarm, sprinkler impairment, slip and fall, hot work; level 5 workplace complaint, banquet bar shrink, regional LP audit, contract or in-house staffing; level 6 Code Beacon (missing child), Do Not Disturb (guest death), the fire marshal's inspection; level 7 Two Hotels, One Standard (the airport hotel), Former Employee Access Not Removed, Chargeback Fraud Ring, Supervisor Protecting a Friend. Crises C001 Power Outage at Full Occupancy and C002 Bomb Threat Call. Each teaches its own Field Manual card (29 in all) and scores 100 with best play.
  - Consequences that come back: every trap from level 4 on schedules a seeded latent event (claims, grievances, fire authority findings, insurer letters, press) that names the earlier decision; level 6-7 traps also set flags read by later context notes and events. Context notes in M022, C001, C002 and M024 react to earlier choices.
  - Story chapters 2 and 3: clues `airport_clones`, `former_admin` (the payoff: a former systems administrator's shared login and vendor access registered E-77) and `blank_stock` (his arrest, or a cleared flat if you warned him).
  - Inbox: 16 new events for levels 4-7 and fallout for 12 more topics. Cast: 11 new people with their own voices (Docs/FictionalNames.md).
  - Hotel: floor 1 (foyer, Harbour Ballroom with 16 rounds, stage and bar, banquet office and bar store, cameras 1-01 to 1-03), floor 4 (executive offices, HR, finance, GM office, boardroom, camera 4-01), the kitchen cook line and permit board, the floor 14 sprinkler valve, Stair A's floor 2 landing as a place. Scenes: fire panel status for M012/M013/M015/C001, the valve tag, the wet floor and cones, the hot-work ladder and blankets, Theo on the Stair A landing, 1406 open with the attendant's cart then sealed, chairs on the landing, the holdall under the cake table and the cordon tape, and a building-wide drop to emergency lighting during C001. `MissingWorkPoints` makes the tour fail when any mission step has nowhere to be done.
  - Core tests (GCC, ASan + UBSan): `44 test cases, 0 failed, 0 failed checks`. `Campaign_BestPlayToLevelEight` plays M001-M026, C001 and C002 at 100 and reaches level 8 with every card, clue and certain event.
  - Known gaps: the airport hotel is not walkable (its manager comes to the Harbourfront; it is a property in the data only); Theo and the C002 holdall are built from primitives; new avatars Female_Adult_13-18, Male_Adult_12-14, Business_Female_05 and Security_Male_02 are still to be confirmed on the PC.

- 2026-10-22 Phase 2 (levels 1-3), cloud session (UE parts not compiled yet).
  - Missions M004-M011 (sources in Scripts/content): Last Orders, Code Blue at the Bar, The Bag by the Sofa, Footage Before It Overwrites (level 2); The Missing Master, Propped Open, The Wrong Ticket, Encoder E-77 (level 3). Each teaches a Field Manual card (8 new, 12 in all), uses the strict validator, and scores 100 with best play.
  - Cast: six new people (Docs/FictionalNames.md). Story: clues `trolley_master` and `rogue_encoder` (chapter 1). Inbox: 7 events for levels 2-3 (two need a reply), fallout for medical, suspicious_item, cctv, keys, perimeter and luggage. Mission CCTV clips now name the hotel's own cameras (B1-05, B1-06, L-01, L-03, 14-02).
  - Core: content test scores each case at its own level (authority); the "two places" rule now counts every action's location. New test `Campaign_BestPlayThroughLevelThreeReachesLevelFour` (all eleven cases at 100, level 4, the new cards and clues, the certain inbox events arrive).
  - Hotel: B1 housekeeping and laundry (washers, linen, trolleys, master key cabinet with the empty hook, camera B1-03), loading dock (receiving office, two shutters, the alarmed dock door that opens onto a covered apron, liquor cage, pallets), luggage room behind the bell stand (racks, the gap where 4471 stood, release rules), lobby AED, the holdall (shown only in M006), the front office terminal, door 1409's reader, posts for the new people, arrivals for housekeeping, the dock and the kitchen. Scenes: M005 (the bar point moves to where the guest went down, stool, glass, AED once used), M009 (dock door wedged open by the pallet jack until you close it), M010 (luggage room open). Five new tour views. Known gap: no casualty figure in M005 (no lying-down animation in the cast set).
  - Screens: CCTV review console (clip markers, viewer with camera and time, previous and next) and a lock audit readout; other tools keep the table.
  - Self-test play now runs M001-M011 with the three ceremonies and expects level 4; resume expects level 4 and 11 records.
  - Core tests (GCC, ASan + UBSan): `44 test cases, 0 failed, 0 failed checks`. `BuildVoices.py --list`: 203 lines.

- 2026-10-22 PC session round 2 (Remote Control, UE 5.8.4, MSVC). Branch at 4a7c65a before this commit. Nothing changed in C:\Downloads\Riskbound.
  - `FetchAssets.py --resolve`: exit 0, all ids ok. `potted_plant_02_leaves_alpha_1k.jpg` fetched; potted_plant_04 has no alpha map upstream (its Poly Haven file list has no `alpha` key: solid leaves). AssetManifest.json gained the alpha entry.
  - `BuildVoices.py` (kokoro-onnx installed): `KcVoice: 139 lines (139 spoken now), 6.5 minutes of speech`.
  - `KeycardEditor Win64 Development`: `Result: Succeeded`. KcGameTests.cpp first compiled inside the unity blob; once edited it compiled alone and needed `using namespace KcConvert;` (C3861 `ToFString`/`ToStd`).
  - Core tests (MSVC): `43 test cases, 0 failed, 0 failed checks`.
  - Imports, headless: `KcImport: done: 8 meshes, 8 materials, 0 errors`, KcImportReport.json `errors: []` (plant_02 leaves slot: 4 textures, foliage master). `KcAudio: imported 167 sounds, 0 errors` (28 + 139 voice lines). CreateMap: map already exists, exit 0.
  - `RunUnrealTests.bat` first run: 2 pass, 3 fail; all three were test bugs, the game was right:
    - ShippedContentLoads: `"Lobby" != "lobby"` is false in UE (FString compares ignore case); now `Equals(..., ESearchCase::CaseSensitive)`.
    - SlicePromotesWithCeremony: required `LastDebrief()` null when M002/M003 start, but the previous filed case's debrief stays reopenable by design; now checks no debrief exists for the open case.
    - RoundTripAfterPromotion: the second session's `NewGame()` autosaved a level-1 game over the `uetest` slot before `Load()`; the loading session now starts no game.
    - Re-run: `Keycard.Audio.VoiceLookupIsSafe`, `Keycard.Campaign.SlicePromotesWithCeremony`, `Keycard.Campaign.StepsNeedThePlaceAndTheOrder`, `Keycard.Content.ShippedContentLoads`, `Keycard.Save.RoundTripAfterPromotion`: all `Success`, exit 0.
  - Tour shots looked at (before fixes): lobby ceiling no longer blown out (soft grey-white, coffer step reads) but the chandelier's light sat inside the fixture and threw two large hard black shapes on the ceiling; back plants now read as leafy plants; the round-table centrepiece was still green and pink spheres (procedural, not a model); LP office ceiling now light, light panels read, but a dark band remains where the walls meet the ceiling; CCTV room ceiling became a glowing white slab over a dark room (auto exposure lifts `MI_CeilingSoft`); 1412 wardrobe: grain direction (vertical) and scale (one repeat per 1.4 m) are right; what looked wrong was a white streaky sheen and a bright oval highlight from glossy grain lines (oak ARM roughness x 0.9).
  - Fixes: chandelier light no shadows (KcHotel.cpp); round-table centrepiece is `potted_plant_02` at 0.8 (spheres kept as fallback); CCTV room ceiling uses new `MI_CeilingDim` (intensity 0.8); `MI_WoodOak` RoughnessScale 0.9 -> 1.5. Re-shot: ceiling shapes gone, centrepiece is a plant, CCTV ceiling dim grey (a soft reflection of the video wall remains on it), wardrobe reads as satin oak.
  - `BuildWindows.bat Development`: `BUILD SUCCESSFUL`, ExitCode=0. Packaged self-tests at 1280x720: play `PASS` (M001-M003 score 100, ceremony Senior LP Officer, level 2, 4 cards), resume `PASS` (3 records, ceremony played), tour `PASS` 17 places, `average_fps=58.1`.
  - Tour 1920x1080, High (GameUserSettings `sg.*=2`): `PASS`, `average_frame_ms=17.25 average_fps=58.0`. Frame rate is capped at 60, so every place but the first reads ~60 fps; worst: LP office 37.6 fps (first stop, warm-up), highest GPU time Floor 14 closet 17.81 ms, Floor 14 corridor 17.61 ms, Room 1406 17.54 ms. Shots in Saved/TourShots (not committed).

- 2026-10-22 Phase 1 follow-up, cloud session (not compiled yet).
  - `Data/VoiceCast.json`: 23 characters, each with their own Kokoro voice; `BuildVoices.py` now collects case calls, action lines, interview replies and the in-passing lines (139 lines, `--list` run here). Core test `Voice_EveryCharacterHasTheirOwnVoice`.
  - UE automation tests `Keycard.*` (Source/Keycard/Private/Tests/KcGameTests.cpp): content loads, M001-M003 best play promotes with the ceremony, step place and order rules with the rubric hidden, save round trip, voice lookup.
  - Visual fixes from the tour: lobby cove light (the whole coffer underside was a 13 m emissive panel; now rim strips on top of the island), back-of-house ceilings get a faint self-lit `MI_CeilingSoft` instead of reading black, leaf cards fetch their opacity map and use a masked two-sided `M_KcFoliage`.
  - Core tests (GCC, ASan + UBSan): `43 test cases, 0 failed, 0 failed checks`.

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
Hands-on playtest; LP office wall-top dark band.
