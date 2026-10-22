#include "KcTest.h"

#include "KcJson.h"
#include "KcRng.h"

using namespace kc;

TEST(Rng_SameSeedSameSequence)
{
	Rng A(42), B(42), C(43);
	bool bDiffer = false;
	for (int i = 0; i < 1000; ++i)
	{
		const uint64_t X = A.NextU64();
		CHECK(X == B.NextU64());
		if (X != C.NextU64()) bDiffer = true;
	}
	CHECK(bDiffer);
	Rng D(7);
	for (int i = 0; i < 1000; ++i)
	{
		const int V = D.UniformInt(1, 6);
		CHECK(V >= 1 && V <= 6);
		const double U = D.Uniform();
		CHECK(U >= 0.0 && U < 1.0);
	}
}

TEST(Json_ParsesAndReportsLines)
{
	JsonValue V;
	std::string Err;
	REQUIRE(ParseJson("{\n \"a\": [1, 2.5, true, null],\n \"b\": \"x\\u00e9\"\n}", V, Err));
	REQUIRE(V.IsObject());
	const JsonValue* A = V.Find("a");
	REQUIRE(A && A->IsArray() && A->Items.size() == 4u);
	CHECK_NEAR(A->Items[1].Number, 2.5, 1e-12);
	CHECK(V.Find("b")->String == "x\xC3\xA9");
	CHECK_EQ(V.Find("b")->Line, 3);
	CHECK(!ParseJson("{\"a\": 1, \"a\": 2}", V, Err));
	CHECK(Err.find("duplicate") != std::string::npos);
	CHECK(!ParseJson("{\n\"a\": }", V, Err));
	CHECK(Err.find("line 2") == 0);
}
