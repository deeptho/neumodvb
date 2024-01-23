/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 *
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
#include "neumodb/{{dbname}}/{{dbname}}_db.h"

using namespace {{dbname}};
{% if false %}
{%for enum in enums %}
template<>
EXPORT const char* enum_to_str<{{enum.name}}>(const {{enum.name}}& val)
{
	switch(({{enum.storage}})val) {
		{%for f in enum.values %}
	case {{f.val}}:
	return "{{ f.display_name }}";
	break;
	{% endfor %}
	default:
		dterrorf("Illegal value for enum {{enum.name}}");
		return "????";
	}
};

{%endfor%}
{% endif %}


{%for enum in enums %}
template<>
bool enum_is_valid<{{enum.name}}>(const {{enum.name}}& val)
{
	switch(({{enum.storage}})val) {
		{%for f in enum.values %}
	case {{f.val}}:
	return true;
	break;
	{% endfor %}
	default:
		return false;
	}
};

{%endfor%}
