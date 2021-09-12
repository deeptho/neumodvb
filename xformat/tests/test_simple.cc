#include <xformat/ioformat.h>

#include "catch.hpp"
#include "streamtest.h"

#include <sstream>

using namespace stdex;

TEST_CASE("traditional")
{
	std::stringstream ss;

	REQUIRE(printf(ss, ""_cfmt));
	REQUIRE(ss.str() == "");

	REQUIRE(printf(ss, "", 42, "meow"));
	REQUIRE(ss.str() == "");

	REQUIRE(printf(ss, "x"));
	REQUIRE(ss.str() == "x");

	REQUIRE(printf(ss, "-man"));
	REQUIRE(str(ss) == "x-man");

	REQUIRE(printf(ss, "%s", 42));
	REQUIRE(str(ss) == "42");

	REQUIRE(printf(ss, "%1$s", 42));
	REQUIRE(str(ss) == "42");

	REQUIRE(printf(ss, "%s%%", 42));
	REQUIRE(str(ss) == "42%");

	REQUIRE(printf(ss, "%%akka"));
	REQUIRE(str(ss) == "%akka");

	REQUIRE_THROWS_AS("%k"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("only1%"_cfmt, std::invalid_argument);

	REQUIRE_THROWS_AS("%s and %1$s"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$s and %s"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$s and %1"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%0$s"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%$s"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$"_cfmt, std::invalid_argument);

	REQUIRE_THROWS_AS(printf(ss, "%s\n"), std::out_of_range);
	REQUIRE_THROWS_AS(printf(ss, "%s %s %s", 1, 2), std::out_of_range);
	REQUIRE_THROWS_AS(printf(ss, "%3$s", 1, 2), std::out_of_range);

	ss.str({});

	REQUIRE(printf(ss, "%s\n", 42));
	REQUIRE(str(ss) == "42\n");

	REQUIRE(printf(ss, "hello, %s\n"_cfmt, "world"));
	REQUIRE(str(ss) == "hello, world\n");

	REQUIRE(printf(ss, "%s, %%%s%%, %s-body", "hey", 'u', 3));
	REQUIRE(str(ss) == "hey, %u%, 3-body");

	REQUIRE(printf(ss, "%2$s, %%%2$s%%, %3$s-body", "hey", 'u', 3));
	REQUIRE(str(ss) == "u, %u%, 3-body");

	int i = 3;
	printf(ss, "%s", std::ref(i));
	REQUIRE(str(ss) == "3");
}

TEST_CASE("modern")
{
	std::stringstream ss;

	REQUIRE(printf(ss, ""_fmt));
	REQUIRE(ss.str() == "");

	REQUIRE(printf(ss, ""_fmt, 42, "meow"));
	REQUIRE(ss.str() == "");

	REQUIRE(printf(ss, "x"_fmt));
	REQUIRE(ss.str() == "x");

	REQUIRE(printf(ss, "-man"_fmt));
	REQUIRE(str(ss) == "x-man");

	REQUIRE(printf(ss, "{}"_fmt, 42));
	REQUIRE(str(ss) == "42");

	REQUIRE(printf(ss, "{0}"_fmt, 42));
	REQUIRE(str(ss) == "42");

	REQUIRE(printf(ss, "{{{0:}}}"_fmt, 42));
	REQUIRE(str(ss) == "{42}");

	REQUIRE(printf(ss, "ak}}{{ka"_fmt));
	REQUIRE(str(ss) == "ak}{ka");

	REQUIRE_THROWS_AS("{k}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("single}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("single{"_fmt, std::invalid_argument);

	REQUIRE_THROWS_AS("{} and {0}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{0} and {}"_fmt, std::invalid_argument);

	REQUIRE_THROWS_AS(printf(ss, "{}"_fmt), std::out_of_range);
	REQUIRE_THROWS_AS(printf(ss, "{} {} {}"_fmt, 1, 2), std::out_of_range);
	REQUIRE_THROWS_AS(printf(ss, "{2}"_fmt, 1, 2), std::out_of_range);

	ss.str({});

	REQUIRE(printf(ss, "{0:}\n"_fmt, 42));
	REQUIRE(str(ss) == "42\n");

	REQUIRE(printf(ss, "hello, {:}\n"_fmt, "world"));
	REQUIRE(str(ss) == "hello, world\n");

	REQUIRE(printf(ss, "{}, {}"_fmt, "hey", 'u', 3));
	REQUIRE(str(ss) == "hey, u");

	REQUIRE(printf(ss, "{1}, {1}"_fmt, "hey", 'u', 3));
	REQUIRE(str(ss) == "u, u");
}

TEST_CASE("limitations")
{
	REQUIRE_NOTHROW("  %s %s %s %s %s %s %s %s %s "_cfmt);
	REQUIRE_NOTHROW("  %% %% %% %% %% %% %% %% %% %% "_cfmt);
	REQUIRE_THROWS_AS("%s %s %s %s %s %s %s %s %s %s %s"_cfmt,
	                  std::length_error);

	REQUIRE_NOTHROW("{} {} {} {} {} {} {} {} {} {}"_fmt);
	REQUIRE_NOTHROW("{0} {1} {2} {3} {4} {5} {6} {7} {8} {9}"_fmt);
	REQUIRE_THROWS_AS("{} {} {} {} {} {} {} {} {} {} {}"_fmt,
	                  std::length_error);

	REQUIRE_THROWS_AS("%10$s"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{10}"_fmt, std::invalid_argument);
}
