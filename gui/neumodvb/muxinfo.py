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

import pychdb

class MuxInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)

    def ShowRecord(self, table, mux):
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.2))
        num_muxes = table.screen.list_size
        app = wx.GetApp()
        if app.scan_in_progress:
            self.SetDefaultStyle(wx.TextAttr(wx.BLACK, font=large.Bold()))
            val=f"{num_muxes} mux" if num_muxes == 1 else f"{num_muxes} muxes"
            self.ChangeValue(f'{val}; ')
            self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
            self.AppendText(f"{app.last_scan_text_dict['muxes']}; ")
            self.SetDefaultStyle(wx.TextAttr(wx.ColourDatabase().Find('MAGENTA'), font=large.Bold()))
            self.AppendText(f"{app.last_scan_text_dict['peaks']}; ")
            self.SetDefaultStyle(wx.TextAttr(wx.GREEN, font=large.Bold()))
            self.AppendText(f"{app.last_scan_text_dict['bands']}")
            return
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        self.ChangeValue(f"{num_muxes} mux" if num_muxes == 1 else f"{num_muxes} muxes" )


    def ShowScanRecord(self, panel):
        self.ShowRecord(panel.grid.table, panel.grid.table.CurrentlySelectedRecord())
