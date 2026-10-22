# Keycard (working title)

A first-person career simulator about hotel Loss Prevention. You join the LP department of the Veldane Grand Harbourfront as a night-shift LP Officer and work your way up to Vice President, Global Safety & Security, by doing the job: answering radio calls, walking to the scene, interviewing people face to face, interrogating locks, reviewing CCTV, keeping chain of custody, running fire and life safety, handling claims and investigations, passing audits and leading through crises. Every mission teaches a real procedure, and the debrief explains what a strong LP professional would have done and why.

Offline and single-player. Unreal Engine 5.8, Windows x64. Sister game to [Riskbound](https://github.com/mutima89/riskbound).

**Status:** content complete through Phase 4: 34 ladder cases, four career crises and the capstone across ten career levels, 47 Field Manual cards, 11 endings, and challenge modes (survival, daily, classroom) with a local leaderboard. Phase 5 (full walkthrough, polish, installer, 1.0.0) is in progress. See [Docs/Progress.md](Docs/Progress.md).

## Running the walkthrough
```
powershell -ExecutionPolicy Bypass -File Scripts\RunWalkthrough.ps1 -Exe Release\Development\Windows\Keycard.exe
```
Plays the whole career to the capstone and its ending, then a daily challenge, without input, and writes `KcSelfTest_career.txt` and `KcSelfTest_challenge.txt` to the build's Saved folder.

## Building the simulation core
```
Scripts/run_core_tests.sh        # Linux/macOS
Scripts\RunCoreTests.bat         # Windows (CMake + Visual Studio C++ tools)
```

## Names are fictional
Veldane Hotels & Resorts, every property, company, product and person in Keycard is fictional. Any resemblance to a real business is unintended; names are checked before use (Docs/FictionalNames.md).

Keycard is training-style fiction about widely published hotel security practice. It is not legal advice and does not describe any real company's procedures; follow local law and your own organisation's policies.

## Licence and credits
Code: MIT (LICENSE). Created by M A Almahlaki. Third-party assets and their licences: Docs/AssetCredits.md.
