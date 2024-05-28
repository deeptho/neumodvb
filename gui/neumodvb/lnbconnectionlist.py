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

from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer,  GridPopup, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage
from neumodvb.util import find_parent_prop
import pydevdb
import pychdb

class lnbconnection_screen_t(object):
    def __init__(self, parent):
        self.parent = parent

    @property
    def list_size(self):
        return len(self.parent.lnb.connections)

    def record_at_row(self, rowno):
        assert(rowno>=0)
        if rowno >= self.list_size:
            assert(rowno == self.list_size)
        assert rowno < self.list_size
        return self.parent.lnb.connections[rowno]

    def update(self, txn):
        return True

    def set_reference(self, rec):
        lnb = self.parent.lnb
        for i in range(len(lnb.connections)):
            if lnb.connections[i].has_same_key(rec):
                return i
        return -1

class LnbConnectionTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    card_rf_input_dfn = lambda x: x[0].connection_name
    card_rf_input_cfn = lambda table: table.card_rf_input_cfn()
    card_rf_input_sfn = lambda x: x[2].card_rf_input_sfn(x[0], x[1])
    all_columns = \
        [#CD(key='card_mac_address',  label='MAC', basic=True, no_combo=False, readonly=False,
         #   dfn=adapter_fn, example=" AA:BB:CC:DD:EE:FF "),
         CD(key='rf_input',  label='Card RF#in', basic=True, readonly=False, example="TBS 6909X C0#3 ",
            dfn=card_rf_input_dfn, sfn=card_rf_input_sfn),
         CD(key='enabled',   label='ena-\nbled', basic=False),
         CD(key='can_be_used',   label='avail\nable', basic=False),
         CD(key='priority',  label='prio'),
         CD(key='rf_coupler_id',  label='rf\ncoupler', basic=False),
         CD(key='rotor_control',  label='rotor\ncontrol', basic=False, dfn=lambda x: lastdot(x[1]), example='ROTOR Master USALS'),
         CD(key='diseqc_10',  label='diseqc\n10'),
         CD(key='diseqc_11',  label='diseqc\n11'),
         #CD(key='diseqc_mini',  label='diseqc\nmini'),
         CD(key='tune_string',  label='tune\nstring'),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'connection_name'
        data_table= pydevdb.lnb_connection
        self.lnb_ = None
        self.changed = False
        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         record_t=pydevdb.lnb_connection.lnb_connection,
                         screen_getter = self.screen_getter,
                         initial_sorted_column = initial_sorted_column,
                         **kwds)

    @property
    def lnb(self):
        if self.lnb_  is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
        return self.lnb_

    @lnb.setter
    def lnb(self, val):
        assert False

    @property
    def lnb_connection(self):
        if hasattr(self.parent, "lnb_connection"):
            return self.parent.lnb_connection
        return None
    @lnb_connection.setter
    def lnb_connection(self, val):
        if hasattr(self.parent, "lnb_connection"):
            self.parent.lnb_connection = val

    def screen_getter(self, txn, sort_field):
        """
        txn is not used; instead we use self.lnb
        """
        self.screen = screen_if_t(lnbconnection_screen_t(self), self.sort_order==2)

    def __save_record__(self, wtxn, record):
        dtdebug(f'CONNECTIONS: {len(self.lnb.connections)}')
        changed = pydevdb.lnb.add_or_edit_connection(wtxn, self.lnb, record)
        if changed:
            self.changed = True
        return record

    def __delete_record__(self, txn, record):
        for i in range(len(self.lnb.connections)):
            if self.lnb.connections[i].card_mac_address == record.card_mac_address and \
                self.lnb.connections[i].rf_input == record.rf_input:
                self.lnb.connections.erase(i)
                self.changed = True
                return
        dtdebug("ERROR: cannot find record to delete")
        self.changed = True

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def card_rf_input_sfn(self, rec, v):
        d = wx.GetApp().get_cards_with_rf_in()
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
        rtxn = self.db.rtxn()
        #we do not want to overwrite the official lnb yet (would disturb detection of record being edited)
        changed = pydevdb.lnb.add_or_edit_connection(rtxn, self.lnb, rec)
        rtxn.abort()
        return rec

    def needs_highlight(self, conn):
        """
        show lnb connectiobs for missing adapters in colour
        """
        return self.parent.default_highlight_colour if not conn.can_be_used else None

class LnbConnectionGrid(NeumoGridBase):
    def _add_accels(self, items):
        accels=[]
        for a in items:
            randomId = wx.NewId()
            accels.append([a[0], a[1], randomId])
            self.Bind(wx.EVT_MENU, a[2], id=randomId)
        accel_tbl = wx.AcceleratorTable(accels)
        self.SetAcceleratorTable(accel_tbl)

    def __init__(self, basic, readonly, *args, **kwds):
        table = LnbConnectionTable(self)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        #todo: these accellerators should be copied from neumomenu
        self._add_accels([
            (wx.ACCEL_CTRL,  ord('D'), self.OnDelete),
            (wx.ACCEL_CTRL,  ord('N'), self.OnNew),
            (wx.ACCEL_CTRL,  ord('E'), self.OnEditMode)
        ])
        self.EnableEditing(self.app.frame.edit_mode)

    def InitialRecord(self):
        return self.table.lnb_connection

    def OnDone(self, evt):
        #@todo(). When a new record has been inserted and connection has been changed, and then user clicks "done"
        #this is not seen as a change, because the editor has not yet saved itself
        self.table.SaveModified() #fake save
        if self.table.changed:
            if len(self.table.lnb.connections) ==0:
                ShowMessage(title=_("Need at least one connection per LNB"),
                            message=_("Each LNB needs at least one connection. A default one has been added"))
        dtdebug(f"OnDone called changed-{self.table.changed}")

    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN  and not evt.HasAnyModifiers():
            self.MoveCursorRight(False)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def CmdTune(self, evt):
        row = self.GetGridCursorRow()
        mux_key = self.screen.record_at_row(row).ref_mux
        txn = self.db.wtxn()
        mux = pychdb.dvbs_mux.find_by_key(txn, mux_key)
        txn.abort()
        del txn
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

    def OnNew(self, evt):
        self.app.frame.SetEditMode(True)
        self.EnableEditing(self.app.frame.edit_mode)
        return super().OnNew(evt)

    def CmdNew(self, event):
        dtdebug("CmdNew")
        f = wx.GetApp().frame
        if not f.edit_mode:
            f.SetEditMode(True)
        self.OnNew(event)

    def OnEditMode(self, evt):
        dtdebug(f'old_mode={self.app.frame.edit_mode}')
        self.app.frame.ToggleEditMode()
        self.EnableEditing(self.app.frame.edit_mode)

    def handle_lnb_change(self, lnb, rf_path):
        self.table.GetRow.cache_clear()
        self.OnRefresh(None, rf_path)
        if rf_path is None:
            self.rf_path = self.table.screen.record_at_row(0)
        else:
            self.rf_path = rf_path

class BasicLnbConnectionGrid(LnbConnectionGrid):
    def __init__(self, *args, **kwds):
        basic = True
        readonly = True
        super().__init__(basic, readonly, *args, **kwds)
