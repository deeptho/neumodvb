#!/usr/bin/python3
# Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
from neumodvb.servicelist import BasicServiceGrid, ChannelNoDialog
from neumodvb.servicelist_combo import ServiceGridPopup

__content_types ={
    0x10: 'movie/drama',
    0x20: 'news/current affairs',
    0x30: 'show/game show',
    0x40: 'sports',
    0x50: "children's/youth programmes",
    0x60: "music/ballet/dance",
    0x70: "arts/culture",
    0x80: "social/political issues/economics",
    0x90: "education/science/factual topics",
    0xA0: "leisure hobbies"
    }

def content_type(content_code):
    return __content_types.get(content_code >>4, '')

def content_types(content_codes):
    ret=[]
    for c in content_codes:
        ret.append(content_type(c))
    return '; '.join(ret)

class ChEpgTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%a %Y-%m-%d %H:%M")
    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%H:%M")
    content_fn =  lambda x: content_types(x[1])
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [CD(key='k.service.sat_pos', label='sat'),
         CD(key='k.service.network_id',  label='nid'),
         CD(key='k.service.ts_id',  label='tsid'),
         CD(key='k.service.service_id',  label='sid'),
         CD(key='k.event_id',  label='id'),
         CD(key='k.start_time',  label='start', basic=True, dfn=datetime_fn, example=' Sat 1970-01-01 00:00 '),
         CD(key='end_time',  label='end', basic=True,  dfn=time_fn, example=" 00:12:11 "),
         CD(key='content_codes',  label='type', dfn=content_fn, example="movie/drama"),
         CD(key='event_name',  label='event', basic=True),
         CD(key='icons',  label='', basic=True, dfn=bool_fn, example=' '*8),
         CD(key='story',  label='story'),
         CD(key='parental_rating',  label='rating'),
         CD(key='series_link',  label='series', example='12345'),
         CD(key='source.table_id',  label='tbl'),
         CD(key='source.version_number',  label='vers'),
         CD(key='source.section_number',  label='secno'),
         CD(key='source.sat_pos',  label='src sat'),
         CD(key='source.network_id',  label='source nid'),
         CD(key='source.ts_id',  label='source tsid'),
         CD(key='mtime',  label='type', dfn=datetime_fn)
         ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.start_time'
        data_table= pyepgdb.epg_record
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pyepgdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pyepgdb.epg_record.epg_record,
                         initial_sorted_column = initial_sorted_column,  **kwds)

    def __save_record__(self, txn, record):
        pyepgdb.put_record(txn, record)
        return record

    def screen_getter_xxx(self, txn, sort_field):
        service = self.parent.CurrentService()
        now = int(time.time())
        screen = pyepgdb.chepg_screen(txn, service_key=service.k, start_time=now, sort_order=sort_field)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __new_record__(self):
        return self.record_t()

    def get_icons(self):
        b= wx.GetApp().bitmaps;
        return (b.rec_scheduled_bitmap,b.rec_inprogress_bitmap)

    def get_icon_sort_key(self):
        return 'rec_status'

    def get_icon_state(self, rowno, colno):
        rec = self.GetRow(rowno)
        t=pyepgdb.rec_status_t
        return ( rec.rec_status==t.SCHEDULED, rec.rec_status in (t.IN_PROGRESS, t.FINISHING) )

class ChEpgGrid(NeumoGridBase):
    def __init__(self, *args, **kwds):
        basic = True
        readonly = True
        table = ChEpgTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        #self.Bind(wx.EVT_CHAR, self.OnKeyCheck)
        self.service = None #service for which to show epg
        self.grid_specific_menu_items=['epg_record_menu_item']

    def OnToggleRecord(self, evt):
        epg = self.table.CurrentlySelectedRecord()
        service = self.CurrentService()
        assert epg is not None
        assert service is not None
        from neumodvb.record_dialog import show_record_dialog
        show_record_dialog(self, service, epg=epg)

    def OnShow(self, evt):
        service = wx.GetApp().live_service_screen.selected_service
        if  self.service !=  service:
            #This can happen when a new service was selected while epg panel was hidden
            dtdebug(f"SERVICE CHANGED {self.service} {service}")
            #self.service = service
            self.SelectService(service)
        evt.Skip()

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            row = self.GetGridCursorRow()
            self.app.ServiceTune(self.service)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def EditMode(self):
        return False

    def SelectService(self, service):
        self.service = service
        oldlen = self.table.GetNumberRows()
        self.table.reload()
        newlen = self.table.GetNumberRows()
        msg = None
        if newlen < oldlen:
            msg = wx.grid.GridTableMessage(self.table, wx.grid.GRIDTABLE_NOTIFY_ROWS_DELETED, newlen, oldlen - newlen)
        elif oldlen < newlen:
            msg = wx.grid.GridTableMessage(self.table, wx.grid.GRIDTABLE_NOTIFY_ROWS_APPENDED, newlen-oldlen)
        if msg is not None:
            self.ProcessTableMessage(msg)
        self.Refresh()
        rowno = self.GetGridCursorRow()
        rec = self.table.GetValue(rowno, None)
        if rec is not None:
            self.infow.ShowRecord(rec)

    def CurrentService(self):
        if self.service is None:
            service = wx.GetApp().live_service_screen.selected_service
            self.service = service
        return self.service

    def CurrentGroupText(self):
        if self.service is None:
            self.service = wx.GetApp().live_service_screen.selected_service
        if self.service is None:
            return ""
        return str(self.service)
