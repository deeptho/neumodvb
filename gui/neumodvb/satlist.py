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

from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, screen_if_t, IconRenderer, MyColLabelRenderer

import pychdb

class SatTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='sat_pos',  label='position', basic=True, no_combo = True, #allow entering sat_pos
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='name',  label='Name', basic=True, example=" Eutelsat 6a/12b13c "),
        ]

    def InitialRecord(self):

        self.sat = getattr(self.parent, "sat", None)
        #Todo: improve this ugliness
        #If we are part of positioner or spectrum dialig, use the sat selected there
        if self.sat:
            self.sat = self.parent.sat
            return self.sat
        fn = getattr(getattr(self.parent, "controller", None), "CurrentSatAndMux", None)
        if fn is not None:
            #Todo: improve this ugliness
            #If we are part of positioner or spectrum dialig, use the sat selected there
            self.sat, _  = fn()
            return self.sat
        ls = wx.GetApp().live_service_screen
        if ls.filter_sat is not None:
            self.sat = ls.filter_sat
            return self.sat
        service = ls.selected_service
        if service is not None:
            txn = wx.GetApp().chdb.rtxn()
            self.sat=pychdb.sat.find_by_key(txn, service.k.mux.sat_pos)
            txn.abort()
            del txn
        return self.sat

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'sat_pos'
        data_table= pychdb.sat

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)
        self.sat = None
        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table=data_table,
                         screen_getter = screen_getter,
                         record_t=pychdb.sat.sat, initial_sorted_column = initial_sorted_column,
                         **kwds)

    def screen_getter_xxx(self, txn, sort_order):
        match_data, matchers = self.get_filter_()
        if pychdb.sat.find_by_key(txn, pychdb.sat.sat_pos_dvbc) is None  or \
           pychdb.sat.find_by_key(txn, pychdb.sat.sat_pos_dvbt) is None:
            from neumodvb.init_db import fix_db
            fix_db()
            #open a read txn to reflect the update
            #note that parent will continue to use outdated txn, but screen will still be ok
            #and we should not close the parent's txn, because parent will do that
            #also note that garbage collection will clean up the txn
            txn = self.db.rtxn()
        screen = pychdb.sat.screen(txn, sort_order=sort_order,
                                   field_matchers=matchers, match_data = match_data)
        if screen.list_size==0:
            from neumodvb.init_db import init_db
            dtdebug("Empty database; adding sats")
            #open a read txn to reflect the update
            #note that parent will continue to use outdated txn, but screen will still be ok
            #and we should not close the parent's txn, because parent will do that
            #also note that garbage collection will clean up the txn
            init_db()
            txn = self.db.rtxn()
            screen = pychdb.sat.screen(txn, sort_order=sort_order,
                                       field_matchers=matchers, match_data = match_data)
            txn.abort()
            del txn
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        pychdb.put_record(txn, record)
        return record

    def __new_record__(self):
        ret=self.record_t()
        return ret

class SatGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = SatTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = int(pychdb.sat.column.sat_pos) << 24
        self.sort_column = None

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

    def OnTune(self, evt):
        row = self.GetGridCursorRow()
        rec = self.screen.record_at_row(row).ref_mux
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'OnTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

    def CmdPositioner(self, event):
        dtdebug('CmdPositioner')
        self.OnPositioner(event)

    def OnPositioner(self, evt):
        row = self.GetGridCursorRow()
        sat = self.table.screen.record_at_row(row)
        dtdebug(f'Positioner requested for sat={sat}')
        from neumodvb.positioner_dialog import show_positioner_dialog
        show_positioner_dialog(self, sat=sat)
        #TODO: we can only know lnb after tuning!
        self.table.SaveModified()

    def CmdSpectrum(self, evt):
        row = self.GetGridCursorRow()
        sat = self.table.screen.record_at_row(row)
        dtdebug(f'Spectrum requested for sat={sat}')
        from neumodvb.spectrum_dialog import show_spectrum_dialog
        show_spectrum_dialog(self, sat=sat)
        #TODO: we can only know lnb after tuning!
        self.table.SaveModified()
        #self.app.MuxTune(mux)

class BasicSatGrid(SatGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        self.SetSelectionMode(wx.grid.Grid.SelectRows)


class SatGrid(SatGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
