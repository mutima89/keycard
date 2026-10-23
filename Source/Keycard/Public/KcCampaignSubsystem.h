// Casework in the world: the content (Data/), the case in progress and its debrief, the inbox and
// the Field Manual. Thin calls into kc:: (KeycardSim); every rule and score lives there.
// The hotel, the HUD, the self-test and the automation tests all act through these calls.
#pragma once

#include "CoreMinimal.h"
#include "KcGameState.h"
#include "KcLearning.h"
#include "KcMission.h"
#include "KcTypes.h"
#include "KcWorld.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KcCampaignSubsystem.generated.h"

class UKcGameSubsystem;

// The case reaching the player (a radio call, the front desk), for the HUD to play once.
struct FKcCaseCall
{
	FString MissionId;
	FString FromId;
	FString Text;
	FString Channel;
};

UCLASS()
class KEYCARD_API UKcCampaignSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- Content (Data/, staged with the game; validated on load) ----
	// Reads every content file. False, and nothing playable, if any file is broken.
	bool LoadFromDisk();
	const TArray<FString>& GetLoadErrors() const { return LoadErrors; }
	const kc::MissionCatalogue& GetCatalogue() const { return Catalogue; }
	const kc::WorldContent& GetWorldContent() const { return World; }
	const std::vector<kc::ProcedureCard>& GetCards() const { return Cards; }
	const std::vector<kc::GlossaryTerm>& GetGlossary() const { return Glossary; }
	const std::vector<kc::EndingDef>& GetEndings() const { return Endings; }
	const std::vector<kc::ClassroomSet>& GetClassroom() const { return Classroom; }
	void InitWorldFor(kc::GameState& State) const;

	// ---- People ----
	const kc::CharacterDef* FindPerson(const FString& Id) const;
	FString PersonName(const std::string& Id) const;
	FString PersonRole(const std::string& Id) const;

	// ---- Cases ----
	// Open now, in play order (the next case first).
	TArray<const kc::MissionDef*> AvailableCases() const;
	const kc::MissionDef* Find(const FString& MissionId) const;
	const kc::MissionDef* Active() const;
	bool IsWhatIf() const;
	// The case to start next in a career: the first available that is not passed yet (nullptr if none).
	const kc::MissionDef* NextCase() const;

	EKcCaseResult StartCase(const FString& MissionId);
	EKcCaseResult StartWhatIf(const FString& MissionId);
	EKcCaseResult AbandonCase();
	// The call that opens the case (queued by StartCase; the HUD takes it once).
	bool TakeCaseCall(FKcCaseCall& Out);

	// Clock and place.
	FString ClockText() const;
	FString CurrentLocation() const;
	static FString LocationName(const FString& LocationId);

	// What can be done here now. An action is done with its person (face to face), else at its spot,
	// else at the place's own case point (Docs/MissionData.md "How the hotel shows it").
	TArray<const kc::ActionDef*> ActionsAt(const FString& Where, const FString& PersonId, bool bAll = false) const;
	TArray<const kc::ActionDef*> ActionsAtSpot(const FString& Where, const FString& Spot) const;
	// True when the action is done by talking to its person in the world.
	static bool IsFaceToFace(const kc::ActionDef& A);
	TArray<const kc::QuestionDef*> QuestionsAt(const FString& Where, const FString& PersonId) const;
	// Every place where the active case has something left to do (objective markers).
	TArray<FString> PlacesWithWork() const;
	// Does it: walks there (case clock), records the step and the custody entry. Notifies the finding.
	EKcCaseResult DoAction(const FString& ActionId, const FString& Where);
	EKcCaseResult AnswerInPerson(const FString& QuestionId, const TArray<FString>& OptionIds, const FString& Where);
	// Options in their seeded display order (position never reveals the answer).
	TArray<const kc::OptionDef*> OptionsInOrder(const kc::QuestionDef& Q) const;
	FString FindingFor(const kc::ActionDef& A) const;
	bool IsDone(const FString& ActionId) const;

	// The incident report: questions on the form, the draft kept for the session, and filing it.
	TArray<const kc::QuestionDef*> ReportQuestions() const;
	TMap<FString, TArray<FString>>& Draft() { return ReportDraft; }
	EKcCaseResult ValidateReport(const TMap<FString, TArray<FString>>& Answers) const;
	EKcCaseResult FileReport(const TMap<FString, TArray<FString>>& Answers, const FString& Where);
	// Plays the expert's fieldwork and files the best report (self-test, walkthrough, tests).
	EKcCaseResult PlayBestAndFile(const FString& Where);

	// The debrief of the latest filed report in this session (bValid false before one).
	const kc::MissionDebrief* LastDebrief() const { return bHasDebrief ? &Debrief : nullptr; }

	// What the active case still needs, for the objective line.
	FString Objective() const;

	// ---- Inbox ----
	EKcCaseResult ReplyToMessage(const FString& MessageId, const FString& ChoiceId, FString& OutError);
	void MarkMessageRead(const FString& MessageId);

	static FString ResultText(EKcCaseResult Result);
	static EKcCaseResult FromSim(kc::EMissionError E);

private:
	UKcGameSubsystem* Game() const;
	std::vector<kc::MissionAnswer> ToAnswers(const TMap<FString, TArray<FString>>& Answers) const;
	void AfterSubmit(const kc::MissionDef& M);

	kc::MissionCatalogue Catalogue;
	kc::WorldContent World;
	std::vector<kc::ProcedureCard> Cards;
	std::vector<kc::GlossaryTerm> Glossary;
	std::vector<kc::EndingDef> Endings;
	std::vector<kc::ClassroomSet> Classroom;
	TArray<FString> LoadErrors;
	TMap<FString, TArray<FString>> ReportDraft;
	kc::MissionDebrief Debrief;
	bool bHasDebrief = false;
	TArray<FKcCaseCall> PendingCalls;
};
