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
from neumodvb.util import dtdebug, dterror

import pychdb

SatBandSelectEvent, EVT_SATBAND_SELECT = wx.lib.newevent.NewCommandEvent()

class SatBandChoice(wx.Choice):
    def __init__(self, parent, id, value, **kwds):
        self.sat_band = None
        self.allow_all = True
        #self.sat_band = pychdb.sat_band_t.Ku
        value=neumodbutils.enum_to_str(self.sat_band)
        d = neumodbutils.enum_values_and_labels(pychdb.sat_band_t)
        from collections import OrderedDict
        self.sat_band_dict = OrderedDict(((k,v) for k,v in d.items() if int(k)>=0))
        self.all_value = len(self.sat_band_dict)
        self.sat_band_dict[self.all_value] = "All bands"

        super().__init__(parent, id, choices=[*self.sat_band_dict.values()])
        self.font_dc =  wx.ScreenDC()
        self.font = self.GetFont()
        self.SetFont(self.font)
        self.font_dc.SetFont(self.font) # for estimating label sizes
        #self.SetSatBand(self.sat_band, self.allow_all)
        self.Bind(wx.EVT_CHOICE, self.OnSelectSatBand)

    def SetSatBand(self, sat_band, allow_all=False):
        """
        Called by parent window to intialise state
        """
        self.sat_band, self.allow_all = sat_band, allow_all
        value = self.all_value if self.sat_band is None else int(self.sat_band)
        self.SetSelection(value)


    def OnSelectSatBand(self, evt):
        """Called when user selects a sat
        """
        self.sat_band = pychdb.sat_band_t(evt.Int)
        wx.PostEvent(self, SatBandSelectEvent(wx.NewIdRef(), sat_band=self.sat_band))


    def show_all(self):
        """
        Instead of a single satellite show all of them
        """
        print(f'show_all')
        self.SetSatBand(None, allow_all=True)
        wx.PostEvent(self, SatBandSelectEvent(wx.NewIdRef(), sat_band=None))

    def OnWindowCreateOFF(self, evt):
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
