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

import sys
import os
import copy
import datetime
from dateutil import tz
from enum import Enum


from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb.neumo_dialogs_gui import StreamDialog_, StreamParameters_
from pydevdb import subscription_type_t
import pychdb
import pydevdb

class StreamParameters(StreamParameters_):
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)

    def init(self, parent, stream):
        self.parent = parent
        self.stream = stream
        self.receiver = wx.GetApp().receiver

    def Prepare(self):
        self.host_name.SetValue(self.stream.dest_host)
        self.port.SetValue(int(self.stream.dest_port))
        self.stream_state_choice.SetValue(self.stream.stream_state)
        self.autostart.SetValue(self.stream.autostart)
        self.autostart.SetValue(self.stream.preserve)

    def CheckCancel(self, event):
        event.Skip()


    def get_stream_state(self):
        return self.stream.stream_state

    def OnDone(self):
        self.stream.stream_state = self.stream_state_choice.GetValue()
        self.stream.dest_host = self.host_name.GetValue()
        self.stream.dest_port = int(self.port.GetValue())
        self.stream.autostart = bool(self.autostart.GetValue())
        self.stream.preserve = bool(self.preserve.GetValue())

class StreamDialog_(StreamDialog_):

    def __init__(self, parent, title, stream, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.stream = stream
        self.stream_parameters_panel.init(parent, self.stream)
        if title is not None:
            self.title_label.SetLabel(title)
            self.SetTitle(title)

    def Prepare(self):
        t = self.stream.stream_state
        self.stream_parameters_panel.stream_state_choice.SetValue(t)
        self.stream_parameters_panel.Prepare()
        self.SetSizerAndFit(self.main_sizer)

    def OnCancel(self):
        dtdebug("OnCancel")

    def OnDone(self):
        #self.stream.stream_state = self.get_stream_state_choice()
        self.stream_parameters_panel.OnDone()
        return self.stream

class StreamDialog(StreamDialog_):
    def __init__(self, parent, title, stream, *args, **kwds):
        super().__init__(parent, title, stream, *args, **kwds)

def show_stream_dialog(parent, title='Stream service', stream = None, service = None,
                       dvbs_mux = None, dvbc_mux=None, dvbt_mux=None):
    """
    create a dialog for creating or editing a stream
    service, dvbs_mux, dvbc_mux, dvbt_mux: if not None, stream this service or mux
    exactly one of these parameters must be set

    """
    if stream is None:
        stream = pydevdb.stream.stream()
    if service is not None:
        stream.content = service
    if dvbs_mux is not None:
        stream.content = dvbs_mux
    if dvbc_mux is not None:
        stream.content = dvbc_mux
    if dvbt_mux is not None:
        stream.content = dvbt_mux

    dlg = StreamDialog(parent.GetParent(), title, stream = stream)
    dlg.Prepare()
    dlg.Fit()
    ret = dlg.ShowModal()
    if ret == wx.ID_OK:
        stream = dlg.OnDone()
    else:
        dlg.OnCancel()
        stream = None
    dlg.Destroy()
    return stream
