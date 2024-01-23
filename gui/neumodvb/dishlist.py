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

from neumodvb import neumodbutils
from neumodvb.util import setup, lastdot
from neumodvb.neumolist import NeumoTable, NeumoGridBase, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror

import pychdb
import pydevdb


def strike_through(str):
    return '\u0336'.join(str) + '\u0336'

def speeds_sfn(record, val, idx):
    record.speeds[idx] = int(float(val)*100)
    return record

class DishTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    lof_offset_fn =  lambda x: '; '.join([ f'{int(x[0].lof_offsets[i])}kHz' for i in range(len(x[0].lof_offsets))]) if len(x[0].lof_offsets)>0 else ''
    lnb_key_fn = lambda x: str(x[0])
    cur_pos_fn = lambda x:  pychdb.sat_pos_str(x[0].usals_pos + x[0].offset_angle)
    basic_columns=[CD(key='dish_id', sort=('dish_id', ), example='1 ', label='Dish', basic=True, readonly=True)
                   ]
    all_columns = \
        [CD(key='dish_id', example='1 ', label='Dish', basic=True, readonly=True),
         CD(key='cur_usals_pos',  label='usals\npos', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1]) if x[0].cur_usals_pos==x[0].target_usals_pos \
            else strike_through(pychdb.sat_pos_str(x[1]))),
         CD(key='target_usals_pos',  label='usals\ntgt', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='powerup_time',  label='Powerup\ntime (ms)', basic=True, readonly=False),
         CD(key='speeds',  label='Speed\n12V', basic=True, readonly=False, no_combo = True,
            dfn= lambda x: "" if len(x[0].speeds)<1 else x[0].speeds[0]/100,
            sfn = lambda x: speeds_sfn(x[0], x[1], 0), example="1.50 "),
         CD(key='speeds',  label='Speed\n18V', basic=True, readonly=False, no_combo = True,
            dfn= lambda x: "" if len(x[0].speeds)<2 else x[0].speeds[1]/100,
            sfn = lambda x: speeds_sfn(x[0], x[1], 1), example="1.50 "),
         CD(key='enabled',   label='ena-\nbled', basic=False),
         CD(key='mtime',   label='Modified', dfn=datetime_fn, example="2024-01-03 23:06:40 ", basic=False),
        ]


    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'dish_id'
        data_table= pydevdb.dish
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        if basic:
            CD = NeumoTable.CD
            self.all_columns= self.basic_columns
        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.dish.dish, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.dish.screen(txn, sort_order=sort_field,
                                    field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

        if False:
            sort_order = pydevdb.fe.subfield_from_name('adapter_mac_address')<<24
            self.fe_screen =pydevdb.fe.screen(txn, sort_order=sort_order)
            self.aux_screens = [ self.fe_screen]

    def __save_record__(self, txn, dish):
        pydevdb.put_record(txn, dish)
        return dish

    def get_usals_locationOFF(self):
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        return opts.usals_location

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def highlight_colourOFF(self, lnb):
        """
        show lnbs for missing adapters in colour
        """
        if not lnb.can_be_used and len(lnb.networks)>0 and len(lnb.connections)>0:
            return self.parent.default_highlight_colour
        elif not lnb.enabled:
            return '#A0A0A0' #light gray
        else:
            return None

class DishGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = DishTable(self, basic)
        self.lnb = None #lnb for which networks will be edited
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)


    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN  and not evt.HasAnyModifiers():
            rowno = self.GetGridCursorRow()
            colno = self.GetGridCursorCol()
        else:
            evt.Skip(True)

    def CmdPositioner(self, event):
        dtdebug('CmdPositioner')
        self.OnPositioner(event)

    def OnPositioner(self, evt):
        """
        todo: mux,sat can be incompatible with lnb, in case lnb has no diseqc enabled
        This should be discovered by checking if sat is present in lnb.networks.
        We should NOT check for lnb.sat_id, as this will be removed later. lnb.sat_id
        only serves to distinghuish multiple lnbs on the same (usually fixed) dish
        """
        row = self.GetGridCursorRow()
        lnb = self.table.screen.record_at_row(row)
        dtdebug(f'Positioner requested for lnb={lnb}')
        from neumodvb.positioner_dialog import show_positioner_dialog
        if not lnb.enabled:
            ShowMessage(f' LNB {lnb} not enabled')
            return
        show_positioner_dialog(self, rf_path=None, lnb=lnb)
        self.table.SaveModified()
        #self.app.MuxTune(mux)

    def CmdSpectrum(self, evt):
        """
        todo: mux,sat can be incompatible with lnb, in case lnb has no diseqc enabled
        This should be discovered by checking if sat is present in lnb.networks.
        We should NOT check for lnb.sat_id, as this will be removed later. lnb.sat_id
        only serves to distinghuish multiple lnbs on the same (usually fixed) dish
        """
        row = self.GetGridCursorRow()
        lnb = self.table.screen.record_at_row(row)
        dtdebug(f'Spectrum requested for lnb={lnb}')
        self.table.SaveModified()
        from neumodvb.spectrum_dialog import show_spectrum_dialog
        show_spectrum_dialog(self, lnb=lnb)

    def CurrentDish(self):
        assert self.selected_row is not None
        if self.selected_row >= self.table.GetNumberRows():
            self.selected_row = max(self.table.GetNumberRows() -1, 0)
        lnb = self.table.GetRow(self.selected_row)
        dtdebug(f'CURRENT LNB: sel={self.selected_row} {lnb}  {len(lnb.networks)}')
        return lnb

class BasicDishGrid(DishGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)


class DishGrid(DishGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
