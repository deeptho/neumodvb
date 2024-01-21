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

from neumodvb import neumodbutils
from neumodvb.util import setup, lastdot
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror
from neumodvb.scan_dialog import  show_scan_dialog

import pydevdb

class ScanCommandTable(NeumoTable):
    CD = NeumoTable.CD
    adapter_fn = lambda x: x[0].adapter_name
    mac_fn = lambda x: x[1].to_bytes(6, byteorder='little').hex(":") if x[1]>=0 else '???'
    card_fn = lambda x: card_label(x[0])
    card_rf_in_fn = lambda x: x[2].connection_name(x[0])
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    lof_offset_fn =  lambda x: '; '.join([ f'{int(x[0].lof_offsets[i])}kHz' for i in range(len(x[0].lof_offsets))]) if len(x[0].lof_offsets)>0 else ''
    freq_fn = lambda x: f'{x[1]/1000.:9.3f}' if x[1]>=0 else '-1'
    lnb_key_fn = lambda x: str(x[0])
    cur_pos_fn = lambda x:  pychdb.sat_pos_str(x[0].usals_pos + x[0].offset_angle)
    all_columns=[CD(key='id', label='ID', basic=True, readonly=True),
                 CD(key='start_time', label='Starts', basic=True, readonly=False,
                    dfn=datetime_fn, example='2021-06-16 18:30:33*'),
                 CD(key='repeat_type', label='Repeats', basic=True, readonly=False,
                    dfn=lambda x: f'{x[0].interval} {x[0].repeat_type}', example='6 weeks'),
                 CD(key='max_duration', label='Max\nDuration', basic=True, readonly=False),
                 CD(key='catchup', label='Catch up', basic=True, readonly=False),
                 CD(key='tune_options.subscription_type', label='Command', basic=True, readonly=False),
                 CD(key='tune_options', label='options', basic=False, readonly=False),
                 CD(key='mtime', label='Modified', basic=True, readonly=False,
                    dfn=datetime_fn, example='2021-06-16 18:30:33*'),
                 ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'start_time'
        data_table= pydevdb.scan_command
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.scan_command.scan_command,
                         initial_sorted_column = initial_sorted_column,
                         sort_order=2, #most recent on top
                         **kwds)
        self.do_autosize_rows = True

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.scan_command.screen(txn, sort_order=sort_field,
                                             field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, scan_command):
        pydevdb.scan_command.make_unique_if_template(txn, scan_command)
        pydevdb.put_record(txn, scan_command)
        return scan_command

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def highlight_colour(self, lnb):
        """
        """
        if False:
            return '#A0A0A0' #light gray
        else:
            return None

class ScanCommandGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = ScanCommandTable(self, basic)
        self.scan_command = None #scan_ for which parameters will be edited
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.Bind(wx.grid.EVT_GRID_CELL_LEFT_DCLICK, self.OnLeftClicked)

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)

    def CheckShowDialog(self, evt, rowno, colno):
        key = self.table.columns[colno].key
        if key in('tune_options', ) and self.GetGridCursorRow() == rowno:
            if False:
                lnb = self.dlg.lnb
                self.table.Backup("edit", rowno, oldlnb, lnb)
                self.table.SaveModified()
                self.dlg.Destroy()
                del self.dlg
            return True
        else:
            return False


    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN  and not evt.HasAnyModifiers():
            rowno = self.GetGridCursorRow()
            colno = self.GetGridCursorCol()
            self.CheckShowDialog(evt, rowno, colno)
        else:
            evt.Skip(True)

    def OnLeftClicked(self, evt):
        """
        Create and display a popup menu on right-click event
        """
        colno = evt.GetCol()
        rowno = evt.GetRow()
        if self.CheckShowDialog(evt, rowno, colno):
            evt.Skip(False)
        else:
            evt.Skip(True)


class BasicScanCommandGrid(ScanCommandGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)


class ScanCommandGrid(ScanCommandGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)

    def CmdNew(self, event):
        ShowMessage('Add Scan command', 'Scan commands should be added from the satellite list')
