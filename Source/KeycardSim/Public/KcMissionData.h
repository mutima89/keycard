// Mission content files (Data/Missions/*.json) -> MissionDef. Format: Docs/MissionData.md.
// Strict: unknown keys, wrong types and unknown enum names are errors, reported with file and line.
#pragma once

#include "KcApi.h"

#include "KcMission.h"

#include <string>
#include <utility>
#include <vector>

namespace kc
{
constexpr int MissionSchemaVersion = 1;

KC_API bool MissionFromJson(const std::string& Text, const std::string& SourceName, MissionDef& Out, std::vector<std::string>& OutErrors);
// Parses every (file name, text) pair, checks that "<id>.json" holds mission <id>, then runs
// ValidateCatalogue. Returns true only if there are no errors; Out holds what parsed either way.
KC_API bool LoadMissionCatalogue(const std::vector<std::pair<std::string, std::string>>& Files, MissionCatalogue& Out, std::vector<std::string>& OutErrors);
} // namespace kc
