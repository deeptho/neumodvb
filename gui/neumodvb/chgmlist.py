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
from neumodvb.neumolist import NeumoTable, NeumoGridBase, GridPopup, screen_if_t
from neumodvb.chglist_combo import EVT_CHG_SELECT

import pychdb

class ChgmTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [CD(key='user_order', label='chno', basic=True, example="1000"),
         CD(key='chgm_order', label='lcn', basic=False, example="1000"),
         CD(key='k.chg.bouquet_id', label='bouquet\nid', basic=False, example="1000"),
         CD(key='k.channel_id', label='id', basic=False, example="1000"),
         CD(key='name',  label='Name', basic=True, example="Investigation discovery12345"),
         CD(key='media_mode',  label='media_mode', dfn=lambda x: lastdot(x), example="RADIO"),
         CD(key='service.mux.sat_pos', label='Sat', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='service.network_id',  label='nid'),
         CD(key='service.ts_id',  label='tsid'),
         CD(key='service.mux.stream_id',  label='isi'),
         CD(key='service.mux.t2mi_pid',  label='t2mi'),
         CD(key='expired',  label='Expired',  dfn=bool_fn),
         CD(key='media_mode',  label='type', dfn=lambda x: lastdot(x)),
         CD(key='service.service_id',  label='sid'),
         CD(key='mtime', label='Modified', dfn=datetime_fn, example="2020-12-29 18:35:01"),
         CD(key='icons',  label='', basic=False, dfn=bool_fn, example='1234'),
         ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'chgm_order'
        data_table= pychdb.chgm
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table= data_table,
                         screen_getter = screen_getter,
                         record_t = pychdb.chgm.chgm,
                         initial_sorted_column = initial_sorted_column, **kwds)
        self.app = wx.GetApp()

    def __save_record__(self, txn, record):
        pychdb.put_record(txn, record)
        return record

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        if  self.parent.restrict_to_chg:
            chg, chgm = self.parent.CurrentChgAndChgm()
            ref = pychdb.chgm.chgm()
            ref.k.chg = chg.k
            txn = self.db.rtxn()
            screen = pychdb.chgm.screen(txn, sort_order=sort_field,
                                        key_prefix_type=pychdb.chgm.chgm_prefix.chg, key_prefix_data=ref,
                                        field_matchers=matchers, match_data = match_data)
            txn.abort()
            del txn
        else:
            chg = None
            chgm = None
            screen = pychdb.chgm.screen(txn, sort_order=sort_field,
                                        field_matchers=matchers, match_data = match_data)
        self.screen=screen_if_t(screen, self.sort_order==2)

    def __new_record__(self):
        return self.record_t()

    def get_icons(self):
        return (self.app.bitmaps.encrypted_bitmap, self.app.bitmaps.expired_bitmap)

    def get_icon_state(self, rowno, colno):
        col = self.columns[colno]
        chgm = self.GetRow(rowno)
        return ( chgm.encrypted, chgm.expired)

    def get_icon_sort_key(self):
        return 'encrypted'

    def highlight_colour(self,chgm):
        e = self.app.frame.bouquet_being_edited
        if e is None:
            return None

        txn =self.db.rtxn()
        ret = pychdb.chg.contains_service(txn, e, chgm.service)
        txn.abort()
        del txn
        return self.parent.default_highlight_colour if ret else None

class ChgmGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        self.allow_all = True
        table = ChgmTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.restrict_to_chg = None
        self.chgm = None
        self.GetParent().Bind(EVT_CHG_SELECT, self.CmdSelectChg)

    def InitialRecord(self):
        chg, chgm = self.CurrentChgAndChgm()
        dtdebug(f"INITIAL RECORD chdg={chg} chgm={chgm}")
        return chgm


    def MoveToChno(self, chno):
        txn = wx.GetApp().chdb.rtxn()
        channel = pychdb.chgm.find_by_chgm_order(txn, chno)
        txn.abort()
        del txn
        if channel is None:
            return
        row = self.table.screen.set_reference(channel)
        if row is not None:
            self.GoToCell(row, self.GetGridCursorCol())
            self.SelectRow(row)
            self.SetFocus()

    def OnShow(self, evt):
        self.chgm = None
        super().OnShow(evt)

    def CmdToggleRecord(self, evt):
        row = self.GetGridCursorRow()
        chgm = self.table.screen.record_at_row(row)
        start_time = datetime.datetime.now(tz=tz.tzlocal())
        from neumodvb.record_dialog import show_record_dialog
        show_record_dialog(self, chgm, start_time=start_time)

    def CmdAutoRec(self, evt):
        row = self.GetGridCursorRow()
        chgm = self.table.screen.record_at_row(row)
        start_time = datetime.datetime.now(tz=tz.tzlocal())
        from neumodvb.autorec_dialog import show_autorec_dialog
        ret, autorec = show_autorec_dialog(self, chgm)
        if ret == wx.ID_OK:
            wx.GetApp().receiver.update_autorec(autorec)
        elif ret ==wx.ID_DELETE: #delete
            wx.GetApp().receiver.delete_autorec(autorec)
        else:
            pass


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
        from neumodvb.channelno_dialog import ask_channel_number, IsNumericKey
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            if self.EditMode():
                self.MoveCursorRight(False)
            else:
                row = self.GetGridCursorRow()
                chgm = self.table.GetRow(row)
                self.app.ServiceTune(chgm)
            evt.Skip(False)
        else:
            from neumodvb.channelno_dialog import ask_channel_number, IsNumericKey
            if not self.EditMode() and not is_ctrl and IsNumericKey(keycode):
                self.MoveToChno(ask_channel_number(self, keycode- ord('0')))
            else:
                return

    def EditMode(self):
        return  self.GetParent().GetParent().edit_mode

    def CmdSelectChg(self, evt):
        chg = evt.chg
        dtdebug(f'chgmlist received SelectChg {chg}')
        wx.CallAfter(self.SelectChg, chg)

    def SelectChg(self, chg):
        self.restrict_to_chg = chg
        self.chgm = None
        self.app.live_service_screen.set_chg_filter(chg)
        self.restrict_to_chg = self.app.live_service_screen.filter_chg
        wx.CallAfter(self.handle_chg_change, None, self.chgm)

    def handle_chg_change(self, evt, chgm):
        self.table.GetRow.cache_clear()
        self.OnRefresh(evt, chgm)

    def CurrentChgAndChgm(self):
        if self.restrict_to_chg is None:
            chgm = self.app.live_service_screen.selected_chgm
            self.restrict_to_chg =  self.app.live_service_screen.filter_chg
            self.chgm = chgm
        else:
            if self.chgm is None:
                self.chgm = self.app.live_service_screen.selected_chgm
        return self.restrict_to_chg, self.chgm

    def CurrentGroupText(self):
        chg, chgm = self.CurrentChgAndChgm()
        if chg is None:
            return "All bouquets"
        return str(chg.name if len(chg.name)>0 else str(chg))


    def CmdTune(self, evt):
        dtdebug('CmdTune')
        rowno = self.GetGridCursorRow()
        chgm = self.table.GetRow(rowno)
        self.table.SaveModified()
        self.app.ServiceTune(chgm)

    def CmdTuneAdd(self, evt):
        dtdebug('CmdTuneAdd')
        rowno = self.GetGridCursorRow()
        service = self.table.GetRow(rowno)
        self.table.SaveModified()
        self.app.ServiceTune(service, replace_running=False)

    def CmdBouquetAddService(self, evt):
        rows = self.GetSelectedRows()
        chgms = [ self.table.screen.record_at_row(row) for row in rows]
        if self.app.frame.bouquet_being_edited is None:
            dtdebug(f'request to add chgm {chgms} to bouquet={self.app.frame.bouquet_being_edited} IGNORED')
            return
        else:
            dtdebug(f'request to add chgm {chgms} to {self.app.frame.bouquet_being_edited}')

        wtxn =  wx.GetApp().chdb.wtxn()
        assert self.app.frame.bouquet_being_edited is not None
        for chgm in chgms:
            pychdb.chg.toggle_channel_in_bouquet(wtxn, self.app.frame.bouquet_being_edited, chgm)
        wtxn.commit()
        self.table.OnModified()

    @property
    def CmdEditBouquetMode(self):
        if self.app.frame.bouquet_being_edited is None:
            return False #signal to neumomenu that item is disabled
        return self.app.frame.chggrid.CmdEditBouquetMode

class BasicChgmGrid(ChgmGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class ChgmGrid(ChgmGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
        self.GetParent().Bind(EVT_CHG_SELECT, self.CmdSelectChg)
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)

    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        super().OnWindowCreate(evt)
        chg, _ = self.CurrentChgAndChgm()
        self.SelectChg(chg)
        self.GrandParent.chgm_chg_sel.SetChg(self.restrict_to_chg, self.allow_all)
