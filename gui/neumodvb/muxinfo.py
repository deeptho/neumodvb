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


import pychdb

class MuxInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        if False:
            self.ChangeValue("test ")
            self.SetDefaultStyle(wx.TextAttr(wx.RED))
            self.AppendText("Red text\n")
            f = self.GetFont()
            self.SetDefaultStyle(wx.TextAttr(wx.NullColour, font=f.Bold()))
            self.AppendText("Red on grey text\n")
            self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=f))
            self.AppendText("Blue on grey text\n")
        self.last_scan_text = ""

    def ShowRecord(self, rec):
        if rec is None:
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        self.ChangeValue(f"{str(rec)}: {rec.c.num_services} services.")
        app = wx.GetApp()
        if app.scan_subscription_id>=0:
            self.SetDefaultStyle(wx.TextAttr(wx.RED, font=large.Bold()))
            st = app.receiver.get_scan_stats(app.scan_subscription_id)
            if st.scheduled_muxes !=0:
                pending = st.scheduled_muxes - st.finished_muxes
                ok = st.finished_muxes - st.failed_muxes
                self.last_scan_text = f" {ok} ok /{st.failed_muxes} failed / {pending} pending"
                self.AppendText(self.last_scan_text)
        if False:
            self.SetDefaultStyle(wx.TextAttr(wx.RED, font=f))
            self.AppendText(f"{rec.mux_desc} nid={rec.k.mux.network_id} tid={rec.k.mux.ts_id} ")
            self.AppendText(f"sid={rec.k.service_id} pmt={rec.pmt_pid}\n\n")
            self.SetDefaultStyle(wx.TextAttr(wx.BLACK, font=f))

            self.AppendText(f"Provider: {rec.provider}\n")
            self.AppendText(f"Modified: {dt(rec.mtime)}\n")
            exp = "Yes" if rec.expired else "No"
            enc = "Yes" if rec.encrypted else "No"
            self.AppendText(f"Encrypted: {enc} Expired: {exp}\n")
