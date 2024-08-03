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

from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
import pychdb
import pydevdb

class DvbcMuxTable(NeumoTable):
    record_t =  pychdb.dvbc_mux.dvbc_mux
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S") \
        if x[1]>0 else "never"
    time_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%M:%S")
    epg_types_fn =  lambda x: '; '.join([ lastdot(t) for t in x[1]])
    all_columns = \
        [CD(key='frequency', label='Freq.', dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114 "),
         CD(key='delivery_system', label='System',
            dfn=lambda x: lastdot(x).replace('SYS',"")),
         CD(key='modulation', label='Modul-\nation',
            dfn=lambda x: lastdot(x)),
         CD(key='symbol_rate', label='Symbol\nRate',  dfn= lambda x: x[1]//1000),
         CD(key='fec_outer', label='FEC\nOuter', dfn=lambda x: lastdot(x).replace('FEC','')),
         CD(key='fec_inner', label='FEC \nInner', dfn=lambda x: lastdot(x).replace('FEC','')),
         CD(key='k.stream_id', label='ISI', readonly=True),
         CD(key='k.t2mi_pid', label='t2mi\npid', readonly=True),
         CD(key='k.mux_id', label='mux\nid', readonly=True),
         CD(key='c.network_id', label='nid'),
         CD(key='c.ts_id', label='tsid'),
         CD(key='c.num_services', label='#srv'),
         CD(key='c.mtime', label='Modified', dfn=datetime_fn, example='2021-06-16 18:30:33*'),
         CD(key='c.scan_time', label='Scanned', dfn=datetime_fn, example='2021-06-16 18:30:33*', readonly=True),
         CD(key='c.scan_lock_result', label='lock', dfn=lambda x: lastdot(x)) ,
         CD(key='c.scan_status', label='Scan\nstatus', dfn=lambda x: lastdot(x)),
         CD(key='c.scan_result', label='Scan\nresult', dfn=lambda x: lastdot(x)) ,
         CD(key='c.scan_duration', label='Scan\ntime', dfn=time_fn),
         CD(key='c.epg_scan_completeness', label='EPG \n%'),
         CD(key='c.epg_scan', label='Epg\nscan', dfn=bool_fn),
         CD(key='c.epg_types', label='Epg\ntypes', dfn=epg_types_fn, example='MOVIS ', readonly=True),
         CD(key='c.tune_src', label='tun\nsrc', dfn=lambda x: pychdb.tune_src_str(x[1]), readonly=True, example="nita"),
         CD(key='c.key_src', label='ids\nsrc', dfn=lambda x: pychdb.key_src_str(x[1]), readonly=True, example="nita"),
         #CD(key='c.scan_id', label='Scan\nID', dfn=lambda x: "" if x[1]==0 else str(x[1] & 0xff))
         ]

    other_columns =  \
        [
            CD(key='plp_id', label='plp id')
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'frequency'
        data_table= pychdb.dvbc_mux
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, record_t=self.record_t,
                         data_table= data_table,
                         screen_getter = screen_getter,
                         initial_sorted_column = initial_sorted_column, **kwds)

    def __save_record__(self, txn, record):
        pychdb.dvbc_mux.make_unique_if_template(txn, record)
        pychdb.put_record(txn, record)
        return record

    def get_filter_and_relax_(self):
        """
        make some filters less strict to make them more practica
        """
        match_data, matchers = self.get_filter_()
        freq_field_id = self.data_table.subfield_from_name("frequency")
        srate_field_id = self.data_table.subfield_from_name("symbol_rate")
        match_data2 = self.record_t()
        matchers2 = pydevdb.field_matcher_t_vector()
        added = False
        if matchers is not None:
            for m in matchers:
                if m.field_id == freq_field_id:
                    m.match_type = pydevdb.field_matcher.match_type.GEQ
                    m2 = pydevdb.field_matcher.field_matcher(freq_field_id, pydevdb.field_matcher.match_type.LEQ)
                    matchers2.push_back(m2)
                    freq = match_data.frequency
                    dtdebug(f"Relaxing filter frequency={freq}")
                    match_data.frequency = freq-500
                    match_data2.frequency = freq+500
                    added =True
                if m.field_id == srate_field_id:
                    m.match_type = pydevdb.field_matcher.match_type.GEQ
                    m2 = pydevdb.field_matcher.field_matcher(srate_field_id, pydevdb.field_matcher.match_type.LEQ)
                    matchers2.push_back(m2)
                    srate = match_data.symbol_rate
                    dtdebug(f"Relaxing filter symbol_rate={srate}")
                    match_data.symbol_rate = int(srate*0.95)
                    match_data2.symbol_rate = int(srate*1.05)
                    if match_data2.symbol_rate == match_data.symbol_rate:
                       match_data.symbol_rate = max(0,  match_data2.symbol_rate -100)
                       match_data2.symbol_rate = match_data2.symbol_rate +100

                    added =True
        if added:
            return match_data, matchers, match_data2, matchers2
        else:
            return match_data, matchers, None, None

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers, match_data2, matchers2 = self.get_filter_and_relax_()
        ref = pychdb.dvbc_mux.dvbc_mux()
        ref.k.sat_pos = pychdb.sat.sat_pos_dvbc
        txn = self.db.rtxn()
        screen=pychdb.dvbc_mux.screen(txn, sort_order=sort_field,
                                      key_prefix_type=pychdb.dvbc_mux.dvbc_mux_prefix.none, key_prefix_data=ref,
                                      field_matchers=matchers, match_data = match_data,
                                      field_matchers2=matchers2, match_data2 = match_data2)
        txn.abort()
        del txn
        self.screen=screen_if_t(screen, self.sort_order==2)

    def __new_record__(self):
        ret=self.record_t()
        ret.k.sat_pos = pychdb.sat.sat_pos_dvbc
        ret.c.tune_src = pychdb.tune_src_t.TEMPLATE
        return ret

    def highlight_colour(self,mux):
        e = wx.GetApp().frame.command_being_edited
        if e is None:
            return None

        ret = e.dvbc_muxes.index(mux)!=-1
        return self.parent.default_highlight_colour if ret else None

class DvbcMuxGrid(NeumoGridBase):

    def __init__(self, *args, **kwds):
        basic=False
        readonly = False
        table = DvbcMuxTable(self)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_CHAR, self.OnKeyCheck)
        self.sat = None #sat for which to show muxes

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

    def CmdTune(self, evt):
        self.table.SaveModified()
        row = self.GetGridCursorRow()
        mux = self.table.screen.record_at_row(row)
        mux_name= f"{int(mux.frequency/1000)}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.app.MuxTune(mux)

    def CmdCreateScanHelper(self, with_schedule):
        from neumodvb.scan_dialog import show_scan_dialog
        self.table.SaveModified()
        rows = self.GetSelectedRows()
        if len(rows)==0:
            ShowMessage("No muxes selected for scan")
            return None
        muxes = []
        for row in rows:
            mux = self.table.GetRow(row)
            muxes.append(mux)
        title =  ', '.join([str(mux) for mux in muxes[:3]])
        if len(muxes) >=3:
            title += '...'

        return show_scan_dialog(self, with_schedule = with_schedule, allow_band_scan=False,
                                title=f'Scan {len(muxes)} muxes', dvbc_muxes=muxes)

    def CmdScan(self, evt):
        scan_command=self.CmdCreateScanHelper(with_schedule=False)
        muxes, subscription_type = (None, None) if scan_command is None else \
            (scan_command.dvbc_muxes, scan_command.tune_options.subscription_type)
        assert subscription_type ==  pydevdb.subscription_type_t.MUX_SCAN
        if scan_command is None or muxes is None:
            dtdebug(f'CmdScan aborted for {0 if muxes is None else len(muxes)} muxes')
            return
        dtdebug(f'CmdScan requested for {len(muxes)} muxes')
        self.app.MuxScan(muxes, scan_command.tune_options)

    def CmdCreateScanCommand(self, evt):
        scan_command=self.CmdCreateScanHelper(with_schedule=True)
        muxes, subscription_type = (None, None) if scan_command is None else \
            (scan_command.dvbc_muxes, scan_command.tune_options.subscription_type)
        assert subscription_type ==  pydevdb.subscription_type_t.MUX_SCAN
        if scan_command is None or muxes is None:
            dtdebug(f'CmdScan aborted for {0 if muxes is None else len(muxes)} muxes')
            return
        dtdebug(f'CmdScan requested for {len(muxes)} muxes')

        wtxn =  wx.GetApp().devdb.wtxn()
        pydevdb.scan_command.make_unique_if_template(wtxn, scan_command)
        scan_command.mtime = int(datetime.datetime.now(tz=tz.tzlocal()).timestamp())
        pydevdb.put_record(wtxn, scan_command)
        wtxn.commit()

    def CmdCommandAddMux(self, evt):
        row = self.GetGridCursorRow()
        rows = self.GetSelectedRows()
        muxes = [ self.table.screen.record_at_row(row) for row in rows]
        if self.app.frame.command_being_edited is None:
            dtdebug(f'request to add mux {muxes} to command={self.app.frame.command_being_edited} IGNORED')
            return
        else:
            dtdebug(f'request to add mux {muxes} to {self.app.frame.command_being_edited}')
        command = self.app.frame.command_being_edited
        assert command is not None
        for mux in muxes:
            idx = command.dvbc_muxes.index(mux)
            if idx <0:
                command.dvbc_muxes.push_back(mux)
            else:
                command.dvbc_muxes.erase(idx)
        wtxn = wx.GetApp().devdb.wtxn()
        pydevdb.put_record(wtxn, command)
        wtxn.commit()
        self.table.OnModified()

    def CmdCreateStreamHelper(self):
        from neumodvb.stream_dialog import show_stream_dialog
        self.table.SaveModified()
        rowno = self.GetGridCursorRow()
        mux = self.table.GetRow(rowno)
        return show_stream_dialog(self, title=f'Stream {mux}', dvbc_mux=mux)

    def CmdAddStream(self, evt):
        stream = self.CmdCreateStreamHelper()
        if stream is None:
            dtdebug(f'CmdAddStream aborted')
            return
        dtdebug(f'CmdAddStream requested for {stream}')
        return wx.GetApp().receiver.update_and_toggle_stream(stream)

    @property
    def CmdEditCommandMode(self):
        if wx.GetApp().frame.command_being_edited is None:
            return False #signal to neumomenu that item is disabled
        return self.app.frame.scancommandgrid.CmdEditCommandMode

    def OnTimer(self, evt):
        super().OnTimer(evt)
