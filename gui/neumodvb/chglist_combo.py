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
from neumodvb.chglist import BasicChgGrid
from neumodvb.util import dtdebug, dterror

import pychdb

class ChgGridPopup(BasicChgGrid):
    """
    grid which appears in dvbs_mux list popup
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.controller = self.Parent.Parent.Parent.controller

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.selected_row is not None:
                rec= self.table.GetValue(self.selected_row, None)
                self.controller.SelectChg(rec)
                wx.CallAfter(self.controller.SetFocus)
            self.Parent.Parent.Parent.GetPopupControl().Dismiss()
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return  False

    def OnDone(self, rec):
        self.controller.SelectChg(rec)
        self.controller.SetFocus()

    def GetItemText(self, rowno):
        rec = self.table.GetValue(rowno, None)
        return str(rec)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno

class ChgListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = 'BySkyB Bouquet 12 - commercial scotland'+' '*8
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+6)
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(ChgGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def show_all(self):
        self.controller.SelectChg(None)
        cgt = self.controller.CurrentGroupText()
        w,h = self.font_dc.GetTextExtent(self.example)
        self.SetMinSize((w,h))
        self.SetValue(cgt)
        wx.CallAfter(self.controller.SetFocus)

    def OnWindowCreate(self, evt):
        """
        Attach an event handler, but make sure it is called only once
        """
        #for some reacon EVT_WINDOW_CREATE is called many time when popup is shown
        #unbinding the event handler takes care of this
        self.Parent.Unbind (wx.EVT_WINDOW_CREATE)
        parent_grid = self.controller
        cgt = self.controller.CurrentGroupText()
        w,h = self.font_dc.GetTextExtent(self.example)
        self.SetMinSize((w,h))
        self.SetValue(cgt)
        evt.Skip(True)
