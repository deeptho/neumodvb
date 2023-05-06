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

from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumo_dialogs_gui import ChannelNoDialog_
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t
from neumodvb.satlist_combo import EVT_SAT_SELECT

import pychdb

class ChannelNoDialog(ChannelNoDialog_):
    def __init__(self, parent, basic, *args, **kwds):
        self.parent= parent
        self.timeout = 1000
        if "initial_chno" in kwds:
            initial_chno = str(kwds['initial_chno'])
            del kwds['initial_chno']
        else:
            initial_chno = None
        kwds["style"] =  kwds.get("style", 0) | wx.NO_BORDER
        super().__init__(parent, basic, *args, **kwds)
        if initial_chno is not None:
            self.chno.ChangeValue(initial_chno)
            self.chno.SetInsertionPointEnd()
        self.timer= wx.Timer(owner=self , id =1)
        self.Bind(wx.EVT_TIMER, self.OnTimer)
        self.timer.StartOnce(milliseconds=self.timeout)
        self.chno.Bind(wx.EVT_CHAR, self.CheckCancel)

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def OnText(self, event):
        self.timer.Stop()
        self.timer.StartOnce(milliseconds=self.timeout)
        event.Skip()

    def OnTextEnter(self, event):  # wxGlade: ChannelNoDialog.<event_handler>
        self.OnTimer(None)
        event.Skip()

    def OnTimer(self, event, ret=wx.ID_OK):
        self.EndModal(ret)


def ask_channel_number(caller, initial_chno=None):
    if initial_chno is not None:
        initial_chno = str(initial_chno)
    dlg = ChannelNoDialog(caller, -1, "Channel Number", initial_chno = initial_chno)

    val = dlg.ShowModal()
    chno = None
    if val == wx.ID_OK:
        try:
            chno = int(dlg.chno.GetValue())
        except:
            pass
    dlg.Destroy()
    return chno

class ServiceTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    bool_fn = NeumoTable.bool_fn
    lang_fn = lambda x: ';'.join([ str(xx) for xx in x[1]])
    all_columns = \
        [CD(key='ch_order',  label='#', basic=True, example="10000"),
         CD(key='name',  label='Name', basic=True, example="Investigation discovery12345"),
         CD(key='frequency',  label='freq', dfn= lambda x: f'{x[1]/1000.:9.3f}', example=" 10725.114 "),
         CD(key='pol',  label='pol', dfn=lambda x: lastdot(x[1]).replace('POL',''), example='V'),
         CD(key='k.mux.sat_pos', label='Sat', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='k.mux.mux_id',  label='mux\nid'),
         CD(key='k.network_id',  label='nid'),
         CD(key='k.ts_id',  label='tsid'),
         CD(key='k.service_id',  label='sid'),
         CD(key='k.mux.stream_id',  label='isi'),
         CD(key='k.mux.t2mi_pid',  label='t2mi'),
         CD(key='encrypted',  label='Encrypted', dfn=bool_fn),
         CD(key='expired',  label='Expired',  dfn=bool_fn),
         CD(key='media_mode',  label='type', dfn=lambda x: lastdot(x)),
         CD(key='pmt_pid',  label='PMT pid'),
         CD(key='provider',  label='Provider'),
         CD(key='service_type',  label='Type'),
         CD(key='mtime',  label='Modified', dfn=datetime_fn, example='2021-06-16 18:30:33'),
         CD(key='video_pid',  label='VPID'),
         CD(key='icons',  label='', basic=False, dfn=bool_fn, example='1234'),
         CD(key='audio_pref',  label='pref', basic=False, dfn=lang_fn, example='1234dddddddddddddd'),
         ]

    def InitialRecord(self):
        service = self.app.live_service_screen.selected_service
        dtdebug(f"INITIAL service: service={service}")
        return service

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'ch_order'
        data_table= pychdb.service
        self.basic = basic

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t = pychdb.service.service,
                         initial_sorted_column = initial_sorted_column, **kwds)
        self.app = wx.GetApp()

    def __save_record__(self, txn, record):
        pychdb.put_record(txn, record)
        return record

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        if self.parent.restrict_to_sat:
            sat, service = self.parent.CurrentSatAndService()
            ref = pychdb.service.service()
            ref.k.mux.sat_pos = sat.sat_pos
            txn = self.db.rtxn()
            screen = pychdb.service.screen(txn, sort_order=sort_field,
                                           key_prefix_type=pychdb.service.service_prefix.sat_pos,
                                           key_prefix_data=ref,
                                           field_matchers=matchers, match_data = match_data)
            txn.abort()
            del txn
        else:
            sat = None
            service = None
            screen = pychdb.service.screen(txn, sort_order=sort_field,
                                           field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __new_record__(self):
        ret = self.record_t()
        sat, _ = self.parent.CurrentSatAndService()
        if sat:
            ret.k.mux.sat_pos = sat.sat_pos
        return ret

    def get_icons(self):
        return (self.app.bitmaps.encrypted_bitmap, self.app.bitmaps.expired_bitmap)

    def get_icon_state(self, rowno, colno):
        #col = self.columns[colno]
        service = self.GetRow(rowno)
        if service is None:
            return (False, False)
        return ( service.encrypted, service.expired)

    def get_icon_sort_key(self):
        return 'encrypted'

    def highlight_colour(self,service):
        e = self.app.frame.bouquet_being_edited
        if e is None:
            return None

        txn =self.db.rtxn()
        ret = pychdb.chg.contains_service(txn, e, service.k)
        txn.abort()
        del txn
        return self.parent.default_highlight_colour if ret else None

class ServiceGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        self.allow_all = True
        table = ServiceTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.SetSelectionMode(wx.grid.Grid.SelectRows)
        self.SetTabBehaviour(wx.grid.Grid.Tab_Leave)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.grid_specific_menu_items=['epg_record_menu_item']
        self.restrict_to_sat = None
        self.service = None

    def MoveToChno(self, chno):
        txn = wx.GetApp().chdb.rtxn()
        service = pychdb.service.find_by_ch_order(txn, chno)
        txn.abort()
        del txn
        if service is None:
            return
        row = self.table.screen.set_reference(service)
        if row is not None:
            self.GoToCell(row, self.GetGridCursorCol())
            self.SelectRow(row)
            self.SetFocus()

    def OnShow(self, evt):
        self.service = None
        super().OnShow(evt)

    def OnToggleRecord(self, evt):
        row = self.GetGridCursorRow()
        service = self.table.screen.record_at_row(row)
        return service, None

    def OnCellChanged(self, evt):
        self.MoveCursorRight(False)
        evt.Skip(False)

    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        modifiers = evt.GetModifiers()
        is_ctrl = (modifiers & wx.ACCEL_CTRL)
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            self.MoveCursorRight(False)
            evt.Skip(False)
        else:
            evt.Skip(True)
        keycode = evt.GetUnicodeKey()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.EditMode():
                self.MoveCursorRight(False)
            else:
                row = self.GetGridCursorRow()
                service = self.table.GetRow(row)
                self.app.ServiceTune(service)
            evt.Skip(False)
        elif not self.EditMode() and not is_ctrl and IsNumericKey(keycode):
            self.MoveToChno(ask_channel_number(self, keycode- ord('0')))
        else:
            return #evt.Skip(True)

    def EditMode(self):
        return  self.GetParent().GetParent().edit_mode

    def CmdSelectSat(self, evt):
        sat = evt.sat
        wx.CallAfter(self.SelectSat, sat)

    def SelectSat(self, sat):
        self.restrict_to_sat = sat
        self.service = None
        self.app.live_service_screen.set_sat_filter(sat)
        self.restrict_to_sat = self.app.live_service_screen.filter_sat
        wx.CallAfter(self.handle_sat_change, None, self.service)

    def handle_sat_change(self, evt, service):
        dtdebug(f'doit rec_to_select={service}')
        self.table.GetRow.cache_clear()
        self.OnRefresh(evt, service)

    def CurrentSatAndService(self):
        if self.restrict_to_sat is None:
            service = self.app.live_service_screen.selected_service
            self.restrict_to_sat =  self.app.live_service_screen.filter_sat
            self.service = service
        else:
            if self.service is None:
                self.service =  self.app.live_service_screen.selected_service
        return self.restrict_to_sat, self.service

    def CurrentGroupText(self):
        sat, service = self.CurrentSatAndService()
        if sat is None:
            return "All satellites" if self.allow_all else ""
        return str(sat.name if len(sat.name)>0 else str(sat))


    def CmdTune(self, evt):
        rowno = self.GetGridCursorRow()
        service = self.table.GetRow(rowno)
        self.table.SaveModified()
        self.app.ServiceTune(service, replace_running=True)

    def CmdTuneAdd(self, evt):
        rowno = self.GetGridCursorRow()
        service = self.table.GetRow(rowno)
        self.table.SaveModified()
        self.app.ServiceTune(service, replace_running=False)

    def CmdPositioner(self, event):
        dtdebug('CmdPositioner')
        self.OnPositioner(event)

    def OnPositioner(self, evt):
        row = self.GetGridCursorRow()
        service = self.table.screen.record_at_row(row)
        if service is None:
            return
        txn = wx.GetApp().chdb.rtxn()
        mux = pychdb.dvbs_mux.find_by_key(txn, service.k.mux)
        if mux is None:
            return
        txn.commit()
        del txn
        mux_name= f"{mux}"
        dtdebug(f'Positioner requested for mux={mux}')
        from neumodvb.positioner_dialog import show_positioner_dialog
        show_positioner_dialog(self, mux=mux)
        #TODO: we can only know lnb after tuning!

    def CmdBouquetAddService(self, evt):
        row = self.GetGridCursorRow()
        service = self.table.screen.record_at_row(row)
        if self.app.frame.bouquet_being_edited is None:
            dtdebug(f'request to add service {service} to bouquet={self.app.frame.bouquet_being_edited} IGNORED')
            return
        else:
            dtdebug(f'request to add service {service} to {self.app.frame.bouquet_being_edited}')
        wtxn =  wx.GetApp().chdb.wtxn()
        assert self.app.frame.bouquet_being_edited is not None
        pychdb.chg.toggle_service_in_bouquet(wtxn, self.app.frame.bouquet_being_edited, service)
        wtxn.commit()
        self.table.OnModified()

    @property
    def CmdEditBouquetMode(self):
        if self.app.frame.bouquet_being_edited is None:
            return False #signal to neumomenu that item is disabled
        return self.app.frame.chggrid.CmdEditBouquetMode



def IsNumericKey(keycode):
    return keycode >= ord('0') and keycode <= ord('9')

class BasicServiceGrid(ServiceGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)

    def OnKeyDownOFF(self, evt):
        keycode = evt.GetUnicodeKey()
        #print(f"KEY CHECKxxx111 {evt.HasAnyModifiers()}")
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            row = self.GetGridCursorRow()
            service = self.table.screen.record_at_row(row)
            dtdebug(f'RETURN pressed on row={row}: PLAY service={service.name}')
            self.app.ServiceTune(service)
            evt.Skip(False)
        elif not self.EditMode() and IsNumericKey(keycode):
            self.MoveToChno(ask_channel_number(self, keycode- ord('0')))
        else:
            evt.Skip(True)


class ServiceGrid(ServiceGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
        self.GetParent().Bind(EVT_SAT_SELECT, self.CmdSelectSat)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        sat, _ = self.CurrentSatAndService()
        self.SelectSat(sat)
        self.GrandParent.service_sat_sel.SetSat(self.restrict_to_sat, self.allow_all)
