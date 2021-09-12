#include <xformat/format.h>

#include "catch.hpp"
#include "fmttest.h"

using namespace stdex;

TEST_CASE("cfmt")
{
	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(sp.facade() == 's');
		               REQUIRE(sp.options() == fmtoptions::right);
		               REQUIRE(w == -1);
		               REQUIRE(p == -1);
		       }),
	       L"%s"_cfmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 's');
		               REQUIRE((sp.options() & fmtoptions::left) !=
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::right) !=
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::zero) ==
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::alt) !=
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::sign) !=
		                       fmtoptions::none);
		               REQUIRE(
		                   (sp.options() & fmtoptions::aligned_sign) !=
		                   fmtoptions::none);
		       }),
	       u"%-# + s"_cfmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 's');
		               REQUIRE((sp.options() & fmtoptions::left) !=
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		       }),
	       U"%-0s"_cfmt, 0);

	REQUIRE_THROWS_AS("%h"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%hld"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%lhd"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%ljd"_cfmt, std::invalid_argument);
	REQUIRE_NOTHROW("%jd"_cfmt);
	REQUIRE_NOTHROW("%zu"_cfmt);
	REQUIRE_NOTHROW("%tx"_cfmt);
	REQUIRE_NOTHROW("%Ls"_cfmt);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 'd');
		       }),
	       u"%-hhd"_cfmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 'u');
		       }),
	       u"%lu"_cfmt, 0);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(sp.facade() == 'u');
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		               REQUIRE(w == 7);
		       }),
	       u"%07lu"_cfmt, 0);

	REQUIRE_THROWS_AS("%*d %1$d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$d %*d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$*d"_cfmt, std::invalid_argument);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(w == 12);
		       }),
	       u"%0*lu %12d"_cfmt, 12, 3, 4);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(w == 12);
		       }),
	       u"%2$*1$d %3$*1$d"_cfmt, 12UL, 3, 4);

	REQUIRE_NOTHROW(u"%2$*1$d"_cfmt);
	REQUIRE_NOTHROW(u"%*d"_cfmt);
	REQUIRE_THROWS_AS(format(fmtnull(), u"%2$*1$d"_cfmt, 12.0, 3),
	                  std::invalid_argument);
	REQUIRE_THROWS_AS(format(fmtnull(), u"%*d"_cfmt, "", 3),
	                  std::invalid_argument);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(sp.facade() == 'u');
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		               REQUIRE(w == -1);
		               REQUIRE(p == 7);
		       }),
	       u"%0.7u"_cfmt, 0);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == -1);
		               REQUIRE(p == 0);
		       }),
	       u"%.u"_cfmt, 0);

	REQUIRE_THROWS_AS("%."_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%.*d %1$d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$d %.*d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%1$.*d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%*.*1$d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%*1$.*d"_cfmt, std::invalid_argument);

	format(fmttest([](fmtshape sp, int, int p)
	               {
		               REQUIRE(p == 12);
		       }),
	       u"%0.*lu %.12d"_cfmt, 12, 3, 4);

	format(fmttest([](fmtshape sp, int, int p)
	               {
		               REQUIRE(p == 12);
		       }),
	       u"%2$.*1$d %3$.*1$d"_cfmt, 12UL, 3, 4);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == 12);
		               REQUIRE(p == 3);
		       }),
	       u"%0*.*lu"_cfmt, 12, 3, 4);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == 3);
		               REQUIRE(p == 12);
		       }),
	       u"%1$*2$.*1$d"_cfmt, 12, 3);

	// limitations
	REQUIRE_THROWS_AS("%*12$d"_cfmt, std::invalid_argument);
	REQUIRE_THROWS_AS("%.*12$d"_cfmt, std::invalid_argument);
}
