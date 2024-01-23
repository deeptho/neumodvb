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

from neumodvb.preferences_dialog_gui import  PreferencesDialog_
from neumodvb.neumowidgets import DurationTextCtrl, DtIntCtrl

if False:
    import re
    import wx.lib.newevent
    import math

    from neumodvb import  minispinctrl, minifloatspin
    from neumodvb.positioner_dialog_gui import  PositionerDialog_, SignalPanel_ , TuneMuxPanel_
    from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
    from neumodvb import neumodbutils
    from neumodvb.lnblist import has_network, get_network, must_move_dish
    from neumodvb.util import setup, lastdot
    from neumodvb.util import dtdebug, dterror
    from neumodvb.satlist_combo import EVT_SAT_SELECT
    from neumodvb.lnblist_combo import EVT_LNB_SELECT, EVT_RF_PATH_SELECT
    from neumodvb.muxlist_combo import EVT_MUX_SELECT

    import pyreceiver
    import pychdb
    import pydevdb
    import pystatdb
    from pyreceiver import get_object as get_object_

class PreferencesDialog(PreferencesDialog_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent
        self.app = wx.GetApp()
        opts =  self.app.receiver.get_options()
        print(f'{opts}')
        #[e for e in dir(self) if e.endswith('_text')]
        print(f"{[e for e in dir(self) if type(getattr(self,e))==DurationTextCtrl]}")
        print(f"{[e for e in dir(self) if type(getattr(self,e))==wx.TextCtrl]}")
        print(f"{[e for e in dir(self) if type(getattr(self,e))==DtIntCtrl]}")

    def Close(self):
        pass

    def OnClose(self, evt):
        dtdebug("CLOSE")
        self.Close()
        wx.CallAfter(self.Destroy)
        evt.Skip()

def show_preferences_dialog(parent):
    dlg = PreferencesDialog(parent, wx.ID_ANY, "")
    dlg.Show()
    return None
