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
import wx.lib.newevent
import sys
import os
import copy
from collections import namedtuple, OrderedDict
import numbers
import datetime
from dateutil import tz
import regex as re

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import GridPopup
from neumodvb.satlist import BasicSatGrid
from neumodvb.util import dtdebug, dterror

import pychdb

SatSelectEvent, EVT_SAT_SELECT = wx.lib.newevent.NewCommandEvent()

class SatGridPopup(BasicSatGrid):
    """
    grid which appears in dvbs_mux list popup
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.selected_row is not None:
                sat = self.table.GetValue(self.selected_row, None)
                self.Parent.GrandParent.OnSelectSat(sat)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return  False

    def GetItemText(self, rowno):
        rec = self.table.GetValue(rowno, None)
        return str(rec)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno

class SatListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = 'All Satellites'+' '*4
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        #self.font.SetPointSize(self.font.GetPointSize()+6)
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(SatGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.sat = None
        self.allow_all = True
        self.window_for_computing_width = None

    def SetSat(self, sat, allow_all=False):
        """
        Called by parent window to intialise state
        """
        self.sat, self.allow_all = sat, allow_all
        self.SetText(self.CurrentGroupText())

    def OnSelectSat(self, sat):
        """Called when user selects a sat
        """
        self.sat = sat
        wx.PostEvent(self, SatSelectEvent(wx.NewIdRef(), sat=sat))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.sat is None:
            return "All satellites" if self.allow_all else ""
        return str(self.sat.name if len(self.sat.name)>0 else str(self.sat))

    def show_all(self):
        """
        Instead of a single satellite show all of them
        """
        wx.PostEvent(self, SatSelectEvent(wx.NewIdRef(), sat=None))
        self.sat = None
        cgt = self.CurrentGroupText()
        w,h = self.font_dc.GetTextExtent(self.example)
        button_size = self.GetButtonSize()
        self.SetMinSize((w+button_size.width,h))
        self.SetPopupMinWidth(w)
        self.SetValue(cgt)

    def OnWindowCreate(self, evt):
        """
        Attach an event handler, but make sure it is called only once
        """
        if evt.GetWindow() != self:
            return
        cgt = self.CurrentGroupText()
        w,h = self.font_dc.GetTextExtent(self.example)
        button_size = self.GetButtonSize()
        self.SetMinSize((w+button_size.width,h))
        self.SetPopupMinWidth(w)
        self.SetValue(cgt)
        evt.Skip(True)
