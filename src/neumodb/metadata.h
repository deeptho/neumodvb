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

#pragma once

#include "stackstring.h"
#include "neumotime.h"
#include "encode.h"

#define sat_pos_none -32767 //important that this is a very low negative value, e.g. when searching with find_type_geq
#define sat_pos_dvbc 20001
#define sat_pos_dvbt 20002
#define sat_pos_dvbs 20003 // dvbs mux with unknown sat
#define sat_pos_tolerance 100 /*1 degree (allowed difference in sat_pos
																within this tolerance, muxes are assumed to be from the same sat
															*/
#define channel_id_template ((int32_t)0x80000001)
#define bouquet_id_template ((int32_t)0x80000001)
#define bouquet_id_movistar 0x010001
#define bouquet_id_sky_opentv 4104 //HD other; used for preloading (most) of the channels for the epg code


typedef bool boolean_t;
typedef float float32_t;
namespace data_types {
/*
	New data types can be added, but old data types should never be removed; their meaning can never be changed
*/
	enum container_type_t {
		builtin =     0x20000000,
		vector =      0x80000000, //vector<type>
		enumeration = 0x40000000
};
//builtin types
	enum builtin_type_t : uint32_t {
	uint8 =                    0 | builtin,
	int8 =                     1 | builtin,
	uint16 = 2| builtin,
	int16  = 3 | builtin,
	uint32 = 4 | builtin,
	int32  = 5 | builtin,
	uint64 = 6 | builtin,
	int64  = 7 | builtin,
	milliseconds = int64 | builtin,  //deliberately the same
	time   = int64 | builtin,
	boolean= 8 | builtin,
	string = 9  | builtin,
	float32= 10  | builtin,
	variant= 20 | builtin,
	none=    21 | builtin,
	optional=    22 | builtin,
	field_desc = 0xfd | builtin,
	record_desc = 0xfe | builtin,
	schema = 0xff | builtin
};


/*
	type modifies to be or-ed with any type
*/

	template<typename T> constexpr uint32_t data_type();

	inline constexpr bool is_int_type(int type_id){ //built in scalar type; hardwired
		switch(type_id & ~data_types::enumeration & ~data_types::vector) {
		case uint8:
		case int8:
		case uint16:
		case int16:
		case uint32:
		case int32:
		case uint64:
		case int64:
			return true;
		default:
			return false;
		}
	}

	inline constexpr bool is_float_type(int type_id){ //built in scalar type; hardwired
		switch(type_id & ~data_types::enumeration & ~data_types::vector) {
		case float32:
			return true;
		default:
			return false;
		}
	}

	inline constexpr bool is_boolean_type(int type_id){ //built in scalar type; hardwired
		return  (type_id & ~ data_types::enumeration) == boolean;
	}

	inline constexpr bool is_string_type(int type_id){ //built in scalar type; hardwired
		return (type_id & ~ data_types::enumeration) == string;
	}
#ifdef PURE_PYTHON
	EXPORT  ss::string<32>  typename_for_type_id(int32_t type_id);
#endif
	template<typename T> inline constexpr bool is_string_type(){ //
		//if constexpr(requires(T a) {(ss::string_&)a;})
		if constexpr(requires {typename T::is_string_type_t;})
			return true;
		return false;
	}

	template<typename T> inline constexpr bool is_vector_type(){ //
		if constexpr(requires {typename T::element_type_t;})
			return true;
		return false;
	}


	inline constexpr bool is_vector_type(int type_id){ //
		return !!(type_id & vector);
	}

	struct record_desc_t;
	struct field_desc_t;

	template<> constexpr uint32_t data_type<std::monostate>() { return none;}
	template<> constexpr uint32_t data_type<record_desc_t>() { return record_desc;}
	template<> constexpr uint32_t data_type<field_desc_t>() { return field_desc;}
	template<> constexpr uint32_t data_type<uint8_t>() { return uint8;}
	template<> constexpr uint32_t data_type<int8_t>() { return int8;}
	template<> constexpr uint32_t data_type<uint16_t>() { return uint16;}
	template<> constexpr uint32_t data_type<int16_t>() { return int16;}
	template<> constexpr uint32_t data_type<uint32_t>() { return uint32;}
	template<> constexpr uint32_t data_type<int32_t>() { return int32;}
	template<> constexpr uint32_t data_type<uint64_t>() { return uint32;}
	template<> constexpr uint32_t data_type<int64_t>() { return int64;}
	template<> constexpr uint32_t data_type<boolean_t>() { return boolean;}
	template<> constexpr uint32_t data_type<float32_t>() { return float32;}
	template<> constexpr uint32_t data_type<milliseconds_t>() { return milliseconds;}
	template<> constexpr uint32_t data_type<ss::string<>>() { return string;}

	template<typename T>
	requires(is_string_type<T>())
	constexpr uint32_t data_type() {
		return string;
	}

	template<typename T>
	requires(is_vector_type<T>() && !is_string_type<T>())
	constexpr uint32_t data_type() {
		return data_type<typename T::element_type_t>() | vector;
	}

	template<typename T> constexpr bool is_builtin_type(){ //built in scalar type; hardwired
		return (data_type<T>()& builtin)!=0;
	}

	inline bool is_builtin_type(int type_id){ //built in scalar type; hardwired
		return (type_id& builtin);
	}
};

/*
	returns serialized size when known at compile time.
	Needs to be specialised for any types which are considered built-ins
 */
template<typename data_t>
constexpr inline int32_t compile_time_serialized_size()  {
	static_assert(std::is_fundamental<data_t>::value || std::is_enum<data_t>::value, "compile time serialized size unknown");
	if(std::is_fundamental<data_t>::value || std::is_enum<data_t>::value)
		return sizeof(data_t);
}

template<>
constexpr inline int32_t compile_time_serialized_size<milliseconds_t>()  {
	milliseconds_t m;
	return compile_time_serialized_size<decltype(m.ms)>();
}



/*
	any type is either an elementary data_type_t type, a container vector|type, or
	a structure with user defined id or a vector of structure.
  The respective type_id is type, vector|type,  usr|structure, usr|structure|vector

 */
