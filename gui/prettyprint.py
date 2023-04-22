#!/usr/bin/python3
# Neumo dvb (C) 2019-2023 deeptho@gmail.com
# Copyright notice:
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
class StdStringPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return self.val['_M_dataplus']['_M_p'].string()

def str_lookup_function(val):
    lookup_tag = val.type.tag
    if lookup_tag == None:
        return None
    regex = re.compile("^std::basic_string<char,.*>$")
    if regex.match(lookup_tag):
        return StdStringPrinter(val)
    return None

class SSPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        #return str(self.val['header'].address)
        eval_string = "(*("+str(self.val.type)+"*)("+str(self.val['header'].address)+")).c_str()"
        #return str(self.val.type.code)
        v=gdb.parse_and_eval(eval_string).string()
        return f'ss::str: "{v}"';
        #return self.val['c_str'](self.val) #['_M_dataplus']['_M_p'].c_str()

    #gdb.pretty_printers['^std::basic_string<char,.*>$'] = StdStringPrinter

class DBPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        #return str(self.val['header'].address)
        v1 = "(*("+str(self.val.type)+"*)("+str(self.val['header'].address)+"))"
        eval_string = f"{v1}.buffer()[0]@{v1}.size()"
        #return str(self.val.type.code)
        #return eval_string
        v=gdb.parse_and_eval(eval_string)
        return v
        #return self.val['c_str'](self.val) #['_M_dataplus']['_M_p'].c_str()

    #gdb.pretty_printers['^std::basic_string<char,.*>$'] = StdStringPrinter

def my_pp_func(val):
  if str(val.type).startswith('std::basic_string<char'):
      return StdStringPrinter(val)
  if str(val.type).startswith('ss::string') and  val.type.code != gdb.TYPE_CODE_PTR:
      return SSPrinter(val)
  #print(str(val.type))
  if str(val.type).startswith('ss::vector') and  val.type.code != gdb.TYPE_CODE_PTR:
      return DBPrinter(val)

gdb.pretty_printers.append(my_pp_func)
