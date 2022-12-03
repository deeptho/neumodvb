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
from cachetools.func import ttl_cache

from neumodvb import neumodbutils
from neumodvb.util import setup, lastdot
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb.util import dtdebug, dterror

import pystatdb
import pychdb
import pydevdb


def get_snr(x):
    return x[len(x)-1].snr/1000 if len(x)>0 else 0
def get_rf(x):
    return x[len(x)-1].signal_strength/1000 if len(x)>0 else 0

def get_ber(x):
    return x[len(x)-1].ber if len(x)>0 else 0


class StatusTable(NeumoTable):
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    freq_fn = lambda x: f'{x[1]/1000.:9.3f}' if x[1]>=0 else '-1'
    snr_fn = lambda x: f'{get_snr(x[1]):6.2f}dB'
    rf_fn = lambda x: f'{get_rf(x[1]):6.2f}dB'
    ber_fn = lambda x: f'{get_ber(x[1]):8.2e}'
    mac_fn = lambda x: x[2].mac_fn(x[1])
    all_columns = \
        [CD(key='k.live',  label='live', basic=True, readonly=True),
         CD(key='k.lnb',  label='lnb', basic=True, example="D0A0 Ku 28.2E 32766  ",
            dfn = lambda x: x[2].lnb_label(x[0]),
            sort=('k.lnb.dish_id', 'k.lnb.adapter_mac_address', 'k.lnb.lnb_id')),
         CD(key='k.fe.adapter_mac_address',  label='adapter', basic=True, readonly=True, no_combo = True,
            dfn=lambda x: x[2].adapter_name(x[0]), example=" TBS6909X #12 "),
         CD(key='k.lnb.lnb_id',  label='ID', basic=True, readonly=True),
         CD(key='k.mux.sat_pos', label='Sat', dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='k.mux.network_id', label='nid'),
         CD(key='k.mux.ts_id', label='tsid'),
         CD(key='k.mux.t2mi_pid', label='t2mi\npid', readonly=False),
         CD(key='k.mux.extra_id', label='subid', readonly=False),
         CD(key='k.frequency',  label='freq', basic=True, readonly = True, dfn= freq_fn, example="10725.114"),
         CD(key='k.pol', label='Pol', basic=True, dfn=lambda x: lastdot(x).replace('POL',''), example='V'),

         CD(key='k.time', label='Start', basic=True, dfn=datetime_fn, example='2021-06-16 18:30:33'),
         CD(key='stats', label='Signal', basic=True, dfn=rf_fn, example='-12.3dB '),
         CD(key='stats', label='SNR', basic=True, dfn=snr_fn, example='12.3dB '),
         CD(key='stats', label='BER', basic=True, dfn=ber_fn, example='1.111e-12 '),
        ]


    def lnb_label(self, signal_stat):
        sat_pos=pychdb.sat_pos_str(signal_stat.k.mux.sat_pos)
        t= lastdot(signal_stat.k.lnb.lnb_type)
        if t == 'UNIV':
            t='Ku'
        lnb_key = signal_stat.k.lnb
        rf_input = lnb_key.rf_input
        return f'D{lnb_key.dish_id}#{"??" if rf_input< 0 else rf_input} {sat_pos:>5}{t} {lnb_key.lnb_id}'

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.lnb.dish_id'
        data_table= pystatdb.signal_stat

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        if basic:
            CD = NeumoTable.CD
            assert 0
            self.all_columns= self.basic_columns
        super().__init__(*args, parent=parent, basic=basic, db_t=pystatdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pystatdb.signal_stat.signal_stat, initial_sorted_column = initial_sorted_column,
                         **kwds)

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        ref=pystatdb.signal_stat.signal_stat()
        ref.k.live = True
        screen = pystatdb.signal_stat.screen(txn, sort_order=sort_field,
                                             key_prefix_type=pystatdb.signal_stat.signal_stat_prefix.live,
                                             key_prefix_data=ref,
                                             field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, signal_stat):
        pystatdb.put_record(txn, signal_stat)
        return lnb

    def __new_record__(self):
        ret=self.record_t()
        return ret
    @property
    @ttl_cache(maxsize=128, ttl=10) #refresh every 10 seconds
    def adapter_names(self):
        txn = wx.GetApp().devdb.rtxn()
        ret={}
        for a in  pydevdb.fe.list_all_by_adapter_no(txn):
            ret[a.k.adapter_mac_address] = a.adapter_name
        txn.abort()
        del txn
        return ret

    def adapter_name(self, status):
        return self.adapter_names.get(status.k.fe.adapter_mac_address, status.k.fe.adapter_mac_address)

class StatusGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = StatusTable(self, basic)
        self.lnb = None #lnb for which networks will be edited
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None


    def OnLeftClicked(self, evt):
        """
        Create and display a popup menu on right-click event
        """
        colno = evt.GetCol()
        rowno = evt.GetRow()
        if self.CheckShowNetworkDialog(evt, rowno, colno):
            evt.Skip(False)
        else:
            evt.Skip(True)


class BasicStatusGrid(StatusGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)


class StatusGrid(StatusGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
