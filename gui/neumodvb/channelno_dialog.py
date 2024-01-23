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
import sys
import os

from neumodvb.neumo_dialogs_gui import ChannelNoDialog_

def IsNumericKey(keycode):
    return keycode >= ord('0') and keycode <= ord('9')

class ChannelNoDialog(ChannelNoDialog_):
    def __init__(self, parent, basic, *args, **kwds):
        self.parent= parent
        self.timeout = 1000
        if "initial_chno" in kwds:
            initial_chno = str(kwds['initial_chno'])
            del kwds['initial_chno']
        else:
            initial_chno = None
        kwds["style"] =  kwds.get("style", 0) | wx.NO_BORDER
        super().__init__(parent, basic, *args, **kwds)
        if initial_chno is not None:
            self.chno.ChangeValue(initial_chno)
            self.chno.SetInsertionPointEnd()
        self.timer= wx.Timer(owner=self , id =1)
        self.Bind(wx.EVT_TIMER, self.OnTimer)
        self.timer.StartOnce(milliseconds=self.timeout)
        self.chno.Bind(wx.EVT_CHAR, self.CheckCancel)

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def OnText(self, event):
        self.timer.Stop()
        self.timer.StartOnce(milliseconds=self.timeout)
        event.Skip()

    def OnTextEnter(self, event):
        self.OnTimer(None)
        event.Skip()

    def OnTimer(self, event, ret=wx.ID_OK):
        self.EndModal(ret)

def ask_channel_number(caller, initial_chno=None):
    if initial_chno is not None:
        initial_chno = str(initial_chno)
    dlg = ChannelNoDialog(caller, -1, "Channel Number", initial_chno = initial_chno)

    val = dlg.ShowModal()
    chno = None
    if val == wx.ID_OK:
        try:
            chno = int(dlg.chno.GetValue())
        except:
            pass
    dlg.Destroy()
    return chno
