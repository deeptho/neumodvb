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
import importlib
import sys
from io import StringIO
from neumodvb.util import dtdebug, dterror

def major_upgrade(stored_version, current_version):
    old_stdout = sys.stdout
    sys.stdout = StringIO()
    try:
        upg = importlib.import_module('neumodvb.upgrade.major_upgrade_2_3')
        upgrader = upg.upgrader_t()
        dtdebug(f'Starting major upgrade from version {stored_version} to version {current_version}')
        ret = upgrader.upgrade(stored_version, current_version)
        msg = sys.stdout.getvalue()
        del upgrader
    except:
        ret = False
    msg=sys.stdout.getvalue()
    sys.stdout = old_stdout
    return ret, msg
