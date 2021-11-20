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
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer,  GridPopup, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage

import pychdb

class lnbnetwork_screen_t(object):
    def __init__(self, parent):
        self.parent = parent
        assert self.parent.lnb is not None

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
        [CD(key='sat_pos',  label='LNB Pos.', dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='priority',  label='priority'),
         CD(key='usals_pos',  label='Usals pos.', allow_others=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='diseqc12',  label='diseqc 1.2'),
         CD(key='enabled',  label='enabled', dfn=bool_fn),
         CD(key='ref_mux',  label='ref mux', readonly= True, example="28.2E: nid=1234 tid=1234")
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'sat_pos'
        data_table= pychdb.lnb_network
        self.lnb = None
        self.changed = False
        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table = data_table,
                         record_t=pychdb.lnb_network.lnb_network,
                         screen_getter = self.screen_getter,
                         initial_sorted_column = initial_sorted_column,
                         **kwds)

    def screen_getter(self, txn, subfield):
        """
        txn is not used; instead we use self.lnb
        """
        if self.lnb is None:
            lnbgrid = self.parent.GetParent().GetParent().lnbgrid
            self.lnb = lnbgrid.CurrentLnb().copy()
            assert self.lnb is not None
        self.screen = screen_if_t(lnbnetwork_screen_t(self))

    def __save_record__(self, txn, record):
        dtdebug(f'NETWORKS: {len(self.lnb.networks)}')
        if True:
            added = pychdb.lnb.add_network(self.lnb, record)
            if added:
                self.changed = True
            return record
        else:
            for i in range(len(self.lnb.networks)):
                if self.lnb.networks[i].sat_pos == record.sat_pos:
                    self.lnb.networks[i] = record
                    self.changed = True
                    return record
            i = len(self.lnb.networks)
            dtdebug(f"Adding network={record} to llnb{self.lnb}")
            self.lnb.networks.resize(i+1)
            self.lnb.networks[i] = record
            self.changed = True
            return record

    def __delete_record__(self, txn, record):
        for i in range(len(self.lnb.networks)):
            if self.lnb.networks[i].sat_pos == record.sat_pos:
                self.lnb.networks.erase(i)
                self.changed = True
                return
        dtdebug("ERROR: cannot find record to delete")
        self.changed = True

    def remove_duplicate_networks(self):
        idx1 = 0
        erased = False
        while idx1 < len(self.lnb.networks):
            for idx2 in range(idx1+1, len(self.lnb.networks)):
                if self.lnb.networks[idx1].sat_pos == self.lnb.networks[idx2].sat_pos:
                    self.lnb.networks.erase(idx2)
                    erased = True
                    break
            idx1 += 1
        return erased

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
        table = LnbNetworkTable(self)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        #todo: these accellerators should be copied from neumomenu
        self._add_accels([
            (wx.ACCEL_CTRL,  ord('D'), self.OnDelete),
            (wx.ACCEL_CTRL,  ord('N'), self.OnNew),
            (wx.ACCEL_ALT,  ord('E'), self.OnEditMode)
        ])
        self.EnableEditing(self.app.frame.edit_mode)

    def OnDone(self, evt):
        #@todo(). When a new record has been inserted and network has been changed, and then user clicks "done"
        #this is not seen as a change, because the editor has not yet saved itself
        self.table.SaveModified() #fake save
        if self.table.changed:
            if len(self.table.lnb.networks) ==0:
                ShowMessage(title=_("Need at least one network per LNB"),
                            message=_("Each LNB needs at least one network. A default one has been added"))
            if self.table.remove_duplicate_networks():
                ShowMessage(title=_("Duplicate network on LNB"),
                            message=_("All networks on an LNB need to unique. One or more duplicates have been removed."))

            lnbgrid = self.GetParent().GetParent().lnbgrid
            lnbgrid.set_networks(self.table.lnb.networks)
            lnbgrid.table.SaveModified()
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

        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

    def OnNew(self, evt):
        wx.CallAfter(wx.GetApp().frame.SetEditMode, True)
        return super().OnNew(evt)

    def OnEditMode(self, evt):
        self.app.frame.ToggleEditMode()
        self.EnableEditing(self.app.frame.edit_mode)
