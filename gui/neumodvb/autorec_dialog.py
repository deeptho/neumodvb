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
from neumodvb.neumo_dialogs_gui import AutoRecDialog_

import pychdb
import pyrecdb
import pyepgdb


class Mode(Enum):
    NEW = 1
    STOP_RECORDING = 2
    STOP_SCHEDULED = 3

class AutoRecDialog(AutoRecDialog_):
    def __init__(self, parent, autorec, service, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        if autorec.id < 0 : #new
            self.delete.Hide()
        self.autorec = autorec
        self.restrict_to_service = service
        t=pyepgdb.rec_status_t
        self.mode = Mode.NEW
        self.autorec = autorec

    def Prepare(self):
        self.autorec_service_sel.SetService(self.restrict_to_service, False)
        self.event_name_contains_text.SetValue(self.autorec.event_name_contains)
        self.story_contains_text.SetValue(self.autorec.story_contains)

        after = datetime.datetime.fromtimestamp(self.autorec.starts_after, tz=tz.tzutc())
        self.starts_after_text.SetValue(after.strftime('%H:%M'))

        before = datetime.datetime.fromtimestamp(self.autorec.starts_before, tz=tz.tzutc())
        self.starts_before_text.SetValue(before.strftime('%H:%M'))

        self.min_duration_text.SetValueTime(self.autorec.min_duration)
        self.max_duration_text.SetValueTime(self.autorec.max_duration)

        #self.Layout()
        #wx.CallAfter(self.Layout)

    def CheckCancel(self, event):
        event.Skip()

    def OnCancel(self):
        dtdebug("OnCancel")
    def OnDone(self):
        #assert false #update
        #self.autorec_service_sel.GetService()
        self.autorec.service=self.autorec_service_sel.service.k
        self.autorec.event_name_contains=self.event_name_contains_text.GetValue()
        self.autorec.story_contains = self.story_contains_text.GetValue()
        self.autorec.starts_before = self.starts_before_text.GetSeconds()
        self.autorec.starts_after = self.starts_after_text.GetSeconds()
        self.autorec.min_duration = self.min_duration_text.GetSeconds()
        self.autorec.max_duration = self.max_duration_text.GetSeconds()

    def OnWindowCreateOFF(self, evt):
        if evt.GetWindow() != self:
            return
        service, _ = self.CurrentServiceAndEpgRecord()
        self.SelectService(service)
        self.GrandParent.chepg_service_sel.SetService(self.restrict_to_service, self.allow_all)

def seconds_after_midnight(timestamp):
    start_time = datetime.datetime.fromtimestamp(timestamp,  tz=tz.tzlocal())
    midnight = start_time.replace(hour=0, minute=0, second=0, microsecond=0)
    return (start_time - midnight).seconds


def make_autorec(service, epg):
    autorec = pyrecdb.autorec.autorec()
    autorec.service = service.k
    if epg is not None:
        autorec.starts_after = seconds_after_midnight(epg.k.start_time - 600)
        autorec.starts_before = seconds_after_midnight(epg.k.start_time + 600)
        d = epg.end_time - epg.k.start_time
        autorec.min_duration = d - 600
        autorec.max_duration = d + 600
        autorec.content_codes = epg.content_codes
        autorec.event_name_contains = epg.event_name
        autorec.story_contains = ""
    autorec.service_name = service.name
    return autorec

def service_for_key(service_key):
    txn = wx.GetApp().chdb.rtxn()
    service = pychdb.service.find_by_key(txn, service_key.mux, service_key.service_id)
    txn.abort()
    del txn
    return service


def show_autorec_dialog(parent, record, epg=None, start_time=None):
    """
    create a dialog for creating or editing an autorec
    record can be of type service, rec, or autorec
    in addition, for type service, epg can be set to provide defaults for
    a new autorec
    """

    if type(record) == pyrecdb.autorec.autorec:
        autorec = record
        service = service_for_key(autorec.service)
    if type(record) == pyrecdb.rec.rec:
        autorec = make_autorec(rec.service, rec.epg)
        service = service_for_key(record.service)
    elif type(record) == pyepgdb.epg_record.epg_record:
        assert False
    elif type(record) == pychdb.service.service:
        service = record
        autorec = make_autorec(service, epg)
    elif type(record) ==  pychdb.chgm.chgm:
        service = service_for_key(record.service)
        autorec = make_autorec(service, epg)
    else:
        pass
    if autorec is None or service is None:
        return None
    title = _("New Auto Rec") if autorec.id < 0 else  _("Update Auto Rec")
    dlg = AutoRecDialog(parent.GetParent(), autorec=autorec, service=service, title=title)
    dlg.Prepare()
    dlg.Fit()
    ret = dlg.ShowModal()
    if ret == wx.ID_OK:
        dlg.OnDone()
    elif ret ==wx.ID_DELETE: #delete
        pass
    else:
        dlg.OnCancel()
    dlg.Destroy()
    return ret, autorec
