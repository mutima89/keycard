# Save format

Code: `KcSave`. Tests: `Tests/Core/TestSave.cpp`. Same format as Riskbound.

- Text file, UTF-8. First line `KEYCARD-SAVE`, then one `key<TAB>value` line per field (`\\`, `\t`, `\n`, `\r` escaped), last line `checksum<TAB><16 hex>` = FNV-1a 64 over everything before it.
- `version` is `CurrentSaveVersion` (1). A newer version is refused as unsupported (not corrupt) so an old build never overwrites a newer save.
- Lists: `<key>.n` then `<key>.0` ... counts capped at 100,000.
- Write: temp file, verify by loading it, copy the valid primary to `.bak`, rename temp over primary. A corrupt primary is moved to `.corrupt` and never rotated into the backup.
- Load: primary; on failure the backup (status `RecoveredFromBackup`, shown to the player). Loading never writes.
- Settings are clamped on load (`ClampSettings`).
- **Any format change**: bump `CurrentSaveVersion`, add a step to `Migrate` in `KcSave.cpp`, add a test that loads a saved older file.

## Keys (version 1)
`profile.name`, `profile.difficulty`, `profile.seed`, `checkpoint`, `settings.*` (mouse, text, volume, reducedMotion, hints, fullscreen, gfx, upscale, fps, vsync, invertY, pad, colourSafe), `career.level`, `career.xp`, `career.origin`, `career.comp.<i>.s/.a`, `career.passed`, `career.attempted`, `career.skills`, `manual`, `learning`, `achievements`.
