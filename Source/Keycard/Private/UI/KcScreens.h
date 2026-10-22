// Builders for the modal screens. Each returns the widget and the control that takes initial
// keyboard focus. Screens only display views and forward button presses to AKcHUD.
#pragma once

#include "CoreMinimal.h"
#include "KcTypes.h"
#include "KcUIStyle.h"

class AKcHUD;
struct FKcConversationView;
class SWidget;

struct FKcScreenParts
{
	TSharedRef<SWidget> Widget;
	TSharedPtr<SWidget> Focus;
};

namespace KcScreens
{
FKcScreenParts MainMenu(AKcHUD* Hud);
FKcScreenParts DifficultyPicker(AKcHUD* Hud);
FKcScreenParts PauseMenu(AKcHUD* Hud);
FKcScreenParts Settings(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Journal(AKcHUD* Hud);
FKcScreenParts Credits(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Workstation(AKcHUD* Hud);
FKcScreenParts Document(const FText& Title, const FText& Subtitle, const FText& Body, TFunction<void()> OnClose);
FKcScreenParts Dialogue(const FText& Speaker, const FText& Text, TFunction<void()> OnContinue);
FKcScreenParts BackgroundPicker(AKcHUD* Hud, EKcDifficulty Difficulty);
FKcScreenParts Challenges(AKcHUD* Hud);
FKcScreenParts ChallengeResults(AKcHUD* Hud);
FKcScreenParts Glossary(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts AiChat(AKcHUD* Hud, const FString& PersonId, const FString& Status);
FKcScreenParts Review(AKcHUD* Hud, bool bEnding, TFunction<void()> OnBack);
FKcScreenParts Skills(AKcHUD* Hud, TFunction<void()> OnBack);
FKcScreenParts Conversation(AKcHUD* Hud, const FKcConversationView& View, TFunction<void()> OnEnd);
// A colleague who walked over with a message: read it now (opens the inbox) or later.
FKcScreenParts Approach(const FText& Speaker, const FText& Text, TFunction<void()> OnRead, TFunction<void()> OnLater);
TSharedRef<SWidget> CeremonyOverlay(const FString& Title, const FString& Subtitle);
FKcScreenParts Debrief(const FKcDebriefView& Debrief, const FKcCareerView& Career, const TArray<FString>& Achievements, TFunction<void()> OnClose);

FText BriefingText(bool bHints);
FText RegisterEntryText(const FKcRegisterEntryView& Entry);
} // namespace KcScreens
