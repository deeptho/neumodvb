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


from neumodvb.util import setup
from neumodvb.neumodbutils import enum_to_str
from neumodvb.chepglist import content_types

import pychdb
import pyepgdb


class ChEpgInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
    def ShowRecord(self, table, rec):
        if rec is None:
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        t =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, wx.WHITE, font=large.Bold()))
        self.ChangeValue(f"{rec.event_name}\n\n")

        self.SetDefaultStyle(wx.TextAttr(wx.BLACK, wx.WHITE, font=f))
        self.AppendText(f"{content_types(rec.content_codes)}\n\n")

        self.AppendText(f"{dt(rec.k.start_time)} - {t(rec.end_time)}\n\n")

        self.SetDefaultStyle(wx.TextAttr(wx.BLACK, wx.WHITE, font=f))
        self.AppendText(f"{rec.story}\n\n")

        self.SetDefaultStyle(wx.TextAttr(wx.RED, font=f))
        self.AppendText(str(rec.source))
