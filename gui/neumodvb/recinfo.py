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
import pyepgdb
import pyrecdb

class RecInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.ChangeValue("test ")
        self.SetDefaultStyle(wx.TextAttr(wx.RED))
        self.AppendText("Red text\n")
        f = self.GetFont()
        self.SetDefaultStyle(wx.TextAttr(wx.NullColour, font=f.Bold()))
        self.AppendText("Red on grey text\n")
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=f))
        self.AppendText("Blue on grey text\n")

    def ShowRecord(self, table, rec):
        wx.GetApp().currently_selected_rec = rec
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        t =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = self.GetFont()
        if rec is not None:
            large.SetPointSize(int(f.GetPointSize()*1.5))
            self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
            self.ChangeValue(f"{str(rec.service.ch_order)}: {rec.service.name}\n\n")
            self.SetDefaultStyle(wx.TextAttr(wx.BLACK, font=f))
            self.AppendText(f"{dt(rec.real_time_start)} -{dt(rec.real_time_end)}\n")
            self.AppendText(f"{rec.epg.event_name}\n")
            self.AppendText(f"{rec.epg.story}\n")
