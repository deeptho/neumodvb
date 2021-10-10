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

from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t
from neumodvb.satlist import BasicSatGrid

import pychdb

class DvbsMuxTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%M:%S")
    epg_types_fn =  lambda x: '; '.join([ lastdot(t) for t in x[1]])
    all_columns = \
        [CD(key='k.sat_pos', label='Sat', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='frequency', label='Frequency', basic=True, dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114"),
         CD(key='pol', label='Pol', basic=True, dfn=lambda x: lastdot(x).replace('POL',''), example='V'),
         CD(key='delivery_system', label='System',
            dfn=lambda x: lastdot(x).replace('SYS',""), example='DVBS2'),
         CD(key='modulation', label='Modul-\nation',
            dfn=lambda x: lastdot(x), example='PSK8'),
         CD(key='symbol_rate', label='Symbol\nRate',  dfn= lambda x: x[1]//1000),
         CD(key='pls_mode', label='Pls\nMode', dfn=lastdot, example='COMBO'),
         CD(key='pls_code', label='Pls\nCode'),
         CD(key='stream_id', label='Stream', basic=True),
         CD(key='fec', label='FEC', dfn=lambda x: lastdot(x).replace('FEC',''), example='AUTO'),
         CD(key='k.network_id', label='nid'),
         CD(key='k.ts_id', label='tsid'),
         CD(key='k.t2mi_pid', label='t2mi\npid', readonly=False),
         CD(key='k.extra_id', label='subid', readonly=True),
         CD(key='c.num_services', label='#srv'),
         CD(key='c.mtime', label='Modified', dfn=datetime_fn, example='2021-06-16 18:30:33'),
         CD(key='c.scan_time', label='Scanned', dfn=datetime_fn, example='2021-06-16 18:30:33', readonly=True),
         CD(key='c.scan_status', label='Scan\nstatus', dfn=lambda x: lastdot(x).replace('FEC','')),
         CD(key='c.scan_result', label='Scan\nresult', dfn=lambda x: lastdot(x).replace('FEC','')) ,
         CD(key='c.scan_duration', label='Scan time', dfn=time_fn),
         CD(key='c.epg_scan', label='Epg\nscan', dfn=bool_fn),
         CD(key='c.epg_types', label='Epg\ntypes', dfn=epg_types_fn, example='FST'*2, readonly=True)
         ]

    other_columns =  \
        [CD(key='LP_code_rate', label='LP_code_rate'),
         CD(key='bandwidth', label='bandwidth'),
         CD(key='guard_interval', label='guard_interval'),
         CD(key='hierarchy', label='hierarchy'),
         CD(key='rolloff', label='rolloff'),
         CD(key='transmission_mode', label='transmission_mode')]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'frequency'
        data_table= pychdb.dvbs_mux
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t =  pychdb.dvbs_mux.dvbs_mux,
                         initial_sorted_column = initial_sorted_column, **kwds)

    def InitialRecord(self):
        sat, mux= self.parent.CurrentSatAndMux()
        return mux

    def __save_record__(self, txn, record):
        pychdb.dvbs_mux.make_unique_if_template(txn, record)
        pychdb.put_record(txn, record) #this will overwrite any mux with given ts_id even if frequency is very wrong
        return record

    def screen_getter_xxx(self, txn, sort_order):
        if self.parent.allow_all and self.parent.sat:
            sat, mux= self.parent.CurrentSatAndMux()
            ref = pychdb.dvbs_mux.dvbs_mux()
            ref.k.sat_pos = sat.sat_pos
            txn = self.db.rtxn()
            screen = pychdb.dvbs_mux.screen(txn, sort_order=sort_order,
                                            key_prefix_type=pychdb.dvbs_mux.dvbs_mux_prefix.sat_pos,
                                            key_prefix_data=ref)
        else:
            sat = None
            mux = None
            screen = pychdb.dvbs_mux.screen(txn, sort_order=sort_order)
        self.screen=screen_if_t(screen)

    def screen_getter_transposed(self, txn, sort_order):
        if self.mux is None:
            positioner_dialog = self.parent.GetParent().GetParent().positioner
            self.mux = positioner_dialog.mux
        self.screen=screen_if_t(dvbs_mux_screen_t(self))

    def __new_record__(self):
        ret=self.record_t()
        if self.parent.sat is not None:
            ret.k.sat_pos = self.parent.sat.sat_pos
        ret.c.is_template = True
        return ret

class DvbsMuxGridBase(NeumoGridBase):

    def __init__(self, basic, readonly, *args, **kwds):
        self.allow_all = True
        table = DvbsMuxTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_CHAR, self.OnKeyCheck)
        self.mux = None #currently selected mux
        h = wx.GetApp().receiver.browse_history
        self.sat = h.h.dvbs_muxlist_filter_sat
        if self.sat.sat_pos == pychdb.sat.sat_pos_none:
            self.sat = None

    def OnKeyCheck(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.EditMode():
                self.MoveCursorRight(False)
            else:
                row = self.GetGridCursorRow()
                mux = self.table.screen.record_at_row(row)
                dtdebug(f'RETURN pressed on row={row}: PLAY mux={mux}')
                self.app.MuxTune(mux)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def SelectSat(self, sat):
        self.sat = sat
        if sat is not None:
            self.mux = None
        sat, mux = self.CurrentSatAndMux()
        h = wx.GetApp().receiver.browse_history
        if sat is None:
            h.h.dvbs_muxlist_filter_sat.sat_pos = pychdb.sat.sat_pos_none
        else:
            h.h.dvbs_muxlist_filter_sat = sat
        h.save()
        wx.CallAfter(self.doit, None, self.mux)

    def doit(self, evt, mux):
        self.OnRefresh(None, mux)
        if self.mux is None:
            self.mux = self.table.screen.record_at_row(0)
        if self.infow is not None:
            self.infow.ShowRecord(self.mux)

    def CurrentSatAndMux(self):
        if not self.allow_all and self.sat is None:
            default_satpos = 192;
            service = wx.GetApp().live_service_screen.selected_service
            sat_pos = service.k.mux.sat_pos  if service is not None else default_satpos
            txn = wx.GetApp().chdb.rtxn()
            self.sat=pychdb.sat.find_by_key(txn, sat_pos)
            self.mux=pychdb.dvbs_mux.find_by_key(txn, service.k.mux)
            del txn
        if self.mux is None:
            service = wx.GetApp().live_service_screen.selected_service
            if service is None:
                pass
            elif self.sat is None or service.k.mux.sat_pos == self.sat.sat_pos:
                txn = wx.GetApp().chdb.rtxn()
                self.mux=pychdb.dvbs_mux.find_by_key(txn, service.k.mux)
                dtdebug(f"CurrentSatAndMux {self.sat} {self.mux}")
                del txn
            elif self.table.screen:
                self.mux = None
        return self.sat, self.mux

    def CurrentGroupText(self):
        if not self.allow_all:
            sat, mux = self.CurrentSatAndMux()
        else:
            sat = self.sat
        if sat is None:
            return "All satellites" if self.allow_all else ""
        return str(sat.name if len(sat.name)>0 else str(sat))

    def CmdTune(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

    def OnPositioner(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        if mux is None:
            return
        mux_name= f"{mux}"
        dtdebug(f'Positioner requested for mux={mux}')
        from neumodvb.positioner_dialog import show_positioner_dialog
        show_positioner_dialog(self, mux=mux)
        self.table.SaveModified()

    def CmdSpectrum(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        if mux is None:
            return
        mux_name= f"{mux}"
        dtdebug(f'CmdSpectrum requested for mux={mux}')
        from neumodvb.spectrum_dialog import show_spectrum_dialog
        show_spectrum_dialog(self, mux=mux)
        self.table.SaveModified()

    def CmdScan(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdScan requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxScan(mux)


class BasicDvbsMuxGrid(DvbsMuxGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class DvbsMuxGrid(DvbsMuxGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
