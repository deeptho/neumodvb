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
from neumodvb.neumo_dialogs_gui import  RecordDialog_

import pychdb
import pyepgdb


class Mode(Enum):
    NEW = 1
    STOP_RECORDING = 2
    STOP_SCHEDULED = 3

class RecordDialog(RecordDialog_):
    def __init__(self, parent, service, epg, start_time, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.service = service
        t=pyepgdb.rec_status_t
        self.mode = Mode.NEW
        if start_time is None:
            assert epg is not None
        elif type(start_time) == int:
            self.start_time = start_time
            self.duration = datetime.timedelta(hours=2)
        elif type(start_time) == datetime.datetime:
            self.start_time = int(start_time.timestamp())
            self.duration = datetime.timedelta(hours=2)
        else:
            assert 0

        if epg is None:
            txn = wx.GetApp().epgdb.rtxn()
            #attempt to load record
            epg = pyepgdb.running_now(txn, service.k, self.start_time)
            txn.abort()
            del txn
            dtdebug(f'EPG now = {epg} start={self.start_time} {datetime.datetime.fromtimestamp(self.start_time, tz=tz.tzlocal())}')
        self.epg = epg
        if epg is None:
            self.record_status = t.NONE
            self.recording = False
            self.scheduled = False
        else:
            self.start_time = epg.k.start_time
            self.duration = datetime.timedelta(seconds=epg.end_time - self.start_time)
            self.record_status = epg.rec_status
            if epg.rec_status==t.SCHEDULED:
                self.mode = Mode.STOP_SCHEDULED
            elif  epg.rec_status in (t.IN_PROGRESS, t.FINISHING):
                self.mode = Mode.STOP_RECORDING
        if self.mode == Mode.STOP_RECORDING:
            self.title_label.SetLabel("Stop recording")
            self.ok.SetLabel(_("Stop"))
        elif self.mode == Mode.STOP_SCHEDULED:
            self.title_label.SetLabel("Delete scheduled recording")
            self.ok.SetLabel(_("Delete"))
            self.ok.SetForegroundColour('red')

    def Prepare(self):
        start_time = datetime.datetime.fromtimestamp(self.start_time,  tz=tz.tzlocal())
        end_time =  start_time+self.duration
        self.service_name_text.SetValue(f'{self.service.ch_order: <6} {self.service.name}')
        if self.epg is None:
            event_name = f"{self.service.name} {start_time.strftime('%Y-%m-%d %H:%M')}-{end_time.strftime('%H:%M')}"
        else:
            event_name = self.epg.event_name

        self.event_name_text.SetValue(event_name)
        self.event_name_text.SetEditable(self.epg is None)


        self.starttime_text.SetValue(start_time.strftime('%H:%M'))
        self.starttime_text.SetEditable(self.epg is None)

        d= wx.DateTime.FromTimeT(self.start_time)
        self.startdate_datepicker.SetValue(d)

        #self.startdate_datepicker.SetEditable(self.epg is None)

        self.duration_text.SetValueTime(self.duration)
        self.duration_text.SetEditable(self.epg is None)

        #self.Fit()

    def CheckCancel(self, event):
        event.Skip()

    def OnDone(self):
        assert self.service is not None
        if self.epg is None:
            assert self.start_time
            d = self.startdate_datepicker.GetValue()
            start_time = self.startdate_datepicker.GetValue().GetTicks()
            start_time += self.starttime_text.GetSeconds()
            duration = self.duration_text.GetSeconds()
            event_name = self.event_name_text.GetValue()
            dtdebug(f'REC={event_name} start={datetime.datetime.fromtimestamp(start_time,  tz=tz.tzlocal())} duration={duration}')
            wx.GetApp().receiver.toggle_recording(self.service, start_time, duration, event_name)
        else:
            wx.GetApp().receiver.toggle_recording(self.service, self.epg)

    def OnCancel(self):
        dtdebug("OnCancel")

def show_record_dialog(parent, record, epg=None, start_time=None):
    if type(record) == pychdb.service.service:
        service = record
    else:
        key = record.service
        txn = wx.GetApp().chdb.rtxn()
        service = pychdb.service.find_by_key(txn, key.mux, key.service_id)
        txn.abort()
        del txn
    dlg = RecordDialog(parent.GetParent(), service, epg, start_time, title="New recording")
    dlg.Prepare()
    ret = dlg.ShowModal()
    if ret == wx.ID_OK:
        dlg.OnDone()
    else:
        dlg.OnCancel()
    dlg.Destroy()
    return ret
