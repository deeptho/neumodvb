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


from neumodvb.util import lastdot, dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel

import pyrecdb
import pychdb
import pyepgdb

class AutoRecTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S") \
        if x[1]>0 else "never"

    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1]/1000, tz=tz.tzlocal()).strftime("%H:%M")
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [
         CD(key='service_name',  label='service', basic=True,  dfn=lambda x: lastdot(x)),
         CD(key='service.mux.sat_pos', label='Sat', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='service.mux.mux_id',  label='mux\nid'),
         CD(key='service.network_id',  label='nid'),
         CD(key='service.ts_id',  label='tsid'),
         CD(key='service.service_id',  label='sid'),
         CD(key='service.mux.stream_id',  label='isi'),
         CD(key='service.mux.t2mi_pid',  label='t2mi'),
         CD(key='starts_after', label='Starting\nafter', dfn=datetime_fn, example="15:30"),
         CD(key='starts_before', label='Starting\nbefore', dfn=datetime_fn, example="15:30"),
         CD(key='min_duration', label='Min\nduration', dfn=datetime_fn, example="10"),
         CD(key='max_duration', label='Max\nduration', dfn=datetime_fn, example="10"),
         #CD(key='content_codes', label='Type', dfn=content_codes_fn, example="None"),
         CD(key='event_name_contains',  label='Title\nContains', dfn=time_fn, example="House MD"),
         CD(key='story_contains',  label='Story\n contains', dfn=time_fn, example="House MD"),
         ]

    def InitialRecord(self):
        return self.app.currently_selected_rec

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'id'
        data_table= pyrecdb.autorec

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pyrecdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t = pyrecdb.autorec.autorec, initial_sorted_column = initial_sorted_column, **kwds)
        self.app = wx.GetApp()

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pyrecdb.autorec.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        pyrecdb.put_record(txn, record)
        return record

    def __new_record__(self):
        return self.record_t()

    def get_iconsOFF(self):
        return (self.app.bitmaps.encrypted_bitmap, self.app.bitmaps.expired_bitmap)

    def get_icon_stateOFF(self, rowno, colno):
        #col = self.columns[colno]
        #rec = self.data[rowno]
        return (True, True)

    def get_icon_sort_keyOFF(self):
        return 'encrypted'



class AutoRecGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = AutoRecTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        #self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        #self.grid_specific_menu_items=['epg_record_menu_item']

    def OnShow(self, evt):
        super().OnShow(evt)

    def EditMode(self):
        return  self.GetParent().GetParent().edit_mode


class BasicAutoRecGrid(AutoRecGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class AutoRecGrid(AutoRecGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)