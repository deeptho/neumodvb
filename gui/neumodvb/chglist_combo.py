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

from neumodvb.util import setup, lastdot, find_parent_prop, wxpythonversion, wxpythonversion42
from neumodvb import neumodbutils
from neumodvb.neumolist import GridPopup
from neumodvb.chglist import BasicChgGrid
from neumodvb.util import dtdebug, dterror

import pychdb

ChgSelectEvent, EVT_CHG_SELECT = wx.lib.newevent.NewCommandEvent()

class ChgGridPopup(BasicChgGrid):
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
                chg = self.table.GetValue(self.selected_row, None)
                self.Parent.GrandParent.OnSelectChg(chg)
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

class ChgListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = 'BySkyB Bouquet 12 - commercial scotland'+' '*8
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+ (6 if wxpythonversion < wxpythonversion42 else 1))
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(ChgGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.chg = None
        self.allow_all = True

    def SetChg(self, chg, allow_all=False):
        """
        Called by parent window to intialise state
        """
        self.chg, self.allow_all = chg, allow_all
        self.SetText(self.CurrentGroupText())

    def OnSelectChg(self, chg):
        """Called when user selects a chg
        """
        dtdebug(f'satlist_combo received OnSelectChg {chg}')
        self.chg = chg
        wx.PostEvent(self, ChgSelectEvent(wx.NewIdRef(), chg=chg))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.chg is None:
            return "All Groups" if self.allow_all else ""
        return str(self.chg)


    def show_all(self):
        """
        Instead of a single group show all of them
        """
        dtdebug(f'showall chg select={None}')
        wx.PostEvent(self, ChgSelectEvent(wx.NewIdRef(), chg=None))
        self.chg = None
        cgt = self.CurrentGroupText()
        w,h = self.font_dc.GetTextExtent(self.example)
        self.SetMinSize((w,h))
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
