#!/usr/bin/python3
# Neumo dvb (C) 2019-2022 deeptho@gmail.com
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
from functools import cached_property

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer
from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str

import pychdb

def delsys_fn(x):
    vals= set([enum_to_str(xx).split('_')[0] for xx in x[1]])
    vals = vals.difference(set(['AUTO']))
    return "/".join(vals)

class FrontendTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    #frontend_fn = lambda x: f'{x[0].adapter_no}.{x[0].frontend_no}'
    frontend_fn = lambda x: f'{x[0].adapter_no}.{x[0].frontend_no}'
    mac_fn = lambda x: x[1].to_bytes(6, byteorder='little').hex(":")
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='adapter_name',  label='adapter', basic=True, no_combo=True, readonly=True,
            example="TurboSight TBS 6916X #12 "),
         CD(key='adapter_no',  label='AD#', basic=True, readonly=True),
         CD(key='k.frontend_no',  label='FE#', basic=True, readonly=True),
          CD(key='k.adapter_mac_address',  label='MAC', basic=True, no_combo=True, readonly=True,
             dfn=mac_fn, example=" AA:BB:CC:DD:EE:FF "),
         CD(key='card_name',  label='Card', basic=True, example=" TurboSight TBS 6916 (Octa DVB-S/S2/S2X)"),
         CD(key='card_short_name',  label='Card', basic=True, example=" TBS 6916X "),
         CD(key='rf_in',  label='RF#', basic=True, readonly=True),
         #CD(key='card_mac_address',  label='card MAC#', basic=True, no_combo=True, readonly=True,
         #   dfn=mac_fn, example=" 00:00:ab:00:00:00 "),
         CD(key='card_address',  label='Bus', basic=True, example=" Turbosight 6909x "),
         CD(key='present',  label='present', basic=True, dfn=bool_fn, readonly=True),
         CD(key='can_be_used',  label='available', basic=True, dfn=bool_fn, readonly=True),
         CD(key='enabled',  label='enabled', basic=True, dfn=bool_fn),
         CD(key='supports.multistream',  label='MIS', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.blindscan',  label='blind\n-scan', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.spectrum_sweep',  label='spec\nsweep', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.spectrum_fft',  label='spec\nfft', basic=True, dfn=bool_fn, readonly=True),
         CD(key='priority',  label='priority', basic=True),
         CD(key='master_adapter_mac_address',  label='master', basic=True, no_combo=False,
            dfn=lambda x: x[0].adapter_name, example=" TBS 6909X #12 "),
         CD(key='delsys',  label='delsys', basic=True, dfn=delsys_fn, readonly=True, example='DVBT/'*6)
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'adapter_no'
        data_table= pychdb.fe
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pychdb.fe.fe, initial_sorted_column = initial_sorted_column,
                         **kwds)

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pychdb.fe.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        dtdebug(f'saving {record.k.adapter_mac_address}')
        pychdb.put_record(txn, record)
        return record

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
