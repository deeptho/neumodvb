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


def IsNumericKey(keycode):
    return keycode >= ord('0') and keycode <= ord('9')

class RecTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S") \
        if x[1]>0 else "never"

    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1]/1000, tz=tz.tzlocal()).strftime("%H:%M")
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [
         CD(key='epg.rec_status',  label='Status', basic=True,  dfn=lambda x: lastdot(x)),
         CD(key='epg.k.start_time', label='EStart', dfn=datetime_fn, example="2020-11-15 15:30"),
         CD(key='epg.end_time',  label='EEnd', dfn=time_fn, example="15:40"),
         CD(key='real_time_start', label='Start', basic=True, dfn=datetime_fn),
         CD(key='real_time_end',  label='End', basic=True, dfn=datetime_fn),
         CD(key='pre_record_time',  label='Pre', example="100"),
         CD(key='post_record_time',  label='Post', example="100"),
         CD(key='service.ch_order', label='#', basic=True, example="10000"),
         CD(key='service.name',  label='Service', basic=True, example="Investigation disc"),
         CD(key='epg.event_name',  label='Program', basic=True, example="Investigation discovery12345 Investigation discovery12345 "),
         CD(key='stream_time_start',  label='Start'),
         CD(key='stream_time_end', label='End'),
         ]

    def InitialRecord(self):
        return self.app.currently_selected_rec

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'real_time_start'
        data_table= pyrecdb.rec

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pyrecdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t = pyrecdb.rec.rec, initial_sorted_column = initial_sorted_column, **kwds)
        self.app = wx.GetApp()

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pyrecdb.rec.screen(txn, sort_order=sort_field,
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



class RecGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = RecTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.grid_specific_menu_items=['epg_record_menu_item']

    def OnShow(self, evt):
        super().OnShow(evt)

    def OnToggleRecord(self, evt):
        row = self.GetGridCursorRow()
        rec = self.screen.record_at_row(row)
        dtdebug(f'OnToggleRecord {rec}')
        return rec, None

    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            self.MoveCursorRight(False)
            evt.Skip(False)
        else:
            evt.Skip(True)
        keycode = evt.GetUnicodeKey()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.EditMode():
                self.MoveCursorRight(False)
            else:
                row = self.GetGridCursorRow()
                rec = self.screen.record_at_row(row)
                dtdebug(f'RETURN pressed on row={row}: PLAY rec={rec.filename}')
                self.app.PlayRecording(rec)
            evt.Skip(False)
        elif not self.EditMode() and IsNumericKey(keycode):
            self.MoveToChno(ask_channel_number(self, keycode- ord('0')))
        else:
            evt.Skip(True)

    def EditMode(self):
        return  self.GetParent().GetParent().edit_mode

    def CmdPlay(self, evt):
        row = self.GetGridCursorRow()
        rec = self.table.screen.record_at_row(row)
        dtdebug (f'CmdPlay requested for row={row}: PLAY service={rec.filename}')
        self.table.SaveModified()
        self.app.PlayRecording(rec)

class BasicRecGrid(RecGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class RecGrid(RecGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
