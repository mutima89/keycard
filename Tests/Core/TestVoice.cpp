#include "KcTest.h"

#include "KcJson.h"
#include "KcTestContent.h"
#include "KcVoice.h"

#include <algorithm>
#include <filesystem>

using namespace kc;

TEST(Voice_KeyIsStableAndMatchesTheBuildScript)
{
	// Published FNV-1a 32-bit vectors.
	CHECK(VoiceLineHash("") == 0x811c9dc5u);
	CHECK(VoiceLineHash("a") == 0xe40c292cu);
	CHECK(VoiceLineHash("foobar") == 0xbf9cf968u);
	// Layout and spacing do not change the clip; words and punctuation do. The same vectors are
	// checked by Scripts/BuildVoices.py before it generates anything.
	CHECK(NormalizeVoiceLine("  Two cases in.\n\nHow are you?  ") == "Two cases in. How are you?");
	CHECK(VoiceLineKey("priya", "  Two cases in.\n\nHow are you?  ") == "priya_4001ff99");
	CHECK(VoiceLineKey("priya", "Two cases in. How are you?") == "priya_4001ff99");
	CHECK(VoiceLineKey("priya", "It\xE2\x80\x99s fine.") == "priya_175ecf54");
	CHECK(VoiceLineKey("priya", "Two cases in. How are you!") != "priya_4001ff99");
	CHECK(VoiceLineKey("", "Hello.").empty());
	CHECK(VoiceLineKey("priya", " \n ").empty());
}

TEST(Voice_EveryCharacterHasTheirOwnVoice)
{
	// Data/VoiceCast.json must cast every person in Data/People.json, each with a distinct Kokoro voice,
	// or Scripts/BuildVoices.py silently skips that character's lines.
	const KcContent& C = LoadContent();
	JsonValue Cast;
	std::string Error;
	REQUIRE(ParseJson(KcReadFile(std::filesystem::path(KC_DATA_DIR) / "VoiceCast.json"), Cast, Error));
	const JsonValue* Entries = Cast.Find("cast");
	REQUIRE(Entries && Entries->IsObject());
	std::vector<std::string> Voices;
	for (const CharacterDef& P : C.World.People.People)
	{
		const JsonValue* Entry = Entries->Find(P.Id);
		if (!Entry) std::printf("    no voice for %s\n", P.Id.c_str());
		REQUIRE(Entry && Entry->IsObject());
		const JsonValue* Voice = Entry->Find("voice");
		REQUIRE(Voice && (Voice->IsString() || Voice->IsObject()));
		// A voice is a Kokoro id or a blend {id: weight}; either way no two characters share one.
		std::string Sig = Voice->String;
		for (const auto& [Id, Weight] : Voice->Members) Sig += Id + ":" + std::to_string(Weight.Number) + ";";
		if (std::find(Voices.begin(), Voices.end(), Sig) != Voices.end()) std::printf("    %s shares a voice\n", P.Id.c_str());
		CHECK(std::find(Voices.begin(), Voices.end(), Sig) == Voices.end());
		Voices.push_back(Sig);
	}
	CHECK(Entries->Members.size() == C.World.People.People.size());
}
