# Mission data

Code: `KcMission` (rules and scoring), `KcMissionData` (strict JSON loader). Files: `Data/Missions/<id>.json`, generated from `Scripts/content/<id>.py` (edit the script, run it, commit both). Tests: `Tests/Core/TestMissions.cpp` (every file is loaded and validated; a broken file fails the build's tests).

Unknown keys, wrong types and unknown names are errors, reported with the file and line.

## A mission is a case worked on foot
- `start` ("HH:MM") starts the case clock. Every action has a place (`at`, from `KnownLocations` in `KcMission.cpp`) and `minutes`. Walking costs time: 0 in the same place, 2 on the same floor, 5 between floors (lifts and stairs).
- `opening`: how the case reaches the player (`channel`: radio, frontDesk, phone, firePanel, email, letter, inPerson), who from and where the player is.
- `actions`: `id`, `label`, `kind` (look, interview, lockAudit, cctvReview, exportFootage, photograph, bagEvidence, logEvidence, check, call, secure, search, detain, enter, other), `at`, `person`, `spot` (the prop in that place where it is done, from `KnownSpots` in `KcMission.cpp`: the CCTV console, the lock on 1412, the LP radio), `minutes`, `requires`, `required`, `expiresAt` + `expiredFinding` (a witness who goes off shift, footage that overwrites), `findingTitle`, `finding` (text or paragraphs), `columns` + `rows` (tables for lock audits, CCTV and logs), `lines` (spoken dialogue), `evidence` + `custody` (chain-of-custody entry), `clue`, and effects: `effects` (hotel metrics), `relations`, `schedules` (latent consequences).
- Trap actions (a search of a staff member's cart, making a key without ID, entering a guest room) are ordinary actions. A `notDone` check marks them; `critical` caps the score below the pass mark.
- `questions`: answered in person (`at` and `person`, opened by `requires`) or on the report form (`section`: who, what, when, where, how, actions, facts). Kinds: single (`score` per option), multi (`correct` options; wrong picks cost 25), numeric (answer computed by a named `KcLpMath` formula: `"formula": {"name", "inputs"}`; never typed by hand), order. Options can carry `reply` (the person's answer), `feedback`, `cost` (checked against the rank's decision authority), `escalates` (the decision is passed to someone with authority), `critical`, and effects.
- `checks`: scored from the fieldwork: `done`, `notDone`, `before` (`action` before `other`), `byMinute` (`action` by `by`). 100 when met, 40 when late or out of order, 0 otherwise.
- Every question and check has a `dimension` (the hidden rubric: Procedure, Evidence, Compliance, Care, Report, Communication, Cost) and a `competency`. Weights sum to 100; at least four dimensions are assessed.
- Case times: never write a time by hand for something the player does (no "02:2x"). `{time}` in an action's title, finding, custody or table rows is the clock when that action finished; `{time:action_id}` (anywhere, report options included) is when that action finished, or "--:--" if it was not done. Times of things the player only observes (camera clips, panel print-outs) are written out.
- `expiresAt`: done late, the action finds `expiredFinding`, seals no custody entry, and its optional follow-ups (actions that require it and are not `required`) are no longer offered.
- `bestOrder`: the expert's route. Validation walks it: it must be possible, meet every check and leave nothing expired.
- `debrief`: whatHappened, bestPractice, why, commonMistakes. `procedureCards`: Field Manual cards earned. `lesson`, `passMark`, `xp`, `weeks`, `reportAt` (where the report is filed: always `lp_office`, the only report terminal).

## How the hotel shows it
- An action with a `person` (not a call) is done by talking to that person, who stands in that place while the case needs them.
- An action with a `spot` is done at that prop. Calls name the radio or phone they are made from.
- Anything else is done at the place's own case point.

## Rules the engine keeps
- Rubric and scores stay hidden until submission.
- A what-if replay (from the debrief) is scored and debriefed but changes nothing in the career, the hotel or the world.
- Weak answers leave latent risk (fallout by mission `topic`, from `Data/Events.json`); every consequence carries a `Cause` naming the decision so a later incident can say "this traces back to...".
