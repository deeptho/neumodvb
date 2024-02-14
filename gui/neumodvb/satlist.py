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

from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, screen_if_t, IconRenderer, MyColLabelRenderer
from neumodvb.neumo_dialogs import ShowMessage
from neumodvb.neumodbutils import enum_to_str
from neumodvb.satbandlist_combo import EVT_SATBAND_SELECT

import pychdb
import pydevdb

def band_scans_fn(x):
    scans = x[1]
    ret =[]
    is_ku = x[0].sat_band == pychdb.sat_band_t.Ku
    for scan in scans:
        if(scan.scan_time == 0):
            continue
        d = datetime.datetime.fromtimestamp(scan.scan_time, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        hilo = f'{enum_to_str(scan.sat_sub_band)} ' if is_ku  else ""
        ret.append(f'{hilo}{enum_to_str(scan.pol)}: {d}: {enum_to_str(scan.scan_status)}')
    return '\n'.join(ret)

class SatTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    scan_time_fn = lambda x: datetime.datetime.fromtimestamp(x[1].scan_time, \
                                                             tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='sat_pos',  label='position', basic=True, no_combo = True, #allow entering sat_pos
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='sat_band',  label='Band', basic=True, dfn = lambda x: enum_to_str(x[1]), example="KaA "),
         CD(key='name',  label='Name', basic=True, example=" Eutelsat 6a/12b13c "),
         CD(key='band_scans', label='Scan\nstatus', dfn=band_scans_fn,
            example='H: 2023-12-31 00:00:00 PARTIAL/PARTIAL '*4),
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'sat_pos'
        data_table= pychdb.sat
        self.sats_inited = False
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)
        self.sat = None
        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table=data_table,
                         screen_getter = screen_getter,
                         record_t=pychdb.sat.sat, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True

    def __save_record__(self, txn, record):
        pychdb.put_record(txn, record)
        return record

    def filter_band_(self, sat_band):
        """
        install a filter to only allow satellites from a specific satellite band which is identified
        by sat_band
        """
        import pydevdb
        match_data, matchers = self.get_filter_()
        if matchers is None:
            match_data = self.record_t()
            matchers = pydevdb.field_matcher_t_vector()

        sat_band_field_id = self.data_table.subfield_from_name("sat_band")

        #if user has already filtered for a specific sat, then setting a limit is pointless
        for m in matchers:
            if m.field_id == freq_field_id:
                return match_data, matchers # this matcher is more specific
        #push sat_band for matching
        m = pydevdb.field_matcher.field_matcher(sat_band_field_id, pydevdb.field_matcher.match_type.EQ)
        matchers.push_back(m)

        match_data.sat_band = sat_band
        return match_data, matchers

    def screen_getter_xxx(self, txn, sort_order):
        if self.parent.sat_band is None:
            match_data, matchers = self.get_filter_()
        else:
            match_data, matchers = self.filter_band_(self.parent.sat_band)
        screen = pychdb.sat.screen(txn, sort_order=sort_order,
                                   field_matchers=matchers, match_data = match_data)
        if screen.list_size==0 and not self.sats_inited:
            self.sats_inited = True
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

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def highlight_colour(self,sat):
        e = wx.GetApp().frame.command_being_edited
        if e is None:
            return None

        ret = e.sats.index(sat)!=-1
        return self.parent.default_highlight_colour if ret else None

class SatGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = SatTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = int(pychdb.sat.column.sat_pos) << 24
        self.sort_column = None
        self.sat = None
        self.sat_band = None #only show satellites for this band
        self.allow_all = True

    def handle_sat_band_change(self, evt, sat_band, sat):
        self.table.GetRow.cache_clear()
        self.OnRefresh(None, sat)

    def CmdSelectSatBand(self, evt):
        sat_band = evt.sat_band
        dtdebug(f'satlist received CmdSelectSatBand {sat_band}')
        wx.CallAfter(self.SelectSatBand, sat_band)

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

    def CmdCreateScanHelper(self, with_schedule):
        from neumodvb.scan_dialog import show_scan_dialog
        self.table.SaveModified()
        rows = self.GetSelectedRows()
        if len(rows)==0:
            ShowMessage("No sats selected for scan")
            return None
        sats = []
        for row in rows:
            sat = self.table.GetRow(row)
            sats.append(sat)
        title =  ', '.join([str(sat) for sat in sats[:3]])
        if len(sats) >=3:
            title += '...'

        return show_scan_dialog(self, with_schedule=with_schedule, allow_band_scan=True, title=f'Scan {title}',
                                sats=sats)

    def CmdScan(self, evt):
        scan_command = self.CmdCreateScanHelper(with_schedule=False)
        sats, subscription_type = (None, None) if scan_command is None  else \
            (scan_command.sats, scan_command.tune_options.subscription_type)
        if scan_command is None or sats is None:
            dtdebug(f'CmdScan aborted for {0 if sats is None else len(sats)} sats')
            return
        dtdebug(f'CmdScan requested for {len(sats)} sats')
        import pydevdb
        if subscription_type == pydevdb.subscription_type_t.MUX_SCAN:
            self.app.MuxesOnSatScan(sats, scan_command.tune_options, scan_command.band_scan_options)
        elif subscription_type == pydevdb.subscription_type_t.SPECTRUM_ACQ:
            self.app.SpectrumOnSatAcq(sats, tune_options, band_scan_options)
        elif subscription_type == pydevdb.subscription_type_t.BAND_SCAN:
            self.app.BandsOnSatScan(scan_command.sats, scan_command.tune_options, scan_command.band_scan_options)
        else:
            assert False

    def CmdCreateScanCommand(self, evt):
        scan_command =  self.CmdCreateScanHelper(with_schedule=True)
        sats, tune_options = (None, None) if scan_command is None  else \
            (scan_command.sats, scan_command.tune_options)
        if sats is None or tune_options is None:
            dtdebug(f'CmdCreateScanCommand aborted for {0 if sats is None else len(sats)} sats')
            return
        dtdebug(f'CmdCreateScanCommand requested for {len(sats)} sats')
        import pydevdb
        wtxn =  wx.GetApp().devdb.wtxn()
        pydevdb.scan_command.make_unique_if_template(wtxn, scan_command)
        scan_command.mtime = int(datetime.datetime.now(tz=tz.tzlocal()).timestamp())
        pydevdb.put_record(wtxn, scan_command)
        wtxn.commit()

    def CurrentSatAndSatBand(self):
        if self.sat_band is not None:
            if self.sat is not None and self.sat.sat_band == self.sat_band:
                return self.sat_band, self.sat #already ok
            service = wx.GetApp().live_service_screen.selected_service
            sat_pos = pychdb.sat.sat_pos_none if service is None else service.k.mux.sat_pos
            txn = wx.GetApp().chdb.rtxn()
            self.sat = pychdb.select_sat_for_sat_band(txn, self.sat_band)
            txn.abort()
        elif self.sat is not None and self.sat_band is not None:
            txn = wx.GetApp().chdb.rtxn()
            self.sat = pychdb.select_sat_for_sat_band(txn, self.sat_band, self.sat.sat_pos)
            txn.abort()
        return self.sat_band, self.sat

    def CmdCommandAddSat(self, evt):
        rows = self.GetSelectedRows()
        sats = [ self.table.screen.record_at_row(row) for row in rows]
        if self.app.frame.command_being_edited is None:
            dtdebug(f'request to add sat {sats} to command={self.app.frame.command_being_edited} IGNORED')
            return
        else:
            dtdebug(f'request to add sat {sats} to {self.app.frame.command_being_edited}')
        command = self.app.frame.command_being_edited
        assert command is not None
        for sat in sats:
            idx = command.sats.index(sat)
            if idx <0:
                command.sats.push_back(sat)
            else:
                command.sats.erase(idx)
        wtxn = wx.GetApp().devdb.wtxn()
        pydevdb.put_record(wtxn, command)
        wtxn.commit()
        self.table.OnModified()

    @property
    def CmdEditCommandMode(self):
        if wx.GetApp().frame.command_being_edited is None:
            return False #signal to neumomenu that item is disabled
        return self.app.frame.scancommandgrid.CmdEditCommandMode

class BasicSatGrid(SatGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        self.SetSelectionMode(wx.grid.Grid.SelectRows)

class SatGrid(SatGridBase):
    def get_initial_data(self):
        h = wx.GetApp().receiver.browse_history
        self.sat_band = h.h.satlist_filter_sat_band
        if self.sat_band == pychdb.sat_band_t.UNKNOWN:
            self.sat_band = None
            ls = wx.GetApp().live_service_screen
            if ls.filter_sat is not None:
                self.sat = ls.filter_sat
            else:
                service = ls.selected_service
                if service is not None:
                    txn = wx.GetApp().chdb.rtxn()
                    sat_band, low_high = pychdb.sat_band_for_freq(service.frequency)
                    self.sat=pychdb.sat.find_by_key(txn, service.k.mux.sat_pos, sat_band)
                    txn.abort()
                    self.sat_band = sat_band
                    del txn
        else:
            txn = wx.GetApp().chdb.rtxn()
            self.sat = pychdb.select_sat_for_sat_band(txn, self.sat_band)
            txn.abort()
            del txn

    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
        #self.get_initial_data()
        #if self.sat.sat_pos == pychdb.sat.sat_pos_none:
        #    self.sat = None

        self.GetParent().Bind(EVT_SATBAND_SELECT, self.CmdSelectSatBand)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        super().OnWindowCreate(evt)
        self.GrandParent.sat_satband_sel.SetSatBand(self.sat_band, self.allow_all)
    def InitialRecord(self):
        if self.sat is None:
            self.get_initial_data()
        return self.sat

    def SelectSatBand(self, sat_band):
        self.sat_band = sat_band

        sat_band, sat = self.CurrentSatAndSatBand()
        h = wx.GetApp().receiver.browse_history
        if sat_band is None:
            h.h.satlist_filter_sat_band = pychdb.sat_band_t.UNKNOWN
        else:
            h.h.satlist_filter_sat_band = sat_band
        h.save()
        wx.CallAfter(self.SetFocus)
        wx.CallAfter(self.handle_sat_band_change, None, sat_band, self.sat)

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)
