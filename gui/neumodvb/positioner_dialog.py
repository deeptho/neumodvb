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
import re
import wx
import wx.lib.newevent
import math


from neumodvb import  minispinctrl, minifloatspin
from neumodvb.positioner_dialog_gui import  PositionerDialog_, SignalPanel_ , TuneMuxPanel_
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb import neumodbutils
from neumodvb.lnblist import has_network, get_network, must_move_dish
from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb.satlist_combo import EVT_SAT_SELECT
from neumodvb.lnblist_combo import EVT_LNB_SELECT, EVT_RF_PATH_SELECT
from neumodvb.muxlist_combo import EVT_MUX_SELECT

import pyreceiver
import pychdb
import pydevdb
import pystatdb
from pyreceiver import get_object as get_object_

AbortTuneEvent, EVT_ABORT_TUNE = wx.lib.newevent.NewEvent()

def on_positioner(lnb):
    return False if lnb is None else lnb.on_positioner

def can_move_dish(lnb_connection):
    return False if lnb_connection is None else pydevdb.lnb.can_move_dish(lnb_connection)

def same_mux_key(a, b):
    return a.sat_pos == b.sat_pos and  a.stream_id == b.stream_id \
        and a.t2mi_pid == b.t2mi_pid and a.mux_id == b.mux_id

def get_object(evt):
    s = evt.GetExtraLong()
    return get_object_(s)

def get_isi_list(stream_id, signal_info):
    lst = [ x & 0xff for x in signal_info.matype_list ]
    if stream_id not in lst:
        lst.append(stream_id)
    lst.sort()
    prefix = ''
    suffix = ''
    if len(lst) > 16:
        try:
            idx=lst.index(stream_id)
            start = max(idx-4, 0)
            end = min(idx + 4 + start - (idx - 4), len(lst))
            if start > 0:
                prefix = '...'
            if end < len(lst):
                suffix = '...'
            lst = lst [start:end]
        except ValueError:
            pass
    assert stream_id in lst
    return lst, prefix, suffix

class Diseqc12SpinCtrl(minispinctrl.MiniSpinCtrl):
    def __init__(self, parent, *args, size=(35,30), **kwds):
        super().__init__(parent, *args, size=size, **kwds, example="12")
        self.parent = parent
        self.Bind(minispinctrl.EVT_MINISPIN, self.parent.GetParent().OnDiseqc12PositionChanged)

class UsalsPosSpinCtrl(minifloatspin.MiniFloatSpin):
    def __init__(self, parent, *args, size=(35,30), **kwds):
        super().__init__(parent, *args, size=size, **kwds, example="0.20")
        self.parent = parent
        self.Bind(minifloatspin.EVT_MINISPIN, self.parent.GetParent().OnUsalsStepChanged)

class TuneMuxPanel(TuneMuxPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent
        self.Bind(wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.Bind(wx.EVT_WINDOW_DESTROY, self.OnWindowDestroy)
        self.positioner_sat_sel.window_for_computing_width = self
        self.positioner_lnb_sel.window_for_computing_width = self
        self.positioner_rf_path_sel.window_for_computing_width = self
        self.positioner_mux_sel.window_for_computing_width = self

    def clear_template_strings(self):
        """
        remove some data used by wxg to estimate window size
        """
        for key in self.other_status_keys:
            w = getattr(self, f'status_{key}')
            w.SetLabel('')
        self.si_freq_text.SetLabel('')
        self.si_symbolrate_text.SetLabel('')
        self.si_nit_ids_text.SetLabel('')
        self.si_sdt_services_text.SetLabel('')
    def OnWindowCreate(self, evt):
        if evt.GetWindow() != self:
            return
        self.positioner_mux_sel.SetSat(self.sat)
        self.positioner_mux_sel.SetMux(self.mux)
        wx.CallAfter(self.clear_template_strings)

    def init(self, parent, sat, lnb,  mux, window_for_computing_width=None):
        if window_for_computing_width is not None:
            self.positioner_sat_sel.window_for_computing_width = window_for_computing_width
        self.rf_path = None
        self.lnb = None
        self.mux = None
        self.rf_path, self.lnb, self.sat, self.mux = self.SelectInitialData(lnb, sat, mux)
        self.si_status_keys= ('pat', 'nit', 'sdt')
        self.other_status_keys= ('fail', 'si_done')
        self.ref_mux = None
        self.diseqc12 = 0
        self.last_tuned_mux = None # needed because user can change self.mux while tuning is in progress
        assert (mux is None and sat is None) or (sat is None and lnb is None) or (lnb is None and mux is None)
        self.GetParent().Bind(EVT_SAT_SELECT, self.CmdSelectSat)
        self.GetParent().Bind(EVT_LNB_SELECT, self.CmdSelectLnb)
        self.GetParent().Bind(EVT_RF_PATH_SELECT, self.CmdSelectRfPath)
        self.GetParent().Bind(EVT_MUX_SELECT, self.CmdSelectMux)
        opts =  wx.GetApp().receiver.get_options()
        self.positioner_mux_sel.SetSat(self.sat)
        self.last_selected_mux = None if self.mux is None else self.mux.copy()
        self.current_lnb_network_changed = False
        self.mux_subscriber_ = None
        self.tuned_ = False
        self.lnb_activated_ = False
        self.use_blindscan_ = opts.positioner_dialog_use_blind_tune
        self.blind_toggle.SetValue(self.use_blindscan_)
        self.retune_mode_ =  pydevdb.retune_mode_t.IF_NOT_LOCKED
        self.signal_info = pyreceiver.signal_info_t()
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)

    def CmdSelectMux(self, evt):
        """
        called when user selects mux from list
        """
        mux = evt.mux
        dtdebug(f"selected mux: {mux}")
        wx.CallAfter(self.ChangeMux, mux)

    def CmdSelectSat(self, evt):
        """
        called when user selects sat from list
        """
        sat = evt.sat
        dtdebug(f"selected sat: {sat}")
        wx.CallAfter(self.ChangeSat, sat)

    def CmdSelectLnb(self, evt):
        """
        called when user selects lnb from list
        """
        lnb = evt.lnb
        dtdebug(f"selected lnb: {lnb}")
        wx.CallAfter(self.SelectLnb, lnb)

    def CmdSelectRfPath(self, evt):
        """
        called when user selects an rf_path from a list
        """
        rf_path = evt.rf_path
        dtdebug(f"selected rf_path: {rf_path}")
        wx.CallAfter(self.ChangeRfPath, rf_path)
    def OnWindowDestroy(self, evt):
        dtdebug('TuneMuxPanel destroyed')

    def read_lnb_from_db(self):
        txn = wx.GetApp().devdb.rtxn()
        lnb = pydevdb.lnb.find_by_key(txn, self.lnb.k)
        txn.abort()
        del txn
        return lnb

    def save_use_blindscan(self, val):
        wx.GetApp().save_option_to_db("positioner_dialog_use_blind_tune", val)

    @property
    def use_blindscan(self):
        return self.use_blindscan_

    @use_blindscan.setter
    def use_blindscan(self, value):
        self.save_use_blindscan(value)
        self.use_blindscan_ = value

    @property
    def retune_mode(self):
        return self.retune_mode_

    @retune_mode.setter
    def retune_mode(self, value):
        pass
        self.retune_mode_ = value

    @property
    def lnb_connection (self):
        return None if self.rf_path is None else pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)

    @property
    def mux_subscriber(self):
        if self.mux_subscriber_ is None:
            receiver = wx.GetApp().receiver
            import pyreceiver
            self.mux_subscriber_ = pyreceiver.subscriber_t(receiver, self)
        return self.mux_subscriber_

    @property
    def lnb_subscriber(self):
        if not self.lnb_activated_:
            self.lnb_activated_ = True;
            self.SubscribeLnb(retune_mode=pydevdb.retune_mode_t.NEVER)
        return self.mux_subscriber

    @property
    def tuned_mux_subscriber(self):
        if not self.tuned_:
            self.OnTune()
        return self.mux_subscriber

    def get_usals_location(self):
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        return opts.usals_location

    def OnSubscriberCallback(self, evt):
        data = get_object(evt)
        if type(data) == str:
            self.parent.OnSubscriberCallback(data)
            return
        if self.mux_subscriber_ is None:
            return
        if type(data) == pyreceiver.sdt_data_t:
            self.OnSdtInfoUpdate(data)
            return
        elif type(data) == pyreceiver.signal_info_t:
            self.signal_info = data
            self.OnSignalInfoUpdate(data)
        elif type(data) == pyreceiver.positioner_motion_report_t:
            print(f'POSITIONER MOTION: {data.start_time} {data.end_time}')
            if data.start_time != data.end_time:
                from neumodvb.neumowidgets import show_progress_dialog
                cur_pos = pychdb.sat_pos_str(data.dish.cur_usals_pos)
                target_pos = pychdb.sat_pos_str(data.dish.target_usals_pos)
                show_progress_dialog(self,
                                     f"Positioner on dish {data.dish} moving",
                                     f"Positioner on dish {data.dish} is moving from {cur_pos} to {target_pos}",
                                     duration =data.end_time - data.start_time, dark_mode=False)


        self.parent.OnSubscriberCallback(data)
    def save_current_lnb_network(self):
        if self.current_lnb_network_changed:
            ok = ShowOkCancel("Save changed?",
                              f"Save data for network {pychdb.sat_pos_str(self.sat.sat_pos)} on "
                              f"{self.lnb} before closing?")
            if ok:
                self.OnSave(None)

    def Close(self):
        self.save_current_lnb_network()
        if self.tuned_:
            self.AbortTune()
            del self.mux_subscriber_
            self.mux_subscriber_ = None
        elif self.lnb_activated_:
            self.lnb_subscriber.unsubscribe()
            self.tuned_ = False
            self.lnb_activated_ = False
            del self.mux_subscriber_
            self.mux_subscriber_ = None

    def SelectInitialData(self, lnb, sat, mux):
        """
        select an inital choice for rf_path, lnb, mux and sat
        """
        #force mux and sat to be compatible
        rf_path = None
        if lnb is None and (mux is not None or sat is not None):
            #initialise from mux
            devdb_txn = wx.GetApp().devdb.rtxn()
            rf_path, lnb = pydevdb.lnb.select_lnb(devdb_txn, sat, mux)
            devdb_txn.abort()
            del devdb_txn
            if lnb is None:
                return None, None, None, None

        if lnb is not None:
            if rf_path is None:
                rf_path = pydevdb.lnb.select_rf_path(lnb)
            #if mux is None on input, the following call will pick a mux on the sat to which the rotor points
            chdb_txn = wx.GetApp().chdb.rtxn()
            mux, sat = pychdb.select_sat_and_reference_mux(chdb_txn, lnb, mux)
            if sat is None:
                sat_pos = None
                if mux is not None and mux.k.sat_pos != pychdb.sat.sat_pos_none:
                    sat_pos =  mux.k.sat_pos
                elif len(lnb.networks)>0:
                    sat_pos = lnb.networks[0].sat_pos
                if sat_pos != None:
                    sat_band = pydevdb.lnb.sat_band(lnb)
                    sat = pychdb.sat.find_by_key(chdb_txn, lnb.networks[0].sat_pos, sat_band)
                    sat = pychdb.sat.sat()
                    sat.sat_band = pydevdb.lnb.sat_band(lnb)
                    sat.sat_pos = sat_pos
                    chdb_wtxn = wx.GetApp().chdb.wtxn()
                    pychdb.put_record(chdb_wtxn, sat)
                    chdb_wtxn.commit()
            if mux is None:
                mux = pychdb.dvbs_mux.dvbs_mux()
            chdb_txn.abort()
            del chdb_txn
            return rf_path, lnb, sat, mux

    def OnSave(self, event=None):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("saving")
        for n in self.lnb.networks:
            if n.sat_pos == self.sat.sat_pos:
                changed = self.ref_mux is None or not same_mux_key(self.ref_mux.k, self.mux.k)
                self.ref_mux = self.mux if self.signal_info is None else self.signal_info.driver_mux
                self.ref_mux.k.sat_pos = self.sat.sat_pos
                n.ref_mux = self.ref_mux.k
                if self.mux is not None:
                    self.current_lnb_network_changed |= changed
                break

        if self.current_lnb_network_changed:
            txn = wx.GetApp().devdb.wtxn()
            #adjust lnb_connections based on possible changes in frontends
            pydevdb.lnb.update_lnb_network_from_positioner(txn, self.lnb, self.sat.sat_pos)
            #make sure that tuner_thread uses updated values (e.g., update_lof will save bad data)
            txn.commit()
            self.mux_subscriber.update_current_lnb(self.lnb)
        self.current_lnb_network_changed = False
        if event is not None:
            event.Skip()

    def OnResetLof(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Resetting LOF offset")
        assert self.lnb is not None
        ok = ShowOkCancel("Reset LOF offset?", f"Do you wish to reset the estimate local oscillator "
                          "ofset for this LNB?")
        if ok:
            txn = wx.GetApp().devdb.wtxn()
            pydevdb.lnb.reset_lof_offset(txn, self.lnb)
            txn.commit()
        if event:
            event.Skip()

    def OnUsalsTypeChanged(self):
        dtdebug("Saving usals changes")
        assert self.lnb is not None
        lnb_connection = pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)
        txn = wx.GetApp().devdb.wtxn()
        pydevdb.lnb.update_lnb_connection_from_positioner(txn, self.lnb, lnb_connection)
        txn.commit()
        #make sure that tuner_thread uses updated values (e.g., update_lof will save bad data)
        self.mux_subscriber.update_current_lnb(self.lnb)

    def OnToggleConstellation(self, evt):
        self.parent.OnToggleConstellation(evt)

    def SubscribeLnb(self, retune_mode, silent=False):
        """
        used when we want change a positioner without tuning
        """
        if self.tuned_:
            assert self.lnb_activated_
            return self.tuned_ # no need to subscribe
        self.retune_mode = retune_mode
        dtdebug(f'Subscribe LNB')
        ret = self.mux_subscriber.subscribe_lnb(self.rf_path, self.lnb,  self.retune_mode)
        if ret < 0:
            if not silent:
                ShowMessage("SubscribeLnb failed", self.mux_subscriber.error_message) #todo: record error message
            dtdebug(f"Tuning failed {self.mux_subscriber.error_message}")

        self.tuned_ = False
        self.lnb_activated_ = True
        return self.lnb_activated_

    def Tune(self, mux, retune_mode, pls_search_range=None, silent=False):
        self.retune_mode = retune_mode
        self.pls_search_range = pyreceiver.pls_search_range_t() if pls_search_range is None else pls_search_range
        dtdebug(f'Tuning - {"BLIND" if self.use_blindscan else "REGULAR"} scan: {mux} pls_search_range={pls_search_range}')

        if self.use_blindscan and not wx.GetApp().neumo_drivers_installed:
            ShowMessage("Unsupported", "Blindscan not supported (neumo drivers not installed)")
            return False

        ret = self.mux_subscriber.subscribe_lnb_and_mux(self.rf_path, self.lnb, mux, self.use_blindscan,
                                                        self.pls_search_range,
                                                        self.retune_mode)
        self.last_tuned_mux = mux.copy()
        if ret < 0:
            if not silent:
                ShowMessage("Tuning failed", self.mux_subscriber.error_message) #todo: record error message
            dtdebug(f"Tuning failed {self.mux_subscriber.error_message}")
            self.AbortTune()
        else:
            self.tuned_ = True
            self.lnb_activated_ = True
        return self.tuned_

    def OnTune(self, event=None, pls_search_range=None):  # wxGlade: PositionerDialog_.<event_handler>
        self.muxedit_grid.table.FinalizeUnsavedEdits()
        self.UpdateRefMux(self.mux)
        dtdebug(f"positioner: subscribing to lnb={self.lnb} mux={self.mux}")
        can_tune, error = pydevdb.lnb_can_tune_to_mux(self.lnb, self.mux)
        if not can_tune:
            ShowMessage(f"Cannot tune to {self.mux}: {error}")
            if event is not None:
                event.Skip()
            return
        mux =self.mux.copy()
        if self.use_blindscan:
            mux.k.t2mi_pid = -1
            #mux.k.mux_id = 0
        mux.c.tune_src = pychdb.tune_src_t.TEMPLATE
        mux.matype = -1
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
        #reread usals in case we are part of spectrum_dialog and positioner_dialog has changed them
        self.lnb = self.read_lnb_from_db()
        wx.CallAfter(self.Tune,  mux, retune_mode=pydevdb.retune_mode_t.IF_NOT_LOCKED,
                     pls_search_range=pls_search_range)
        if event is not None:
            event.Skip()

    def OnSearchPls(self, event=None):  # wxGlade: PositionerDialog_.<event_handler>
        pls_search_range = pyreceiver.pls_search_range_t()
        pls_search_range.start = 0
        pls_search_range.end = 262142
        dtdebug(f'RANGE={pls_search_range}')
        self.OnTune(event, pls_search_range=pls_search_range)

    def AbortTune(self):
        if self.mux_subscriber_ is not None:
            self.mux_subscriber.unsubscribe()
            del self.mux_subscriber_
            self.mux_subscriber_ = None
        self.tuned_ = False
        self.lnb_activated_ = False
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
    def OnAbortTune(self, event):
        dtdebug(f"positioner: unsubscribing")
        self.last_tuned_mux = None
        evt = AbortTuneEvent()
        wx.PostEvent(self, evt)

        wx.CallAfter(self.AbortTune)

    def OnResetTune(self, event):
        dtdebug("OnResetTune")
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
        self.mux = self.last_selected_mux.copy()
        self.muxedit_grid.Reset()
        event.Skip()

    def OnToggleBlindscan(self, event):
        self.use_blindscan = event.IsChecked()
        dtdebug(f"OnToggleBlindscan={self.use_blindscan_}")
        event.Skip()

    def OnClose(self, evt):
        return self.parent.OnClose(evt);

    def OnSdtInfoUpdate(self, sdt):
        nid_tid_text = f'nid={sdt.network_id} tid={sdt.ts_id}:'
        ret =[]
        for s in sdt.services:
            ret.append(f'{s.name}')
        self.si_sdt_services_text.SetLabel(nid_tid_text + ('; '.join(ret)))
        w=self.si_sdt_services_text.GetParent().GetClientSize()[0]-50
        self.si_sdt_services_text.Wrap(w)

    def OnSignalInfoUpdate(self, signal_info):
        self.parent.UpdateSignalInfo(signal_info, self.tuned_)
        mux = signal_info.driver_mux
        sat_pos_confirmed = signal_info.sat_pos_confirmed #and not signal_info.on_wrong_sat
        nit_valid = mux.c.tune_src in (pychdb.tune_src_t.NIT_TUNED, pychdb.tune_src_t.NIT_CORRECTED)
        pol = neumodbutils.enum_to_str(mux.pol)
        if self.signal_info.nit_received:
            if nit_valid:
                self.si_freq_text.SetForegroundColour(wx.Colour('black'))
                self.si_symbolrate_text.SetForegroundColour(wx.Colour('black'))
            else:
                self.si_freq_text.SetForegroundColour(wx.Colour('red'))
                self.si_symbolrate_text.SetForegroundColour(wx.Colour('red'))
            m = mux if signal_info.received_si_mux is None else signal_info.received_si_mux
            if m is not None:
                self.si_freq_text.SetLabel(f'{m.frequency/1e3:,.3f} MHz {pol}'.replace(',', ' '))
                self.si_symbolrate_text.SetLabel(f'{m.symbol_rate/1e3:,.0f} kS/s'.replace(',', ' '))
            else:
                self.si_freq_text.SetLabel(f'NO NIT')
                self.si_symbolrate_text.SetLabel('')
        else:
            self.si_freq_text.SetLabel('')
            self.si_symbolrate_text.SetLabel('')

        if not self.signal_info.sdt_received:
            self.si_sdt_services_text.SetLabel('')

        locked = signal_info.has_lock

        cn = '' if signal_info.network_id_confirmed  else "?"
        ct = '' if signal_info.ts_id_confirmed  else "?"
        stream = f' stream={mux.k.stream_id}' if mux.k.stream_id>=0 else ''
        if not locked:
            self.ClearSignalInfo()
            return
        else:
            pass
        received_nit = signal_info.received_si_mux
        bad_nit = signal_info.received_si_mux_is_bad
        sat_text = f'{pychdb.sat_pos_str(mux.k.sat_pos)}' if sat_pos_confirmed else \
            f'{pychdb.sat_pos_str(received_nit.k.sat_pos)}' if received_nit is not None else '??'

        nid_text = f'{mux.c.nit_network_id}' if nit_valid else f'{received_nit.c.nit_network_id}' \
            if received_nit is not None else '??'
        tid_text = f'{mux.c.nit_ts_id}' if nit_valid else f'{received_nit.c.nit_ts_id}'if received_nit is not None else '??'

        if self.signal_info.nit_received:
            self.si_nit_ids_text.SetForegroundColour(wx.Colour('black' if nit_valid else 'red'))
            self.si_nit_ids_text.SetLabel(f'{sat_text} nid={nid_text} tid={tid_text}')
        else:
            self.si_nit_ids_text.SetLabel(f'')
        si_done = self.signal_info.has_si_done
        for key in self.si_status_keys:
            received = getattr(self.signal_info, f'{key}_received')
            present = getattr(self.signal_info, f'has_{key}')
            absent = si_done and not present
            w = getattr(self, f'status_{key}')
            w.SetForegroundColour(wx.Colour('blue' if received  \
                                            else 'black' if present \
                                            else 'red' if absent else 'gray'))
        for key in self.other_status_keys:
            val = getattr(self.signal_info, f'has_{key}')
            w = getattr(self, f'status_{key}')
            if key == 'fail':
                w.SetLabel('' if val == 0 else 'fail')
            elif key == 'si_done':
                w.SetLabel('' if val == 0 else 'fin')

    def ClearSignalInfo(self):
        self.si_freq_text.SetLabel('')
        self.si_symbolrate_text.SetLabel('')
        self.si_nit_ids_text.SetLabel('')
        self.si_sdt_services_text.SetLabel('')
        self.si_sdt_services_text.SetLabel('')
        for key in self.si_status_keys:
            val = 0
            w = getattr(self, f'status_{key}')
            w.SetForegroundColour(wx.Colour('gray'))
        for key in self.other_status_keys:
            val = 0
            w = getattr(self, f'status_{key}')
            w.SetLabel('')

    def SelectLnb(self, lnb):
        self.save_current_lnb_network()
        add = False
        self.lnb = lnb
        if lnb is None:
            self.rf_path = None
            return
        self.rf_path = pydevdb.lnb.select_rf_path(lnb)
        self.positioner_rf_path_sel.Update()
        if has_network(lnb, self.sat.sat_pos) and not must_move_dish(lnb, self.sat.sat_pos):
            # no change needed
            network = get_network(self.lnb, self.sat.sat_pos)
            self.parent.SetDiseqc12Position(network.diseqc12)
        else:
            #we need to also select a different satellite, to one which is allowed by the lnb
            chdb_txn = wx.GetApp().chdb.rtxn()
            mux, sat = pychdb.select_sat_and_reference_mux(chdb_txn, self.lnb, None)
            #mux and sat will be None if positioner would need to be moved
            chdb_txn.abort()
            del chdb_txn
            self.ChangeSat(sat)
            self.positioner_mux_sel.SetMux(self.mux)
        self.parent.SetWindowTitle(self.lnb, self.lnb_connection, self.sat) #update window title
        self.positioner_lnb_sel.Update()

    def ChangeRfPath(self, rf_path):
        add = False
        if self.lnb is None:
            self.rf_path = None
            return
        self.rf_path = rf_path
        #lnb_connection = pydevdb.lnb.connection_for_rf_path(self.lnb, rf_path)
        self.parent.SetWindowTitle(self.lnb, self.lnb_connection, self.sat) #update window title
        self.positioner_rf_path_sel.Update()

    def ChangeMux(self, mux):
        dtdebug(f"selected mux: {mux}")
        self.last_selected_mux = None if mux is None else mux.copy()
        wx.CallAfter(self.UpdateRefMux, mux)

    def ChangeSat(self, sat):
        self.save_current_lnb_network()
        if sat is None or self.lnb is None:
            return
        if sat.sat_pos == self.sat.sat_pos:
            return
        add = False
        lnb_connection = pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)
        network = get_network(self.lnb, sat.sat_pos)
        if (not on_positioner(self.lnb) or can_move_dish(lnb_connection)) and network is not None:
            pass #we can switch to the network using diseqc or by moving the dish
        elif on_positioner(self.lnb):
            #allow only network or add network
            added = ShowOkCancel("Add network?", f"Network {sat} not yet defined for lnb {self.lnb}. Add it?")
            if not added:
                return
            network = pydevdb.lnb_network.lnb_network()
            network.sat_pos = sat.sat_pos
            network.usals_pos = pychdb.sat.sat_pos_none
            dtdebug(f"Saving new lnb network: lnb={self.lnb} network={network}")
            changed = pydevdb.lnb.add_or_edit_network(self.lnb, self.get_usals_location(), network)
            self.current_lnb_network_changed = True
        else:
            ShowMessage("Network unavailable",
                         f"Network {sat} not defined for lnb {self.lnb} on fixed dish. Add it in lnb list first")
            return
        assert network is not None
        txn = wx.GetApp().chdb.rtxn()
        self.sat = sat
        if on_positioner(self.lnb):
            #force network update
            #self.current_lnb_network_changed |= self.lnb.usals_pos != network.usals_pos
            self.lnb.usals_pos = network.usals_pos
        self.mux = pychdb.dvbs_mux.find_by_key(txn, network.ref_mux)
        if self.mux is None or self.mux.k.sat_pos != self.sat.sat_pos: #The latter can happen when sat_pos of ref_mux was updated
            self.mux = pychdb.select_reference_mux(txn, self.lnb, network.sat_pos)
        if self.mux.k.sat_pos == pychdb.sat.sat_pos_none:
            self.mux.k.sat_pos = self.sat.sat_pos
        txn.abort()
        del txn
        dtdebug(f"self.mux={self.mux} self.sat={self.sat}")
        assert abs(self.mux.k.sat_pos - self.sat.sat_pos) < 5
        if network is not None:
            self.parent.SetDiseqc12Position(network.diseqc12)
        self.positioner_sat_sel.Update()
        self.positioner_mux_sel.SetSat(self.sat)
        self.positioner_mux_sel.SetMux(self.mux)
        self.muxedit_grid.Reset()
        sat_pos = self.sat.sat_pos if network is None else network.usals_pos
        self.parent.SetUsalsPos(sat_pos)
        self.parent.SetWindowTitle(self.lnb, self.lnb_connection, self.sat) #update window title

    def UpdateRefMux(self, rec):
        #if rec is None:
        #    dterror("Called with None")
        #    rec = pychdb.dvs_mux.dvbs_mux()
        #    rec.k.sat_pos = self.sat.sat_pos
        #    rec.frequency = self.lnb.k.lnb_type == pydevdb.lnb.lnb_
        # main effect is to update ref_mux (will be saved later) and self.current_lnb_network_changed
        # Note that this is an internal update and does not affect the database
        if rec.k.sat_pos == pychdb.sat.sat_pos_none:
            rec.k.sat_pos = self.sat.sat_pos
        dtdebug(f"UpdateRefMux: rec.k.sat_pos={rec.k.sat_pos} self.sat.sat_pos={self.sat.sat_pos}")
        if rec.k.sat_pos != self.sat.sat_pos:
            dtdebug(f"changing sat_pos from={self.sat.sat_pos} to={rec.k.sat_pos}")
            rec.k.sat_pos = self.sat.sat_pos
        self.mux = rec
        self.muxedit_grid.Reset()
        if self.lnb is not None:
            for n in self.lnb.networks:
                if n.sat_pos == self.mux.k.sat_pos:
                    assert self.sat.sat_pos == self.mux.k.sat_pos
                    n.ref_mux = self.mux.k
                    dtdebug(f"saving ref_mux={self.mux}")
                    return
        dtdebug(f"network not found: lnb={self.lnb} sat_pos={self.mux.k.sat_pos}")

class SignalPanel(SignalPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.signal_info = pyreceiver.signal_info_t()
        self.SetDefaultLevels()
        self.status_keys = ('carrier', 'timing_lock', 'lock', 'fec', 'sync')
        from neumodvb.speak import Speaker
        self.speak_signal = False
        self.speaker = Speaker()
        self.ber_accu = 0

    def Speak(self):
        if not self.speak_signal:
            return
        locked = self.signal_info.has_timing_lock
        received_nit = self.signal_info.received_si_mux
        mux = self.signal_info.driver_mux
        received_nit = self.signal_info.nit_received
        sat_pos_confirmed = self.signal_info.sat_pos_confirmed #and not signal_info.on_wrong_sat
        sat_pos = mux.k.sat_pos if sat_pos_confirmed else \
            mux.k.sat_pos if received_nit is not None \
            else mux.k.sat_pos

        snr = self.signal_info.snr/1000 if locked else None
        if snr is not None:
            if snr <= -1000.:
                snr = None
        self.speaker.speak(sat_pos, snr, received_nit)

    def SetDefaultLevels(self):
        self.snr_ranges=[0, 10, 12,  20]
        self.rf_level_ranges=[-80, -60, -40,  -20]
        self.ber_ranges=[-6, -3, 2,  4]

        self.snr_gauge.SetRange(self.snr_ranges)
        self.snr_gauge.SetValue(self.snr_ranges[0])

        self.rf_level_gauge.SetRange(self.rf_level_ranges)
        self.rf_level_gauge.SetValue(self.rf_level_ranges[0])

        self.ber_gauge.SetRange(self.ber_ranges)
        self.ber_gauge.SetBandsColour(wx.Colour('green'), wx.Colour('yellow'), wx.Colour('red'))
        self.ber_gauge.SetValue(self.ber_ranges[0])

        rf_level =-100
        snr =0
        ber = 0
        self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
        self.snr_text.SetLabel('N.A.' if snr is None else f'{snr:6.2f}dB' )
        self.ber_text.SetLabel(f'{ber:8.2E}')

    def ClearSignalInfo(self):
        self.ber_accu = 0
        self.rf_level_gauge.SetValue(None)
        self.snr_gauge.SetValue(None)
        self.ber_gauge.SetValue(None)
        self.snr_text.SetLabel('')
        self.rf_level_text.SetLabel('')
        self.ber_text.SetLabel('')
        self.isi_list_text.SetLabel('')
        rf_level = self.signal_info.signal_strength/1000
        self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
        self.sat_pos_text.SetForegroundColour(wx.Colour('red'))
        self.sat_pos_text.SetLabel('')
        self.freq_sr_modulation_text.SetLabel("")
        self.matype_pls_text.SetLabel('')

        for key in self.status_keys:
            val = 0
            w = getattr(self, f'has_{key}')
            if key in ('fail', 'si_done'):
                w.SetLabel('')
            else:
                w.SetForegroundColour(wx.Colour('blue' if val else 'red'))
        self.lnb_lof_offset_text.SetLabel('')
        return True

    def OnSignalInfoUpdate(self, signal_info, is_tuned):
        self.Speak()
        locked = signal_info.has_lock
        driver_mux = signal_info.driver_mux
        pol = neumodbutils.enum_to_str(driver_mux.pol)
        frequency_text = f'{driver_mux.frequency/1e3:,.3f} MHz {pol}'.replace(',', ' ')
        symbolrate_text = f'{driver_mux.symbol_rate/1e3:.0f} kS/s'
        fec=lastdot(driver_mux.fec).replace(' ', '/') if locked else ''
        delsys=lastdot(driver_mux.delivery_system)
        modulation=lastdot(driver_mux.modulation)
        modulation_text = f'{delsys} - {modulation}  {fec}' if signal_info.has_timing_lock else ''
        fec = f'{fec}' if signal_info.has_fec else ''
        self.freq_sr_modulation_text.SetForegroundColour(wx.Colour('blue' if signal_info.has_timing_lock else 'red'))
        self.freq_sr_modulation_text.SetLabel(f'{frequency_text}  {symbolrate_text} {modulation_text}')
        self.signal_info = signal_info
        snr = self.signal_info.snr/1000
        if snr <= -1000:
            snr = None
        rf_level = self.signal_info.signal_strength/1000
        min_snr = self.signal_info.min_snr/1000
        snr_ranges = [0, max(min_snr, 0), max(min_snr+2, 0), self.snr_ranges[3]]
        if snr_ranges != self.snr_ranges:
            self.snr_ranges = snr_ranges
            self.snr_gauge.SetRange(snr_ranges)
        for key in self.status_keys:
            val = getattr(self.signal_info, f'has_{key}')
            w = getattr(self, f'has_{key}')
            if key == 'fail':
                w.SetLabel('' if val == 0 else 'fail')
            elif key == 'si_done':
                w.SetLabel('' if val == 0 else 'fin')
            else:
                w.SetForegroundColour(wx.Colour('blue' if val else 'red'))
        if not signal_info.has_timing_lock:
            dtdebug("SignalPanel: NO LONGER LOCKED")
            self.rf_level_gauge.SetValue(rf_level)
            self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
            self.snr_gauge.SetValue(self.snr_ranges[0] if snr is None else snr)
            self.snr_text.SetLabel('N.A.' if snr is None else f'{snr:6.2f}dB' )
            return False
        self.ber_accu = 0.9*self.ber_accu + 0.1*  self.signal_info.ber
        ber = self.ber_accu if self.signal_info.ber> self.ber_accu else self.signal_info.ber
        lber =math.log10(max(1e-9,ber))

        self.rf_level_gauge.SetValue(rf_level)
        self.snr_gauge.SetValue(self.snr_ranges[0] if snr is None else snr)
        self.ber_gauge.SetValue(lber)
        self.snr_text.SetLabel('N.A.' if snr is None else f'{snr:6.2f}dB' )
        self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
        self.ber_text.SetLabel(f'{ber:8.2E}')

        stream_id = driver_mux.k.stream_id
        isi = ''
        if signal_info.has_timing_lock:
            lst, prefix, suffix = get_isi_list(stream_id, signal_info)
            isi = ', '.join([f'<span foreground="blue">{str(i)}</span>' if i==stream_id else str(i) for i in lst])
            isi = f'[{len(signal_info.isi_list)}]: {prefix}{isi}{suffix}'
        self.isi_list_text.SetLabelMarkup(isi)
        matype = signal_info.matype.replace("ACM/VCM", f'<span foreground="blue">ACM/VCM</span>')
        if locked and signal_info.has_matype:
            pls_mode = lastdot(str(driver_mux.pls_mode))
            pls = f'PLS: {pls_mode} {driver_mux.pls_code}'
        else:
            pls =''
        self.matype_pls_text.SetLabelMarkup(f'{matype} {pls}')

        nit_received, sat_confirmed = self.signal_info.nit_received, signal_info.sat_pos_confirmed
        on_wrong_sat = self.signal_info.on_wrong_sat
        c = '' if sat_confirmed  else "?"
        self.sat_pos_text.SetForegroundColour(wx.Colour(
            'red' if on_wrong_sat else 'blue' if nit_received else 'black'))
        self.sat_pos_text.SetLabel(f'{pychdb.sat_pos_str(driver_mux.k.sat_pos)}{c}' if locked else '')
        self.lnb_lof_offset_text.SetLabel(f'{self.signal_info.lnb_lof_offset:,d} kHz'.replace(',', ' ') \
            if self.signal_info.lnb_lof_offset is not None else '???')

        self.freq_sr_sizer.Layout()
        return True

    def OnToggleSpeak(self, evt):
        self.GetParent().GetParent().OnToggleSpeak(evt)


class PositionerDialog(PositionerDialog_):
    def __init__(self, parent, sat, rf_path, lnb, mux, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.tune_mux_panel.init(self, sat, lnb, mux)
        self.parent = parent

        self.SetWindowTitle(self.lnb, self.lnb_connection, self.sat)
        self.diseqc_type_choice.SetValue(self.lnb_connection)
        self.enable_disable_diseqc_panels()
        network = None if self.sat is None else get_network(self.lnb, self.sat.sat_pos)
        self.SetUsalsPos( (pychdb.sat.sat_pos_none if self.sat is None else self.sat.sat_pos) \
                          if network is None else network.usals_pos)
        self.SetLnbOffsetPos(self.lnb.offset_angle)
        self.SetDiseqc12Position(0 if network is None else network.diseqc12)
        self.SetUsalsLocation()
        if network is not None:
            dtdebug(f'DISEQC12={network.diseqc12}')
            self.SetDiseqc12(network.diseqc12)
        self.SetStep(10)
        self.Bind(wx.EVT_CLOSE, self.OnClose) #ony if a nonmodal dialog is used
        from pyreceiver import set_gtk_window_name
        set_gtk_window_name(self, "positioner") #needed to couple to css stylesheet
        self.update_constellation = True
        self.tune_mux_panel.constellation_toggle.SetValue(self.update_constellation)
    @property
    def lnb(self):
        return self.tune_mux_panel.lnb

    @property
    def rf_path(self):
        return self.tune_mux_panel.rf_path

    @rf_path.setter
    def rf_path(self, val):
        assert False

    @property
    def lnb_connection (self):
        return None if self.rf_path is None else pydevdb.lnb.connection_for_rf_path(self.lnb, self.rf_path)

    @property
    def sat(self):
        return self.tune_mux_panel.sat

    @property
    def mux(self):
        return self.tune_mux_panel.mux

    @property
    def mux_subscriber(self):
        return self.tune_mux_panel.mux_subscriber

    @property
    def tuned_mux_subscriber(self):
        return self.tune_mux_panel.tuned_mux_subscriber

    @property
    def lnb_subscriber(self):
        return self.tune_mux_panel.lnb_subscriber

    def Prepare(self, lnbgrid):
        self.Layout()

    def Close(self):
        self.tune_mux_panel.Close()

    def OnClose(self, evt):
        dtdebug("CLOSE")
        self.Close()
        wx.CallAfter(self.Destroy)
        evt.Skip()

    def OnSubscriberCallback(self, data):
        if type(data) == str:
            ShowMessage("Error", data)


    def UpdateSignalInfo(self, signal_info, tuned):
        self.signal_panel.OnSignalInfoUpdate(signal_info, tuned);
        if signal_info.constellation_samples is not None and self.update_constellation:
            self.tune_mux_panel.constellation_plot.show_constellation(signal_info.constellation_samples)

    def ClearSignalInfo(self):
        self.signal_panel.ClearSignalInfo()
        self.tune_mux_panel.constellation_plot.clear_constellation()
        self.tune_mux_panel.constellation_plot.clear_data()

    def SetUsalsPos(self, usals_pos):
        self.position = usals_pos #the satellite pointed to pos (even for an offset lnb)
        self.rotor_position_text_ctrl.SetValue(pychdb.sat_pos_str(self.position))

    def SetLnbOffsetPos(self, offset_angle=None):
        self.lnb_offset_angle_text_ctrl.SetLabel(pychdb.sat_pos_str(offset_angle))

    def SetDiseqc12Position(self, idx):
        self.diseqc_position_spin_ctrl.SetValue(idx)

    def get_usals_location(self):
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        return opts.usals_location

    def save_usals_location(self, loc):
        wx.GetApp().save_option_to_db("usals_location", loc)

    def SetUsalsLocation(self, longitude=None, latitude=None):
        loc = self.get_usals_location()
        if longitude is not None:
            loc.usals_longitude = longitude
        if latitude is not None:
            loc.usals_latitude = latitude
        if longitude is not None or latitude is not None:
            self.save_usals_location(loc)
        longitude = f'{abs(loc.usals_longitude)/100.:3.1f}'
        self.longitude_text_ctrl.ChangeValue(longitude)

        latitude = f'{abs(loc.usals_latitude)/100.:3.1f}'
        self.latitude_text_ctrl.ChangeValue(latitude)
        self.latitude_north_south_choice.SetSelection(loc.usals_latitude<0)
        self.longitude_east_west_choice.SetSelection(loc.usals_longitude<0)

    def SetDiseqc12(self, diseqc12):
        self.tune_mux_panel.diseqc12 = diseqc12
        self.diseqc_position_spin_ctrl.SetValue(self.tune_mux_panel.diseqc12)

    def SetStep(self, pos):
        self.step = pos
        self.rotor_step_spin_ctrl.SetValue(self.step/100)

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            dtdebug("ESCAPE")
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def SetWindowTitle(self, lnb, lnb_connection, sat):
        self.SetTitle(f'Positioner Control - ???' if lnb is None \
                      else f'Positioner Control - {lnb.k} {lnb_connection} {sat}' )

    def positioner_command(self, *args):
        if self.lnb_connection.rotor_control in (pydevdb.rotor_control_t.MASTER_DISEQC12,
                                      pydevdb.rotor_control_t.MASTER_USALS):
            ret, new_usals_pos = self.lnb_subscriber.positioner_cmd(*args)
            if ret >= 0:
                if new_usals_pos is not None:
                    self.UpdateUsalsPosition_(new_usals_pos)
                    self.SetUsalsPos(new_usals_pos)
                return True
            else:
                ShowMessage("Cannot control rotor", f"Failed to send positioner command")

        else:
            ShowMessage("Cannot control rotor",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb_connection.rotor_control)} does not "
                        "allow moving the positioner")
        return False

    def OnToggleSpeak(self, evt):
        self.signal_panel.speak_signal = evt.IsChecked()
        dtdebug(f"OnToggleSpeak={self.signal_panel.speak_signal}")
        evt.Skip()

    def OnToggleConstellation(self, evt):
        self.update_constellation = evt.IsChecked()
        dtdebug(f"OnToggleConstellation={self.update_constellation}")
        evt.Skip()


    def UpdateUsalsPosition_(self, usals_pos):
        dtdebug(f"USALS position set to {usals_pos/100}")
        self.lnb.usals_pos = usals_pos
        found = False
        for network in self.lnb.networks:
            if network.sat_pos == self.sat.sat_pos:
                self.tune_mux_panel.current_lnb_network_changed = network.usals_pos != usals_pos
                network.usals_pos = usals_pos
                found = True
                break
        if not found:
            dtdebug(f"lnb network not found: lnb={self.lnb} sat_pos={self.sat.sat_pos}")

    def UpdateUsalsPosition(self, usals_pos):
        self.UpdateUsalsPosition_(usals_pos)
        dtdebug(f"Goto XX {usals_pos}" )
        ret=self.positioner_command(pydevdb.positioner_cmd_t.GOTO_XX, usals_pos)
        assert ret>=0

    def UpdateDiseqc12(self, diseqc12):
        dtdebug(f"DISEQC12 set to {diseqc12}")
        for n in self.lnb.networks:
            if n.sat_pos == self.sat.sat_pos:
                n.diseqc12 = diseqc12
                self.tune_mux_panel.current_lnb_network_changed = True
                self.tune_mux_panel.diseqc12 = diseqc12
                dtdebug(f"updated lnb diseqc12 position: lnb={self.lnb} diseqc12={diseqc12}")
                return
        dtdebug("lnb network not found: lnb={self.lnb} sat_pos={self.sat.sat_pos}")

    def OnDiseqcTypeChoice(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.lnb_connection.rotor_control = self.diseqc_type_choice.GetValue()
        dtdebug(f"diseqc type set to {self.lnb_connection.rotor_control}")
        self.tune_mux_panel.current_lnb_network_changed = True
        t = pydevdb.rotor_control_t
        self.enable_disable_diseqc_panels()
        self.tune_mux_panel.OnUsalsTypeChanged()
        event.Skip()

    def enable_disable_diseqc_panels(self):
        t = pydevdb.rotor_control_t
        if self.lnb_connection is None or \
           self.lnb_connection.rotor_control in (t.MASTER_USALS, t.NONE, t.MASTER_MANUAL):
            self.diseqc12_panel.Disable()
        else:
            self.diseqc12_panel.Enable()
        if self.lnb_connection is None or \
           self.lnb_connection.rotor_control in (t.MASTER_DISEQC12, t.NONE, t.MASTER_MANUAL):
            self.usals_panel.Disable()
        else:
            self.usals_panel.Enable()

    def OnUsalsPosChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        val = event if type(event) == str  else event.GetString()
        from neumodvb.util import parse_longitude
        pos = parse_longitude(val)

        self.SetUsalsPos(pos)
        self.UpdateUsalsPosition(pos)
        if type(event) != str:
            event.Skip()

    def OnGotoUsals(self, event):
        """
        Called when user presses "Set button" next to usals location
        """
        self.OnUsalsPosChanged(self.rotor_position_text_ctrl.GetValue())

    def OnUsalsStepEast(self, event):
        self.position += self.step
        self.SetUsalsPos(self.position)
        self.UpdateUsalsPosition(self.position)
        event.Skip()

    def OnUsalsStepWest(self, event):
        self.position -= self.step
        self.SetUsalsPos(self.position)
        self.UpdateUsalsPosition(self.position)
        event.Skip()

    def OnUsalsStepChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        step = event.GetValue()
        dtdebug(f"USALS step changed to {step}")
        self.step = int(100*step)
        self.SetStep(self.step)
        event.Skip()

    def OnStopPositioner(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Stop Positioner")
        self.positioner_command(pydevdb.positioner_cmd_t.HALT)
        event.Skip()

    def OnLatitudeChanged(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        val = self.latitude_text_ctrl.GetValue()  if type(evt) == wx.FocusEvent \
            else evt.GetString()
        from neumodvb.util import parse_latitude
        val = parse_latitude(val)
        dtdebug(f'site latitude changed to {val}')
        self.SetUsalsLocation(latitude = val)
        evt.Skip()

    def OnLatitudeNorthSouthSelect(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        val = abs(opts.usals_location.usals_latitude)
        if evt.GetSelection() == 1:
            val = -val
        opts.usals_location.usals_latitude = val
        receiver.set_options(opts);
        dtdebug(f'site latitude changed to {val}')
        evt.Skip()

    def OnLongitudeChanged(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        val = self.longitude_text_ctrl.GetValue()  if type(evt) == wx.FocusEvent \
            else evt.GetString()
        from neumodvb.util import parse_longitude
        val = parse_longitude(val)
        dtdebug(f'site longitude changed to {val}')
        self.SetUsalsLocation(longitude = val)
        evt.Skip()

    def OnLongitudeEastWestSelect(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        opts =  receiver.get_options()
        val = abs(opts.usals_location.usals_longitude)
        if evt.GetSelection() == 1:
            val = -val
        opts.usals_location.usals_longitude = val
        receiver.set_options(opts);
        dtdebug(f'site longitude changed to {val}')
        evt.Skip()

    def OnStorePosition(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug(f"Diseqc12 position stored: {self.tune_mux_panel.diseqc12}")
        self.positioner_command(pydevdb.positioner_cmd_t.STORE_NN, self.tune_mux_panel.diseqc12)
        evt.Skip()

    def OnDiseqc12PositionChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        val = event.GetValue()
        self.UpdateDiseqc12(val)
        dtdebug(f"OnDiseqc12 position changed to {val}")
        event.Skip()

    def OnGotoRef(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Goto ref")
        self.positioner_command(pydevdb.positioner_cmd_t.GOTO_REF)
        event.Skip()

    def OnGotoSat(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        #Called from usals button "goto sat"
        dtdebug("Goto sat")
        self.tune_mux_panel.muxedit_grid.table.FinalizeUnsavedEdits()
        self.tune_mux_panel.UpdateRefMux(self.mux)
        lnb = self.tune_mux_panel.read_lnb_from_db() #to reread the networks
        network = get_network(lnb, self.sat.sat_pos)
        pos = network.usals_pos
        if self.lnb_connection.rotor_control == pydevdb.rotor_control_t.MASTER_USALS:
            self.positioner_command(pydevdb.positioner_cmd_t.GOTO_XX, pos)
            self.UpdateUsalsPosition(pos)
        elif self.lnb_connection.rotor_control == pydevdb.rotor_control_t.MASTER_DISEQC12:
            self.positioner_command(pydevdb.positioner_cmd_t.GOTO_NN, network.diseqc12)
            self.SetDiseqc12(network.diseqc12)
        else:
            ShowMessage("Cannot goto sat",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb.rotor_control)} does not "
                        "allow moving the positioner")
            return

        self.SetUsalsPos(pos)
        event.Skip()

    def OnGotoPosition(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug(f"Goto NN: diseqc12={self.tune_mux_panel.diseqc12}")
        self.positioner_command(pydevdb.positioner_cmd_t.GOTO_NN, self.tune_mux_panel.diseqc12)
        event.Skip()

    def OnToggleGotoEast(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        if event.IsChecked():
            dtdebug("Drive east")
            self.continuous_motion = 1 # going east
            self.goto_west_toggle.SetValue(0)
            self.positioner_command(pydevdb.positioner_cmd_t.DRIVE_EAST, 0) #0=drive continuous
        else:
            dtdebug("End Drive east")
            self.continuous_motion = 0
            self.positioner_command(pydevdb.positioner_cmd_t.HALT)

        event.Skip()

    def OnToggleGotoWest(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        if event.IsChecked():
            dtdebug("Drive west")
            self.continuous_motion = -1 # going west
            self.goto_east_toggle.SetValue(0)
            self.positioner_command(pydevdb.positioner_cmd_t.DRIVE_WEST, 0) #0=drive continuous
        else:
            dtdebug("End Drive west")
            self.continuous_motion = 0
            self.positioner_command(pydevdb.positioner_cmd_t.HALT)

        event.Skip()

    def OnStepEast(self, event):
        self.positioner_command(pydevdb.positioner_cmd_t.DRIVE_EAST, -1)
        event.Skip()

    def OnStepWest(self, event):
        self.positioner_command(pydevdb.positioner_cmd_t.DRIVE_WEST, -1)
        event.Skip()

    def OnSetEastLimit(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Set east limit")
        self.positioner_command(pydevdb.positioner_cmd_t.LIMIT_EAST)
        event.Skip()

    def OnDisableLimits(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Disable limits")
        self.positioner_command(pydevdb.positioner_cmd_t.LIMITS_OFF)
        event.Skip()

    def OnSetWestLimit(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Set west limit")
        self.positioner_command(pydevdb.positioner_cmd_t.LIMIT_WEST)
        event.Skip()

def show_positioner_dialog(caller, sat=None, rf_path=None, lnb=None, mux=None):
    dlg = PositionerDialog(caller, sat=sat, rf_path=rf_path, lnb=lnb, mux=mux)
    dlg.Show()
    return None
