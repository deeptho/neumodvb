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
#include <unistd.h>
#include <stdint.h>
#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif


//The enum type
namespace {{dbname}} {

	{%for enum in enums %}
	enum class {{enum.name}} : {{enum.storage}}
	{
    {%for f in enum.values %}
		{{ f['type'] }} {{ f.name }}
		{%-if f.val is not none %} = {{f.val}}{%-endif%},
    {% endfor %}

	};

	constexpr auto to_str({{enum.name}} v) {
		switch(v) {
    {%for f in enum.values %}
		case {{enum.name}}::{{f.name}}: return "{{f.short_name}}";
    {% endfor %}
		default: return "";
		};
	}

	{%endfor%}

} //end of namespace {{dbname}}


namespace data_types {
		{%for enum in enums %}
	template<> inline constexpr uint32_t data_type<{{dbname}}::{{enum.name}}>()
	{ return enumeration| builtin | data_types::data_type<{{enum.storage}}>();
			//|{{enum.type_id_}};
	}

	{%endfor%}
};
