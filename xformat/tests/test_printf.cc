#include <xformat/ioformat.h>

#include "catch.hpp"
#include "streamtest.h"

using namespace stdex;

TEST_CASE("printf guarantees")
{
	std::stringstream ss;

	ss.width(12);
	printf(ss, "mixing|%#+8.3ff", 0.12);

	REQUIRE(str(ss) == "mixing|  +0.120f");
	REQUIRE(ss.width() == 12);
	REQUIRE(ss.precision() == 6);

	printf(ss, "%0*u|%-*.*g", 8, 42, 10, 3, 0.12);
	auto s = aprintf("%0*u|%-*.*g", 8, 42, 10, 3, 0.12);

	REQUIRE(ss.str() == s);
	REQUIRE(str(ss) == "00000042|0.12      ");

	ss.fill('_');
	printf(ss, "%3$0+*2$d|%4$-*2$.*1$g", 3, 10, 42, 0.12);

	REQUIRE(str(ss) == "+000000042|0.12______");
	REQUIRE(ss.width() == 12);
	REQUIRE(ss.precision() == 6);

	std::string st = "test";
	printf(ss, "%-7.s", st);
	s = str(ss);
	printf(ss, "%-7.s", string_view(st));

	REQUIRE(s == "test___");
	REQUIRE(str(ss) == s);
}

TEST_CASE("printf")
{
	std::stringstream ss;

	test("%%|%c|%s|%d|%i|%lu|%llx", '-', "", 42, 43, 44ul, 45ull);

	test("%2c", 'a');
	test("%-12c", 'x');

	test("%12s", "str");
	test("%12.4s", "A long time ago");
	char s[] = "in a galaxy far far away";
	auto p = s;
	test("%12.4s", p);

	printf(ss, "%60p", &ss);
	REQUIRE(ss.str().size() == 60);

	void* addr;
	ss >> addr;
	CHECK(addr == &ss);
}

TEST_CASE("wprintf")
{
	std::wstringstream ss;

	test(L"%12s", "str");
	test(L"%12.4s", "A long time ago");
	char s[] = "in a galaxy far far away";
	auto p = s;
	test(L"%12.4s", p);

	test(L"%12ls", L"str");
	test(L"%12.4ls", L"There was an old lady called Wright");
	wchar_t ws[] = L" who could travel much faster than light.";
	auto wp = ws;
	test(L"%12.4ls", wp);

	printf(ss, L"%+#07c", L'a');
	REQUIRE(str(ss) == L"      a");

	printf(ss, L"%+#07c", 'a');
	REQUIRE(str(ss) == L"      a");

	printf(ss, L"%#07i", L'a');
	REQUIRE(str(ss) == aprintf(L"%07u", L'a'));

	printf(ss, L"%+#07i", 'a');
	REQUIRE(str(ss) == aprintf(L"%+07d", 'a'));
}

TEST_CASE("printf extras")
{
	std::stringstream ss;

	printf(ss, "%8s", true);
	REQUIRE(str(ss) == "    true");

	printf(ss, "%8s", false);
	REQUIRE(str(ss) == "   false");

	printf(ss, "%-7s", false);
	REQUIRE(str(ss) == "false  ");

	printf(ss, "%7d", true);
	REQUIRE(str(ss) == "      1");

	printf(ss, "%7.4d", true);
	REQUIRE(str(ss) == "      1");

	printf(ss, "%- 7d", true);
	REQUIRE(str(ss) == "1      ");

	printf(ss, "%+#07s", true);
	REQUIRE(str(ss) == "   true");

	printf(ss, "%+#07d", true);
	REQUIRE(str(ss) == aprintf("%+07d", true));

	printf(ss, "%+#07c", 'a');
	REQUIRE(str(ss) == "      a");

	printf(ss, "%+#07i", 'a');
	REQUIRE(str(ss) == aprintf("%+07d", 'a'));

	WHEN("C locale is set to use a different decimal point")
	{
		auto p = []
		{
			if (auto p = setlocale(LC_ALL, "de_DE"))
				return p;
			else
				return setlocale(LC_ALL, "de_DE.UTF-8");
		}();
		if (p == nullptr)
			WARN("Cannot set locale to de_DE");

		THEN("C++ xformat does not follow it")
		{
			if (p)
			{
				auto s = aprintf("%#6a", 3.0);
				REQUIRE(s == "0x1,8p+1");

				printf(ss, "%#6a", 3.0);
				REQUIRE(str(ss) == "0x1.8p+1");
			}
		}

		if (p)
			setlocale(LC_ALL, "C");
	}
}
