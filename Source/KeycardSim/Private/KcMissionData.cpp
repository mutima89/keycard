#include "KcMissionData.h"

#include "KcJson.h"
#include "KcJsonReader.h"
#include "KcLpMath.h"
#include "KcWorld.h"

#include <cctype>
#include <cmath>
#include <set>

namespace kc
{
namespace json
{
bool ParseDoc(const std::string& Text, const std::string& SourceName, JsonValue& Doc, std::vector<std::string>& OutErrors)
{
	std::string JsonError;
	if (!ParseJson(Text, Doc, JsonError))
	{
		OutErrors.push_back(SourceName + ": " + JsonError);
		return false;
	}
	return true;
}

void Schema(Reader& R)
{
	int S = 0;
	R.Int("schema", S, true);
	if (S != 1) R.Error(R.Obj.Line, "schema must be 1");
}

bool IsLowerId(const std::string& Id)
{
	if (Id.empty()) return false;
	for (char Ch : Id)
	{
		if (!(std::islower(static_cast<unsigned char>(Ch)) || std::isdigit(static_cast<unsigned char>(Ch)) || Ch == '_')) return false;
	}
	return true;
}

void ReadConsequence(const JsonValue& V, const std::string& Where, ConsequenceDef& C, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("id", C.Id, true);
	R.Text("description", C.Description, true);
	R.Int("delayWeeks", C.DelayWeeks, true);
	R.Dbl("probability", C.Probability, true);
	R.Metrics("effects", C.Effects);
	R.Str("flagIfOccurs", C.FlagIfOccurs, false);
	R.Str("mitigatedBy", C.MitigatingFlag, false);
	R.Dbl("mitigatedProbabilityFactor", C.MitigatedProbabilityFactor, false);
	R.Dbl("mitigatedSeverityFactor", C.MitigatedSeverityFactor, false);
}

void Reader::Effects(EffectsDef& E)
{
	Metrics("effects", E.Metrics);
	Relations("relations", E.Relations);
	Int("stress", E.Stress, false);
	Strings("setsFlags", E.SetsFlags);
	Str("clue", E.Clue, false);
	for (const JsonValue& S : Items("schedules", false))
	{
		ConsequenceDef Def;
		ReadConsequence(S, Where + " schedule", Def, Errors);
		E.Schedules.push_back(Def);
	}
}
} // namespace json

namespace
{
using json::Reader;

void ReadLine(const JsonValue& V, const std::string& Where, DialogueLine& L, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("speaker", L.Speaker, true);
	R.Text("text", L.Text, true);
}

void ReadEnum(Reader& R, const char* Key, bool bRequired, const std::string& Default, bool (*Parse)(const std::string&, ERubric&), ERubric& Out)
{
	std::string S = Default;
	R.Str(Key, S, bRequired);
	if (!S.empty() && !Parse(S, Out)) R.Error(R.Obj.Line, std::string("unknown ") + Key + " \"" + S + "\"");
}

void ReadCompetency(Reader& R, ECompetency& Out)
{
	std::string S;
	R.Str("competency", S, true);
	if (!S.empty() && !CompetencyFromName(S, Out)) R.Error(R.Obj.Line, "unknown competency \"" + S + "\"");
}

void ReadAction(const JsonValue& V, const std::string& Where, ActionDef& A, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("id", A.Id, true);
	R.Str("label", A.Label, true);
	std::string Kind;
	R.Str("kind", Kind, true);
	if (!Kind.empty() && !ActionKindFromName(Kind, A.Kind)) R.Error(V.Line, "unknown action kind \"" + Kind + "\"");
	R.Str("at", A.At, true);
	R.Str("person", A.Person, false);
	R.Int("minutes", A.Minutes, false);
	R.Strings("requires", A.Requires);
	R.Bool("required", A.bRequired);
	R.Str("findingTitle", A.FindingTitle, false);
	R.Text("finding", A.Finding, true);
	for (const JsonValue& L : R.Items("lines", false))
	{
		DialogueLine D;
		ReadLine(L, Where + " line", D, Errors);
		A.Lines.push_back(D);
	}
	R.Strings("columns", A.Columns);
	for (const JsonValue& Row : R.Items("rows", false))
	{
		std::vector<std::string> Cells;
		if (!Row.IsArray()) R.Error(Row.Line, "a row must be an array of strings");
		for (const JsonValue& C : Row.Items)
		{
			if (C.IsString()) Cells.push_back(C.String);
			else R.Error(C.Line, "table cells must be strings");
		}
		A.Rows.push_back(Cells);
	}
	R.Clock("expiresAt", A.ExpiresAt, false);
	R.Text("expiredFinding", A.ExpiredFinding, false);
	R.Str("evidence", A.Evidence, false);
	R.Text("custody", A.Custody, false);
	R.Dbl("cost", A.Cost, false);
	R.Effects(A.Effects);
}

void ReadOption(const JsonValue& V, const std::string& Where, OptionDef& O, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("id", O.Id, true);
	R.Text("label", O.Label, true);
	R.Text("feedback", O.Feedback, false);
	R.Int("score", O.Score, false);
	R.Bool("correct", O.bCorrect);
	R.Bool("critical", O.bCritical);
	R.Dbl("cost", O.Cost, false);
	R.Bool("escalates", O.bEscalates);
	if (const JsonValue* Reply = R.Get("reply", false)) ReadLine(*Reply, Where + " reply", O.Reply, Errors);
	R.Effects(O.Effects);
}

void ReadQuestion(const JsonValue& V, const std::string& Where, QuestionDef& Q, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("id", Q.Id, true);
	R.Text("prompt", Q.Prompt, true);
	std::string Kind = "single";
	R.Str("kind", Kind, false);
	if (Kind == "single") Q.Kind = EQuestionKind::Single;
	else if (Kind == "multi") Q.Kind = EQuestionKind::Multi;
	else if (Kind == "numeric") Q.Kind = EQuestionKind::Numeric;
	else if (Kind == "order") Q.Kind = EQuestionKind::Order;
	else R.Error(V.Line, "kind must be \"single\", \"multi\", \"numeric\" or \"order\"");
	ReadEnum(R, "dimension", true, "", RubricFromName, Q.Dimension);
	ReadCompetency(R, Q.Competency);
	R.Int("weight", Q.Weight, true);
	R.Str("section", Q.Section, false);
	R.Str("at", Q.At, false);
	R.Str("person", Q.Person, false);
	R.Strings("requires", Q.Requires);
	R.Text("explanation", Q.Explanation, Q.Kind == EQuestionKind::Numeric || Q.Kind == EQuestionKind::Order);
	if (Q.Kind == EQuestionKind::Numeric)
	{
		const JsonValue* F = R.Get("formula", false);
		if (F)
		{
			Reader FR(*F, Where + " formula", Errors);
			FR.Str("name", Q.Formula, true);
			if (const JsonValue* In = FR.Get("inputs", true))
			{
				if (!In->IsObject()) FR.Error(In->Line, "\"inputs\" must be an object of name: number");
				for (const auto& M : In->Members)
				{
					if (!M.second.IsNumber()) FR.Error(M.second.Line, "input " + M.first + " must be a number");
					else Q.FormulaInputs.push_back({M.first, M.second.Number});
				}
			}
			std::string Err;
			if (!Q.Formula.empty() && !lp::EvaluateFormula(Q.Formula, Q.FormulaInputs, Q.Answer, Err)) FR.Error(F->Line, Err);
		}
		R.Dbl("answer", Q.Answer, F == nullptr);
		R.Dbl("tolerance", Q.Tolerance, false);
		R.Str("unit", Q.Unit, false);
		for (const JsonValue& Item : R.Items("mistakes", false))
		{
			NumericMistake Mk;
			Reader MR(Item, Where + " mistake", Errors);
			MR.Dbl("value", Mk.Value, true);
			MR.Text("feedback", Mk.Feedback, true);
			Q.Mistakes.push_back(Mk);
		}
	}
	for (const JsonValue& O : R.Items("options", Q.Kind != EQuestionKind::Numeric))
	{
		OptionDef Def;
		ReadOption(O, Where + " option", Def, Errors);
		Q.Options.push_back(Def);
	}
}

void ReadCheck(const JsonValue& V, const std::string& Where, CheckDef& C, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Str("id", C.Id, true);
	R.Text("label", C.Label, true);
	std::string Kind;
	R.Str("kind", Kind, true);
	if (Kind == "done") C.Kind = ECheckKind::Done;
	else if (Kind == "notDone") C.Kind = ECheckKind::NotDone;
	else if (Kind == "before") C.Kind = ECheckKind::Before;
	else if (Kind == "byMinute") C.Kind = ECheckKind::ByMinute;
	else if (!Kind.empty()) R.Error(V.Line, "kind must be \"done\", \"notDone\", \"before\" or \"byMinute\"");
	R.Str("action", C.Action, true);
	R.Str("other", C.Other, C.Kind == ECheckKind::Before);
	R.Clock("by", C.Minute, C.Kind == ECheckKind::ByMinute);
	ReadEnum(R, "dimension", true, "", RubricFromName, C.Dimension);
	ReadCompetency(R, C.Competency);
	R.Int("weight", C.Weight, true);
	R.Bool("critical", C.bCritical);
	R.Text("pass", C.Pass, true);
	R.Text("fail", C.Fail, true);
}

void ReadContext(const JsonValue& V, const std::string& Where, ContextNoteDef& C, std::vector<std::string>& Errors)
{
	Reader R(V, Where, Errors);
	R.Text("text", C.Text, true);
	std::string IfFlag, IfNotFlag, Metric;
	R.Str("ifRelation", C.RelationId, false);
	const JsonValue* RelBelow = R.Get("relationBelow", false);
	const JsonValue* RelAtLeast = R.Get("relationAtLeast", false);
	if (!C.RelationId.empty())
	{
		if ((RelBelow != nullptr) == (RelAtLeast != nullptr)) R.Error(V.Line, "ifRelation needs exactly one of relationBelow / relationAtLeast");
		const JsonValue* T = RelBelow ? RelBelow : RelAtLeast;
		if (T && !T->IsNumber()) R.Error(T->Line, "relation threshold must be a number");
		else if (T) C.RelationThreshold = static_cast<int>(T->Number);
		C.bRelationBelow = RelBelow != nullptr;
	}
	else if (RelBelow || RelAtLeast)
	{
		R.Error(V.Line, "relationBelow / relationAtLeast need ifRelation");
	}
	R.Int("ifClues", C.MinClues, false);
	R.Str("ifFlag", IfFlag, false);
	R.Str("ifNotFlag", IfNotFlag, false);
	R.Str("ifMetric", Metric, false);
	const JsonValue* Below = R.Get("below", false);
	const JsonValue* AtLeast = R.Get("atLeast", false);
	if (!IfFlag.empty() && !IfNotFlag.empty()) R.Error(V.Line, "use either ifFlag or ifNotFlag");
	C.Flag = !IfFlag.empty() ? IfFlag : IfNotFlag;
	C.bWhenAbsent = !IfNotFlag.empty();
	if (!Metric.empty())
	{
		C.bUseMetric = true;
		if (!MetricFromName(Metric, C.Metric)) R.Error(V.Line, "unknown metric \"" + Metric + "\"");
		if ((Below != nullptr) == (AtLeast != nullptr)) R.Error(V.Line, "ifMetric needs exactly one of below / atLeast");
		const JsonValue* T = Below ? Below : AtLeast;
		if (T && !T->IsNumber()) R.Error(T->Line, "threshold must be a number");
		else if (T) C.Threshold = T->Number;
		C.bBelow = Below != nullptr;
	}
	else if (Below || AtLeast)
	{
		R.Error(V.Line, "below / atLeast need ifMetric");
	}
}

// Clock times earlier than the start are on the next day (a case that runs past midnight).
int AfterStart(int Minute, int Start)
{
	return Minute > 0 && Minute < Start ? Minute + 1440 : Minute;
}
} // namespace

bool MissionFromJson(const std::string& Text, const std::string& SourceName, MissionDef& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	MissionDef M;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		R.Str("id", M.Id, true);
		R.Str("title", M.Title, true);
		std::string Kind = "mission";
		R.Str("kind", Kind, false);
		if (Kind == "mission") M.Kind = EMissionKind::Mission;
		else if (Kind == "crisis") M.Kind = EMissionKind::Crisis;
		else if (Kind == "capstone") M.Kind = EMissionKind::Capstone;
		else R.Error(Doc.Line, "kind must be \"mission\", \"crisis\" or \"capstone\"");
		R.Str("topic", M.Topic, true);
		R.Str("property", M.Property, false);
		R.Int("minLevel", M.MinLevel, true);
		R.Bool("optional", M.bOptional);
		R.Bool("inspiredByRealEvents", M.bInspiredByRealEvents);
		R.Strings("requiresPassed", M.RequiresPassed);
		R.Strings("requiresFlags", M.RequiresFlags);
		R.Strings("excludedByFlags", M.ExcludedByFlags);
		R.Int("requiresClues", M.RequiresClues, false);
		if (const JsonValue* O = R.Get("opening", true))
		{
			Reader OR(*O, SourceName + " opening", OutErrors);
			std::string Channel;
			OR.Str("channel", Channel, true);
			if (!Channel.empty() && !ChannelFromName(Channel, M.Opening.Channel)) OR.Error(O->Line, "unknown channel \"" + Channel + "\"");
			OR.Str("from", M.Opening.From, true);
			OR.Str("at", M.Opening.At, true);
			OR.Text("text", M.Opening.Text, true);
		}
		R.Text("briefing", M.Briefing, true);
		for (const JsonValue& C : R.Items("context", false))
		{
			ContextNoteDef Def;
			ReadContext(C, SourceName + " context", Def, OutErrors);
			M.Context.push_back(Def);
		}
		R.Strings("learningObjectives", M.LearningObjectives, true);
		R.Clock("start", M.StartMinute, true);
		for (const JsonValue& A : R.Items("actions", true))
		{
			ActionDef Def;
			ReadAction(A, SourceName + " action", Def, OutErrors);
			Def.ExpiresAt = AfterStart(Def.ExpiresAt, M.StartMinute);
			M.Actions.push_back(Def);
		}
		for (const JsonValue& Q : R.Items("questions", true))
		{
			QuestionDef Def;
			ReadQuestion(Q, SourceName + " question", Def, OutErrors);
			M.Questions.push_back(Def);
		}
		for (const JsonValue& C : R.Items("checks", true))
		{
			CheckDef Def;
			ReadCheck(C, SourceName + " check", Def, OutErrors);
			Def.Minute = AfterStart(Def.Minute, M.StartMinute);
			M.Checks.push_back(Def);
		}
		R.Strings("bestOrder", M.BestOrder, true);
		if (const JsonValue* D = R.Get("debrief", true))
		{
			Reader DR(*D, SourceName + " debrief", OutErrors);
			DR.Text("whatHappened", M.Debrief.WhatHappened, true);
			DR.Strings("bestPractice", M.Debrief.BestPractice, true);
			DR.Text("why", M.Debrief.Why, true);
			DR.Strings("commonMistakes", M.Debrief.CommonMistakes, true);
		}
		R.Strings("procedureCards", M.ProcedureCards, true);
		R.Str("reportAt", M.ReportAt, false);
		R.Int("passMark", M.PassMark, false);
		R.Int("xp", M.Xp, false);
		R.Int("weeks", M.Weeks, false);
		R.Text("lesson", M.Lesson, false);
		R.Int("deadlineWeeks", M.DeadlineWeeks, false);
		if (const JsonValue* E = R.Get("escalation", false))
		{
			Reader ER(*E, SourceName + " escalation", OutErrors);
			ER.Metrics("effects", M.EscalationEffects);
			ER.Text("text", M.EscalationText, true);
		}
	}
	Out = std::move(M);
	return OutErrors.size() == Before;
}

bool LoadMissionCatalogue(const std::vector<std::pair<std::string, std::string>>& Files, MissionCatalogue& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	MissionCatalogue C;
	for (const auto& F : Files)
	{
		MissionDef M;
		if (!MissionFromJson(F.second, F.first, M, OutErrors)) continue;
		std::string Base = F.first;
		const size_t Slash = Base.find_last_of("/\\");
		if (Slash != std::string::npos) Base = Base.substr(Slash + 1);
		if (Base != M.Id + ".json") OutErrors.push_back(F.first + ": file name must be " + M.Id + ".json");
		C.Missions.push_back(std::move(M));
	}
	for (const std::string& E : ValidateCatalogue(C)) OutErrors.push_back(E);
	Out = std::move(C);
	return OutErrors.size() == Before;
}
} // namespace kc
