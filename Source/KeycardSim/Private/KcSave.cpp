#include "KcSave.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace kc
{
// Paths are UTF-8 throughout the simulation layer.
static fs::path ToPath(const std::string& Utf8)
{
#if defined(__cpp_char8_t)
	return fs::path(std::u8string(Utf8.begin(), Utf8.end()));
#else
	return fs::u8path(Utf8);
#endif
}

const char* ToString(ELoadStatus S)
{
	switch (S)
	{
	case ELoadStatus::Ok: return "Ok";
	case ELoadStatus::RecoveredFromBackup: return "Recovered from backup";
	case ELoadStatus::NotFound: return "Not found";
	case ELoadStatus::Corrupt: return "Corrupt";
	case ELoadStatus::UnsupportedVersion: return "Unsupported version";
	}
	return "?";
}

static const char* const Magic = "KEYCARD-SAVE";

static uint64_t Fnv1a64(const std::string& S)
{
	uint64_t H = 0xCBF29CE484222325ULL;
	for (unsigned char C : S)
	{
		H ^= C;
		H *= 0x100000001B3ULL;
	}
	return H;
}

static std::string Escape(const std::string& V)
{
	std::string O;
	O.reserve(V.size());
	for (char C : V)
	{
		switch (C)
		{
		case '\\': O += "\\\\"; break;
		case '\t': O += "\\t"; break;
		case '\n': O += "\\n"; break;
		case '\r': O += "\\r"; break;
		default: O += C;
		}
	}
	return O;
}

static std::string Unescape(const std::string& V)
{
	std::string O;
	O.reserve(V.size());
	for (size_t i = 0; i < V.size(); ++i)
	{
		if (V[i] == '\\' && i + 1 < V.size())
		{
			const char N = V[++i];
			O += N == 't' ? '\t' : N == 'n' ? '\n' : N == 'r' ? '\r' : N;
		}
		else
		{
			O += V[i];
		}
	}
	return O;
}

// ---------------- writer ----------------
class KvWriter
{
public:
	std::string Body;

	void Put(const std::string& K, const std::string& V) { Body += K + "\t" + Escape(V) + "\n"; }
	void Int(const std::string& K, long long V) { Put(K, std::to_string(V)); }
	void U64(const std::string& K, uint64_t V)
	{
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%" PRIu64, V);
		Put(K, Buf);
	}
	void Dbl(const std::string& K, double V)
	{
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.17g", V);
		Put(K, Buf);
	}
	void Bool(const std::string& K, bool V) { Put(K, V ? "1" : "0"); }
	void List(const std::string& K, const std::vector<std::string>& V)
	{
		Int(K + ".n", static_cast<long long>(V.size()));
		for (size_t i = 0; i < V.size(); ++i) Put(K + "." + std::to_string(i), V[i]);
	}
};

// ---------------- reader ----------------
class KvReader
{
public:
	std::map<std::string, std::string> Map;
	bool bOk = true;

	bool Has(const std::string& K) const { return Map.count(K) != 0; }
	std::string Str(const std::string& K, const std::string& Def = std::string()) const
	{
		const auto It = Map.find(K);
		return It == Map.end() ? Def : It->second;
	}
	long long Int(const std::string& K, long long Def = 0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const long long V = std::strtoll(It->second.c_str(), &End, 10);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return V;
	}
	uint64_t U64(const std::string& K, uint64_t Def = 0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const unsigned long long V = std::strtoull(It->second.c_str(), &End, 10);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return static_cast<uint64_t>(V);
	}
	double Dbl(const std::string& K, double Def = 0.0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const double V = std::strtod(It->second.c_str(), &End);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return V;
	}
	bool Bool(const std::string& K, bool Def = false) { return Int(K, Def ? 1 : 0) != 0; }
	template <typename E>
	E Enum(const std::string& K, E Def, int Max)
	{
		const long long V = Int(K, static_cast<long long>(Def));
		if (V < 0 || V > Max)
		{
			bOk = false;
			return Def;
		}
		return static_cast<E>(V);
	}
	// Caps element counts so a hostile/corrupt file cannot request huge allocations.
	int Count(const std::string& K)
	{
		const long long N = Int(K + ".n", 0);
		if (N < 0 || N > 100000)
		{
			bOk = false;
			return 0;
		}
		return static_cast<int>(N);
	}
	std::vector<std::string> List(const std::string& K)
	{
		std::vector<std::string> V;
		const int N = Count(K);
		for (int i = 0; i < N; ++i) V.push_back(Str(K + "." + std::to_string(i)));
		return V;
	}
};


// ---------------- sections ----------------
static void WriteCareer(KvWriter& W, const CareerState& C)
{
	W.Int("career.level", C.Level);
	W.Int("career.xp", C.Xp);
	W.Int("career.origin", static_cast<int>(C.Origin));
	for (int i = 0; i < CompetencyCount; ++i)
	{
		W.Int("career.comp." + std::to_string(i) + ".s", C.Competencies[static_cast<size_t>(i)].Score);
		W.Int("career.comp." + std::to_string(i) + ".a", C.Competencies[static_cast<size_t>(i)].Assessments);
	}
	W.List("career.passed", C.PassedMissions);
	W.List("career.attempted", C.AttemptedMissions);
	W.List("career.skills", C.Skills);
}

static void ReadCareer(KvReader& R, CareerState& C)
{
	C.Level = static_cast<int>(R.Int("career.level", 1));
	if (C.Level < 1 || C.Level > TopLevel) R.bOk = false;
	C.Xp = static_cast<int>(R.Int("career.xp", 0));
	if (C.Xp < 0) R.bOk = false;
	C.Origin = R.Enum<EBackground>("career.origin", EBackground::FrontOffice, BackgroundCount - 1);
	for (int i = 0; i < CompetencyCount; ++i)
	{
		CompetencyRecord& Rec = C.Competencies[static_cast<size_t>(i)];
		Rec.Score = static_cast<int>(R.Int("career.comp." + std::to_string(i) + ".s", 0));
		Rec.Assessments = static_cast<int>(R.Int("career.comp." + std::to_string(i) + ".a", 0));
		if (Rec.Score < 0 || Rec.Score > 100 || Rec.Assessments < 0) R.bOk = false;
	}
	C.PassedMissions = R.List("career.passed");
	C.AttemptedMissions = R.List("career.attempted");
	C.Skills = R.List("career.skills");
}

std::string SerializeGame(const GameState& S)
{
	KvWriter W;
	W.Body = std::string(Magic) + "\n";
	W.Int("version", CurrentSaveVersion);
	W.Put("profile.name", S.ProfileName);
	W.Int("profile.difficulty", static_cast<int>(S.Difficulty));
	W.U64("profile.seed", S.Seed);
	W.Put("checkpoint", S.Checkpoint);
	W.Dbl("settings.mouse", S.Settings.MouseSensitivity);
	W.Dbl("settings.text", S.Settings.TextScale);
	W.Dbl("settings.volume", S.Settings.MasterVolume);
	W.Bool("settings.reducedMotion", S.Settings.bReducedMotion);
	W.Bool("settings.hints", S.Settings.bHints);
	W.Bool("settings.fullscreen", S.Settings.bFullscreen);
	W.Int("settings.gfx", S.Settings.GraphicsQuality);
	W.Int("settings.upscale", S.Settings.Upscaling);
	W.Int("settings.fps", S.Settings.FrameCap);
	W.Bool("settings.vsync", S.Settings.bVSync);
	W.Bool("settings.invertY", S.Settings.bInvertY);
	W.Dbl("settings.pad", S.Settings.GamepadSensitivity);
	W.Bool("settings.colourSafe", S.Settings.bColourSafe);
	WriteCareer(W, S.Career);
	W.List("manual", S.FieldManual);
	W.List("learning", S.LearningHistory);
	W.List("achievements", S.Achievements);
	char Sum[32];
	std::snprintf(Sum, sizeof(Sum), "%016" PRIx64, Fnv1a64(W.Body));
	return W.Body + "checksum\t" + Sum + "\n";
}

// Migrations: one function per version step, run in order on the raw key/value map before the
// fields are read (Docs/SaveFormat.md). Version 1 is the first format, so there are none yet.
static void Migrate(KvReader& /*R*/, int /*FromVersion*/, LoadResult& /*Info*/) {}

bool DeserializeGame(const std::string& Text, GameState& Out, LoadResult& Info)
{
	Info = LoadResult();
	Info.Status = ELoadStatus::Corrupt;
	const std::string Header = std::string(Magic) + "\n";
	if (Text.compare(0, Header.size(), Header) != 0)
	{
		Info.Message = "Missing save header";
		return false;
	}
	const size_t SumPos = Text.rfind("checksum\t");
	if (SumPos == std::string::npos || (SumPos > 0 && Text[SumPos - 1] != '\n'))
	{
		Info.Message = "Missing checksum";
		return false;
	}
	std::string Expected = Text.substr(SumPos + 9);
	while (!Expected.empty() && (Expected.back() == '\n' || Expected.back() == '\r')) Expected.pop_back();
	char Actual[32];
	std::snprintf(Actual, sizeof(Actual), "%016" PRIx64, Fnv1a64(Text.substr(0, SumPos)));
	if (Expected != Actual)
	{
		Info.Message = "Checksum mismatch";
		return false;
	}

	KvReader R;
	std::istringstream In(Text.substr(Header.size(), SumPos - Header.size()));
	std::string Line;
	while (std::getline(In, Line))
	{
		const size_t Tab = Line.find('\t');
		if (Tab == std::string::npos)
		{
			Info.Message = "Malformed line";
			return false;
		}
		R.Map[Line.substr(0, Tab)] = Unescape(Line.substr(Tab + 1));
	}

	Info.LoadedVersion = static_cast<int>(R.Int("version", 0));
	if (Info.LoadedVersion > CurrentSaveVersion)
	{
		Info.Status = ELoadStatus::UnsupportedVersion;
		Info.Message = "Save is from a newer version of Keycard";
		return false;
	}
	if (Info.LoadedVersion < 1)
	{
		Info.Message = "Invalid version";
		return false;
	}
	Migrate(R, Info.LoadedVersion, Info);

	GameState S;
	S.ProfileName = R.Str("profile.name", "Officer");
	S.Difficulty = R.Enum<EDifficulty>("profile.difficulty", EDifficulty::Professional, 2);
	S.Seed = R.U64("profile.seed", 1);
	S.Checkpoint = R.Str("checkpoint");
	S.Settings.MouseSensitivity = R.Dbl("settings.mouse", 1.0);
	S.Settings.TextScale = R.Dbl("settings.text", 1.0);
	S.Settings.MasterVolume = R.Dbl("settings.volume", 0.8);
	S.Settings.bReducedMotion = R.Bool("settings.reducedMotion", false);
	S.Settings.bHints = R.Bool("settings.hints", true);
	S.Settings.bFullscreen = R.Bool("settings.fullscreen", true);
	S.Settings.GraphicsQuality = static_cast<int>(R.Int("settings.gfx", 2));
	S.Settings.Upscaling = static_cast<int>(R.Int("settings.upscale", 0));
	S.Settings.FrameCap = static_cast<int>(R.Int("settings.fps", 0));
	S.Settings.bVSync = R.Bool("settings.vsync", true);
	S.Settings.bInvertY = R.Bool("settings.invertY", false);
	S.Settings.GamepadSensitivity = R.Dbl("settings.pad", 1.0);
	S.Settings.bColourSafe = R.Bool("settings.colourSafe", false);
	S.Settings = ClampSettings(S.Settings); // a hand-edited save must not break the UI
	ReadCareer(R, S.Career);
	S.FieldManual = R.List("manual");
	S.LearningHistory = R.List("learning");
	S.Achievements = R.List("achievements");
	if (!R.bOk)
	{
		Info.Message = "Invalid field values";
		return false;
	}
	Out = std::move(S);
	Info.Status = ELoadStatus::Ok;
	return true;
}

std::string BackupPath(const std::string& Path)
{
	return Path + ".bak";
}

static bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(ToPath(Path), std::ios::binary);
	if (!F) return false;
	std::ostringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

static bool IsValidSaveFile(const std::string& Path)
{
	std::string Text;
	GameState Tmp;
	LoadResult Info;
	return ReadFile(Path, Text) && DeserializeGame(Text, Tmp, Info);
}

bool SaveGameToFile(const std::string& Path, const GameState& State, std::string* OutError)
{
	const auto Fail = [&](const std::string& Msg) {
		if (OutError) *OutError = Msg;
		return false;
	};
	const fs::path Primary = ToPath(Path);
	const fs::path Temp = ToPath(Path + ".tmp");
	const fs::path Backup = ToPath(BackupPath(Path));
	std::error_code Ec;
	if (Primary.has_parent_path()) fs::create_directories(Primary.parent_path(), Ec);

	const std::string Text = SerializeGame(State);
	{
		std::ofstream F(Temp, std::ios::binary | std::ios::trunc);
		if (!F) return Fail("Cannot open temp file");
		F.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		F.flush();
		if (!F) return Fail("Write failed");
	}
	if (!IsValidSaveFile(Path + ".tmp"))
	{
		fs::remove(Temp, Ec);
		return Fail("Verification of written save failed");
	}
	if (fs::exists(Primary, Ec))
	{
		if (IsValidSaveFile(Path))
		{
			fs::copy_file(Primary, Backup, fs::copy_options::overwrite_existing, Ec);
			if (Ec) return Fail("Backup rotation failed: " + Ec.message());
		}
		else
		{
			// Keep the corrupt file for inspection; do not let it replace a good backup.
			fs::rename(Primary, ToPath(Path + ".corrupt"), Ec);
		}
	}
	fs::rename(Temp, Primary, Ec);
	if (Ec) return Fail("Final rename failed: " + Ec.message());
	return true;
}

LoadResult LoadGameFromFile(const std::string& Path, GameState& Out)
{
	LoadResult Primary;
	std::string Text;
	const bool bPrimaryExists = ReadFile(Path, Text);
	if (bPrimaryExists && DeserializeGame(Text, Out, Primary))
	{
		return Primary;
	}
	if (Primary.Status == ELoadStatus::UnsupportedVersion)
	{
		return Primary;
	}
	LoadResult Backup;
	std::string BakText;
	const bool bBackupExists = ReadFile(BackupPath(Path), BakText);
	if (bBackupExists && DeserializeGame(BakText, Out, Backup))
	{
		Backup.Status = ELoadStatus::RecoveredFromBackup;
		Backup.Message = bPrimaryExists ? "Primary save invalid (" + Primary.Message + "); loaded backup" : "Primary save missing; loaded backup";
		return Backup;
	}
	LoadResult R;
	R.Status = (bPrimaryExists || bBackupExists) ? ELoadStatus::Corrupt : ELoadStatus::NotFound;
	R.Message = bPrimaryExists ? Primary.Message : "No save found";
	return R;
}
} // namespace kc
