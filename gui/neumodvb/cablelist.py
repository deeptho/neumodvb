#!/usr/bin/python3
# Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror
from neumodvb.lnb_dialog import  LnbNetworkDialog, LnbConnectionDialog, LnbUnicableChannelDialog
import pydevdb

def strike_through(str):
    return '\u0336'.join(str) + '\u0336'

class CableTable(NeumoTable):
    CD = NeumoTable.CD
    card_rf_input_dfn = lambda x: x[0].connection_name
    card_rf_input_sfn = lambda x: x[2].card_rf_input_sfn(x[0], x[1])
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S") \
        if x[1]>0 else "never"

    all_columns = \
        [CD(key='cable_id',  label='cable', basic=True, readonly=True),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='cable_name',  label='name', basic=True, readonly=False, example="Cable long name xxxx"),
         #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='rf_input',  label='Card RF#in', basic=True, readonly=False, example="C0#3  TBS 6909seXXX ",
            dfn=card_rf_input_dfn, sfn=card_rf_input_sfn),
         CD(key='mtime', label='Modified', dfn=datetime_fn, example='2021-06-16 18:30:33*'),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'cable_id'
        data_table= pydevdb.cable
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.cable.cable, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.cable.screen(txn, sort_order=sort_field,
                                      field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, wtxn, cable, old_cable):
        pydevdb.cable.update_cable(wtxn, cable, old_cable)
        return cable

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def card_rf_input_sfn(self, rec, v):
        d,_ = wx.GetApp().get_cards_with_rf_in()
        newval = d.get(v, None)
        if newval is None:
            try:
                v, newval = next(iter(d.items()))
            except:
                pass
        if newval is None:
            return rec
        #this is needed to correctly display the name of the record if user moves cursor to different cell in new record
        rec.card_mac_address, rec.rf_input = newval
        rec.connection_name = v
        return rec

    def highlight_colour(self, cable):
        """
        show lnbs for missing adapters in colour
        """
        if cable.card_mac_address == -1:
            return self.parent.default_highlight_colour
        else:
            return None

class CableGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = CableTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        #self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        #self.Bind(wx.grid.EVT_GRID_CELL_LEFT_DCLICK, self.OnLeftClicked)

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)

    def CurrentCable(self):
        assert self.selected_row is not None
        if self.selected_row >= self.table.GetNumberRows():
            self.selected_row = max(self.table.GetNumberRows() -1, 0)
        cable = self.table.GetRow(self.selected_row)
        dtdebug(f'CURRENT CABLE: sel={self.selected_row} {cable}')
        return lnb

class CableGrid(CableGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
