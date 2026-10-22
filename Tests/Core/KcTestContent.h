// Loads the shipped content (Data/) once for the content and campaign tests.
#pragma once

#include "KcLearning.h"
#include "KcMission.h"
#include "KcMissionData.h"
#include "KcWorld.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef KC_DATA_DIR
#error "KC_DATA_DIR must point at Data/"
#endif

struct KcContent
{
	kc::MissionCatalogue Missions;
	kc::WorldContent World;
	std::vector<kc::ProcedureCard> Cards;
	std::vector<kc::GlossaryTerm> Terms;
	std::vector<kc::EndingDef> Endings;
	std::vector<std::string> Errors;
};

inline std::string KcReadFile(const std::filesystem::path& P)
{
	std::ifstream In(P, std::ios::binary);
	std::ostringstream S;
	S << In.rdbuf();
	return S.str();
}

inline const KcContent& LoadContent()
{
	static const KcContent C = [] {
		KcContent Out;
		const std::filesystem::path Dir(KC_DATA_DIR);
		std::vector<std::pair<std::string, std::string>> Files;
		for (const auto& E : std::filesystem::directory_iterator(Dir / "Missions"))
		{
			if (E.path().extension() == ".json") Files.emplace_back(E.path().filename().string(), KcReadFile(E.path()));
		}
		std::sort(Files.begin(), Files.end());
		kc::LoadMissionCatalogue(Files, Out.Missions, Out.Errors);
		const auto Read = [&](const char* Name) { return KcReadFile(Dir / Name); };
		kc::PeopleFromJson(Read("People.json"), "People.json", Out.World.People, Out.Errors);
		kc::StoryFromJson(Read("Story.json"), "Story.json", Out.World.Story, Out.Errors);
		kc::EventsFromJson(Read("Events.json"), "Events.json", Out.World.Events, Out.World.Fallout, Out.Errors);
		kc::FieldManualFromJson(Read("FieldManual.json"), "FieldManual.json", Out.Cards, Out.Errors);
		kc::GlossaryFromJson(Read("Glossary.json"), "Glossary.json", Out.Terms, Out.Errors);
		kc::EndingsFromJson(Read("Endings.json"), "Endings.json", Out.Endings, Out.Errors);
		for (const std::string& E : kc::ValidateWorld(Out.World, &Out.Missions)) Out.Errors.push_back(E);
		for (const std::string& E : kc::ValidateFieldManual(Out.Cards, &Out.Missions)) Out.Errors.push_back(E);
		for (const std::string& E : kc::ValidateGlossary(Out.Terms, &Out.Missions)) Out.Errors.push_back(E);
		for (const std::string& E : kc::ValidateEndings(Out.Endings)) Out.Errors.push_back(E);
		return Out;
	}();
	return C;
}
