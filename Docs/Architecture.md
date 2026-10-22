# Architecture

```
Slate screens / Blueprints (presentation, level scripting)
        |  read view models, send player intents
Keycard module (UE): character, interaction, people, subsystems (Phase 1)
        |  plain C++ calls, no UObjects below this line
KeycardSim module (C++17, deterministic, unit tested outside UE)
```

## KeycardSim
| File | Responsibility |
|---|---|
| `KcApi.h` | Export macro (`KC_API`) for the editor DLL build |
| `KcRng.h` | Seeded xoshiro256** RNG |
| `KcJson` | Strict JSON reader with line numbers for Data/ |
| `KcVoice` | Stable voice line keys (mirrors Scripts/BuildVoices.py) |
| `KcCareer` | Ladder, competencies, backgrounds, promotion gates |
| `KcLpMath` | Every LP formula (Docs/LpMethodology.md) |
| `KcGameState` | Persistent state, settings limits, new game |
| `KcSave` | Versioned checksummed saves, safe writes, backup recovery, migrations (v2, Docs/SaveFormat.md) |
| `KcProperty` | The hotel the player answers for: metrics, flags, decisions, latent consequences with their cause, property profiles |
| `KcPeople` | Cast definitions, relationship bands |
| `KcMission` | Locations and floors, travel time, the case clock, fieldwork, in-person questions, report, hidden 7-dimension rubric, what-if replays, submission (Docs/MissionData.md) |
| `KcMissionData` | Strict JSON loader for Data/Missions |
| `KcWorld`, `KcWorldData` | Relationships, stress, clues, inbox, fallout, news; loaders for People/Story/Events (Docs/WorldData.md) |
| `KcLearning` | Field Manual, glossary, skills, endings, review and certificate, challenge modes, leaderboard |

The same sources build standalone with CMake (`Scripts/run_core_tests.sh`, with ASan and UBSan) and as the `KeycardSim` UE module (`KeycardSim.Build.cs`: no RTTI, no exceptions).

## Keycard UE module
Phase 0 has only the module shell (`Keycard.Build.cs`, `Keycard.cpp`, `KcLog.h`). Phase 1 brings the Riskbound UE layer across (Docs/ReuseFromRiskbound.md).

## Determinism
All randomness flows from `GameState.Seed` through `kc::Rng`.
