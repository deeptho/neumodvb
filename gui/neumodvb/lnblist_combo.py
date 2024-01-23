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

from neumodvb.util import setup, lastdot, wxpythonversion, wxpythonversion42
from neumodvb import neumodbutils
from neumodvb.neumolist import GridPopup
from neumodvb.lnblist import BasicLnbGrid
from neumodvb.lnbconnectionlist import BasicLnbConnectionGrid
from neumodvb.lnbnetworklist import BasicLnbNetworkGrid
from neumodvb.util import dtdebug, dterror
from neumodvb.util import find_parent_prop
import pychdb
import pydevdb

from neumodvb.satlist_combo import SatSelectEvent
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
        self.font.SetPointSize(self.font.GetPointSize()+ (6 if wxpythonversion < wxpythonversion42 else 1))
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(LnbGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.lnb_ = None
        self.window_for_computing_width = None

    def Update(self):
        """
        called when user selects LNB
        """
        self.lnb_ = None # force reread
        self.SetText(self.CurrentGroupText())

    @property
    def lnb(self):
        if self.lnb_ is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
        return self.lnb_

    def OnSelectLnb(self, lnb):
        """Called when user selects a lnb
        """
        dtdebug(f'lnblist_combo received OnSelectLnb {lnb}')
        self.lnb_ = lnb
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
        self.lnb_ = None
        #self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    @property
    def lnb(self):
        if self.lnb_ is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
            txn = wx.GetApp().devdb.rtxn()
            self.lnb_= pydevdb.lnb.find_by_key(txn, self.lnb_.k) #reread the networks
            txn.abort()
            del txn
        return self.lnb_

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        lnb = self.Parent.GrandParent.lnb
        rf_path = self.Parent.GrandParent.rf_path
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
        self.font.SetPointSize(self.font.GetPointSize()+ (6 if wxpythonversion < wxpythonversion42 else 1))
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(RfPathGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.lnb_, self.rf_path_ = None, None
        self.window_for_computing_width = None
        self.Bind(wx.EVT_COMBOBOX_CLOSEUP, self.OnEndPopup)

    def Update(self):
        """
        called when user selects LNB
        """
        self.lnb_, self.rf_path_ = None, None # force reread
        self.SetText(self.CurrentGroupText())

    @property
    def lnb(self):
        if self.lnb_ is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
        return self.lnb_

    @property
    def rf_path(self):
        if self.rf_path_ is None:
            self.rf_path_ = find_parent_prop(self, 'rf_path')
        return self.rf_path_

    @property
    def lnb_connection (self):
        return pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)

    def OnEndPopup(self, evt):
        popup = self.popup
        self.popup = GridPopup(RfPathGridPopup)
        self.SetPopupControl(self.popup)
        del popup

    def OnSelectRfPath(self, rf_path):
        """Called when user selects an rf_path
        """
        dtdebug(f'lnblist_combo received OnSelectRfPath {rf_path}')
        wx.PostEvent(self, RfPathSelectEvent(wx.NewIdRef(), rf_path=rf_path))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.rf_path is None or self.lnb is None:
            return ""
        return '???' if self.lnb_connection is None else str(self.lnb_connection.connection_name)

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

class LnbNetworkSatGridPopup(BasicLnbNetworkGrid):
    """
    grid which appears on positioner_dialog
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.network_ = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.lnb_ = None
        #self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    @property
    def lnb(self):
        if self.lnb_ is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
            txn = wx.GetApp().devdb.rtxn()
            self.lnb_= pydevdb.lnb.find_by_key(txn, self.lnb_.k) #reread the networks
            txn.abort()
            del txn
        return self.lnb_

    @property
    def sat(self):
        return self.Parent.GrandParent.sat
    @sat.setter
    def sat(self, val):
        self.Parent.GrandParent.sat = val
        self.SetSat(val)

    @property
    def network(self):
        if self.network_ is None and self.sat is not None:
            self.SetSat(self.sat)
        return self.network_
    @network.setter
    def network(self, val):
        self.network_ = val

    def SetSat(self, sat):
        if self.lnb is None:
            return
        for network in self.lnb.networks:
            if network.sat_pos == sat.sat_pos:
                self.network_ = network
                return

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        sat = self.Parent.GrandParent.sat
        self.table.SetSat(sat)
        super().OnWindowCreate(evt)

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.selected_row is not None:

                lnb_network = self.table.GetValue(self.selected_row, None)
                sat = self.table.matching_sat(lnb_network.sat_pos)
                self.Parent.GrandParent.OnSelectLnbNetworkSat(sat)
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

class LnbNetworkSatListComboCtrl(wx.ComboCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.lnb_, self.sat_ = None, None
        self.example = 'TBS 6909X C0#3 '
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+ (6 if wxpythonversion < wxpythonversion42 else 1))
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        self.popup = GridPopup(LnbNetworkSatGridPopup)
        self.SetPopupControl(self.popup)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.lnb_, self.sat_ = None, None
        self.window_for_computing_width = None
        self.Bind(wx.EVT_COMBOBOX_CLOSEUP, self.OnEndPopup)

    def Update(self):
        """
        called when user selects LNB
        """
        self.lnb_, self.sat_ = None, None # force reread
        self.SetText(self.CurrentGroupText())

    @property
    def lnb(self):
        if self.lnb_ is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
        return self.lnb_

    @property
    def sat(self):
        if self.sat_ is None:
            self.sat_ = find_parent_prop(self, 'sat')
        return self.sat_

    @property
    def lnb_connection (self):
        return pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)

    def OnEndPopup(self, evt):
        popup = self.popup
        self.popup = GridPopup(LnbNetworkSatGridPopup)
        self.SetPopupControl(self.popup)
        del popup

    def OnSelectLnbNetworkSat(self, sat):
        """Called when user selects an sat
        """
        dtdebug(f'lnblist_combo received OnSelectLnbNetworkSat {sat}')
        wx.PostEvent(self, SatSelectEvent(wx.NewIdRef(), sat=sat))
        self.popup.Dismiss()
        self.SetText(self.CurrentGroupText())

    def CurrentGroupText(self):
        if self.sat is None or self.lnb is None:
            return ""
        return str(self.sat)

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
