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

from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer,  GridPopup, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage
from neumodvb.util import find_parent_prop
import pydevdb
import pychdb

class lnbnetwork_screen_t(object):
    def __init__(self, parent):
        self.parent = parent

    @property
    def list_size(self):
        return len(self.parent.lnb.networks)

    def record_at_row(self, rowno):
        assert(rowno>=0)
        if rowno >= self.list_size:
            assert(rowno == self.list_size)
        assert rowno < self.list_size
        return self.parent.lnb.networks[rowno]

    def update(self, txn):
        return True

    def set_reference(self, rec):
        lnb = self.parent.lnb
        for i in range(len(lnb.networks)):
            if lnb.networks[i].sat_pos == rec.sat_pos:
                return i
        return -1

class LnbNetworkTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [CD(key='sat_pos',  label='LNB Pos.', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='priority',  label='priority', basic=False),
         CD(key='usals_pos',  label='Usals pos.', basic=False, allow_others=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='diseqc12',  label='diseqc 1.2', basic=False),
         CD(key='enabled',  label='enabled', basic=False, dfn=bool_fn),
         CD(key='ref_mux',  label='ref mux', basic=False, readonly= True, example="28.2E: nid=1234 tid=1234")
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'sat_pos'
        data_table= pydevdb.lnb_network
        self.lnb_ = None
        self.changed = False
        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         record_t=pydevdb.lnb_network.lnb_network,
                         screen_getter = self.screen_getter,
                         initial_sorted_column = initial_sorted_column,
                         **kwds)

    @property
    def lnb(self):
        if self.lnb_  is None:
            self.lnb_ = find_parent_prop(self, 'lnb')
        return self.lnb_

    @property
    def network(self):
        if hasattr(self.parent, "network"):
            return self.parent.network
        return None

    @network.setter
    def network(self, val):
        if hasattr(self.parent, "network"):
            self.parent.network = val

    def InitialRecord(self):
        return self.network

    def SetSat(self, sat):
        if self.lnb is None:
            return self.network
        for network in self.lnb.networks:
            if network.sat_pos == sat.sat_pos:
                self.network = network
                return self.network

    def screen_getter(self, txn, sort_field):
        """
        txn is not used; instead we use self.lnb
        """
        self.screen = screen_if_t(lnbnetwork_screen_t(self), self.sort_order==2)

    def matching_sat(self, txn, sat_pos):
        sats = wx.GetApp().get_sats()
        if len(sats) == 0:
            from neumodvb.init_db import load_sats
            dtdebug("Empty database; adding sats")
            load_sats(txn)
        for sat in sats:
            if sat.sat_pos == sat_pos:
                return sat
        return None

    def __save_record__(self, txn, record):
        dtdebug(f'NETWORKS: {len(self.lnb.networks)}')
        if record.usals_pos == pychdb.sat.sat_pos_none:
            record.usals_pos = record.sat_pos
        if record.sat_pos == pychdb.sat.sat_pos_none:
            record.sat_pos = record.usals_pos
        changed = pydevdb.lnb.add_or_edit_network(self.lnb, record, save=False)
        if changed:
            self.changed = True

        for n in self.lnb.networks:
            if self.matching_sat(txn, n.sat_pos) is None:
                ss = pychdb.sat_pos_str(n.sat_pos)
                add = ShowOkCancel("Add satellite?", f"No sat yet for position={ss}; add one?")
                if not add:
                    return None
                sat = pychdb.sat.sat()
                sat.sat_pos = n.sat_pos;
                pychdb.put_record(txn, sat)
        return record

    def __delete_record__(self, txn, record):
        for i in range(len(self.lnb.networks)):
            if self.lnb.networks[i].sat_pos == record.sat_pos:
                self.lnb.networks.erase(i)
                self.changed = True
                return
        dtdebug("ERROR: cannot find record to delete")
        self.changed = True

    def __new_record__(self):
        ret=self.record_t()
        return ret

class LnbNetworkGrid(NeumoGridBase):
    def _add_accels(self, items):
        accels=[]
        for a in items:
            randomId = wx.NewId()
            accels.append([a[0], a[1], randomId])
            self.Bind(wx.EVT_MENU, a[2], id=randomId)
        accel_tbl = wx.AcceleratorTable(accels)
        self.SetAcceleratorTable(accel_tbl)

    def __init__(self, basic, readonly, *args, **kwds):
        table = LnbNetworkTable(self, basic=basic)
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

    def SetSat(self, sat):
        self.network = self.table.SetSat(sat)

    def OnDone(self, evt):
        #@todo(). When a new record has been inserted and network has been changed, and then user clicks "done"
        #this is not seen as a change, because the editor has not yet saved itself
        self.table.SaveModified() #fake save
        if self.table.changed:
            if len(self.table.lnb.networks) ==0:
                ShowMessage(title=_("Need at least one network per LNB"),
                            message=_("Each LNB needs at least one network. A default one has been added"))
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

    def OnEditMode(self, evt):
        dtdebug(f'old_mode={self.app.frame.edit_mode}')
        self.app.frame.ToggleEditMode()
        self.EnableEditing(self.app.frame.edit_mode)

    def handle_lnb_change(self, lnb, network):
        self.table.GetRow.cache_clear()
        self.OnRefresh(None, network)
        if lnb_network is None:
            self.network = self.table.screen.record_at_row(0)
        else:
            self.network = network

class BasicLnbNetworkGrid(LnbNetworkGrid):
    def __init__(self, *args, **kwds):
        basic = True
        readonly = True
        super().__init__(basic, readonly, *args, **kwds)
