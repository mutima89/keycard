// People at Veldane and how they feel about the player. Content: Data/People.json.
// Relationships are memory, not a score to grind: they change briefings, help, inbox events and
// story, and never gate promotion (Docs/GameDesign.md "People who remember").
#pragma once

#include "KcApi.h"

#include <string>
#include <vector>

namespace kc
{
constexpr int RelationMin = -100;
constexpr int RelationMax = 100;
// Label bands (inclusive lower bounds): Ally >= 50, Friendly >= 20, Neutral > -20, Wary > -50, else Hostile.
constexpr int RelationAlly = 50;
constexpr int RelationFriendly = 20;
constexpr int RelationWary = -20;
constexpr int RelationHostile = -50;

KC_API const char* RelationLabel(int Value);

struct CharacterDef
{
	std::string Id;          // lower-case key used by content ("gm_okonkwo")
	std::string Name;
	std::string Role;
	std::string Department;
	std::string Faction;     // "revenue", "safety" or empty: the pull in office politics
	std::string Bio;
	std::string Location;    // where they are usually found (location id, KcMission.h), may be empty
	std::string Avatar;      // Rocketbox avatar folder (Scripts/FetchCharacters.py), empty: no body
	std::string Outfit;      // uniform texture set: "lp", "housekeeping", "front_office", "executive", "guest", "police", "fire", "contractor", "engineering", "fnb"
	bool bStaff = true;      // false for guests, police, insurers and other visitors
	bool bExecutive = false;
	int StartRelation = 0;
};

struct PeopleCatalogue
{
	std::vector<CharacterDef> People;
	KC_API const CharacterDef* Find(const std::string& Id) const;
};

struct RelationEffect
{
	std::string CharacterId;
	int Delta = 0;
};

struct Relation
{
	std::string CharacterId;
	int Value = 0;
};

// Unique lower-case ids, names and roles present, start relation in range, known faction and outfit.
KC_API std::vector<std::string> ValidatePeople(const PeopleCatalogue& P);
} // namespace kc
