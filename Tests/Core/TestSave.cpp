#include "KcTest.h"

#include "KcRng.h"
#include "KcSave.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace kc;

static GameState SampleGame()
{
	GameState S = NewGame("Night\tOwl\nTest", EBackground::Military, EDifficulty::Expert, 99);
	S.Career.ApplyAssessment({ECompetency::KeyAccessControl, 77});
	S.Career.RecordMission("M001", true, 120);
	S.Career.Skills = {"interview_technique"};
	S.FieldManual = {"PC-room-theft", "PC-report-writing"};
	S.LearningHistory = {"M001: preserved footage before overwrite"};
	S.Settings.TextScale = 1.4;
	S.Settings.bReducedMotion = true;
	S.Checkpoint = "M001:debriefed";
	return S;
}

static std::string TempPath(const char* Name)
{
	const auto P = std::filesystem::temp_directory_path() / Name;
	std::error_code Ec;
	for (const char* Ext : {"", ".bak", ".tmp", ".corrupt"}) std::filesystem::remove(P.string() + Ext, Ec);
	return P.string();
}

TEST(Save_RoundTrip)
{
	const GameState S = SampleGame();
	GameState L;
	LoadResult Info;
	REQUIRE(DeserializeGame(SerializeGame(S), L, Info));
	CHECK(Info.Status == ELoadStatus::Ok);
	CHECK_EQ(Info.LoadedVersion, CurrentSaveVersion);
	CHECK(L.ProfileName == S.ProfileName);
	CHECK(L.Difficulty == S.Difficulty);
	CHECK(L.Seed == S.Seed);
	CHECK(L.Checkpoint == S.Checkpoint);
	CHECK(L.Career.Origin == EBackground::Military);
	CHECK_EQ(L.Career.Get(ECompetency::KeyAccessControl).Score, 77);
	CHECK_EQ(L.Career.Get(ECompetency::KeyAccessControl).Assessments, 1);
	CHECK(L.Career.PassedMissions == S.Career.PassedMissions);
	CHECK(L.Career.Skills == S.Career.Skills);
	CHECK(L.FieldManual == S.FieldManual);
	CHECK(L.LearningHistory == S.LearningHistory);
	CHECK(L.Settings.bReducedMotion);
	CHECK_NEAR(L.Settings.TextScale, 1.4, 1e-12);
	CHECK(SerializeGame(L) == SerializeGame(S));
}

TEST(Save_RejectsTamperingAndNewerVersions)
{
	std::string Text = SerializeGame(SampleGame());
	GameState L;
	LoadResult Info;
	std::string Bad = Text;
	Bad[Bad.find("career.level\t1") + 13] = '9';
	CHECK(!DeserializeGame(Bad, L, Info));
	CHECK(Info.Message == "Checksum mismatch");
	CHECK(!DeserializeGame("not a save", L, Info));
	// A newer version with a valid checksum is reported as unsupported, not corrupt.
	std::string Body = Text.substr(0, Text.rfind("checksum\t"));
	const std::string V = "version\t" + std::to_string(CurrentSaveVersion) + "\n";
	Body.replace(Body.find(V), V.size(), "version\t" + std::to_string(CurrentSaveVersion + 1) + "\n");
	uint64_t H = 0xCBF29CE484222325ULL;
	for (unsigned char C : Body)
	{
		H ^= C;
		H *= 0x100000001B3ULL;
	}
	char Sum[32];
	std::snprintf(Sum, sizeof(Sum), "%016llx", static_cast<unsigned long long>(H));
	CHECK(!DeserializeGame(Body + "checksum\t" + Sum + "\n", L, Info));
	CHECK(Info.Status == ELoadStatus::UnsupportedVersion);
}

TEST(Save_BackupRecovery)
{
	const std::string Path = TempPath("kc_save_test.kcsave");
	GameState A = SampleGame();
	REQUIRE(SaveGameToFile(Path, A));
	A.Checkpoint = "M002:start";
	REQUIRE(SaveGameToFile(Path, A)); // first save rotates into .bak
	{
		std::ofstream F(Path, std::ios::binary | std::ios::trunc);
		F << "garbage";
	}
	GameState L;
	const LoadResult R = LoadGameFromFile(Path, L);
	CHECK(R.Status == ELoadStatus::RecoveredFromBackup);
	CHECK(L.Checkpoint == "M001:debriefed");
	// Saving over a corrupt primary keeps the good backup and sets the corrupt file aside.
	REQUIRE(SaveGameToFile(Path, A));
	CHECK(std::filesystem::exists(Path + ".corrupt"));
	GameState L2;
	CHECK(LoadGameFromFile(Path, L2).Status == ELoadStatus::Ok);
	CHECK(L2.Checkpoint == "M002:start");
	GameState L3;
	CHECK(LoadGameFromFile(TempPath("kc_missing.kcsave"), L3).Status == ELoadStatus::NotFound);
}

TEST(Save_HandEditedSettingsAreClamped)
{
	GameState S = SampleGame();
	S.Settings.TextScale = 40.0;
	S.Settings.FrameCap = 77;
	GameState L;
	LoadResult Info;
	REQUIRE(DeserializeGame(SerializeGame(S), L, Info));
	CHECK_NEAR(L.Settings.TextScale, 1.6, 1e-12);
	CHECK_EQ(L.Settings.FrameCap, 0);
}

// Random byte flips, truncations and insertions never crash the loader and never load silently.
TEST(Save_FuzzNeverCrashesOrAcceptsDamage)
{
	const std::string Good = SerializeGame(SampleGame());
	Rng R(2026);
	int Accepted = 0;
	for (int i = 0; i < 3000; ++i)
	{
		std::string T = Good;
		const int Kind = R.UniformInt(0, 2);
		const size_t Pos = static_cast<size_t>(R.UniformInt(0, static_cast<int>(T.size()) - 1));
		if (Kind == 0) T[Pos] = static_cast<char>(R.UniformInt(0, 255));
		else if (Kind == 1) T.resize(Pos);
		else T.insert(Pos, 1, static_cast<char>(R.UniformInt(0, 255)));
		GameState L;
		LoadResult Info;
		if (DeserializeGame(T, L, Info) && T != Good) ++Accepted;
	}
	// Only edits the checksum cannot see (trailing newline variants) may load.
	CHECK(Accepted <= 30);
}
