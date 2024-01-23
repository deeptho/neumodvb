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
from cachetools.func import ttl_cache

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer, lnb_network_str
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
import pystatdb
import pychdb


class SpectrumTable(NeumoTable):
    #label: to show in header
    #dfn: display function
    CD = NeumoTable.CD
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    lof_offset_fn =  lambda x: '; '.join([ f'{int(x[0].lof_offsets[i])}kHz' for i in range(len(x[0].lof_offsets))]) if len(x[0].lof_offsets)>0 else ''
    all_columns = \
        [CD(key='k.rf_path.lnb',  label='lnb', basic=True, example="D0A0 Ku 28.2E 32766  ",
            dfn = lambda x: x[2].lnb_label(x[0]),
            sort=('k.rf_path.lnb.dish_id', 'k.rf_path.lnb.card_mac_address', 'k.rf_path.rf_input', 'k.rf_path.lnb.lnb_id')),
         CD(key='k.rf_path',  label='Card/RF', basic=True, readonly=True, no_combo = True,
            dfn=lambda x: x[2].rf_path_name(x[0]), example=" TBS6909X #12 "),
         CD(key='k.sat_pos',  label='sat\npos', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='adapter_no',  label='ad#', basic=False, readonly=True),
         CD(key='k.pol',  label='pol', basic=True, dfn=lambda x: lastdot(x[1]).replace('POL',''), example='V'),
         CD(key='k.start_time',  label='date', basic=True, dfn= datetime_fn),
         CD(key='usals_pos',  label='usals_pos', basic=True, dfn= lambda x: pychdb.sat_pos_str(x[1])),
         CD(key='lof_offsets',  label='lof_offset', dfn=lof_offset_fn, example='-2000kHz; -20000kHz'),
         CD(key='start_freq',  label='start', basic=False, dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114"),
         CD(key='end_freq',  label='end', basic=False, dfn= lambda x: f'{x[1]/1000.:9.3f}', example="10725.114"),
         #CD(key='is_highres',  label='highres', basic=False),
         #CD(key='resolution',  label='step', basic=False),
         CD(key='filename',  label='file', basic=False, example="28.2E/0/2022-07-20_00:19:48_H_dish0_adapter2")
        ]
    def lnb_label(self, spectrum):
        lnb_key = spectrum.k.rf_path.lnb
        sat_pos =pychdb.sat_pos_str(spectrum.k.sat_pos)
        t= lastdot(lnb_key.lnb_type)
        if t == 'UNIV':
            t='Ku'
        return f'D{lnb_key.dish_id} {sat_pos:>5}{t} {lnb_key.lnb_id}'

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'k.start_time'
        data_table= pystatdb.spectrum

        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pystatdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pystatdb.spectrum.spectrum, initial_sorted_column = initial_sorted_column,
                         sort_order=2, #most recent on top
                         **kwds)
        self.app = wx.GetApp()

    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pystatdb.spectrum.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)


    def __save_record__(self, txn, record):
        pystatdb.put_record(txn, record)
        return record
    def __new_record__(self):
        return self.record_t()

    def get_iconsOFF(self):
        return (self.app.bitmaps.encrypted_bitmap, self.app.bitmaps.expired_bitmap)

    def get_icon_stateOFF(self, rowno, colno):
        return (True, True)

    def get_icon_sort_keyOFF(self):
        return 'encrypted'

    def mac_fn(self, mac):
        txn = self.chdb.rtxn()
        ret = pychdb.fe.find_by_key(txn, mac)
        txn.abort()
        del txn
        return str(ret)
        return mac.to_bytes(6, byteorder='little').hex(":") if x[1]>=0 else '???'

    @property
    @ttl_cache(maxsize=128, ttl=10) #refresh every 10 seconds
    def rf_path_dict(self):
        cards = wx.GetApp().get_cards_with_rf_in()
        ret = dict((v,k) for k, v in cards.items())
        return ret

    def rf_path_name(self, status):
        return self.rf_path_dict.get((status.k.rf_path.card_mac_address,status.k.rf_path.rf_input))

class SpectrumGridBase(NeumoGridBase):
    def __init__(self, basic, readonly, *args, **kwds):
        table = SpectrumTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None

    def InitialRecord(self):
        return self.app.currently_selected_spectrum



class BasicSpectrumGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)
        self.Bind (wx.grid.EVT_GRID_RANGE_SELECT, self.OnGridRowSelect)

    def OnGridRowSelect(self, evt):
        pass

class SpectrumSelectionGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)
        self.Bind (wx.grid.EVT_GRID_RANGE_SELECT, self.OnGridRowSelect)

    def OnGridRowSelect(self, evt):
        pass

    def CmdTune(self):
        dtdebug("CmdTune")

class SpectrumGrid(SpectrumGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
