#include "KcCampaignSubsystem.h"

#include "HAL/FileManager.h"
#include "KcCast.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcLog.h"
#include "KcMissionData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <algorithm>

using namespace KcConvert;

void UKcCampaignSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadFromDisk();
}

UKcGameSubsystem* UKcCampaignSubsystem::Game() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UKcGameSubsystem>() : nullptr;
}

// ---------------------------------------------------------------- content

bool UKcCampaignSubsystem::LoadFromDisk()
{
	LoadErrors.Reset();
	const FString DataDir = FPaths::ProjectDir() / TEXT("Data");
	const FString MissionDir = DataDir / TEXT("Missions");
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(MissionDir / TEXT("*.json")), true, false);
	Names.Sort();
	std::vector<std::pair<std::string, std::string>> Files;
	for (const FString& Name : Names)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(MissionDir / Name)))
		{
			LoadErrors.Add(Name + TEXT(": could not be read"));
			continue;
		}
		Files.push_back({ToStd(Name), ToStd(Text)});
	}
	std::vector<std::string> Errors;
	kc::MissionCatalogue LoadedMissions;
	kc::LoadMissionCatalogue(Files, LoadedMissions, Errors);

	auto Read = [&](const TCHAR* Name, std::string& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(DataDir / Name)))
		{
			LoadErrors.Add(FString(Name) + TEXT(": could not be read"));
			return false;
		}
		Out = ToStd(Text);
		return true;
	};
	kc::WorldContent LoadedWorld;
	std::vector<kc::ProcedureCard> LoadedCards;
	std::vector<kc::GlossaryTerm> LoadedTerms;
	std::vector<kc::EndingDef> LoadedEndings;
	std::vector<kc::ClassroomSet> LoadedClassroom;
	std::string Text;
	if (Read(TEXT("People.json"), Text)) kc::PeopleFromJson(Text, "People.json", LoadedWorld.People, Errors);
	if (Read(TEXT("Story.json"), Text)) kc::StoryFromJson(Text, "Story.json", LoadedWorld.Story, Errors);
	if (Read(TEXT("Events.json"), Text)) kc::EventsFromJson(Text, "Events.json", LoadedWorld.Events, LoadedWorld.Fallout, Errors);
	if (Read(TEXT("FieldManual.json"), Text)) kc::FieldManualFromJson(Text, "FieldManual.json", LoadedCards, Errors);
	if (Read(TEXT("Glossary.json"), Text)) kc::GlossaryFromJson(Text, "Glossary.json", LoadedTerms, Errors);
	if (Read(TEXT("Endings.json"), Text)) kc::EndingsFromJson(Text, "Endings.json", LoadedEndings, Errors);
	if (Read(TEXT("Classroom.json"), Text)) kc::ClassroomFromJson(Text, "Classroom.json", LoadedClassroom, Errors);
	for (const std::string& E : kc::ValidateWorld(LoadedWorld, &LoadedMissions)) Errors.push_back(E);
	for (const std::string& E : kc::ValidateFieldManual(LoadedCards, &LoadedMissions)) Errors.push_back(E);
	for (const std::string& E : kc::ValidateGlossary(LoadedTerms, &LoadedMissions)) Errors.push_back(E);
	for (const std::string& E : kc::ValidateEndings(LoadedEndings)) Errors.push_back(E);
	for (const std::string& E : kc::ValidateClassroom(LoadedClassroom, &LoadedMissions)) Errors.push_back(E);
	LoadErrors.Append(ToFStrings(Errors));
	for (const FString& E : LoadErrors) UE_LOG(LogKeycard, Error, TEXT("Content: %s"), *E);

	// Broken content loads nothing rather than half a game (the tests catch it before release).
	const bool bOk = LoadErrors.IsEmpty() && !LoadedMissions.Missions.empty();
	Catalogue = bOk ? MoveTemp(LoadedMissions) : kc::MissionCatalogue();
	World = bOk ? MoveTemp(LoadedWorld) : kc::WorldContent();
	Cards = bOk ? MoveTemp(LoadedCards) : std::vector<kc::ProcedureCard>();
	Glossary = bOk ? MoveTemp(LoadedTerms) : std::vector<kc::GlossaryTerm>();
	Endings = bOk ? MoveTemp(LoadedEndings) : std::vector<kc::EndingDef>();
	Classroom = bOk ? MoveTemp(LoadedClassroom) : std::vector<kc::ClassroomSet>();
	KcCast::SetPeople(World.People);
	UE_LOG(LogKeycard, Display, TEXT("Content: %d missions, %d people, %d cards, %d problems"), static_cast<int32>(Catalogue.Missions.size()),
		static_cast<int32>(World.People.People.size()), static_cast<int32>(Cards.size()), LoadErrors.Num());
	return bOk;
}

void UKcCampaignSubsystem::InitWorldFor(kc::GameState& State) const
{
	kc::InitWorld(State, World);
}

const kc::CharacterDef* UKcCampaignSubsystem::FindPerson(const FString& Id) const
{
	return World.People.Find(ToStd(Id));
}

FString UKcCampaignSubsystem::PersonName(const std::string& Id) const
{
	if (Id == "player") return TEXT("You");
	const kc::CharacterDef* C = World.People.Find(Id);
	return C ? ToFString(C->Name) : ToFString(Id);
}

FString UKcCampaignSubsystem::PersonRole(const std::string& Id) const
{
	const kc::CharacterDef* C = World.People.Find(Id);
	return C ? ToFString(C->Role) : FString();
}

// ---------------------------------------------------------------- cases

EKcCaseResult UKcCampaignSubsystem::FromSim(kc::EMissionError E)
{
	switch (E)
	{
	case kc::EMissionError::None: return EKcCaseResult::Ok;
	case kc::EMissionError::Unavailable: return EKcCaseResult::Unavailable;
	case kc::EMissionError::AnotherMissionActive: return EKcCaseResult::AnotherMissionActive;
	case kc::EMissionError::NotActive: return EKcCaseResult::NotActive;
	case kc::EMissionError::UnknownAction: return EKcCaseResult::UnknownAction;
	case kc::EMissionError::AlreadyDone: return EKcCaseResult::AlreadyDone;
	case kc::EMissionError::WrongPlace: return EKcCaseResult::WrongPlace;
	case kc::EMissionError::NeedsEarlierStep: return EKcCaseResult::NeedsEarlierStep;
	case kc::EMissionError::RequiredStepMissing: return EKcCaseResult::RequiredStepMissing;
	case kc::EMissionError::UnansweredQuestion: return EKcCaseResult::UnansweredQuestion;
	case kc::EMissionError::AnsweredInPerson: return EKcCaseResult::AnsweredInPerson;
	case kc::EMissionError::UnknownOption: return EKcCaseResult::UnknownOption;
	case kc::EMissionError::SingleNeedsOneOption: return EKcCaseResult::SingleNeedsOneOption;
	case kc::EMissionError::OrderNeedsEveryOption: return EKcCaseResult::OrderNeedsEveryOption;
	case kc::EMissionError::FileWhereReportsAre: return EKcCaseResult::FileWhereReportsAre;
	case kc::EMissionError::NotSubmitted: return EKcCaseResult::NotSubmitted;
	}
	return EKcCaseResult::Unavailable;
}

FString UKcCampaignSubsystem::ResultText(EKcCaseResult Result)
{
	switch (Result)
	{
	case EKcCaseResult::Ok: return FString();
	case EKcCaseResult::Unavailable: return TEXT("That case is not open to you yet.");
	case EKcCaseResult::AnotherMissionActive: return TEXT("Finish or set aside the case you are working first.");
	case EKcCaseResult::NotActive: return TEXT("No case is in progress.");
	case EKcCaseResult::UnknownAction: return TEXT("That is not part of this case.");
	case EKcCaseResult::AlreadyDone: return TEXT("Already done.");
	case EKcCaseResult::WrongPlace: return TEXT("That has to be done somewhere else.");
	case EKcCaseResult::NeedsEarlierStep: return TEXT("Something else has to happen first.");
	case EKcCaseResult::RequiredStepMissing: return TEXT("The report needs more fieldwork first. Check your notebook.");
	case EKcCaseResult::UnansweredQuestion: return TEXT("Answer every section of the report.");
	case EKcCaseResult::AnsweredInPerson: return TEXT("That is answered face to face, not on the form.");
	case EKcCaseResult::UnknownOption: return TEXT("Pick one of the options.");
	case EKcCaseResult::SingleNeedsOneOption: return TEXT("Pick exactly one answer.");
	case EKcCaseResult::OrderNeedsEveryOption: return TEXT("Put every step in order.");
	case EKcCaseResult::FileWhereReportsAre: return TEXT("Reports are filed at the LP office terminal.");
	case EKcCaseResult::NotSubmitted: return TEXT("File the case once before replaying it.");
	case EKcCaseResult::UnknownMission: return TEXT("Unknown case.");
	}
	return FString();
}

const kc::MissionDef* UKcCampaignSubsystem::Find(const FString& MissionId) const
{
	return Catalogue.Find(ToStd(MissionId));
}

const kc::MissionDef* UKcCampaignSubsystem::Active() const
{
	const UKcGameSubsystem* G = Game();
	if (!G || !G->HasGame()) return nullptr;
	const std::string& Id = G->GetState().Campaign.ActiveMissionId;
	return Id.empty() ? nullptr : Catalogue.Find(Id);
}

bool UKcCampaignSubsystem::IsWhatIf() const
{
	const UKcGameSubsystem* G = Game();
	return G && G->GetState().Campaign.bWhatIf;
}

TArray<const kc::MissionDef*> UKcCampaignSubsystem::AvailableCases() const
{
	TArray<const kc::MissionDef*> Out;
	const UKcGameSubsystem* G = Game();
	if (!G || !G->HasGame()) return Out;
	for (const kc::MissionDef* M : kc::AvailableMissions(G->GetState(), Catalogue)) Out.Add(M);
	return Out;
}

const kc::MissionDef* UKcCampaignSubsystem::NextCase() const
{
	const UKcGameSubsystem* G = Game();
	if (!G) return nullptr;
	const TArray<const kc::MissionDef*> All = AvailableCases();
	// A level's own cases first, then its crisis or the capstone (the catalogue lists C*.json before M*.json).
	for (bool bGates : {false, true})
	{
		for (const kc::MissionDef* M : All)
		{
			if ((M->Kind != kc::EMissionKind::Mission) == bGates && !G->GetState().Career.HasPassed(M->Id) && !M->bOptional) return M;
		}
	}
	return All.IsEmpty() ? nullptr : All[0];
}

EKcCaseResult UKcCampaignSubsystem::StartCase(const FString& MissionId)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Find(MissionId);
	if (!G || !G->HasGame() || !M) return EKcCaseResult::UnknownMission;
	const EKcCaseResult R = FromSim(kc::StartMission(G->EditState(), *M, &Catalogue));
	if (R == EKcCaseResult::Ok)
	{
		ReportDraft.Reset();
		PendingCalls.Add({MissionId, ToFString(M->Opening.From), ToFString(M->Opening.Text), ToFString(kc::ToString(M->Opening.Channel))});
		UE_LOG(LogKeycard, Display, TEXT("Case %s started at %s"), *MissionId, *ClockText());
		G->StateCheckpoint();
	}
	return R;
}

EKcCaseResult UKcCampaignSubsystem::StartWhatIf(const FString& MissionId)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Find(MissionId);
	if (!G || !G->HasGame() || !M) return EKcCaseResult::UnknownMission;
	const EKcCaseResult R = FromSim(kc::StartWhatIf(G->EditState(), *M));
	if (R == EKcCaseResult::Ok)
	{
		ReportDraft.Reset();
		G->Notify(TEXT("What-if replay: try another approach. Nothing you do here changes your career or the hotel."));
		G->StateEdited();
	}
	return R;
}

EKcCaseResult UKcCampaignSubsystem::AbandonCase()
{
	UKcGameSubsystem* G = Game();
	if (!G || !G->HasGame()) return EKcCaseResult::NotActive;
	const EKcCaseResult R = FromSim(kc::AbandonMission(G->EditState()));
	if (R == EKcCaseResult::Ok)
	{
		ReportDraft.Reset();
		G->StateCheckpoint();
	}
	return R;
}

bool UKcCampaignSubsystem::TakeCaseCall(FKcCaseCall& Out)
{
	if (PendingCalls.IsEmpty()) return false;
	Out = PendingCalls[0];
	PendingCalls.RemoveAt(0);
	return true;
}

FString UKcCampaignSubsystem::ClockText() const
{
	const UKcGameSubsystem* G = Game();
	return G && Active() ? ToFString(kc::ClockText(G->GetState().Campaign.Clock)) : FString();
}

FString UKcCampaignSubsystem::CurrentLocation() const
{
	const UKcGameSubsystem* G = Game();
	return G ? ToFString(G->GetState().Campaign.Location) : FString();
}

FString UKcCampaignSubsystem::LocationName(const FString& LocationId)
{
	return ToFString(kc::LocationName(ToStd(LocationId)));
}

TArray<const kc::ActionDef*> UKcCampaignSubsystem::ActionsAt(const FString& Where, const FString& PersonId, bool bAll) const
{
	TArray<const kc::ActionDef*> Out;
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return Out;
	const std::string P = ToStd(PersonId);
	for (const kc::ActionDef* A : kc::ActionsAt(G->GetState(), *M, ToStd(Where)))
	{
		if (bAll || (IsFaceToFace(*A) && !IsGone(*A) && A->Person == P)) Out.Add(A);
	}
	return Out;
}

bool UKcCampaignSubsystem::IsFaceToFace(const kc::ActionDef& A)
{
	return !A.Person.empty() && A.Kind != kc::EActionKind::Call;
}

bool UKcCampaignSubsystem::IsGone(const kc::ActionDef& A) const
{
	const UKcGameSubsystem* G = Game();
	return A.ExpiresAt > 0 && G && G->HasGame() && G->GetState().Campaign.Clock > A.ExpiresAt;
}

TArray<const kc::ActionDef*> UKcCampaignSubsystem::ActionsAtSpot(const FString& Where, const FString& Spot) const
{
	TArray<const kc::ActionDef*> Out;
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return Out;
	const std::string S = ToStd(Spot);
	for (const kc::ActionDef* A : kc::ActionsAt(G->GetState(), *M, ToStd(Where)))
	{
		if (IsFaceToFace(*A) && !IsGone(*A)) continue;
		if (A->Spot == S) Out.Add(A);
	}
	return Out;
}

TArray<const kc::QuestionDef*> UKcCampaignSubsystem::QuestionsAt(const FString& Where, const FString& PersonId) const
{
	TArray<const kc::QuestionDef*> Out;
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return Out;
	const std::string P = ToStd(PersonId);
	for (const kc::QuestionDef* Q : kc::QuestionsAt(G->GetState(), *M, ToStd(Where)))
	{
		if (PersonId.IsEmpty() || Q->Person == P) Out.Add(Q);
	}
	return Out;
}

TArray<FString> UKcCampaignSubsystem::PlacesWithWork() const
{
	TArray<FString> Out;
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return Out;
	for (const kc::LocationDef& L : kc::KnownLocations())
	{
		if (!kc::ActionsAt(G->GetState(), *M, L.Id).empty() || !kc::QuestionsAt(G->GetState(), *M, L.Id).empty()) Out.Add(ToFString(L.Id));
	}
	return Out;
}

EKcCaseResult UKcCampaignSubsystem::DoAction(const FString& ActionId, const FString& Where)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return EKcCaseResult::NotActive;
	const EKcCaseResult R = FromSim(kc::DoAction(G->EditState(), *M, ToStd(ActionId), ToStd(Where)));
	if (R == EKcCaseResult::Ok)
	{
		if (const kc::ActionDef* A = M->FindAction(ToStd(ActionId)))
		{
			if (!A->FindingTitle.empty()) G->Notify(FString::Printf(TEXT("%s  Notebook: %s"), *ClockText(), *ToFString(kc::FindingTitleFor(G->GetState(), *A))));
			// Sealed only when done in time (a late step seals nothing), stamped with the case clock.
			const kc::CampaignState& CS = G->GetState().Campaign;
			const bool bExpired = !CS.Steps.empty() && CS.Steps.back().bExpired;
			if (!A->Evidence.empty() && !bExpired) G->Notify(FString::Printf(TEXT("%s  Chain of custody: %s"), *ClockText(), *ToFString(A->Evidence)));
		}
		G->StateEdited();
	}
	return R;
}

EKcCaseResult UKcCampaignSubsystem::AnswerInPerson(const FString& QuestionId, const TArray<FString>& OptionIds, const FString& Where)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return EKcCaseResult::NotActive;
	kc::MissionAnswer A;
	A.QuestionId = ToStd(QuestionId);
	A.OptionIds = ToStdVector(OptionIds);
	const EKcCaseResult R = FromSim(kc::AnswerInPerson(G->EditState(), *M, A, ToStd(Where)));
	if (R == EKcCaseResult::Ok) G->StateEdited();
	return R;
}

TArray<const kc::OptionDef*> UKcCampaignSubsystem::OptionsInOrder(const kc::QuestionDef& Q) const
{
	TArray<const kc::OptionDef*> Out;
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	const uint64 Seed = G ? G->GetState().Seed : 1;
	const std::string MissionId = M ? M->Id : std::string();
	for (int Index : kc::OptionDisplayOrder(Seed, MissionId, Q.Id, static_cast<int>(Q.Options.size())))
	{
		Out.Add(&Q.Options[static_cast<size_t>(Index)]);
	}
	return Out;
}

FString UKcCampaignSubsystem::FindingFor(const kc::ActionDef& A) const
{
	const UKcGameSubsystem* G = Game();
	return G ? ToFString(kc::FindingFor(G->GetState(), A)) : FString();
}

kc::ActionDef UKcCampaignSubsystem::ForView(const kc::ActionDef& A) const
{
	kc::ActionDef Out = A;
	const UKcGameSubsystem* G = Game();
	if (!G || !G->HasGame()) return Out;
	const kc::GameState& S = G->GetState();
	Out.FindingTitle = kc::FindingTitleFor(S, A);
	Out.Rows = kc::RowsFor(S, A);
	Out.Custody = kc::ExpandTimes(S, A.Custody, A.Id);
	return Out;
}

FString UKcCampaignSubsystem::Expand(const std::string& Text) const
{
	const UKcGameSubsystem* G = Game();
	return ToFString(G && G->HasGame() ? kc::ExpandTimes(G->GetState(), Text) : Text);
}

bool UKcCampaignSubsystem::IsDone(const FString& ActionId) const
{
	const UKcGameSubsystem* G = Game();
	return G && kc::IsActionDone(G->GetState(), ToStd(ActionId));
}

TArray<const kc::QuestionDef*> UKcCampaignSubsystem::ReportQuestions() const
{
	TArray<const kc::QuestionDef*> Out;
	if (const kc::MissionDef* M = Active())
	{
		for (const kc::QuestionDef* Q : kc::ReportQuestions(*M)) Out.Add(Q);
	}
	return Out;
}

std::vector<kc::MissionAnswer> UKcCampaignSubsystem::ToAnswers(const TMap<FString, TArray<FString>>& Answers) const
{
	std::vector<kc::MissionAnswer> Out;
	// In form order, so the saved record reads like the report.
	for (const kc::QuestionDef* Q : ReportQuestions())
	{
		if (const TArray<FString>* Picked = Answers.Find(ToFString(Q->Id)))
		{
			Out.push_back({Q->Id, ToStdVector(*Picked)});
		}
	}
	return Out;
}

EKcCaseResult UKcCampaignSubsystem::ValidateReport(const TMap<FString, TArray<FString>>& Answers) const
{
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return EKcCaseResult::NotActive;
	return FromSim(kc::ValidateSubmission(G->GetState(), *M, ToAnswers(Answers)));
}

EKcCaseResult UKcCampaignSubsystem::FileReport(const TMap<FString, TArray<FString>>& Answers, const FString& Where)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return EKcCaseResult::NotActive;
	kc::MissionDebrief D;
	const EKcCaseResult R = FromSim(kc::SubmitMission(G->EditState(), *M, ToAnswers(Answers), &D, &Catalogue, &World, ToStd(Where)));
	if (R != EKcCaseResult::Ok) return R;
	Debrief = MoveTemp(D);
	bHasDebrief = true;
	AfterSubmit(*M);
	return R;
}

EKcCaseResult UKcCampaignSubsystem::PlayBestAndFile(const FString& Where)
{
	UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !M) return EKcCaseResult::NotActive;
	std::vector<kc::MissionAnswer> Report;
	if (!kc::PlayBestFieldwork(G->EditState(), *M, Report)) return EKcCaseResult::NeedsEarlierStep;
	TMap<FString, TArray<FString>> Answers;
	for (const kc::MissionAnswer& A : Report) Answers.Add(ToFString(A.QuestionId), ToFStrings(A.OptionIds));
	return FileReport(Answers, Where);
}

void UKcCampaignSubsystem::AfterSubmit(const kc::MissionDef& M)
{
	UKcGameSubsystem* G = Game();
	ReportDraft.Reset();
	G->Notify(FString::Printf(TEXT("Report filed: %s"), *ToFString(M.Title)));
	for (const std::string& Card : Debrief.CardsEarned)
	{
		const kc::ProcedureCard* C = kc::FindCard(Cards, Card);
		G->Notify(FString::Printf(TEXT("Field Manual: new card \"%s\""), C ? *ToFString(C->Title) : *ToFString(Card)));
	}
	if (!Debrief.NewMessages.empty()) G->Notify(FString::Printf(TEXT("%d new message(s) in your inbox"), static_cast<int32>(Debrief.NewMessages.size())));
	UE_LOG(LogKeycard, Display, TEXT("Case %s filed: score %d, %s%s"), *ToFString(M.Id), Debrief.Score, Debrief.bPassed ? TEXT("passed") : TEXT("not passed"),
		Debrief.bPromoted ? TEXT(", promoted") : TEXT(""));
	G->StateCheckpoint();
}

FString UKcCampaignSubsystem::Objective() const
{
	const UKcGameSubsystem* G = Game();
	const kc::MissionDef* M = Active();
	if (!G || !G->HasGame()) return FString();
	if (!M)
	{
		const kc::MissionDef* Next = NextCase();
		return Next ? FString::Printf(TEXT("Start your shift at the LP office: %s"), *ToFString(Next->Title)) : TEXT("No open cases. Check your inbox.");
	}
	// Required steps not done yet, then the report.
	for (const kc::ActionDef& A : M->Actions)
	{
		if (A.bRequired && !kc::IsActionDone(G->GetState(), A.Id))
		{
			return FString::Printf(TEXT("%s: %s (%s)"), *ToFString(M->Title), *ToFString(A.Label), *LocationName(ToFString(A.At)));
		}
	}
	return FString::Printf(TEXT("%s: investigate, then file your report at the %s"), *ToFString(M->Title), *LocationName(ToFString(M->ReportAt)));
}

// ---------------------------------------------------------------- inbox

EKcCaseResult UKcCampaignSubsystem::ReplyToMessage(const FString& MessageId, const FString& ChoiceId, FString& OutError)
{
	UKcGameSubsystem* G = Game();
	if (!G || !G->HasGame()) return EKcCaseResult::NotActive;
	const kc::EReplyError E = kc::ReplyToMessage(G->EditState(), World, ToStd(MessageId), ToStd(ChoiceId));
	if (E != kc::EReplyError::None)
	{
		OutError = ToFString(kc::ToString(E));
		return EKcCaseResult::UnknownOption;
	}
	G->StateCheckpoint();
	return EKcCaseResult::Ok;
}

void UKcCampaignSubsystem::MarkMessageRead(const FString& MessageId)
{
	UKcGameSubsystem* G = Game();
	if (G && kc::MarkMessageRead(G->EditState(), ToStd(MessageId))) G->StateEdited();
}
