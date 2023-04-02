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
import subprocess
import time
from neumodvb.util import dtdebug

class Speaker(object):
    def __init__(self):
        self.count = 0
        self.sat_pos = None
        self.sat = "no sat"
        self.nit_received = False
        from shutil import which
        self.cmd = which('espeak')
        self.p = None

    def execute_unix(self, inputcommand):
        if False:
            subprocess.run(inputcommand)
        else:
            if self.p is not None:
                self.p.poll()
                x= self.p.returncode
                if  x is None:
                    dtdebug(f"previous speak command still running; skipping; poll={x}")
                    return
                elif x != 0:
                    dterror(f"espeak failed: errror={self.p.returncode}")
            self.p = subprocess.Popen(inputcommand,  shell=False)
            dtdebug(f'espeak: {inputcommand}')
    def speak_string(self, x):
        if self.cmd is not None:
            c = [self.cmd,  '-ven-us+f4', '-k5', '-s170', x]
            self.execute_unix(c)

    def update_sat(self, sat_pos, nit_received):
        self.nit_received = nit_received
        if self.sat_pos == sat_pos:
            return
        self.count = 0
        self.sat_pos = sat_pos
        if sat_pos is None:
            self.sat='no sat'
        else:
            import pychdb
            sat = pychdb.sat_pos_str(sat_pos)
            self.sat = sat.replace('E', ' east').replace('W', 'west')

    def speak(self, sat_pos, snr, nit_received):
        self.update_sat(sat_pos, nit_received)
        if self.count >= 5:
            self.count =0
        text = []
        if self.count == 0:
            text.append(self.sat)
        self.count += 1
        snr = "not locked" if snr is None else str(snr)
        text.append(snr)
        text = ('. ' if self.nit_received else '? ').join(text)
        self.speak_string(text)
