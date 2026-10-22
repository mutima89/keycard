# Keycard

Offline, single-player, first-person hotel Loss Prevention (LP) career simulator. Unreal Engine 5.8 (C++ plus Blueprints), Windows x64.
The player joins LP at the Veldane Grand Harbourfront as a night-shift LP Officer and rises to Vice President, Global Safety & Security, by doing real LP work. Learning game first: every mission teaches a real procedure and the debrief explains best practice and why.
Sister game to Riskbound (github.com/mutima89/riskbound, MIT, same owner). Reuse its architecture and code where it fits (Docs/ReuseFromRiskbound.md).

## Architecture (detail: Docs/Architecture.md)
- `Source/KeycardSim/` - engine-independent C++17 simulation core (career, LP formulas, scoring, consequences, saves). `namespace kc`, files prefixed `Kc`. No UObjects, no exceptions, no RTTI. Deterministic.
- `Source/Keycard/` - UE gameplay and presentation: character, interaction, Slate screens, people, adapters calling KeycardSim.
- `Tests/Core/` - core unit tests, built by root `CMakeLists.txt` (runs without UE).
- `Data/` - missions, people, dialogue and voice cast as validated JSON (tests fail when content is broken).

## Commands
- Core tests (Linux/macOS): `Scripts/run_core_tests.sh [filter]` (ASan + UBSan)
- Core tests (Windows): `Scripts\RunCoreTests.bat [filter]`
- UE automation tests: `Scripts\RunUnrealTests.bat <UE dir>`
- Package: `Scripts\BuildWindows.bat <UE dir> [Development|Shipping]` -> `Release\<Config>\Windows\Keycard.exe`
- Voices: `python Scripts/BuildVoices.py` (Kokoro-82M, checked by SHA-256), then `Scripts/ImportAudio.py` in the editor
- Cast: `python Scripts/FetchCharacters.py`, then `Scripts/ImportCharacters.py` in the editor

## Conventions
- UE naming style (PascalCase, `b` bools) in all C++.
- One formula, one place: every LP formula lives in `KcLpMath` and is documented in Docs/LpMethodology.md. Never re-implement it in Blueprints or UI. Ordinal scores are never money. Annualised loss is an average, never a worst case.
- Promotion requires competence evidence and passed missions (and a career crisis at levels 6-9); XP never gates promotion. Relationships never gate promotion.
- Rubrics stay hidden until the player submits.
- Random events use seeded `kc::Rng` only.
- Bump `CurrentSaveVersion` and add a migration for any save-format change (Docs/SaveFormat.md).
- Veldane LP policy in every mission: observe, report and de-escalate. Physical intervention, searches of people and detention follow policy and local law only; breaking policy is scored and has consequences.
- Law-neutral: "local law", "the fire authority", "counsel". Training-style fiction, not legal advice.

## Rules
- All business names are fictional. Web-search every new name before use and record it in Docs/FictionalNames.md. Never imitate a real hotel chain's name, logo, colours, loyalty programme or brand-standard wording.
- Assets: free and game-licensed only, credited in Docs/AssetCredits.md; licensed third-party content stays out of the repo when its licence requires and is fetched by script.
- Target PC: GTX 1650 4 GB at 1920x1080, 60 fps at High in the packaged build. No SM6 or DX12-only features (driver crash), no MetaHuman Creator, no Nanite-heavy or 8K assets.
- Never claim a build, test or package passed without real output from this session. Record results in Docs/Progress.md.
- No destructive git operations or deleting saves without approval. No network services at runtime except the opt-in AI colleagues (off by default, player's own key).
- PC work runs through Claude Remote Control in `C:\Downloads\Keycard`; state the scope and get the owner's go-ahead before long runs; stop Remote Control when idle; never send keystrokes unless the game window is confirmed focused.
- Work in bounded phases (Docs/GameDesign.md "Phases"); stop for the owner between phases.
- Start each task: read Docs/Progress.md, then only the relevant docs and files, then `git diff`.

## Skills (.claude/skills)
`unreal-project-audit`.

## Current phase
Phase 0 (audit, skeleton, core, docs). See Docs/Progress.md.
