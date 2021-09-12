/*-
 * Copyright (c) 2016 Zhihao Yuan.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include "format.h"
#include "ostream_formatter.h"

namespace stdex
{

template <typename charT, typename traits, typename... Args>
inline
decltype(auto) printf(std::basic_ostream<charT, traits>& out,
                      fmtstack<charT> const& fstk, Args&&... args)
{
	auto fl = out.flags();
	auto w = out.width(0);
	auto p = out.precision();
	auto&& r = format(ostream_formatter<charT, traits>(out), fstk,
	                  std::forward<Args>(args)...);
	out.flags(fl);
	out.width(w);
	out.precision(p);
	return r;
}

template <typename charT, typename traits, typename... Args>
inline
decltype(auto) printf(std::basic_ostream<charT, traits>& out,
                      std::decay_t<basic_string_view<charT, traits>> fmt,
                      Args&&... args)
{
	return printf(out, detail::compile_c(fmt.data(), fmt.size()),
	              std::forward<Args>(args)...);
}

}
