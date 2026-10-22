# Career model

Code: `Source/KeycardSim/Public/KcCareer.h`, ladder data in `BuildLadder()` (`KcCareer.cpp`). Tests: `Tests/Core/TestCareer.cpp`.

## Rules
- Ten levels. Every career starts at level 1, LP Officer on nights.
- Ten competencies, each 0 to 100 with an evidence count: Incident Response, Investigation, Evidence & Reporting, Fire & Life Safety, Key & Access Control, Guest Service & De-escalation, People Leadership, Compliance & Audit, Financial Acumen, Crisis Leadership.
- Each assessed piece of work updates a competency: the first result replaces the starting estimate, later ones blend `new = old x 0.6 + result x 0.4`.
- **Promotion gate** (to level + 1) = every listed competency at or above its minimum, with at least `MinAssessmentsPerRequirement` assessed pieces of work each, AND every required mission passed, AND (levels 6 to 9) one career crisis from that level's pool passed. XP is never checked (`Career_XpAloneNeverPromotes`). Relationships change briefings, help and story but never gate promotion.
- Decision authority (currency) caps what the player can approve; anything above it must be escalated to the right person, and failing to escalate is scored.
- Best play reaches VP on competence alone (`Career_BestPlayReachesVpOnCompetenceAlone`; Phase 2 replaces it with a full replay through mission data).

## Backgrounds
Each background sets starting estimates (sum 255 each, so none is stronger overall). Estimates are not evidence: assessments start at 0, so the first missions must still be done well (`Career_BackgroundScoresAreNotEvidence`).

| Background | Strengths | Gaps |
|---|---|---|
| Ex-police officer | Incident response, investigation, evidence | Guest service, budgets; must unlearn police powers (observe and report) |
| Hotel front office | Guest service, keycard systems | Investigation, crisis |
| Military | Incident response, fire drills, leadership | Guest service, budgets |
| Criminology graduate | Investigation, reporting, audit, budgets | Floor experience, leadership |

## Skills (Phase 2)
Ten skills unlocked by rank and choice, stored as ids in `CareerState::Skills`: interview technique, CCTV forensics, fire systems, first aid trainer, claims negotiation, key systems, de-escalation trainer, investigations with HR, budget and staffing, crisis command.

## Levels
| # | Title | Authority | Promotion needs (competency >= min) | Missions | Crisis | Ceremony |
|---|---|---|---|---|---|---|
| 1 | LP Officer | 0 | Incident 45, Evidence 45, Guest 40 | M001-M003 | - | GM and executive committee |
| 2 | Senior LP Officer | 500 | Incident 55, Evidence 55, Keys 45, Fire 45 | M004-M007 | - | property |
| 3 | LP Supervisor | 2.5k | Investigation 55, Keys 58, People 45, Evidence 60 | M008-M011 | - | property |
| 4 | Assistant LP Manager | 10k | Fire 62, Compliance 55, Financial 45, Investigation 60 | M012-M015 | - | property |
| 5 | LP Manager | 50k | People 62, Compliance 62, Financial 58, Investigation 65 | M016-M019 | - | property |
| 6 | Director of Loss Prevention | 150k | Crisis 62, People 66, Financial 64, Compliance 66 | M020-M022 | C001 | property (exec committee) |
| 7 | Cluster Director of LP | 400k | Crisis 68, Compliance 70, Financial 68, People 70 | M023-M026 | C002 | area leadership |
| 8 | Area Director of Safety & Security | 1m | Crisis 74, Financial 72, Compliance 74, Guest 70 | M027-M030 | C003 | area leadership |
| 9 | Regional Director of Safety & Security | 3m | Crisis 78, People 78, Financial 76, Compliance 78 | M031-M034 | C004 | board |
| 10 | VP, Global Safety & Security | 10m | (top) capstone X001 | - | - | board |

Evidence per required competency (`MinAssessmentsPerRequirement`): 1 at level 1, 2 at levels 2 to 7, 3 at levels 8 and 9. Every promotion plays its own ceremony, also with reduced motion on (reduced motion uses cuts, never skips the ceremony).

Ids: ladder missions M001-M034 (34), career crises C001-C004, optional missions M035-M040 and incident-triggered crises C005-C012, capstone X001. Total about 40 missions, 12 crises, 1 capstone.
