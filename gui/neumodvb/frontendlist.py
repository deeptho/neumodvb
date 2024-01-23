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
from functools import cached_property

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer
from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str

import pychdb
import pydevdb

def delsys_fn(x):
    vals= set([enum_to_str(xx).split('_')[0] for xx in x[1]])
    vals = vals.difference(set(['AUTO']))
    return "/".join(vals)
def rf_inputs_fn(x):
    return " ".join(str(v) for v in x[1])

def subscription_fn(x):
    fesub = x[0].sub
    subs = fesub.subs
    mux_key = fesub.mux_key
    if len(subs) == 0:
        return ""
    ret=[]

    #lnb and rf input info common to all subscriptions
    sid = f"" if (mux_key.stream_id < 0) else f'-{mux_key.stream_id}'
    if mux_key.sat_pos == pychdb.sat.sat_pos_none:
        if fesub.sat_pos == pychdb.sat.sat_pos_none:
            ret.append(f"Exclusive")
        elif fesub.sat_pos not in (pychdb.sat.sat_pos_dvbc, pychdb.sat.sat_pos_dvbt):
            t= lastdot(fesub.rf_path.lnb.lnb_type)
            sat_pos=pychdb.sat_pos_str(fesub.sat_pos)
            t= lastdot(fesub.rf_path.lnb.lnb_type)
            e = neumodbutils.enum_to_str
            f = f'{sat_pos} {e(fesub.band)}-{e(fesub.pol)}'
            m = f'{f} #{fesub.rf_path.rf_input} {t}:{fesub.rf_path.lnb.lnb_id}'
            ret.append(m)
        else:
            f = f'{fesub.frequency/1000.:9.3f}Mhz{sid}'
            m = f'{f} #{fesub.rf_path.rf_input}'
            ret.append(m)
    elif mux_key.sat_pos not in (pychdb.sat.sat_pos_dvbc, pychdb.sat.sat_pos_dvbt):
        t= lastdot(fesub.rf_path.lnb.lnb_type)
        sat_pos=pychdb.sat_pos_str(mux_key.sat_pos)
        t= lastdot(fesub.rf_path.lnb.lnb_type)
        e = neumodbutils.enum_to_str
        f = f'{sat_pos} {fesub.frequency/1000.:9.3f}{e(fesub.pol)}{sid}'
        m = f'{f} #{fesub.rf_path.rf_input} {t}:{fesub.rf_path.lnb.lnb_id}'
        ret.append(m)
    else:
        f = f'{fesub.frequency/1000.:9.3f}Mhz{sid}'
        m = f'{f} #{fesub.rf_path.rf_input}'
        ret.append(m)

    for sub in subs:
        if sub.has_service:
            assert type(sub.v) == pychdb.service.service
            #srv = ' '.join(str(sub.service).split(' ')[1:])
            srv=f'{sub.v.k.service_id} [{sub.v.ch_order}] {sub.v.name}'
            ret.append(f'{sub.subscription_id}: {srv}')
        elif sub.has_mux:
            assert type(sub.v) == pychdb.service.service #not a mistake
            ret.append(f'{sub.subscription_id}: {str(sub.v.k.mux)}')
        else:
            assert type(sub.v) == pychdb.band_scan.band_scan
            usals_pos = fesub.usals_pos
            if usals_pos not in (pychdb.sat.sat_pos_dvbc, pychdb.sat.sat_pos_dvbt):
                sat_pos=pychdb.sat_pos_str(usals_pos)
                t= lastdot(fesub.rf_path.lnb.lnb_type)
                e = neumodbutils.enum_to_str
                f = f'{e(fesub.band)}-{e(fesub.pol)}'
                ret.append(f'{sub.subscription_id}: {f}')
            else:
                f = f'Exclusive'
                ret.append(f'{sub.subscription_id}: {f}')
    return '\n'.join(ret)

class FrontendTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    #frontend_fn = lambda x: f'{x[0].adapter_no}.{x[0].frontend_no}'
    frontend_fn = lambda x: f'{x[0].adapter_no}.{x[0].frontend_no}'
    mac_fn = lambda x: x[1].to_bytes(6, byteorder='little').hex(":") if x[1]>=0 else '???'
    enable_cfn = lambda table: table.enable_cfn()
    enable_sfn = lambda x: x[2].enable_sfn(x[0], x[1])
    enable_dfn = lambda x: x[2].enable_dfn(x[0])
    card_no_sfn = lambda x: x[2].card_no_sfn(x[0], x[1])
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='adapter_name',  label='adapter', basic=True, no_combo=True, readonly=True,
            example="TurboSight TBS 6916X #12 "),
         #CD(key='adapter_no',  label='Adap', basic=True, readonly=True),
         CD(key='k.frontend_no',  label='fe', basic=True, readonly=True),
         CD(key='card_no',  label='card#', basic=True, readonly=False, sfn=card_no_sfn),
         CD(key='card_short_name',  label='Card', basic=True, example=" TBS 6916X "),
         CD(key='fe_enable_menu',  label='enable', basic=False, cfn=enable_cfn, dfn=enable_dfn, sfn=enable_sfn,
            example=" DVB T+C "),
         CD(key='priority',  label='priority', basic=True),
         CD(key='sub.rf_path.card_mac_address',  label='subscription', basic=True, dfn=subscription_fn,
            readonly=True, example='#0 10714.250H-255 BBC One London '),
         #CD(key='sub.subs',  label='fe use\ncount', basic=True, readonly=True, cfn=None, dfn= lambda x : len(x[1]) ),
         CD(key='rf_inputs',  label='rf\ninputs', basic=True, dfn=rf_inputs_fn, readonly=True, example='1 '*6),
         #CD(key='rf_in',  label='RF#', basic=True, readonly=True),
         CD(key='card_mac_address',  label='CARD MAC', basic=True, no_combo=True, readonly=True,
            dfn=mac_fn, example=" AA:BB:CC:DD:EE:FF "),
         #CD(key='k.adapter_mac_address',  label='ADAP MAC', basic=True, no_combo=True, readonly=True, dfn=mac_fn, example=" AA:BB:CC:DD:EE:FF "),
         CD(key='card_address',  label='Bus', basic=True, example=" 0000:03:00.0 "),
         CD(key='present',  label='present', basic=True, dfn=bool_fn, readonly=True),
         CD(key='can_be_used',  label='available', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.multistream',  label='MIS', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.blindscan',  label='blind\n-scan', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.spectrum_sweep',  label='spec\nsweep', basic=True, dfn=bool_fn, readonly=True),
         CD(key='supports.spectrum_fft',  label='spec\nfft', basic=True, dfn=bool_fn, readonly=True),
         CD(key='delsys',  label='delsys', basic=True, dfn=delsys_fn, readonly=True, example='DVBT/'*6)
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'card_no'
        data_table= pydevdb.fe
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.fe.fe, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True
    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.fe.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        dtdebug(f'saving {record.k.adapter_mac_address}')
        pydevdb.put_record(txn, record)
        return record

    def __new_record__(self):
        ret=self.record_t()
        return ret

    def enable_cfn(self):
        """
        Present a menu of which delsys can be enabled
        """
        rec = self.CurrentlySelectedRecord()
        d = set([pychdb.delsys_to_type(d) for d in rec.delsys])
        d.discard(pychdb.delsys_type_t.NONE)
        choices = [ '', 'S'  ] if pychdb.delsys_type_t.DVB_S in d else [ '' ]
        choices = choices + [ c + '+T'  for c in choices ] if pychdb.delsys_type_t.DVB_T in d else choices
        choices = choices + [ c + '+C'  for c in choices ] if pychdb.delsys_type_t.DVB_C in d else choices
        choices = ['None' if c=='' else  f'DVB {c.strip("+")}' for c in choices]
        return choices

    def enable_dfn(self, rec):
        ret = []
        d = set([pychdb.delsys_to_type(d) for d in rec.delsys])
        #d.remove(pychdb.delsys_type_t.NONE)
        if rec.enable_dvbs and pychdb.delsys_type_t.DVB_S in d: ret.append('S')
        if rec.enable_dvbt and pychdb.delsys_type_t.DVB_T in d: ret.append('T')
        if rec.enable_dvbc and pychdb.delsys_type_t.DVB_C in d: ret.append('C')
        ret = 'None' if len(ret) ==0  else f'DVB {"+".join(ret)}'
        return ret

    def enable_sfn(self, rec, v):
        v = v.strip('DVB ')
        rec.enable_dvbs = 'S' in v
        rec.enable_dvbt = 'T' in v
        rec.enable_dvbc = 'C' in v
        dtdebug(f"Set called: fe={rec} v={v}")
        return rec

    def card_no_sfn(self, rec, v):
        v = int(v)
        wx.GetApp().receiver.renumber_card(rec.card_no, v)
        rec.card_no = v;
        return rec


    def highlight_colour(self, fe):
        """
        show lnbs for missing adapters in colour
        """
        return self.parent.default_highlight_colour if not fe.can_be_used else None


class FrontendGridBase(NeumoGridBase):
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

    def __init__(self, basic, readonly, *args, **kwds):
        table = FrontendTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)

    def CmdTune(self, evt):
        row = self.GetGridCursorRow()
        mux = self.table.data[row].ref_mux
        mux_name= f"{int(mux.frequency/1000)}{lastdot(mux.pol).replace('POL','')}"
        dtdebug(f'CmdTune requested for row={row}: PLAY mux={mux_name}')
        self.table.SaveModified()
        self.app.MuxTune(mux)

class BasicFrontendGrid(FrontendGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class FrontendGrid(FrontendGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
