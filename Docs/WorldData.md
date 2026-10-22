# World data

Code: `KcWorld`, `KcWorldData`, `KcPeople`, `KcLearning`. Validated by `Content_EveryDataFileIsValid`.

- `Data/People.json`: the cast. `id`, `name`, `role`, `department`, `faction` (revenue or safety, for office politics), `bio`, `location` (where they are usually found), `avatar` (Rocketbox folder, fetched by `Scripts/FetchCharacters.py`), `outfit`, `staff`, `executive`, `startRelation`.
- `Data/Story.json`: the investigation running under the career ("The Blank Cards"): clues by chapter, shown on the case board. Missions give clues with an action's `clue`.
- `Data/Events.json`: inbox events (`category` story, incident, request, personal, politics, team, news; `when` condition; `probability` per week; `deadlineWeeks` and `defaultChoice`; `choices` with effects and an optional `requiresSkill`) and `fallout` templates by mission topic (`default` is the fallback).
- `Data/FieldManual.json`: procedure cards (steps, why, pitfalls) earned from debriefs.
- `Data/Glossary.json`: LP terms, linked to missions.
- `Data/Endings.json`: endings in order; the first whose condition holds is chosen, so the last must have no condition.

Relationships and stress change the world (replies, favours, fallout, burnout leave). They never gate a promotion.
