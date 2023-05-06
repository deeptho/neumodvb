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

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t

import pychdb

class positioner_mux_screen_t(object):
    def __init__(self, parent):
        self.parent = parent

    @property
    def list_size(self):
        return 1

    def record_at_row(self, rowno):
        assert(rowno==0)
        mux = self.parent.parent.tune_mux_panel.mux
        if mux is None:
            mux = self.parent.InitialRecord()
        return mux

    def update(self, txn):
        return True

    def set_reference(self, rec):
        return 0

class DvbsMuxTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%M:%S")
    all_columns = \
        [ \
         CD(key='frequency', label='Frequency', dfn= lambda x: x[1]/1000.),
         CD(key='pol', label='Pol', dfn=lambda x: lastdot(x).replace('POL',''), example='V'),
         CD(key='delivery_system', label='System',
            dfn=lambda x: lastdot(x).replace('SYS',""), example='DVBS2'),
         CD(key='modulation', label='Modulation',
            dfn=lambda x: lastdot(x), example='PSK8'),
         CD(key='symbol_rate', label='SymRate',  dfn= lambda x: x[1]//1000),
         CD(key='pls_mode', label='Pls Mode', dfn=lastdot, example='COMBO'),
         CD(key='pls_code', label='Pls Code'),
         CD(key='k.stream_id', label='Stream')
         ]

    other_columns =  \
        [CD(key='LP_code_rate', label='LP_code_rate'),
         CD(key='bandwidth', label='bandwidth'),
         CD(key='guard_interval', label='guard_interval'),
         CD(key='hierarchy', label='hierarchy'),
         CD(key='rolloff', label='rolloff'),
         CD(key='transmission_mode', label='transmission_mode')]

    def InitialRecord(self):
        return self.__new_record__()

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'frequency'
        data_table= pychdb.dvbs_mux
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t =  pychdb.dvbs_mux.dvbs_mux,
                         initial_sorted_column = initial_sorted_column, **kwds)

    def __save_record__(self, txn, record):
        return record

    def screen_getter_xxx(self, txn, sort_order):
        mux = self.parent.tune_mux_panel.mux
        if mux is None:
            mux = self.InitialRecord()
        self.screen=screen_if_t(positioner_mux_screen_t(self), self.sort_order==2)

    def __new_record__(self):
        ret=self.record_t()
        sat = self.parent.tune_mux_panel.sat
        ret.k.sat_pos = pychdb.sat.sat_pos_none if sat is None else sat.sat_pos
        ret.c.tune_src = pychdb.tune_src_t.TEMPLATE
        return ret

class PositionerDvbsMuxGrid(NeumoGridBase):
    def __init__(self, *args, **kwds):
        basic = False
        readonly = False
        table = DvbsMuxTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.tune_mux_panel = self.Parent.GrandParent
        self.ShowScrollbars(wx.SHOW_SB_NEVER,wx.SHOW_SB_NEVER)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_CHAR, self.OnKeyCheck)
        self.Bind(wx.EVT_KILL_FOCUS, self.OnKillFocus)
        self.sat = None #sat for which to show muxes
        self.mux = None #currently selected mux
        wx.CallAfter(self.OnKillFocus, None)

    def OnKillFocus(self, evt):
        rowno = self.GetGridCursorRow()
        self.DeselectRow(rowno)

    def OnKeyCheck(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.EditMode():
                self.MoveCursorRight(False)
            else:
                row = self.GetGridCursorRow()
                mux = self.table.screen.record_at_row(row)
                dtdebug(f'RETURN pressed on row={row}: PLAY mux={mux}')
                self.app.MuxTune(mux)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def Reset(self):
        self.table.GetRow.cache_clear()
        self.table.FinalizeUnsavedEdits()
        mux = self.tune_mux_panel.mux
        wx.CallAfter(self.doit, None, mux)

    def doit(self, evt, mux):
        self.table.GetRow.cache_clear()
        self.OnRefresh(None, mux)

    def OnTune(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'MuxTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

    def OnScan(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'MuxTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxScan(mux)
