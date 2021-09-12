#include <xformat/format.h>

#include "catch.hpp"
#include "fmttest.h"

using namespace stdex;

TEST_CASE("fmt")
{
	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(sp.facade() == '\0');
		               REQUIRE(sp.options() == fmtoptions::none);
		               REQUIRE(w == -1);
		               REQUIRE(p == -1);
		       }),
	       L"{}"_fmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == '\0');
		               REQUIRE((sp.options() & (fmtoptions::left |
		                                        fmtoptions::right)) ==
		                       fmtoptions::left);
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		       }),
	       u"{:<-0}"_fmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 's');
		               REQUIRE((sp.options() & (fmtoptions::left |
		                                        fmtoptions::right)) ==
		                       fmtoptions::none);
		               REQUIRE((sp.options() & fmtoptions::alt) !=
		                       fmtoptions::none);
		               REQUIRE((sp.options() &
		                        (fmtoptions::sign |
		                         fmtoptions::aligned_sign)) ==
		                       fmtoptions::aligned_sign);
		       }),
	       u"{: #s}"_fmt, 0);

	format(fmttest([](fmtshape sp, ...)
	               {
		               REQUIRE(sp.facade() == 'X');
		               REQUIRE((sp.options() & (fmtoptions::left |
		                                        fmtoptions::right)) ==
		                       fmtoptions::right);
		               REQUIRE((sp.options() &
		                        (fmtoptions::sign |
		                         fmtoptions::aligned_sign)) ==
		                       fmtoptions::sign);
		       }),
	       U"{:>+X}"_fmt, 0);

	REQUIRE_THROWS_AS("{ #}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:# }"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:#+}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:#-}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:0 }"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:0+}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:0-}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{: +}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:-+}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:- }"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:  }"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:--}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:++}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:0#}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:<>}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:+>}"_fmt, std::invalid_argument);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(sp.facade() == 'o');
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		               REQUIRE(w == 7);
		       }),
	       u"{:007o}"_fmt, 0);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(w == 13);
		       }),
	       u"{:{}} {:13}"_fmt, 12, 13, 14);

	format(fmttest([](fmtshape sp, int w, ...)
	               {
		               REQUIRE(w == 14);
		       }),
	       u"{2:{2}} {0:{2}} {1:{}}"_fmt, 12, 13, 14UL);

	REQUIRE_THROWS_AS(format(fmtnull(), u"{1:{0}}"_fmt, 12.0, 3),
	                  std::invalid_argument);
	REQUIRE_THROWS_AS(format(fmtnull(), u"{:{}}"_fmt, 3, ""),
	                  std::invalid_argument);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(sp.facade() == 'u');
		               REQUIRE((sp.options() & fmtoptions::zero) !=
		                       fmtoptions::none);
		               REQUIRE(w == -1);
		               REQUIRE(p == 7);
		       }),
	       u"{:0.7u}"_fmt, 0);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == 0);
		               REQUIRE(p == 0);
		       }),
	       u"{:00.0}"_fmt, 0);

	REQUIRE_NOTHROW("{:}"_fmt);
	REQUIRE_THROWS_AS("{:."_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:.}"_fmt, std::invalid_argument);

	format(fmttest([](fmtshape sp, int, int p)
	               {
		               REQUIRE(p == 13);
		       }),
	       u"{:.{}} {:.13}"_fmt, 12, 13, 14);

	format(fmttest([](fmtshape sp, int, int p)
	               {
		               REQUIRE(p == 14);
		       }),
	       u"{2:.{2}} {0:.{2}} {1:.{}}"_fmt, 12, 13, 14UL);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == 13);
		               REQUIRE(p == 14);
		       }),
	       u"{:{}.{}}"_fmt, 12, 13, 14);

	format(fmttest([](fmtshape sp, int w, int p)
	               {
		               REQUIRE(w == 13);
		               REQUIRE(p == 12);
		       }),
	       u"{:{1}.{0}}"_fmt, 12, 13);

	// limitations
	REQUIRE_THROWS_AS("{:{12}}"_fmt, std::invalid_argument);
	REQUIRE_THROWS_AS("{:.{12}}"_fmt, std::invalid_argument);
}
