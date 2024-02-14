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
from neumodvb.dvbs_muxlist import DvbsBasicMuxGrid
from neumodvb.util import dtdebug, dterror

import pychdb

MuxSelectEvent, EVT_MUX_SELECT = wx.lib.newevent.NewCommandEvent()

class DvbsMuxGridPopup(DvbsBasicMuxGrid):
    """
    grid which appears in dvbs_mux list popup
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        lnb = find_parent_prop(self, 'lnb')
        sat = find_parent_prop(self, 'sat')
        self.sat = sat
        super().OnWindowCreate(evt)
        self.SelectSat(sat)

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.selected_row is not None:
                wx.CallAfter(self.OnSelectMux, self.selected_row)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def OnSelectMux(self, selected_row):
        if self.selected_row is not None:
            mux = self.table.GetValue(selected_row, None)
            self.Parent.GrandParent.OnSelectMux(mux)

    def EditMode(self):
        return  False

    def GetItemText(self, rowno):
        mux = self.table.GetValue(rowno, None)
        return str(mux)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno

class DvbsMuxListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = '28.2E 12709H-12'
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+ (6 if wxpythonversion < wxpythonversion42 else 1))
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(DvbsMuxGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.sat = None
        self.window_for_computing_width = None

    def SetMux(self, mux):
        """
        Called by parent window to intialise state
        """
        self.mux = mux
        self.SetText(self.CurrentGroupText())

    def OnSelectMux(self, mux):
        """Called when user selects a mux
        """
        dtdebug(f'satlist_combo received OnSelectMux {mux}')
        self.mux = mux
        wx.PostEvent(self, MuxSelectEvent(wx.NewIdRef(), mux=mux))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        return str(self.mux)

    def SetSat(self, sat):
        """
        Set sat from external (not by user)
        """
        dtdebug(f'muxlist_combo received SetSat {sat}')
        self.sat = sat
        if self.popup.popup_grid is not None:
            self.popup.popup_grid.SelectSat(sat)

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
