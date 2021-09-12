#pragma once

#include <string>
#include <stdio.h>
#include <wchar.h>
#include <assert.h>

template <typename T>
inline
auto str(T& ss)
{
	auto s = ss.str();
	ss.str({});
	return s;
}

template <typename... T>
auto gprintf(char* buf, size_t sz, char const* fmt, T... v)
{
	return ::snprintf(buf, sz, fmt, v...);
}

template <typename... T>
auto gprintf(wchar_t* buf, size_t sz, wchar_t const* fmt, T... v)
{
	return ::swprintf(buf, sz, fmt, v...);
}

template <typename charT, typename... T>
auto aprintf(charT const* fmt, T... v)
{
	std::basic_string<charT> s(100, 'X');
	auto n = gprintf(&*s.begin(), s.size(), fmt, v...);
	assert(n >= 0);
	s.resize(size_t(n));
	return s;
}

#define test(...)                                       \
	do                                              \
	{                                               \
		stdex::printf(ss, __VA_ARGS__);         \
		CHECK(str(ss) == aprintf(__VA_ARGS__)); \
	} while (0);
