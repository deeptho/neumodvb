#!/usr/bin/python3
# Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer

import pychdb

class FrontendTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='k.adapter_no',  label='adapter', basic=True, readonly=True),
         CD(key='k.frontend_no',  label='frontend', basic=True, readonly=True),
         CD(key='adapter_name',  label='Name', basic=True, example=" TurboSight TBS 6504 DVB-S/S2/S2X/T/T2/C/C2/ISDB-T  #0 "),
         CD(key='card_address',  label='Card#', basic=True, example=" Turbosight 6909x "),
         CD(key='adapter_address',  label='Adapter#', basic=True, example=" Turbosight 6909x "),
         CD(key='present',  label='present', basic=True, dfn=bool_fn, readonly=True),
         CD(key='can_be_used',  label='available', basic=True, dfn=bool_fn, readonly=True),
         CD(key='enabled',  label='enabled', basic=True, dfn=bool_fn),
         CD(key='supports.multistream',  label='MIS', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.blindscan',  label='blindscan', basic=True, dfn=bool_fn, readonly=True),
         CD(key='priority',  label='priority', basic=True),
         CD(key='master_adapter',  label='master', basic=True),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.adapter_no'
        data_table= pychdb.fe
        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table = data_table,
                         record_t=pychdb.fe.fe, initial_sorted_column = initial_sorted_column,
                         **kwds)

    def __save_record__(self, txn, record):
        pychdb.put_record(txn, record)

    def __new_record__(self):
        ret=self.record_t()
        return ret


class FrontendGridBase(NeumoGridBase):
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

    def __init__(self, basic, readonly, *args, **kwds):
        table = FrontendTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None

    def CmdTune(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.data[row].ref_mux
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

class BasicFrontendGrid(FrontendGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class FrontendGrid(FrontendGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
