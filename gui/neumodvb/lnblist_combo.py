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
from neumodvb.lnblist import BasicLnbGrid
from neumodvb.lnbconnectionlist import BasicLnbConnectionGrid
from neumodvb.util import dtdebug, dterror

import pychdb
import pydevdb

LnbSelectEvent, EVT_LNB_SELECT = wx.lib.newevent.NewCommandEvent()
RfPathSelectEvent, EVT_RF_PATH_SELECT = wx.lib.newevent.NewCommandEvent()

class LnbGridPopup(BasicLnbGrid):
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
                lnb = self.table.GetValue(self.selected_row, None)
                self.Parent.GrandParent.OnSelectLnb(lnb)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return  False

    def GetItemText(self, rowno):
        lnb = self.table.GetValue(rowno, None)
        return str(lnb)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno

class LnbListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = 'D0 unv [13355] 30.0W'
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+6)
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(LnbGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.lnb = None
        self.window_for_computing_width = None

    def SetLnb(self, rf_path, lnb):
        """
        Called by parent window to intialise state
        """
        self.rf_path, self.lnb = rf_path, lnb
        self.SetText(self.CurrentGroupText())

    def OnSelectLnb(self, lnb):
        """Called when user selects a lnb
        """
        dtdebug(f'lnblist_combo received OnSelectLnb {lnb}')
        self.lnb = lnb
        wx.PostEvent(self, LnbSelectEvent(wx.NewIdRef(), lnb=lnb))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.lnb is None:
            return ""
        return str(self.lnb)

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

class RfPathGridPopup(BasicLnbConnectionGrid):
    """
    grid which appears on positioner_dialog
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        lnb = self.Parent.GrandParent.lnb
        rf_path = self.Parent.GrandParent.rf_path
        self.SetRfPath(rf_path, lnb)
        super().OnWindowCreate(evt)

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.selected_row is not None:

                lnb_connection = self.table.GetValue(self.selected_row, None)
                rf_path = pydevdb.lnb.rf_path_for_connection(self.lnb.k, lnb_connection)
                self.Parent.GrandParent.OnSelectRfPath(rf_path)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return  False

    def GetItemText(self, rowno):
        rf_input = self.table.GetValue(rowno, None)
        return str(rf_input)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno

class LnbRfPathListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.example = 'TBS 6909X C0#3 '
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+6)
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(RfPathGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.lnb, self.rf_path = None, None
        self.window_for_computing_width = None

    @property
    def lnb_connection (self):
        return pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)

    def SetRfPath(self, rf_path, lnb):
        """
        Called by parent window to intialise state
        """
        self.rf_path, self.lnb = rf_path, lnb
        self.SetText(self.CurrentGroupText())

    def OnSelectRfPath(self, rf_path):
        """Called when user selects an rf_path
        """
        dtdebug(f'lnblist_combo received OnSelectRfPath {rf_path}')
        self.rf_path = rf_path
        wx.PostEvent(self, RfPathSelectEvent(wx.NewIdRef(), rf_path=rf_path))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.rf_path is None or self.lnb is None:
            return ""
        return str(self.lnb_connection.connection_name)

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
