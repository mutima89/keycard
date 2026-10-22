// Packaged-build self check.
//   -KcSelfTest=play    new game; signs on at the LP office terminal and plays M001-M003 the expert's way through
//                       the HUD calls the UI uses, checking that every step is offered by a person or a case point
//                       in the hotel; files each report, checks the debrief, the first promotion and its ceremony,
//                       saves and quits (Saved/KcSelfTest_play.txt).
//   -KcSelfTest=resume  loads that save the way Continue does, checks it, writes Saved/KcSelfTest.txt, quits.
//   -KcSelfTest=tour    visits every place without player input, renders a screenshot per place
//                       (Saved/Screenshots/<platform>/Tour_<place>.png) and writes frame timings to Saved/KcTour.txt.
//   -KcSelfTest=ui      opens every screen through the HUD and captures each with the UI (Ui_<n>_<screen>.png).
// Uses its own save slot ("selftest") so the player's saves are never touched.
#pragma once

#include "CoreMinimal.h"
#include "KcTypes.h"

class AKcHUD;
class UWorld;
namespace kc { struct ActionDef; }

namespace KcSelfTest
{
extern const TCHAR* const SlotName;

bool IsRequested();
FString RequestedMode();
void Run(UWorld* World);

// Fills the report draft with the best answers (the self-test may read the rubric; players never see it).
void FillBestReport(AKcHUD* Hud);
// Answers every in-person question open now with its best answer.
int32 AnswerOpenQuestions(AKcHUD* Hud);
} // namespace KcSelfTest
