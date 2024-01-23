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
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror
from neumodvb.lnb_dialog import  LnbNetworkDialog, LnbConnectionDialog
from neumodvb.lnbnetworklist import LnbNetworkGrid

import pychdb
import pydevdb


def has_network(lnb, sat_pos):
    for n in lnb.networks:
        if n.sat_pos == sat_pos:
            return True
    return False

def must_move_dish(lnb, sat_pos):
    if lnb is None:
        return False
    return lnb.on_positioner and abs(sat_pos - lnb.usals_pos) >= 30

def has_network_with_usals(lnb, usals_pos):
    for n in lnb.networks:
        if n.usals_pos == usals_pos:
            return True
    return False

def get_network(lnb, sat_pos):
    for n in lnb.networks:
        if abs(n.sat_pos - sat_pos) < 5:
            return n
    return None

def get_current_network(lnb):
    for n in lnb.networks:
        if n.usals_pos == lnb.usals_pos:
            return n
    return None

def card_label(lnb):
    parts = lnb.connection_name.split(" ")
    short_name = parts[0]
    name = " ".join(parts[1:])
    short_name = short_name.split('#')[0]
    return f'{short_name}: {name}'

def strike_through(str):
    return '\u0336'.join(str) + '\u0336'

def  lnbconn_fn(x):
    return '; '.join([ strike_through(conn.connection_name) if not conn.can_be_used or not conn.enabled \
                       else conn.connection_name for conn in x[1]])
def lnbnetwork_fn(x):
    return '; '.join([ strike_through(pychdb.sat_pos_str(network.sat_pos)) if not network.enabled \
                       else pychdb.sat_pos_str(network.sat_pos) for network in x[1]])

class LnbTable(NeumoTable):
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
    basic_columns=[CD(key='k',
                      sort=('k.dish_id', 'adapter_mac_address','k.lnb_id', 'usals_pos'),
                      example='D1 unv [32762] 30.0W ',
                      dfn = lnb_key_fn,
                      label='LNB', basic=True, readonly=True)
                              ]
    all_columns = \
        [CD(key='k.dish_id',  label='dish', basic=True, readonly=False),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='on_positioner',  label='on\nrotor', basic=True, readonly=False),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='lnb_usals_pos',  label='lnb\nusals', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='cur_sat_pos',  label='cur sat\npos', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='usals_pos',  label='dish\nusals', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
            #following must be readonly, or change may be accidentally undone by positioner dialog
         CD(key='offset_angle',  label='offset\nangle', basic=True, readonly=True, no_combo = True,
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='k.lnb_id',  label='ID', basic=False, readonly=True, example="12345"),
         CD(key='enabled',   label='ena-\nbled', basic=False),
         CD(key='can_be_used',   label='avail-\nable', basic=False, readonly=True),
         CD(key='k.lnb_type',  label='LNB type', dfn=lambda x: lastdot(x)),
         CD(key='networks',   label='Networks', dfn=lnbnetwork_fn, example='19.0E; '*6),
         CD(key='connections',  label='Connections', dfn=lnbconn_fn, example='C2#1 TBS6909X; '*4),
         CD(key='pol_type',  label='POL\ntype', dfn=lambda x: lastdot(x), basic=False),
         CD(key='priority',  label='prio'),
         CD(key='lof_offsets',  label='lof\noffset', dfn=lof_offset_fn, readonly = True,
            example='-2000kHz; -20000kHz'),
         CD(key='freq_low',   label='freq\nmin', basic=False, dfn=freq_fn, example="10700.000"),
         CD(key='freq_mid',   label='freq\nmid', basic=False, dfn=freq_fn, example="10700.000"),
         CD(key='freq_high',   label='freq\nmax', basic=False, dfn=freq_fn, example="10700.000"),
         CD(key='lof_low',   label='LOF\nlow', basic=False, dfn=freq_fn, example="10700.000"),
         CD(key='lof_high',   label='LOF\nhigh', basic=False, dfn=freq_fn, example="10700.000"),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'usals_pos'
        data_table= pydevdb.lnb
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        if basic:
            CD = NeumoTable.CD
            self.all_columns= self.basic_columns
        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.lnb.lnb, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.lnb.screen(txn, sort_order=sort_field,
                                    field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

        if False:
            sort_order = pydevdb.fe.subfield_from_name('adapter_mac_address')<<24
            self.fe_screen =pydevdb.fe.screen(txn, sort_order=sort_order)
            self.aux_screens = [ self.fe_screen]

    def __save_record__(self, txn, lnb):
        if len(lnb.networks) == 0:
            if lnb.usals_pos != pychdb.sat.sat_pos_none:
                cont = ShowOkCancel("Add network?",
                                f"Add networkk for {pychdb.sat_pos_str(lnb.usals_pos)}?")
                if cont:
                    network = pydevdb.lnb_network.lnb_network()
                    network.usals_pos = lnb.usals_pos
                    network.sat_pos = lnb.usals_pos
                    pydevdb.lnb.add_or_edit_network(lnb, self.get_usals_location(), network)
        if len(lnb.networks) == 0:
            cont = ShowOkCancel("Add network?",
                                f"This LNB has no networks defined and cannot be used. Continue anyway?")
            if not cont:
                return None

        if len(lnb.connections) == 0:
            cont = ShowOkCancel("Add connection?",
                               f"This LNB has no connections defined and cannot be used. Continue anyway?")
            if not cont:
                return None
        pydevdb.lnb.make_unique_if_template(txn, lnb)
        pydevdb.lnb.update_lnb_from_lnblist(txn, lnb)
        return lnb

    def get_usals_location(self):
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        return opts.usals_location

    def connection_name(self, record):
        """
        update connection name if needed
        """
        if record is None:
            return "select card/rfin"
        if len(record.connection_name)==0:
            #update needed
            txn = self.db.rtxn()
            pydevdb.lnb.update_lnb_from_lnblist(txn, record, save=False)
            txn.abort()
        return record.connection_name

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def highlight_colour(self, lnb):
        """
        show lnbs for missing adapters in colour
        """
        if not lnb.can_be_used and len(lnb.networks)>0 and len(lnb.connections)>0:
            return self.parent.default_highlight_colour
        elif not lnb.enabled:
            return '#A0A0A0' #light gray
        else:
            return None

class LnbGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = LnbTable(self, basic)
        self.lnb = None #lnb for which networks will be edited
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
        if key in('networks', 'connections') and self.GetGridCursorRow() == rowno:
            if not hasattr(self, 'dlg'):
                readonly = False
                basic = False
                lnb =  self.table.CurrentlySelectedRecord()
                if key == 'networks' :
                    self.dlg = LnbNetworkDialog(self.GetParent(), lnb, title="LNB Networks",
                                                basic=basic, readonly=readonly)
                elif key == 'connections':
                    self.dlg = LnbConnectionDialog(self.GetParent(), lnb, title="LNB Connections",
                                                   basic=basic, readonly=readonly)
            else:
                pass
            self.dlg.Prepare(self)
            self.dlg.ShowModal()
            oldlnb = lnb
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

    def CmdTune(self, evt):
        row = self.GetGridCursorRow()
        lnb = self.table.screen.record_at_row(row)
        network = get_current_network(lnb)
        if network is None:
            ShowMessage("No ref mux", "Cannot find a ref mux")
            return
        txn = self.table.db.wtxn()
        mux = pychdb.dvbs_mux.find_by_key(txn, network.ref_mux)
        txn.abort()
        del txn
        if mux is None:
            ShowMessage("No ref mux", f"Cannot find a ref mux for network {network.ref_mux}")
            return
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

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

    def CurrentLnb(self):
        assert self.selected_row is not None
        if self.selected_row >= self.table.GetNumberRows():
            self.selected_row = max(self.table.GetNumberRows() -1, 0)
        lnb = self.table.GetRow(self.selected_row)
        dtdebug(f'CURRENT LNB: sel={self.selected_row} {lnb}  {len(lnb.networks)}')
        return lnb

class BasicLnbGrid(LnbGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)


class LnbGrid(LnbGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
