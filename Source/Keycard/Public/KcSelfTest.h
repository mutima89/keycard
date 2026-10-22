// Packaged-build self check.
//   -KcSelfTest=play    new game, plays Mission 001 through the HUD/interactable calls the UI uses, saves, quits.
//   -KcSelfTest=resume  loads that save the way Continue does, checks it, writes Saved/KcSelfTest.txt, quits.
//   -KcSelfTest=tour    visits every room without player input, renders a screenshot per room
//                       (Saved/Screenshots/<platform>/Tour_<room>.png) and writes frame timings to Saved/KcTour.txt.
//   -KcSelfTest=people  the office's people: faces, reactions, meetings, a colleague walking up (Person_<n>_<name>.png, KcPeople.txt).
//   -KcSelfTest=ui      opens every screen through the HUD and captures each with the UI (Ui_<n>_<screen>.png).
// Uses its own save slot ("selftest") so the player's saves are never touched.
#pragma once

#include "CoreMinimal.h"
#include "KcTypes.h"

class UKcGameSubsystem;
class UWorld;

namespace KcSelfTest
{
extern const TCHAR* const SlotName;

bool IsRequested();
FString RequestedMode();
void Run(UWorld* World);

// A complete, well-evidenced answer to the R-014 assessment (what a strong player submits).
FKcSubmissionInput StrongAssessment();

// Direct subsystem play-through used by automation tests.
void PlayMission001(UKcGameSubsystem* Game);
} // namespace KcSelfTest
