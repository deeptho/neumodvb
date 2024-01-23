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
import datetime
from dateutil import tz
import regex as re
from collections import namedtuple, OrderedDict
import numbers

from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel


import pychdb

class ChgTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='k.sat_pos',  label='sat', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='k.group_type',  label='type', dfn=lambda x: lastdot(x) , example="BOUQUET", basic=True),
         CD(key='k.bouquet_id',  label='ID', basic=True),
         CD(key='name',  label='Name', basic=True, example="BSkyB Bouquet 12 - Commercial Other"),
         CD(key='num_channels',  label='#', basic=True, example="777"),
         CD(key='mtime',  label='Modified', dfn=datetime_fn, example="2020-12-29 18:35:01"),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'name'
        data_table= pychdb.chg

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pychdb.chg.chg, initial_sorted_column = initial_sorted_column,
                         **kwds)

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pychdb.chg.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        pychdb.chg.make_unique_if_template(txn, record)
        pychdb.put_record(txn, record) #this will overwrite any mux with given ts_id even if frequency is very wrong
        return record

    def __new_record__(self):
        ret=self.record_t()
        return ret




class ChgGridBase(NeumoGridBase):
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
        table = ChgTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None

    def CmdEditBouquetMode(self, is_checked):
        verbose=True
        if not is_checked:
            if verbose:
                ok = ShowOkCancel("End Edit Bouquet Mode?",
                                  f'Turn off Bouquet Edit Mode?\n', default_is_ok=True)
            if not ok:
                return ok #uncheck menu item
            wx.GetApp().get_menu_item('BouquetAddService').disabled = True
            dtdebug('EditBouquetMode turned OFF')
            self.app.frame.bouquet_being_edited = None
            self.app.frame.current_panel().grid.table.OnModified()
            self.app.frame.CmdChgList(None)
            return True

        row = self.GetGridCursorRow()
        record = self.table.screen.record_at_row(row)
        dtdebug(f'EditBouquet requested for row={row}: {record}')
        self.table.SaveModified()
        if verbose:
            ok = ShowOkCancel("Start Edit Bouquet Mode?",
                              f'Turn on Bouquet Edit Mode for the following bouquet?\n'
                              f'  {record.name}\n'
                              f'When this mode is on, services in this bouquet will be highlighted '
                              f' and you can add/remove services in the service and channel lists',
                              default_is_ok=True)
            if not ok:
                return ok #uncheck menu item
            self.app.get_menu_item('BouquetAddService').disabled = False
            self.app.frame.CmdServiceList(None)
        self.app.frame.bouquet_being_edited = record
        return True


class BasicChgGrid(ChgGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)


class ChgGrid(ChgGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
