#!/usr/bin/python3
# Neumo dvb (C) 2019-2022 deeptho@gmail.com
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
import pyepgdb

class ServiceInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.last_scan_text = None
        self.scan_done = False

    def ShowRecord(self, table, rec):
        if self.last_scan_text is not None:
            self.ChangeValue(self.last_scan_text)
            if self.scan_done:
                self.last_scan_text = None
            return
        ls = wx.GetApp().live_service_screen
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.2))
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        app = wx.GetApp()
        num_services = table.screen.list_size
        self.ChangeValue(f"{num_services} service" if num_services == 1 else f"{num_services} services" )

    def ShowScanRecord(self, panel, data):
        st = data.scan_stats
        done = st.pending_muxes + st.active_muxes == 0
        pending = st.pending_muxes
        ok = st.locked_muxes
        active = st.active_muxes
        if pending+active == 0:
            self.scan_done = True
        else:
            self.scan_done = False
        self.last_scan_text = f" ok={ok} failed={st.failed_muxes} pending={pending} active={active}"
        self.ShowRecord(panel.grid.table, panel.grid.table.CurrentlySelectedRecord())
