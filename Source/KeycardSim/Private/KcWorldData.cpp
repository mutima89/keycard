// Content files for the living world: Data/People.json, Data/Story.json, Data/Events.json.
// Format: Docs/WorldData.md. Strict like mission files (KcJsonReader.h).
#include "KcWorld.h"

#include "KcJsonReader.h"
#include "KcLearning.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kc
{
namespace
{
using json::Reader;

void ReadCondition(const JsonValue& V, const std::string& Where, WorldCondition& C, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Strings("flags", C.Flags);
	R.Strings("notFlags", C.NotFlags);
	R.Strings("passed", C.Passed);
	R.Int("minWeek", C.MinWeek, false);
	R.Int("minLevel", C.MinLevel, false);
	R.Int("maxLevel", C.MaxLevel, false);
	R.Int("minClues", C.MinClues, false);
	R.Int("minStress", C.MinStress, false);
	R.Int("minIncidents", C.MinIncidents, false);
	for (const JsonValue& M : R.Items("metrics", false))
	{
		Reader MR(M, Where + " metric", Errors);
		WorldCondition::MetricTest T;
		std::string Name;
		MR.Str("metric", Name, true);
		if (!Name.empty() && !MetricFromName(Name, T.Metric)) MR.Error(M.Line, "unknown metric \"" + Name + "\"");
		const JsonValue* Below = MR.Get("below", false);
		const JsonValue* AtLeast = MR.Get("atLeast", false);
		if ((Below != nullptr) == (AtLeast != nullptr)) MR.Error(M.Line, "needs exactly one of below / atLeast");
		const JsonValue* Th = Below ? Below : AtLeast;
		if (Th && Th->IsNumber()) T.Threshold = Th->Number;
		else if (Th) MR.Error(Th->Line, "threshold must be a number");
		T.bBelow = Below != nullptr;
		C.Metrics.push_back(T);
	}
	for (const JsonValue& M : R.Items("relations", false))
	{
		Reader RR(M, Where + " relation", Errors);
		WorldCondition::RelationTest T;
		RR.Str("id", T.CharacterId, true);
		const JsonValue* Below = RR.Get("below", false);
		const JsonValue* AtLeast = RR.Get("atLeast", false);
		if ((Below != nullptr) == (AtLeast != nullptr)) RR.Error(M.Line, "needs exactly one of below / atLeast");
		const JsonValue* Th = Below ? Below : AtLeast;
		if (Th && Th->IsNumber()) T.Threshold = static_cast<int>(Th->Number);
		else if (Th) RR.Error(Th->Line, "threshold must be a number");
		T.bBelow = Below != nullptr;
		C.Relations.push_back(T);
	}
}

void CheckMetrics(const std::vector<MetricEffect>& Effects, const std::string& Where, std::vector<std::string>& Out)
{
	for (const MetricEffect& E : Effects)
	{
		if (!std::isfinite(E.Delta)) Out.push_back(Where + ": effect is not a number");
		else if (IsIndexMetric(E.Metric) && std::fabs(E.Delta) > 50.0) Out.push_back(Where + ": index effect above 50 points");
	}
}
} // namespace

void json::ReadWorldCondition(const JsonValue& V, const std::string& Where, WorldCondition& C, std::vector<std::string>& Errors)
{
	ReadCondition(V, Where, C, Errors);
}

std::vector<std::string> ValidatePeople(const PeopleCatalogue& P)
{
	static const std::vector<std::string> Outfits = {"", "lp", "housekeeping", "front_office", "executive", "guest", "police", "fire",
		"contractor", "engineering", "fnb", "hr", "insurer"};
	std::vector<std::string> Out;
	std::set<std::string> Ids;
	for (const CharacterDef& C : P.People)
	{
		const std::string W = "People.json '" + C.Id + "'";
		if (!json::IsLowerId(C.Id)) Out.push_back(W + ": id must be lower-case letters, digits or _");
		if (!Ids.insert(C.Id).second) Out.push_back(W + ": duplicate id");
		if (C.Name.empty() || C.Role.empty()) Out.push_back(W + ": needs a name and role");
		if (C.StartRelation < RelationMin || C.StartRelation > RelationMax) Out.push_back(W + ": startRelation outside -100..100");
		if (!C.Faction.empty() && C.Faction != "revenue" && C.Faction != "safety") Out.push_back(W + ": faction must be revenue or safety");
		if (std::find(Outfits.begin(), Outfits.end(), C.Outfit) == Outfits.end()) Out.push_back(W + ": unknown outfit '" + C.Outfit + "'");
		if (!C.Location.empty() && !FindLocation(C.Location)) Out.push_back(W + ": unknown location '" + C.Location + "'");
		if (!C.bStaff && (C.bExecutive || !C.Faction.empty())) Out.push_back(W + ": visitors take no part in office politics");
	}
	return Out;
}

bool PeopleFromJson(const std::string& Text, const std::string& SourceName, PeopleCatalogue& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	PeopleCatalogue P;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		for (const JsonValue& Item : R.Items("people", true))
		{
			CharacterDef C;
			Reader CR(Item, SourceName + " person", OutErrors);
			CR.Str("id", C.Id, true);
			CR.Str("name", C.Name, true);
			CR.Str("role", C.Role, true);
			CR.Str("department", C.Department, false);
			CR.Str("faction", C.Faction, false);
			CR.Text("bio", C.Bio, false);
			CR.Str("location", C.Location, false);
			CR.Str("avatar", C.Avatar, false);
			CR.Str("outfit", C.Outfit, false);
			CR.Bool("staff", C.bStaff);
			CR.Bool("executive", C.bExecutive);
			CR.Int("startRelation", C.StartRelation, false);
			P.People.push_back(C);
		}
	}
	Out = std::move(P);
	return OutErrors.size() == Before;
}

bool StoryFromJson(const std::string& Text, const std::string& SourceName, StoryCatalogue& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	StoryCatalogue S;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		R.Str("title", S.Title, true);
		for (const JsonValue& Item : R.Items("clues", true))
		{
			ClueDef C;
			Reader CR(Item, SourceName + " clue", OutErrors);
			CR.Str("id", C.Id, true);
			CR.Str("title", C.Title, true);
			CR.Text("summary", C.Summary, true);
			CR.Int("chapter", C.Chapter, true);
			S.Clues.push_back(C);
		}
	}
	Out = std::move(S);
	return OutErrors.size() == Before;
}

bool EventsFromJson(const std::string& Text, const std::string& SourceName, EventCatalogue& OutEvents, std::vector<FalloutTemplate>& OutFallout,
	std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	EventCatalogue Events;
	std::vector<FalloutTemplate> Fallout;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		for (const JsonValue& Item : R.Items("events", true))
		{
			EventDef E;
			Reader ER(Item, SourceName + " event", OutErrors);
			ER.Str("id", E.Id, true);
			std::string Cat;
			ER.Str("category", Cat, true);
			if (!Cat.empty() && !MessageCategoryFromName(Cat, E.Category)) ER.Error(Item.Line, "unknown category \"" + Cat + "\"");
			ER.Str("from", E.From, false);
			ER.Str("subject", E.Subject, true);
			ER.Text("body", E.Body, true);
			if (const JsonValue* W = ER.Get("when", false)) ReadCondition(*W, SourceName + " event '" + E.Id + "' when", E.When, OutErrors);
			ER.Dbl("probability", E.Probability, false);
			ER.Int("deadlineWeeks", E.DeadlineWeeks, false);
			ER.Str("defaultChoice", E.DefaultChoice, false);
			ER.Str("clue", E.Clue, false);
			ER.Str("headline", E.Headline, false);
			ER.Int("headlineTone", E.HeadlineTone, false);
			for (const JsonValue& C : ER.Items("choices", false))
			{
				EventChoiceDef Def;
				Reader CR(C, SourceName + " event '" + E.Id + "' choice", OutErrors);
				CR.Str("id", Def.Id, true);
				CR.Str("label", Def.Label, true);
				CR.Text("outcome", Def.Outcome, true);
				CR.Str("requiresSkill", Def.RequiresSkill, false);
				CR.Effects(Def.Effects);
				E.Choices.push_back(Def);
			}
			Events.Events.push_back(E);
		}
		for (const JsonValue& Item : R.Items("fallout", true))
		{
			FalloutTemplate T;
			Reader FR(Item, SourceName + " fallout", OutErrors);
			FR.Str("topic", T.Topic, true);
			FR.Text("description", T.Description, true);
			FR.Metrics("effects", T.Effects);
			Fallout.push_back(T);
		}
	}
	OutEvents = std::move(Events);
	OutFallout = std::move(Fallout);
	return OutErrors.size() == Before;
}

std::vector<std::string> ValidateWorld(const WorldContent& W, const MissionCatalogue* Missions)
{
	std::vector<std::string> Out = ValidatePeople(W.People);
	auto Person = [&](const std::string& Id) { return Id == "lp_desk" || Id == "player" || W.People.Find(Id) != nullptr; };

	std::set<std::string> ClueIds;
	if (W.Story.Title.empty()) Out.push_back("Story.json: missing title");
	for (const ClueDef& C : W.Story.Clues)
	{
		const std::string P = "Story.json '" + C.Id + "'";
		if (!json::IsLowerId(C.Id) || !ClueIds.insert(C.Id).second) Out.push_back(P + ": id missing, not lower-case or duplicated");
		if (C.Chapter < 1 || C.Chapter > 4) Out.push_back(P + ": chapter outside 1..4");
		if (C.Title.empty() || C.Summary.empty()) Out.push_back(P + ": needs a title and summary");
	}
	std::set<std::string> ObtainableClues;
	auto Clue = [&](const std::string& Id, const std::string& Where) {
		if (Id.empty()) return;
		if (!ClueIds.count(Id)) Out.push_back(Where + ": unknown clue '" + Id + "'");
		ObtainableClues.insert(Id);
	};
	auto Effects = [&](const EffectsDef& E, const std::string& Where) {
		CheckMetrics(E.Metrics, Where, Out);
		for (const RelationEffect& R : E.Relations)
		{
			if (!W.People.Find(R.CharacterId)) Out.push_back(Where + ": unknown character '" + R.CharacterId + "'");
		}
		Clue(E.Clue, Where);
		for (const ConsequenceDef& Cq : E.Schedules) CheckMetrics(Cq.Effects, Where, Out);
	};

	std::set<std::string> EventIds;
	for (const EventDef& E : W.Events.Events)
	{
		const std::string P = "Events.json '" + E.Id + "'";
		if (!json::IsLowerId(E.Id) || !EventIds.insert(E.Id).second) Out.push_back(P + ": id missing, not lower-case or duplicated");
		if (!E.From.empty() && !Person(E.From)) Out.push_back(P + ": unknown sender '" + E.From + "'");
		if (!(E.Probability > 0.0 && E.Probability <= 1.0)) Out.push_back(P + ": probability outside (0, 1]");
		if (E.DeadlineWeeks < 0 || E.DeadlineWeeks > 12) Out.push_back(P + ": deadlineWeeks outside 0..12");
		if (E.HeadlineTone < -1 || E.HeadlineTone > 1) Out.push_back(P + ": headlineTone must be -1, 0 or 1");
		if (E.Choices.empty() != (E.DeadlineWeeks == 0)) Out.push_back(P + ": choices and deadlineWeeks go together");
		if (!E.Choices.empty())
		{
			if (E.Choices.size() < 2) Out.push_back(P + ": needs at least two choices");
			bool bDefault = false;
			std::set<std::string> ChoiceIds;
			for (const EventChoiceDef& C : E.Choices)
			{
				const std::string CP = P + " choice '" + C.Id + "'";
				if (C.Id.empty() || !ChoiceIds.insert(C.Id).second) Out.push_back(CP + ": id missing or duplicated");
				if (C.Label.empty()) Out.push_back(CP + ": missing label");
				if (C.Effects.Stress < -30 || C.Effects.Stress > 30) Out.push_back(CP + ": stress change outside -30..30");
				bDefault = bDefault || C.Id == E.DefaultChoice;
				if (!C.RequiresSkill.empty() && !FindSkill(C.RequiresSkill)) Out.push_back(CP + ": unknown skill '" + C.RequiresSkill + "'");
				if (!C.RequiresSkill.empty() && C.Id == E.DefaultChoice) Out.push_back(CP + ": the default cannot need a skill");
				Effects(C.Effects, CP);
				for (const ConsequenceDef& Cq : C.Effects.Schedules)
				{
					if (Cq.Id.empty() || Cq.Description.empty() || Cq.DelayWeeks < 1 || !(Cq.Probability >= 0.0 && Cq.Probability <= 1.0))
					{
						Out.push_back(CP + ": consequence needs id, description, delay >= 1 and probability 0..1");
					}
				}
			}
			if (!bDefault) Out.push_back(P + ": defaultChoice must name one of its choices");
		}
		else if (!E.DefaultChoice.empty())
		{
			Out.push_back(P + ": defaultChoice without choices");
		}
		Clue(E.Clue, P);
		for (const WorldCondition::RelationTest& T : E.When.Relations)
		{
			if (!W.People.Find(T.CharacterId)) Out.push_back(P + ": condition on unknown character '" + T.CharacterId + "'");
		}
		if (Missions)
		{
			for (const std::string& M : E.When.Passed)
			{
				if (!Missions->Find(M)) Out.push_back(P + ": condition on unknown mission '" + M + "'");
			}
		}
	}

	bool bDefaultFallout = false;
	for (const FalloutTemplate& T : W.Fallout)
	{
		const std::string P = "Events.json fallout '" + T.Topic + "'";
		bDefaultFallout = bDefaultFallout || T.Topic == "default";
		if (T.Description.empty() || T.Effects.empty()) Out.push_back(P + ": needs a description and effects");
		CheckMetrics(T.Effects, P, Out);
	}
	if (!bDefaultFallout) Out.push_back("Events.json: fallout needs a \"default\" template");

	if (Missions)
	{
		for (const MissionDef& M : Missions->Missions)
		{
			if (!Person(M.Opening.From)) Out.push_back(M.Id + ": the opening comes from unknown person '" + M.Opening.From + "'");
			for (const ActionDef& A : M.Actions)
			{
				const std::string AP = M.Id + " action '" + A.Id + "'";
				if (!A.Person.empty() && !Person(A.Person)) Out.push_back(AP + ": unknown person '" + A.Person + "'");
				for (const DialogueLine& L : A.Lines)
				{
					if (!Person(L.Speaker)) Out.push_back(AP + ": unknown speaker '" + L.Speaker + "'");
				}
				Effects(A.Effects, AP);
			}
			for (const ContextNoteDef& C : M.Context)
			{
				if (!C.RelationId.empty() && !W.People.Find(C.RelationId)) Out.push_back(M.Id + ": context on unknown character '" + C.RelationId + "'");
			}
			for (const QuestionDef& Q : M.Questions)
			{
				if (!Q.Person.empty() && !Person(Q.Person)) Out.push_back(M.Id + " question '" + Q.Id + "': unknown person '" + Q.Person + "'");
				for (const OptionDef& O : Q.Options)
				{
					const std::string OP = M.Id + " option '" + O.Id + "'";
					Effects(O.Effects, OP);
					if (!O.Reply.Speaker.empty() && !Person(O.Reply.Speaker)) Out.push_back(OP + ": unknown speaker '" + O.Reply.Speaker + "'");
				}
			}
		}
		for (const MissionDef& M : Missions->Missions)
		{
			if (M.RequiresClues > static_cast<int>(ObtainableClues.size()))
			{
				Out.push_back(M.Id + ": requires " + std::to_string(M.RequiresClues) + " clues but only " + std::to_string(ObtainableClues.size()) + " can be found");
			}
		}
		for (const std::string& Id : ClueIds)
		{
			if (!ObtainableClues.count(Id)) Out.push_back("Story.json '" + Id + "': nothing in the game gives this clue");
		}
	}
	return Out;
}
} // namespace kc
