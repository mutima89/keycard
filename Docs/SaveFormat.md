# Save format

Code: `KcSave`. Tests: `Tests/Core/TestSave.cpp`. Same format as Riskbound.

- Text file, UTF-8. First line `KEYCARD-SAVE`, then one `key<TAB>value` line per field (`\\`, `\t`, `\n`, `\r` escaped), last line `checksum<TAB><16 hex>` = FNV-1a 64 over everything before it.
- `version` is `CurrentSaveVersion` (2). A newer version is refused as unsupported (not corrupt) so an old build never overwrites a newer save.
- Lists: `<key>.n` then `<key>.0` ... counts capped at 100,000.
- Write: temp file, verify by loading it, copy the valid primary to `.bak`, rename temp over primary. A corrupt primary is moved to `.corrupt` and never rotated into the backup.
- Load: primary; on failure the backup (status `RecoveredFromBackup`, shown to the player). Loading never writes.
- Settings are clamped on load (`ClampSettings`).
- **Any format change**: bump `CurrentSaveVersion`, add a step to `Migrate` in `KcSave.cpp`, add a test that loads a saved older file.

## Keys (version 1)
`profile.name`, `profile.difficulty`, `profile.seed`, `checkpoint`, `settings.*` (mouse, text, volume, reducedMotion, hints, fullscreen, gfx, upscale, fps, vsync, invertY, pad, colourSafe), `career.level`, `career.xp`, `career.origin`, `career.comp.<i>.s/.a`, `career.passed`, `career.attempted`, `career.skills`, `manual`, `learning`, `achievements`.

## Keys added in version 2
- Hotel: `property.id`, `property.name`, `property.week`, `property.seed` (event stream), `property.draws` (draws used, so a reload continues the same sequence), `property.metric.<i>`, `property.flags`, `property.decisions.<n>.*`, `property.pending.<n>.*` (each with its `cause`), `property.history.<n>.*`.
- Case in progress and attempts: `campaign.active`, `campaign.whatIf`, `campaign.clock`, `campaign.location`, `campaign.steps.<n>.*`, field answers, `campaign.custody.<n>.*`, `campaign.seen`, `campaign.records.<n>.*` (score, dimension scores, steps, answers, what-if flag).
- World: `world.relations.<n>.*`, `world.stress`, `world.burnouts`, `world.clues`, `world.inbox.<n>.*`, `world.fired`, `world.news.<n>.*`, `world.crises.<n>.*`, `world.nextMessage`, `world.conversations`.
- Modes: `challenge.mode`, `challenge.sequence`, `challenge.index`, `challenge.lives`, `challenge.points`, `challenge.over`, `challenge.date`.
- `ceremony.pending`, `ceremony.level`: a promotion whose ceremony has not played yet (it plays on the next load if the game closed first).

## Migrations
- v1 -> v2 (`MigrateV1ToV2`): the career, settings and Field Manual are kept; the hotel starts from the Harbourfront profile with `property.seed = profile.seed XOR 0xC0FFEE5EED` (the same value `NewGame` uses), and stress starts at 20. `LoadResult::bMigrated` is set. Test: `Save_V1MigratesToTheHarbourfront`.
