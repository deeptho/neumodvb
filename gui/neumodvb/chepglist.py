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
import copy
from collections import namedtuple, OrderedDict
import numbers

import datetime
from dateutil import tz
import regex as re
import time

from neumodvb.util import setup, lastdot, dtdebug, dterror
import pychdb
import pyepgdb
import pyreceiver

import neumodvb.neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t # IconRenderer, MyColLabelRenderer,
from neumodvb.servicelist import BasicServiceGrid
from neumodvb.servicelist_combo import EVT_SERVICE_SELECT
from neumodvb.epg_content_codes import content_type_name

def content_type(content_code):
    return __content_types.get(content_code, hex(content_code))

def content_types(content_codes):
    ret=[]
    for c in content_codes:
        #ret.append(hex(c))
        ret.append(content_type_name(c))
        #ret.append(content_type(c))
    return '\n'.join(ret)

class ChEpgTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%a %b %d %H:%M")
    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%H:%M")
    content_fn =  lambda x: content_types(x[1])
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [CD(key='icons',  label='', basic=True, dfn=bool_fn, example=' '*8),
         CD(key='k.start_time',  label='start', basic=True, dfn=datetime_fn, example=' Wed Jun 15 00:00  '),
         CD(key='end_time',  label='end', basic=True,  dfn=time_fn, example=" 00:12:11 "),
         CD(key='content_codes',  label='type', dfn=content_fn, example="movie/drama"),
         CD(key='event_name',  label='event', example="M"*24, basic=True),
         CD(key='story',  label='story', example ="x"*80),
         #CD(key='parental_rating',  label='rating'),
         #CD(key='series_link',  label='series', example='12345'),
         #CD(key='source.table_id',  label='tbl'),
         #CD(key='source.version_number',  label='vers'),
         #CD(key='source.sat_pos',  label='src sat'),
         #CD(key='source.network_id',  label='source nid'),
         CD(key='service_name', label='service', example="BBC 1 London"),
         CD(key='k.service.mux.sat_pos', label='sat', dfn= lambda x: pychdb.sat_pos_str(x[1])),
         #CD(key='k.service.network_id',  label='nid'),
         CD(key='k.service.ts_id',  label='tsid'),
         CD(key='k.service.service_id',  label='sid', example="10304 "),
         CD(key='k.event_id',  label='event\nid'),
         CD(key='source.epg_type',  label='epg\ntype', dfn=lambda x: lastdot(x[1]).replace(" ","\n")),
         CD(key='source.ts_id',  label='source\ntsid'),
         CD(key='mtime',  label='Modified', dfn=datetime_fn, example=' Wed Jun 15 00:00xxxx')
         ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.start_time'
        data_table= pyepgdb.epg_record
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pyepgdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pyepgdb.epg_record.epg_record,
                         initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.app = wx.GetApp()
        self.do_autosize_rows = True

    def __save_record__(self, txn, record):
        pyepgdb.put_record(txn, record)
        return record

    def screen_getter_xxx(self, txn, sort_field):
        now = int(time.time())
        match_data, matchers = self.filter_times_()
        txn = self.db.rtxn()
        if self.parent.restrict_to_service:
            service, epg_record = self.parent.CurrentServiceAndEpgRecord()
            screen = pyepgdb.chepg_screen(txn, service_key=service.k, start_time=now, sort_order=sort_field,
                                          field_matchers=matchers, match_data = match_data)
        else:
            screen = pyepgdb.epg_record.screen(txn, sort_order=sort_field,
                                               field_matchers=matchers, match_data = match_data)
        txn.abort()
        del txn
        self.screen = screen_if_t(screen, self.sort_order==2)

    def filter_times_(self):
        """
        install a filter to only allow programs which end in the future
        """
        import pyepgdb
        match_data, matchers = self.get_filter_()
        import pydevdb #hack
        if matchers is None:
            match_data = self.record_t()
            matchers = pydevdb.field_matcher_t_vector()

        end_time_field_id = self.data_table.subfield_from_name("end_time")

        #if user has already filtered for a specific end_time, then setting a limit is pointless
        for m in matchers:
            if m.field_id == end_time_field_id:
                return match_data, matchers # this matcher is more specific

        m = pydevdb.field_matcher.field_matcher(end_time_field_id, pydevdb.field_matcher.match_type.GEQ)
        matchers.push_back(m)

        match_data.end_time = int(datetime.datetime.now(tz=tz.tzlocal()).timestamp())
        return match_data, matchers

    def __new_record__(self):
        return self.record_t()

    def get_icons(self):
        return (self.app.bitmaps.rec_scheduled_bitmap, self.app.bitmaps.rec_inprogress_bitmap)

    def get_icon_state(self, rowno, colno):
        epgrec = self.GetRow(rowno)
        if epgrec is None:
            return (False, False)
        t=pyepgdb.rec_status_t
        return ( epgrec.rec_status==t.SCHEDULED, epgrec.rec_status in (t.IN_PROGRESS, t.FINISHING) )

    def get_icon_sort_key(self):
        return 'rec_status'


class ChEpgGrid(NeumoGridBase):
    def __init__(self, *args, **kwds):
        self.allow_all = True
        basic = True
        readonly = False
        table = ChEpgTable(self, basic=False)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.GetParent().Bind(EVT_SERVICE_SELECT, self.CmdSelectService)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.restrict_to_service = None

    def CmdToggleRecord(self, evt):
        service, epg = self.CurrentServiceAndEpgRecord()
        assert epg is not None
        assert service is not None
        from neumodvb.record_dialog import show_record_dialog
        show_record_dialog(self, service, epg=epg)

    def CmdAutoRec(self, evt):
        service, epg = self.CurrentServiceAndEpgRecord()
        assert epg is not None
        assert service is not None
        from neumodvb.autorec_dialog import show_autorec_dialog
        ret, autorec = show_autorec_dialog(self, service, epg=epg)
        if ret == wx.ID_OK:
            wx.GetApp().receiver.update_autorec(autorec)
        elif ret ==wx.ID_DELETE: #delete
            wx.GetApp().receiver.delete_autorec(autorec)
        else:
            pass

    def OnShow(self, evt):
        service = self.app.live_service_screen.selected_service
        if  self.restrict_to_service !=  service:
            #This can happen when a new service was selected while epg panel was hidden
            dtdebug(f"SERVICE CHANGED {self.restrict_to_service} {service}")
            self.SelectService(service)
        evt.Skip()

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            row = self.GetGridCursorRow()
            self.app.ServiceTune(self.restrict_to_service)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return False

    def CmdSelectService(self, evt):
        service = evt.service
        wx.CallAfter(self.SelectService, service)

    def SelectService(self, service):
        self.restrict_to_service = service
        self.service = None
        wx.CallAfter(self.handle_service_change, None)

    def handle_service_change(self, evt):
        dtdebug(f'handle_service_change')
        self.table.GetRow.cache_clear()
        self.OnRefresh(evt)

    def CurrentServiceAndEpgRecord(self):
        if self.restrict_to_service is None:
            epg_record = None
            service = self.app.live_service_screen.selected_service
            self.restrict_to_service = service
        epg_record = self.table.CurrentlySelectedRecord()
        return self.restrict_to_service, epg_record

    def CurrentGroupText(self):
        service, epg_record = self.CurrentServiceAndEpgRecord()
        if service is None:
            return "All Services" if self.allow_all else ""
        return str(self.restrict_to_service)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        service, _ = self.CurrentServiceAndEpgRecord()
        self.SelectService(service)
        self.GrandParent.chepg_service_sel.SetService(self.restrict_to_service, self.allow_all)
