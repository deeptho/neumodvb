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

from neumodvb.util import setup, lastdot, batched
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror
from neumodvb.scan_dialog import  show_scan_dialog
from neumodvb.neumodbutils import enum_to_str
from neumodvb.neumowidgets import RunType
import pychdb
import pydevdb


RT=RunType()

def run_type_str(interval, run_type):
    return RT.run_type_str(interval, run_type)

def run_type_sfn(triple):
    rec, val, table = triple
    rec.run_type, rec.interval = RT.str_to_runtype(val)
    return rec

def run_type_choices():
    r_t = pydevdb.run_type_t
    return RunType().choices

def mux_str(mux):
    sat = pychdb.sat_pos_str(mux.k.sat_pos) if type(mux) == pychdb.dvbs_mux.dvbs_mux \
        else 'DVBT' if type(mux) == pychdb.dvbt_mux.dvbt_mux \
             else 'DVBC' if type(mux) == pychdb.dvbc_mux.dvbc_mux \
                  else ''
    pol = lastdot(mux.pol).replace('POL','') if hasattr(mux, 'pol') else ''
    return f'{sat} {mux.frequency/1000.:9.3f}{pol}'


def sat_mux_fn(x):
    ret=[]

    for batch in batched([f'{str(sat)}-{enum_to_str(sat.sat_band)}' for sat in x[0].sats], 4):
        ret.append("; ".join([*batch]))
    muxes = [*x[0].dvbs_muxes, *x[0].dvbc_muxes, *x[0].dvbt_muxes]
    for batch in batched([mux_str(mux) for mux in [*muxes]], 2):
        ret.append("; ".join([*batch]))
    return "\n".join(ret)

def cards_dishes_fn(x):
    ret=[]
    cards_dict = x[2].cards
    for batch in batched([f'{cards_dict.get(card, "???")}' for card in x[1].allowed_card_mac_addresses], 2):
        ret.append("; ".join([*batch]))
    for batch in batched([f'D{dish}' for dish in x[1].allowed_dish_ids], 4):
        ret.append("; ".join([*batch]))
    return "\n".join(ret)

def bands_fn(x):
    ret=[]
    pols = [enum_to_str(pol) for pol in x[1].pols]
    ret.append('; '.join(pols))
    if x[1].start_freq >=0 and x[1].end_freq>=0:
        ret.append(f'{x[1].start_freq//1000}-{x[1].end_freq//1000}')
    elif x[1].start_freq >=0:
        ret.append(f'{x[1].start_freq//1000}-end')
    elif x[1].end_freq >=0:
        ret.append(f'sstart-{x[1].end_freq//1000}')
    return "\n".join(ret)

def command_name(cmd):
    return f'{enum_to_str(cmd.tune_options.subscription_type).replace("_", " ")} {cmd.id}'

class ScanCommandTable(NeumoTable):
    CD = NeumoTable.CD
    adapter_fn = lambda x: x[0].adapter_name
    mac_fn = lambda x: x[1].to_bytes(6, byteorder='little').hex(":") if x[1]>=0 else '???'
    card_fn = lambda x: card_label(x[0])
    card_rf_in_fn = lambda x: x[2].connection_name(x[0])
    datetime_fn =  lambda x: '' if x[0].run_type == pydevdb.run_type_t.NEVER else \
        datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    duration_fn =  lambda x: f'{x[1]//3600:02d}:{x[1]//60:02d}'
    run_type_fn = lambda x: run_type_str(x[0].interval, x[0].run_type)
    run_type_cfn = lambda x: run_type_choices()
    all_columns=[CD(key='id', label='ID', basic=True, readonly=True),
                 CD(key='tune_options.subscription_type', label='Command',
                    dfn = lambda x: enum_to_str(x[1]).replace("_", " "), basic=True, readonly=False),
                 CD(key='run_type', label='Runs', basic=True, readonly=False,
                    cfn = run_type_cfn, dfn=run_type_fn, sfn = run_type_sfn,
                    example='Every 12 hours '),
                 CD(key='start_time', label='Starts', basic=True, readonly=False,
                    dfn=datetime_fn, example='2021-06-16 18:30:33*'),
                 CD(key='catchup', label='Catch\nup', basic=True, readonly=False),
                 CD(key='max_duration', label='Max\nDur.', dfn=duration_fn, basic=True, readonly=False),
                 CD(key='dvbs_muxes', label='Sats/Muxes', dfn = sat_mux_fn, basic=False,
                    example = "61.0W-Ku; "*4, readonly=True),
                 CD(key='band_scan_options', label='Bands', dfn = bands_fn, basic=False,
                    example = "10700-12750 ", readonly=True),
                 CD(key='tune_options', label='Cards/Dishes', dfn = cards_dishes_fn, basic=False,
                    example = "61.0W-Ku; "*4, readonly=True),
                 CD(key='run_start_time', label='Last start', basic=True, readonly=False,
                    dfn=datetime_fn, example='2021-06-16 18:30:33*'),
                 CD(key='run_end_time', label='Last end', basic=True, readonly=False,
                    dfn=datetime_fn, example='2021-06-16 18:30:33*'),
                 CD(key='run_result', label='Last\nresult', basic=True, readonly=False,
                    dfn=lambda x: enum_to_str(x[1])),
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
        self.cards = { v:k for k,v in wx.GetApp().get_cards().items()}

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

    def CmdEditCommandMode(self, is_checked):
        verbose=True
        if not is_checked:
            if verbose:
                ok = ShowOkCancel("End Edit Command Mode?",
                                  f'Turn off Edit Command Mode?\n', default_is_ok=True)
            if not ok:
                return ok #uncheck menu item
            app = wx.GetApp()
            app.get_menu_item('CommandAddSat').disabled = True
            app.get_menu_item('CommandAddMux').disabled = True
            app.frame.CmdScanCommandList(None)
            dtdebug('EditCommandMode turned OFF')
            app.frame.command_being_edited = None
            app.frame.current_panel().grid.table.OnModified()
            app.frame.CmdScanCommandList(None)
            return True
        row = self.GetGridCursorRow()
        record = self.table.screen.record_at_row(row)
        s_t = pydevdb.subscription_type_t
        t = record.tune_options.subscription_type
        allow_muxes = t in (s_t.MUX_SCAN, s_t.TUNE)
        allow_sats = t in (s_t.MUX_SCAN, s_t.BAND_SCAN, s_t.SPECTRUM_ACQ)
        dtdebug(f'EditCommand requested for row={row}: {record}')
        self.table.SaveModified()
        if verbose:
            ok = ShowOkCancel("Start Edit Command Mode?",
                              f'Turn on Command Edit Mode for the following command?\n'
                              f'  {command_name(record)}\n'
                              f'When this mode is on, satellites and muxes used by the command will be highlighted '
                              f' and you can add/remove them in the satelltite and mux lists',
                              default_is_ok=True)
            if not ok:
                return ok #uncheck menu item
            app = wx.GetApp()
            app.get_menu_item('CommandAddSat').disabled = not allow_sats
            app.get_menu_item('CommandAddMux').disabled = not allow_muxes
            if len(record.dvbs_muxes) > 0:
                app.frame.CmdDvbsMuxList(None)
            elif len(record.dvbc_muxes) > 0:
                app.frame.CmdDvbcMuxList(None)
            elif len(record.dvbt_muxes) > 0:
                app.frame.CmdDvbtMuxList(None)
            else:
                app.frame.CmdSatList(None)
        self.app.frame.command_being_edited = record
        return True

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
