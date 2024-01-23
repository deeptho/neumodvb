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
import datetime
from dateutil import tz

import pydevdb
import pychdb

from neumodvb.util import dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str
from neumodvb.signalhistory_dialog_gui import SignalHistoryDialog_, MuxListPanel_
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.signalhistoryplot import SignalType
import pyreceiver
from pyreceiver import get_object as get_object_

class MuxListPanel(MuxListPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent
        self.sat_sel.window_for_computing_width = self

    def OnRowSelect(self, evt):
        dtdebug(f"ROW SELECT {evt.GetRow()}")
    def OnGroupShowAll(self, evt):
        print('show all')
        self.sat_sel.SetSat(None, True)

class SignalHistoryDialog(SignalHistoryDialog_):

    def __init__(self, parent, mux=None, sat=None, lnb=None,*args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

        self.parent = parent
        self.mux, self.sat, self.lnb = mux, sat, lnb
        self.SetTitle(f'Signal history - sat={self.sat} {self.mux.frequency/1000:.03f}{enum_to_str(self.mux.pol)}')

        self.Bind(wx.EVT_CLOSE, self.OnClose) #ony if a nonmodal dialog is used

        self.app_main_frame = wx.GetApp().frame
        self.signal_type = SignalType.SNR
        wx.CallAfter(self.signalhistory_plot.show_signal, mux, self.signal_type)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        print(f'SignalHistory WindowCreate lnb={self.lnb} sat={self.sat} mux={self.mux}')
        self.muxlist_panel.muxselect_grid.SelectMux(self.mux)
        self.muxlist_panel.muxselect_grid.SelectSat(self.sat)
        self.muxlist_panel.sat_sel.SetSat(self.sat)

    @property
    def grid(self):
        return self.muxlist_panel.muxselect_grid

    def Close(self):
        self.muxlist_panel.Close()

    def OnClose(self, evt):
        dtdebug("CLOSE DIALOG")
        self.Close()
        wx.CallAfter(self.Destroy)
        evt.Skip()

    def CmdExit(self, evt):
        return wx.GetApp().frame.CmdExit(evt);

    def OnMuxSelect(self, evt):
        rowno = self.grid.GetGridCursorRow()
        mux = self.grid.table.GetRow(rowno)
        self.signalhistory_plot.toggle_signal(mux, self.signal_type)

    def OnSignalTypeSelect(self, evt):
        self.signal_type = SignalType(evt.Int)
        self.signalhistory_plot.change_signal_type(self.signal_type)

def show_signalhistory_dialog(caller, sat=None, mux=None):
    dlg = SignalHistoryDialog(caller, sat=sat, mux=mux)
    dlg.Show()
    return None
    dlg.Close(None)
    if val == wx.ID_OK:
        try:
            pass
        except:
            pass
    dlg.Destroy()
    return None
