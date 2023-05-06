#!/usr/bin/python3
# Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
import os
import sys
import os
import wx.glcanvas #needed for the created mpvglcanvas
import wx
import gettext
import signal
from functools import lru_cache
#the following import also sets up import path
import neumodvb

from neumodvb.util import load_gtk3_stylesheet, dtdebug, dterror, maindir, get_object
from neumodvb.config import options, get_configfile

import pydevdb
import pychdb
import pyepgdb
import pyrecdb
import pystatdb
import pyneumompv
import pyreceiver
q=pychdb.key_src_str(pychdb.key_src_t.PAT_TUNED)
