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
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel

import pystatdb
import pychdb


class SpectrumTable(NeumoTable):
    #label: to show in header
    #dfn: display function
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    lof_offset_fn =  lambda x: '; '.join([ f'{int(x[0].lof_offsets[i])}kHz' for i in range(len(x[0].lof_offsets))]) if len(x[0].lof_offsets)>0 else ''
    all_columns = \
        [CD(key='k.lnb_key',  label='lnb', basic=True, example="lnb1919-T2-dish3"),
         CD(key='k.sat_pos',  label='sat_pos', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='k.pol',  label='pol', basic=True, dfn=lambda x: lastdot(x).replace('POL',''), example='V'),
         CD(key='k.start_time',  label='date', basic=True, dfn= datetime_fn),
         CD(key='usals_pos',  label='usals_pos', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='lof_offsets',  label='lof_offset', dfn=lof_offset_fn, example='-2000kHz; -20000kHz'),
         CD(key='start_freq',  label='start', basic=False, dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114"),
         CD(key='end_freq',  label='end', basic=False, dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114"),
         CD(key='resolution',  label='step', basic=False),
         CD(key='filename',  label='file', basic=False, example="282.E/0/H_dish0_2021-05-31_13:12:03")
        ]

    def InitialRecord(self):
        return self.app.currently_selected_spectrum

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.start_time'
        data_table= pystatdb.spectrum
        super().__init__(*args, parent=parent, basic=basic, db_t=pystatdb, data_table = data_table,
                         record_t=pystatdb.spectrum.spectrum,
                         initial_sorted_column = initial_sorted_column,
                         sort_order=2, #most recent on top
                         **kwds)
        self.app = wx.GetApp()

    def __save_record__(self, txn, record):
        pystatdb.put_record(txn, record)
        return record
    def __new_record__(self):
        return self.record_t()

    def get_iconsOFF(self):
        return (self.app.bitmaps.encrypted_bitmap, self.app.bitmaps.expired_bitmap)

    def get_icon_stateOFF(self, rowno, colno):
        return (True, True)

    def get_icon_sort_keyOFF(self):
        return 'encrypted'



class SpectrumGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = SpectrumTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None


class BasicSpectrumGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)
        self.Bind (wx.grid.EVT_GRID_RANGE_SELECT, self.OnGridRowSelect)

    def OnGridRowSelect(self, evt):
        pass #print(f"ROWxxxx SELECT {evt.GetTopRow()}-{evt.GetBottomRow()}: {evt.Selecting()}")

class SpectrumSelectionGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)
        self.Bind (wx.grid.EVT_GRID_RANGE_SELECT, self.OnGridRowSelect)

    def OnGridRowSelect(self, evt):
        pass

    def CmdTune(self):
        dtdebug("CmdTune")

class SpectrumGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)