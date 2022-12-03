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
import sys
import os
import copy


from neumodvb.util import setup, lastdot
from neumodvb.neumo_dialogs_gui import  LnbNetworkDialog_
from neumodvb.lnbnetworklist import LnbNetworkGrid

class LnbNetworkDialog(LnbNetworkDialog_):
    def __init__(self, parent, basic, readonly, *args, **kwds):
        self.basic = basic
        self.readonly = readonly
        super().__init__(parent, *args, **kwds)
        self.lnbnetworkgrid = None

    def Prepare(self, lnbgrid):
        self.lnbnetworkgrid = LnbNetworkGrid(self.basic, self.readonly, self.lnbnetworklist_panel, \
                                             wx.ID_ANY, size=(1, 1))
        self.lnbgrid = lnbgrid
        self.lnbnetworkgrid_sizer.Add(self.lnbnetworkgrid, 1, wx.ALL | wx.EXPAND | wx.FIXED_MINSIZE, 1)
        self.Layout()

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def OnNew(self, event):
        self.lnbnetworkgrid.OnNew(event)
        event.Skip()

    def OnDelete(self, event):
        self.lnbnetworkgrid.OnDelete(event)
        event.Skip()

    def OnDone(self, event):
        self.lnbnetworkgrid.OnDone(event)
        self.lnbnetworkgrid_sizer.Remove(1) #remove current grid
        self.lnbnetworklist_panel.RemoveChild(self.lnbnetworkgrid)
        lnbnetworkgrid = self.lnbnetworkgrid
        self.lnbnetworkgrid = None
        wx.CallAfter(lnbnetworkgrid.Destroy)
        event.Skip()

    def OnCancel(self, event):
        self.lnbnetworkgrid_sizer.Remove(1) #remove current grid
        self.lnbnetworklist_panel.RemoveChild(self.lnbnetworkgrid)
        self.lnbnetworkgrid.Destroy()
        self.lnbnetworkgrid = None
        event.Skip()
