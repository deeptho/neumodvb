#!/usr/bin/python3
# Neumo dvb (C) 2019-2024 deeptho@gmail.com
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

import wx
import wx.grid
import wx.lib.mixins.listctrl as listmix
import sys
import os
import copy
import datetime
from dateutil import tz

from neumodvb.neumodbutils import enum_to_str
from neumodvb.util import get_last_scan_text_dict


class ScanStatusTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.scan_in_progress = False

    def ShowScanRecord(self, scan_stats):
        f = self.GetFont()
        large = self.GetFont()
        self.scan_in_progress = not scan_stats.finished
        large.SetPointSize(int(f.GetPointSize()*1.2))
        last_scan_text_dict = get_last_scan_text_dict(scan_stats)
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        self.ChangeValue(f"{last_scan_text_dict['muxes']}; ")
        self.SetDefaultStyle(wx.TextAttr(wx.ColourDatabase().Find('MAGENTA'), font=large.Bold()))
        self.AppendText(f"{last_scan_text_dict['peaks']}; ")
        if scan_stats.pending_bands + scan_stats.active_bands > 0:
            self.SetDefaultStyle(wx.TextAttr(wx.GREEN, font=large.Bold()))
            self.AppendText(f"{last_scan_text_dict['bands']}")
        return
