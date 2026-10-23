// Builders for the modal screens. Each returns the widget and the control that takes initial
// keyboard focus. Screens only display state and forward button presses to AKcHUD; rubric scores
// and option feedback appear only in the debrief, after the report is filed.
#pragma once

#include "CoreMinimal.h"
#include "KcTypes.h"
#include "KcUIStyle.h"

class AKcHUD;
class SWidget;
namespace kc { struct ActionDef; struct QuestionDef; struct MissionDebrief; }

struct FKcScreenParts
{
	TSharedRef<SWidget> Widget;
	TSharedPtr<SWidget> Focus;
};

// One button on a choice screen.
struct FKcChoice
{
	FText Label;
	TFunction<void()> OnPick;
	bool bPrimary = false;
	bool bEnabled = true;
	FText Note; // small line under the button
};

namespace KcScreens
{
// ---- Menus ----
FKcScreenParts MainMenu(AKcHUD* Hud);
FKcScreenParts DifficultyPicker(AKcHUD* Hud);
FKcScreenParts BackgroundPicker(AKcHUD* Hud, EKcDifficulty Difficulty);
FKcScreenParts PauseMenu(AKcHUD* Hud);
FKcScreenParts Settings(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Credits(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts ChallengePicker(AKcHUD* Hud);
FKcScreenParts ClassroomPicker(AKcHUD* Hud);
FKcScreenParts Leaderboard(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Ending(AKcHUD* Hud);
FKcScreenParts ChallengeResult(AKcHUD* Hud);

// ---- General ----
FKcScreenParts Document(const FText& Title, const FText& Subtitle, const FText& Body, TFunction<void()> OnClose);
// A line spoken to the player (bottom panel, the speaker stays in view).
FKcScreenParts Dialogue(const FText& Speaker, const FText& Text, TFunction<void()> OnContinue, const FText& ContinueLabel = FText::GetEmpty());
// A list of choices. bConversation puts it in the bottom panel (talking to someone in view).
FKcScreenParts Choices(const FText& Eyebrow, const FText& Title, const FText& Body, const TArray<FKcChoice>& Items, bool bConversation);

// ---- Casework ----
// What an action found: the tool view (lock audit trail, CCTV clips, a log) and the finding.
FKcScreenParts ActionResult(AKcHUD* Hud, const kc::ActionDef& Action, TFunction<void()> OnDone);
// An in-person question: options in their seeded order; OnCommit gets the picked option ids.
FKcScreenParts Question(AKcHUD* Hud, const kc::QuestionDef& Q, const FText& PersonLine, TFunction<void(const TArray<FString>&)> OnCommit, TFunction<void()> OnBack);
FKcScreenParts ShiftBoard(AKcHUD* Hud, const FString& Where);
FKcScreenParts Report(AKcHUD* Hud, const FString& Where);
FKcScreenParts Debrief(AKcHUD* Hud, const kc::MissionDebrief& D, TFunction<void()> OnClose);
FKcScreenParts Journal(AKcHUD* Hud);
FKcScreenParts FieldManual(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Career(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Lift(AKcHUD* Hud, int32 FromFloor);
TSharedRef<SWidget> CeremonyOverlay(const FString& Title, const FString& Subtitle);
} // namespace KcScreens
