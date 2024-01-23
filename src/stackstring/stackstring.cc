/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 * Copyright notice:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "../util/dtassert.h"
#include <memory.h>
#include <stdio.h>
#include <time.h>

#include "../util/logger.h"
#include "stackstring.h"
#include <algorithm>
#include <ctype.h>
#include <iconv.h>
#include <sys/errno.h>
#include <cstdarg>

#ifdef USE_BOOST_LOCALE
#include <boost/locale.hpp>
#endif
#include <iomanip>

namespace ss {

#include <stackstring_impl.h>

	class iconv_context_t {
		std::string enc_;
		iconv_t conv_{iconv_t(-1)};

		size_t _iconv_noconv(const char** inbuf, size_t* inbytesleft, char** outbuf, size_t* outbytesleft) {
			int len = *inbytesleft < (*outbytesleft - 1) ? *inbytesleft : (*outbytesleft - 1);
			strncpy(*outbuf, *inbuf, len);
			(*outbuf)[len] = 0;
			*outbytesleft -= len;
			*inbytesleft -= len;
			return len;
		}

	public:
		iconv_context_t() = default;

		iconv_context_t(const char* enc) : enc_(enc) {
			conv_ = iconv_open("UTF8", enc);
		}

		~iconv_context_t() {
			if (conv_ != (iconv_t)-1)
				iconv_close(conv_);
		}

		size_t iconv(const char** inbuf, size_t* inbytesleft, char** outbuf, size_t* outbytesleft, const char* enc) {
			if (enc_ != enc) {
				if (iconv_close(conv_) == -1) {
				}
				conv_ = iconv_open("UTF8", enc);
				enc_ = enc;
				if (conv_ == (iconv_t)-1) {

					dterrorf("iconv_open failed: {:s}\n", strerror(errno));
				}
			}
			if (conv_ == (iconv_t)-1)
				return _iconv_noconv(inbuf, inbytesleft, outbuf, outbytesleft);
			else
				return ::iconv(conv_, (char**)inbuf, inbytesleft, outbuf, outbytesleft);
		}
	};

	static thread_local iconv_context_t priv;

	void unac_iso_databuffer(char* str, size_t len) {

#define MAP_START ((signed)192)
		unsigned char map[] = {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
			'D', 'N', 'O', 'O', 'O', 'O', 'O', 215, 'O', 'U', 'U', 'U', 'U', 'Y', 'T', 's',
			'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
			'e', 'n', 'o', 'o', 'o', 'o', 'o', 247, 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y'};

#define MAP_END ((signed)(MAP_START + sizeof(map) - 1))

		unsigned int i;
		for (i = 0; i < len; i++) {
			int c = str[i];
			if (c >= MAP_START && c <= MAP_END)
				str[i] = map[c - MAP_START];
		}
		//	str[len]=0;
	}

	static int _strtrim(char* str, int len) {
		if (len == 0)
			return 0;
		char* s = str;
		while (*s && isspace(*s))
			s++;
		char* t = str + len - 1;
		if (s > t) {
			*str = 0;
			return 0;
		}
		while (t - s >= 0 && isspace(*t))
			t--;
		t++;
		*t = 0;
		if (s - str > 0)
			memmove(str, s, t - s + 1);
		return t - s;
	}

	void string_::trim(int start) {
		if (start >= size())
			return;
		int newlen = _strtrim(buffer() + start, size() - start);
		auto size_ = start + newlen;
		set_size(size_ + 1);
		assert(parent::size() == size_ + 1);
		assert(size() == size_);
		assert(size_ + 1 <= capacity());
	}

/*returns 0 on success, -1 on error
	converts input string and appends the result to *output,
	starting at byte *output_len.
	output_size is the size of *output
	output_len and *output_size can be modified
	clean: remove characters such as \0x86 and \x0x87 (emphasis)
*/
	int string_::append_as_utf8(char* input, int input_len, const char* enc) {
		if (!input || !*input)
			return 0;
		auto size_ = size();
		reserve(size_ + input_len);
		size_t inbytesleft = input_len;
		size_t outbytesleft = capacity() - (size_ + 1);
		const char* inbuf = input;
		char* outbuf = buffer() + size_;
		int ret;
		bool retried{false};
		do {
			assert(inbuf);
			assert(outbuf - buffer() + outbytesleft <= capacity());
			if (outbuf - buffer() < (signed)capacity()) {
				ret = priv.iconv(&inbuf, &inbytesleft, &outbuf, &outbytesleft, enc);
				if (ret == -1) {
					if (errno == EILSEQ) {
						// invalid multibyte sequence
						priv.iconv(NULL, 0, &outbuf, &outbytesleft, enc); // reset
						inbytesleft -= 1;
						inbuf = input + (input_len - inbytesleft);
						continue;
					} else if (errno == EINVAL) {
						// truncated input
						priv.iconv(NULL, 0, &outbuf, &outbytesleft, enc); // reset
						break;
					} else if (errno == E2BIG) {
						//not enough room in output buffer
						int extra = capacity();
						priv.iconv(NULL, 0, &outbuf, &outbytesleft, enc); // reset
						if(retried)
							break;
						reserve( capacity() + extra); //grow string

						size_ = size();
						inbytesleft = input_len;
						outbytesleft = capacity() - (size_ + 1);
						inbuf = input;
						outbuf = buffer() + size_;
						retried = true;
						continue;
					} else {
						priv.iconv(NULL, 0, &outbuf, &outbytesleft, enc); // reset
						break;
					}
				}
			} else
				ret = -1;
			assert(outbuf - buffer() <= (signed)capacity());
			size_ = (outbuf - buffer());
			//		buffer[n]=0;
			if (ret < 0) {
				if (outbuf - buffer() >= (signed)capacity() || errno == E2BIG) {
					reserve(capacity() + 256);

					outbuf = buffer() + size_;

					outbytesleft = capacity() - (size_ + 1);
					assert(outbytesleft > 0);
				} else {
					// attempt to deal with rubbish data such as iso characters
					// in the utf8 string
					if (size_ < capacity()) {
						*outbuf = *inbuf++;
						size_++;
						unac_iso_databuffer(outbuf, 1);
						outbuf++;
						outbytesleft--;
						inbytesleft--;
					} else
						inbuf++;
				}
			}
		} while (ret < 0);

		size_ = outbuf - buffer();
		assert(size_ + 1 <= capacity());
		buffer()[size_] = 0;
		set_size(size_ + 1);
		assert(parent::size() == size_ + 1);
		assert(size() == size_);

		if (ret >= 0) {
			// normal completion
			if (inbytesleft > 0) {
				// dterror_nice("unexpected: inbytesleft=" << inbytesleft);
				return -1;
			}
			return 0;
		}

		if (errno == E2BIG) {
			// dterror_nice("string too long\n");
		} else if (errno == EILSEQ) {
			// dterror_nice("invalid multibyte sequence: " << input);
		}
		return -1;
	}

	static int
	Utf8CharLen(const char* s) { // also works if p+1, p+2... point outside the string (trailing zero takes care of that))
#define MT(s, m, v) ((*(s) & (m)) == (v)) // Mask Test
		if (MT(s, 0xE0, 0xC0) && MT(s + 1, 0xC0, 0x80))
			return 2;
		if (MT(s, 0xF0, 0xE0) && MT(s + 1, 0xC0, 0x80) && MT(s + 2, 0xC0, 0x80))
			return 3;
		if (MT(s, 0xF8, 0xF0) && MT(s + 1, 0xC0, 0x80) && MT(s + 2, 0xC0, 0x80) && MT(s + 3, 0xC0, 0x80))
			return 4;
		return 1;
	}

#ifndef USE_BOOST_LOCALE
/*utf8 safe case consistent conversion; the problem with this code
	is that it may not agree well with what the user expects
*/
	void string_::append_tolower(const ss::string_& in) {
		static thread_local auto ctx = iconv_open("ASCII//TRANSLIT", "UTF-8");

		size_t inbytesleft = in.size();

		reserve(inbytesleft + size());
		assert(this->capacity() - this->size() >= inbytesleft);
		size_t outbytesleft = this->capacity() - this->size();
		size_t outbytesleft_at_start = outbytesleft;
		char* inbuf = const_cast<char*>(in.buffer());
		char* outbuf = this->buffer() + this->size();
		int ret = -1;
		for (;;) {
			ret = ::iconv(ctx, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
			if (ret == -1) {
				if (errno == EILSEQ) {
					// invalid multibyte sequence
					::iconv(ctx, NULL, 0, &outbuf, &outbytesleft); // reset
					inbytesleft -= 1;
					inbuf += 1;
					continue;
				} else if (errno == E2BIG) {
					this->reserve(capacity() + inbytesleft);
					outbytesleft += inbytesleft;
					outbytesleft_at_start += inbytesleft;
					continue;
				} else if (errno == EINVAL) {
					// truncated input
					::iconv(ctx, NULL, 0, &outbuf, &outbytesleft); // reset
					break;
				} else {
					dterrorf("append_tolower: {}", strerror(errno));
					break;
				}
			}
			if (inbytesleft == 0) {
				break;
			}
		}
		::iconv(ctx, NULL, 0, &outbuf, &outbytesleft); // reset
		auto old_length = this->size();
		assert(outbytesleft_at_start >= outbytesleft);
		auto new_length = old_length + (outbytesleft_at_start - outbytesleft);
		this->set_size(new_length + 1);
		this->push_back(0); // zero terminator
		for (unsigned int i = old_length; i < new_length; ++i)
			(*this)[i] = std::tolower((unsigned char)(*this)[i]);
	}
#else // USE_BOOST_LOCALE
/*utf8 safe case conversion by utf-8 aware boost locale function.
	The "problem" with this code is that
*/

	void string_::append_tolower(const ss::string_& in) {

		static thread_local std::locale loc = boost::locale::generator{}("");
		auto x = boost::locale::to_lower(in.buffer(), in.buffer() + in.size() + 1, loc);
		append_raw(x.c_str(), x.size());
	}
#endif


	template class databuffer_<char>;

}; // namespace ss

#if 0
void print_hex(ss::bytebuffer_& buffer) {
	for (int i = 0; i < buffer.size(); ++i)
		printf("%02x ", (unsigned char)buffer[i]);
	printf("\n");
}
#endif

int gcd(int a, int b) {
	int temp;
	while (b != 0) {
		temp = a % b;

		a = b;
		b = temp;
	}
	return a;
}

// v_len does not include terminating 0 byte
void ss::string_::copy_raw(const char* v, int v_len) {
	assert(header.h.inited);
	reserve(v_len);
	set_size(v_len + 1);
	assert(parent::size() == v_len + 1);
	assert(size() == v_len);
	auto* dest = buffer();
	memcpy(dest, v, v_len + 1);
	dest[v_len] = 0;
};
