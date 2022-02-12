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
            self.AppendTeext("Red on grey text\n")
            self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=f))
            self.AppendText("Blue on grey text\n")
        self.last_scan_text = ""

    def ShowRecord(self, mux):
        if mux is None:
            h = wx.GetApp().receiver.browse_history
            sat = h.h.dvbs_muxlist_filter_sat
            self.ChangeValue(f"{str(sat)}: No muxes")
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        app = wx.GetApp()
        if False and app.scan_subscription_id >= 0:
            st = app.receiver.get_scan_stats(app.scan_subscription_id)
            if st.last_scanned_mux.k.sat_pos != pychdb.sat.sat_pos_none:
                self.ChangeValue(f"Scanning: last={st.last_scanned_mux}")
            else:
                self.ChangeValue(f"Scanning: ...")
            self.SetDefaultStyle(wx.TextAttr(wx.RED, font=large.Bold()))
            if st.scheduled_muxes !=0:
                pending = st.scheduled_muxes
                ok = st.finished_muxes - st.failed_muxes
                self.last_scan_text = f" {ok} ok / {st.failed_muxes} failed / {pending} pending"
                self.AppendText(self.last_scan_text)
            else:
                return
        else:
            self.ChangeValue(f"{str(mux)}: {mux.c.num_services} services.")


    def ShowScanRecord(self):
        f = self.GetFont()
        large = self.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=large.Bold()))
        app = wx.GetApp()
        if app.scan_subscription_id >= 0:
            st = app.receiver.get_scan_stats(app.scan_subscription_id)
            done = st.scheduled_muxes + st.active_muxes == 0
            if st.last_subscribed_mux.k.sat_pos != pychdb.sat.sat_pos_none:
                if done:
                    self.ChangeValue(f"Scanning: DONE")
                else:
                    self.ChangeValue(f"Scanning {st.last_subscribed_mux}:")
            elif st.scheduled_muxes !=0:
                self.ChangeValue(f"Scanning: ...")
            self.SetDefaultStyle(wx.TextAttr(wx.RED, font=large.Bold()))
            pending = st.scheduled_muxes
            ok = st.finished_muxes - st.failed_muxes
            self.last_scan_text = f" ok={ok} failed={st.failed_muxes} pending={pending} active={st.active_muxes}"
            self.AppendText(self.last_scan_text)
            return done
        return False
