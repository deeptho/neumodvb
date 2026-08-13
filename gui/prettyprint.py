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
import gdb
import string
import subprocess
import sys

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
        size=int(gdb.parse_and_eval(f"{v1}.size()"))
        if size == 0:
            return "empty"
            pass
        else:
            eval_string = f"{v1}.buffer()[0]@{size}"
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
  #if str(val.type).startswith('ss::vector') and  val.type.code != gdb.TYPE_CODE_PTR:
  #    return DBPrinter(val)

gdb.pretty_printers.append(my_pp_func)


class ShellPipe (gdb.Command):
    "Command to pipe gdb internal command output to external commands."

    def __init__(self):
        super (ShellPipe, self).__init__("shell-pipe",
                gdb.COMMAND_DATA,
                gdb.COMPLETE_NONE, True)
        gdb.execute("alias -a sp = shell-pipe", True)

    def invoke(self, arg, from_tty):
        arg = arg.strip()
        if arg == "":
            print("Argument required (gdb_command_and_args | externalcommand..).")
            return

        gdb_command, shell_commands = None, None

        if '|' in arg:
            gdb_command, shell_commands = arg.split("|", maxsplit=1)
            gdb_command, shell_commands = gdb_command.strip(), shell_commands.strip()
        else:
            gdb_command = arg

        # Collect the output and feed it through the pipe
        output = gdb.execute(gdb_command, True, True)
        if shell_commands:
            shell_process = subprocess.Popen(shell_commands, stdin=subprocess.PIPE, shell=True)
            shell_process.communicate(output.encode('utf-8'))
        else:
            sys.stdout.write(output)

ShellPipe()

"""
example: shell-pipe info sharedlib | grep Yes
"""
