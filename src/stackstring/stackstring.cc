/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

#ifdef USE_BOOST_LOCALE
#include <boost/locale.hpp>
// using namespace boost::locale;
#endif
#include "xformat/ioformat.h"
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

					dterrorx("iconv_open failed: %s\n", strerror(errno));
				}
			}
			if (conv_ == (iconv_t)-1)
				return _iconv_noconv(inbuf, inbytesleft, outbuf, outbytesleft);
			else
				return ::iconv(conv_, (char**)inbuf, inbytesleft, outbuf, outbytesleft);
		}
	};

	static iconv_context_t priv;

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

	string_& string_::sprintf(const char* fmt, ss::string_& x) {
		int r = ::snprintf(buffer() + size(), capacity() - size(), fmt, x.c_str());
		auto size_ = size();
		if (r + 1 >= (signed)(capacity() - size())) {
			grow(r + 1 - capacity() + size_);
			size_ += ::snprintf(buffer() + size_, capacity() - size_, fmt, x.c_str());
		} else
			size_ += r;
		set_size(size_ + 1);
		assert(parent::size() == size_ + 1);
		assert(size() == size_);
		assert(1 + size() <= capacity());
		buffer()[size_] = 0;
		return *this;
	}

	string_& string_::sprintf(const dateTime& x) {
		struct tm t_tm;
		const char* tformat = NULL;
		// int n=0;
		time_t t(x);

		localtime_r(&t, &t_tm);

		tformat = x.format;

		auto size_ = size(); // excluding terminating zero byte
		if (16 >= (signed)(capacity() - size_)) {
			grow(16 + 1 - capacity() + size_);
		}

		int r = ::strftime(buffer() + size_, capacity() - size_, tformat, &t_tm);
		if (r > 0) {
			size_ += r;
		} else {
			// string is too short
			grow(32 + 1 - capacity() + size_);
			r = ::strftime(buffer() + size_, capacity() - size_, tformat, &t_tm);
			if (r > 0) {
				size_ += r;
			} else {
				dterror("strftime failed. Buffer too small?");
			}
		}
		set_size(size_ + 1);
		assert(parent::size() == size_ + 1);
		assert(size() == size_);
		assert(1 + size() <= capacity());
		buffer()[size_] = 0;
		return *this;
	}


	int string_::strftime(const char* fmt, const struct tm* tm) {
		auto size_ = size();
		size_t s = capacity() - size_;
		if (16 >= (signed)s) {
			grow(32 + 1 - capacity() + size_);
			s = capacity() - size_;
		}

		size_t ret = ::strftime(buffer() + size_, s, fmt, tm);
		if (ret == 0) {
			dterror("strftime failed. Buffer too small?");
		}
		size_ += ret;
		set_size(size_ + 1);
		assert(parent::size() == size_ + 1);
		assert(size() == size_);
		assert(size_ + 1 < capacity());
		buffer()[size_] = 0;
		return ret;
	}

	int string_::snprintf(int s, const char* fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		if (s + size() > capacity()) {
			grow(s + size() - capacity());
		}
		s = capacity() - size();
		int ret = vsnprintf(buffer() + size(), s, fmt, ap);
		va_end(ap);
		s = size() + ret;
		set_size(s + 1);
		assert(parent::size() == s + 1);
		assert(size() == s);
		assert(size() + 1 <= capacity());
		buffer()[s] = 0;
		return ret;
	}

	int string_::sprintf(const char* fmt, ...) {
		int oldn = size();
		auto size_ = oldn;
		for (int i = 0; i < 2; i++) {
			va_list ap;
			va_start(ap, fmt);
			int s = capacity() - size_;
			int ret = vsnprintf(buffer() + size_, s, fmt, ap);
			va_end(ap);
			if (ret + 1 <= s) {
				size_ += ret;
				set_size(size_ + 1);
				assert(parent::size() == size_ + 1);
				assert(size() == size_);
				assert(size_ + 1 <= capacity());
				return ret;
			}
			grow(ret + 1 - s);
		}
		set_size(size_);
		assert(size() == size_);
		assert(size_ + 1 <= capacity());
		assert(size() + 1 <= capacity());
		buffer()[size_] = 0;
		return size_ - oldn;
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
	int string_::append_as_utf8(const char* input, int input_len, const char* enc, bool clean) {
		int oldn = size();
		int ret1 = _append_as_utf8(input, input_len, enc);
		if (ret1 < 0)
			return ret1;
		int ret = string_::translate_dvb_control_characters(oldn, clean);
		return ret;
	}

	int string_::_append_as_utf8(const char* input, int input_len, const char* enc) {
		if (!input || !*input)
			return 0;
		auto size_ = size();
		reserve(size_ + input_len);
		size_t inbytesleft = input_len;
		size_t outbytesleft = capacity() - (size_ + 1);
		const char* inbuf = input;
		char* outbuf = buffer() + size_;
		int ret;
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
					grow(256);

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
					this->grow(inbytesleft);
					outbytesleft += inbytesleft;
					outbytesleft_at_start += inbytesleft;
					continue;
				} else if (errno == EINVAL) {
					// truncated input
					::iconv(ctx, NULL, 0, &outbuf, &outbytesleft); // reset
					break;
				} else {
					dterror("append_tolower: " << strerror(errno));
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

// used by libsi
	int string_::translate_dvb_control_characters(int startpos, bool clean) {
		auto* to = buffer() + startpos;
		auto* from = to;
		auto* end = buffer() + size();
		auto size_ = size();
		int len = size_ - startpos;

		// Handle control codes:
		while (from < end) {
			int l = Utf8CharLen(from);
			/* cases to treat are the single byte character codes 0x8A, 0xa0, 0x86 and 0x87.
				 These are encoded as 0xc2, X when the original dvb data (whcih has now been utf8 encoded)
				 was single byte. For multibyte tables (Korean, Chinese....) the result may be wrong.
				 It is not clear what the byte values are after utf-8 encoding.
				 It may be better to handle the special characters first:
				 1. check if the table is single or multi byte (by checking language code)
				 2. Special caracters are  0x8A, 0xa0, 0x86 and 0x87 (single byte table)
				 or 0xE08A ... for 2 byte tables
			*/
			if (l == 2 && (uint8_t)from[0] == 0xC2) { // Possible code to replace/delete
				switch ((uint8_t)from[1]) {
				case 0x8A:
					*to++ = '\n';
					break;
				case 0xA0:
					*to++ = ' ';
					break;
				case 0x86:
				case 0x87:
					if (clean) {
						// skip the code
					} else {
						*to++ = from[0];
						*to++ = from[1];
					}
					break;
				default:
					*to++ = from[0];
					*to++ = from[1];
				}
				from += 2;
			} else {
				if (from == to) {
					from += l;
					to += l;
				} else
					for (; l > 0; --l)
						*to++ = *from++;
			}
		}
		int delta = from - to;
		assert(delta >= 0);
		if (delta > 0) {
			auto newlen = size() - delta;
			assert(newlen >= 0);
			buffer()[newlen] = 0;
			set_size(newlen);
		}
		return size();
	}

	template class databuffer_<char>;
// template class databuffer_<false>;

	template class databuffer_<uint16_t>;
	template class databuffer_<uint32_t>;

}; // namespace ss

namespace std {
	std::ostream& operator<<(std::ostream& os, const ss::string_& a) { return os << a.c_str(); }
	std::ostream& operator<<(std::ostream& os, const ss::bytebuffer_& a) {
		stdex::printf(os, "buffer[%d]={", a.size());
		for(auto &c : a) {
			os << std::setw(2) << std::setfill('0') << std::hex << +c;
		}
		stdex::printf(os, "}");
		return os;
	}

}; // namespace std

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
