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
import re
import wx
import wx.lib.newevent
import math


from neumodvb import  minispinctrl, minifloatspin
from neumodvb.positioner_dialog_gui import  PositionerDialog_, SignalPanel_ , TuneMuxPanel_
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
from neumodvb import neumodbutils
from neumodvb.lnblist import has_network, get_network
from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror

import pyreceiver
import pychdb
import pystatdb
from pyreceiver import get_object as get_object_

LnbChangeEvent, EVT_LNB_CHANGE = wx.lib.newevent.NewEvent()


def same_mux_key(a, b):
    return a.sat_pos == b.sat_pos and  a.network_id == b.network_id \
        and a.ts_id == b.ts_id and a.extra_id == b.extra_id

def get_object(evt):
    s = evt.GetExtraLong()
    return get_object_(s)

class Diseqc12SpinCtrl(minispinctrl.MiniSpinCtrl):
    def __init__(self, parent, *args, size=(35,30), **kwds):
        super().__init__(parent, *args, size=size, **kwds, example="12")
        self.parent = parent
        self.Bind(minispinctrl.EVT_MINISPIN, self.parent.GetParent().OnDiseqc12PositionChanged)

class UsalsPosSpinCtrl(minifloatspin.MiniFloatSpin):
    def __init__(self, parent, *args, size=(35,30), **kwds):
        super().__init__(parent, *args, size=size, **kwds, example="0.20")
        self.parent = parent
        self.Bind(minifloatspin.EVT_MINISPIN, self.parent.GetParent().OnStepChanged)

class LnbController(object):
    def __init__(self, parent):
        self.parent = parent

    def GetSize(self):
        width, height = self.parent.GetSize()
        return (width//2, height//2)

    def SelectLnb(self, rec):
        dtdebug(f"selected lnb: {rec}")
        self.parent.lnb = rec
        wx.CallAfter(self.parent.ChangeLnb, rec)

    def SetFocus(self):
        return self.parent.SetFocus()

    def CurrentGroupText(self):
        if self.parent.lnb is None:
            return ""
        return str(self.parent.lnb)

class SatController(object):
    def __init__(self, parent):
        self.parent = parent

    def GetSize(self):
        width, height = self.parent.GetSize()
        return (width//2, height//2)

    def SelectSat(self, rec):
        dtdebug(f"selected sat: {rec}")
        wx.CallAfter(self.parent.ChangeSat, rec)

    def SetFocus(self):
        return self.parent.SetFocus()

    def CurrentGroupText(self):
        if self.parent.sat is None:
            return ""
        return str(self.parent.sat.name if len(self.parent.sat.name)>0 else str(self.parent.sat))

class MuxController(object):
    def __init__(self, parent, initial_mux):
        self.parent = parent
        self.last_selected_mux = None if initial_mux is None else initial_mux.copy() #unmodified copy

    def GetSize(self):
        width, height = self.parent.GetSize()
        return (width//2, height//2)

    def SelectMux(self, rec):
        dtdebug(f"selected mux: {rec}")
        self.last_selected_mux = rec.copy()
        wx.CallAfter(self.parent.UpdateRefMux, rec)
        pass

    def SetFocus(self):
        return self.parent.SetFocus()

    def CurrentGroupText(self):
        if self.parent.mux is None or self.parent.mux.k.sat_pos == pychdb.sat.sat_pos_none:
            return "None"
        return str(self.parent.mux)

def on_rotor(lnb):
    return neumodbutils.enum_to_str(lnb.rotor_control).startswith('ROTOR')


class TuneMuxPanel(TuneMuxPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent

    def init(self, parent, sat, lnb,  mux):
        self.lnb, self.sat, self.mux = self.SelectInitialData(lnb, sat, mux)
        self.status_keys= ('fail', 'pat', 'nit', 'sdt', 'si_done')

        self.ref_mux = None
        self.diseqc12 = 0
        self.last_tuned_mux = None # needed because user can change self.mux while tuning is in progress
        assert (mux is None and sat is None) or (sat is None and lnb is None) or (lnb is None and mux is None)
        self.sat_controller = SatController(self)
        self.lnb_controller = LnbController(self)
        self.mux_controller = MuxController(self, self.mux)
        self.positioner_lnb_sel.controller = self.lnb_controller
        self.positioner_sat_sel.controller = self.sat_controller
        self.positioner_mux_sel.controller = self.mux_controller
        self.positioner_mux_sel.SelectSat(self.sat)
        self.muxedit_grid.controller = self.mux_controller
        self.mux_controller.last_selected_mux = self.mux.copy()
        self.lnb_changed = False
        self.mux_subscriber_ = None
        self.tuned_ = False
        self.use_blindscan_ = False
        self.retune_mode_ =  pyreceiver.retune_mode_t.IF_NOT_LOCKED
        self.tune_attempt = False
        self.signal_info = pyreceiver.signal_info_t()
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)

    @property
    def use_blindscan(self):
        return self.use_blindscan_

    @use_blindscan.setter
    def use_blindscan(self, value):
        self.blind_toggle.SetValue(value)
        self.use_blindscan_ = value

    @property
    def retune_mode(self):
        return self.retune_mode_

    @retune_mode.setter
    def retune_mode(self, value):
        pass
        self.retune_mode_ = value

    @property
    def mux_subscriber(self):
        if self.mux_subscriber_ is None:
            receiver = wx.GetApp().receiver
            import pyreceiver
            self.mux_subscriber_ = pyreceiver.mux_subscriber_t(receiver, self)
        return self.mux_subscriber_

    @property
    def lnb_subscriber(self):
        if not self.tuned_:
            self.SubscribeLnb(retune_mode=pyreceiver.retune_mode_t.NEVER)
        return self.mux_subscriber

    @property
    def tuned_mux_subscriber(self):
        if not self.tuned_:
            self.OnTune()
        return self.mux_subscriber

    def OnSubscriberCallback(self, evt):
        if self.mux_subscriber_ is None:
            return
        data = get_object(evt)

        if type(data) == pyreceiver.signal_info_t:
            self.signal_info = data
            if self.signal_info.tune_attempt != self.tune_attempt:
                pass
                return
            else:
                pass
            if not self.tuned_ :
                self.ClearSignalInfo()
                return
            self.OnSignalInfoUpdate(data)
        self.parent.OnSubscriberCallback(data)

    def Close(self):
        if self.lnb_changed:
            ok = ShowOkCancel("Save changed?", f"Save data for {self.lnb} before closing?")
            if ok:
                self.OnSave(None)
        if self.tuned_:
            self.mux_subscriber.unsubscribe()
            del self.mux_subscriber_
            self.tuned_ = False
            dtdebug("SUBS deleted\n")
            self.mux_subscriber_ = None

    def SelectInitialData(self, lnb, sat, mux):
        """
        select an inital choice for lnb mux and sat
        """
        #force mux and sat to be compatible
        txn = wx.GetApp().chdb.rtxn()
        if lnb is None and (mux is not None or sat is not None):
            #initialise from mux
            lnb = pychdb.lnb.select_lnb(txn, sat, mux)
            if lnb is None:
                return None, None, None
        if lnb is not None:
            #if mux is None on input, the following call will pick a mux on the sat to which the rotor points
            mux = pychdb.lnb.select_reference_mux(txn, lnb, mux)
            if mux.k.sat_pos != pychdb.sat.sat_pos_none:
                sat = pychdb.sat.find_by_key(txn, mux.k.sat_pos)
            elif  len(lnb.networks)>0:
                sat = pychdb.sat.find_by_key(txn, lnb.networks[0].sat_pos)
            return lnb, sat, mux
        #self.app.MuxTune(mux)

    def OnSave(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("saving")
        for n in self.lnb.networks:
            if n.sat_pos == self.mux.k.sat_pos:
                self.ref_mux = self.mux if self.signal_info is None else self.signal_info.dvbs_mux
                n.ref_mux = self.ref_mux.k
                self.lnb_changed |= not same_mux_key(self.ref_mux.k, self.mux.k)
                break

        if self.lnb_changed:
            txn = wx.GetApp().chdb.wtxn()
            #make sure that tuner_thread uses updated values (e.g., update_lof will save bad data)
            self.mux_subscriber.update_current_lnb(self.lnb)
            pychdb.put_record(txn, self.lnb)
            txn.commit()
        self.lnb_changed = False
        if event:
            event.Skip()

    def OnToggleConstellation(self, evt):
        self.parent.OnToggleConstellation(evt)

    def SubscribeLnb(self, retune_mode, silent=False):
        """
        used when we want change a positioner without tuning
        """
        if self.tuned_:
            return self.tuned_ # no need to subscribe
        self.retune_mode = retune_mode
        dtdebug(f'Subscribe LNB')
        ret = self.mux_subscriber.subscribe_lnb(self.lnb,  self.retune_mode)
        if ret < 0:
            if not silent:
                ShowMessage("SubsribeLnb failed", self.mux_subscriber.error_message) #todo: record error message
            dtdebug(f"Tuning failed {self.mux_subscriber.error_message}")

        self.tuned_ = False
        return self.tuned_

    def Tune(self, mux, retune_mode, pls_search_range=None, silent=False):
        self.retune_mode = retune_mode
        self.pls_search_range = pyreceiver.pls_search_range_t() if pls_search_range is None else pls_search_range
        dtdebug(f'Tuning - {"BLIND" if self.use_blindscan else "REGULAR"} scan: {mux} pls_search_range={pls_search_range}')
        ret = self.mux_subscriber.subscribe_lnb_and_mux(self.lnb, mux, self.use_blindscan,
                                                        self.pls_search_range,
                                                        self.retune_mode)
        self.last_tuned_mux = mux.copy()
        if ret < 0:
            if not silent:
                ShowMessage("Tuning failed", self.mux_subscriber.error_message) #todo: record error message
            dtdebug(f"Tuning failed {self.mux_subscriber.error_message}")
            self.tuned_ = False
        else:
            self.tuned_ = True
            self.tune_attempt = ret
        return self.tuned_

    def OnTune(self, event=None, pls_search_range=None):  # wxGlade: PositionerDialog_.<event_handler>
        self.muxedit_grid.table.FinalizeUnsavedEdits()
        self.UpdateRefMux(self.mux)
        dtdebug(f"positioner: subscribing to lnb={self.lnb} mux={self.mux}")
        can_tune, error = pychdb.lnb_can_tune_to_mux(self.lnb, self.mux)
        if not can_tune:
            ShowMessage(f"Cannot tune to {self.mux}: {error}")
            if event is not None:
                event.Skip()
            return
        mux =self.mux.copy()
        mux.c.is_template = True
        mux.c.freq_from_si = False
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
        wx.CallAfter(self.Tune,  mux, retune_mode=pyreceiver.retune_mode_t.IF_NOT_LOCKED,
                     pls_search_range=pls_search_range)
        if event is not None:
            event.Skip()

    def OnSearchPls(self, event=None):  # wxGlade: PositionerDialog_.<event_handler>
        pls_search_range = pyreceiver.pls_search_range_t()
        pls_search_range.start = 0
        pls_search_range.end = 262142
        print(f'RANGE={pls_search_range}')
        self.OnTune(event, pls_search_range=pls_search_range)

    def AbortTune(self):
        self.mux_subscriber.unsubscribe()
        self.mux_subscriber_ = None
        self.tuned_ = False
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
    def OnAbortTune(self, event):
        dtdebug(f"positioner: unsubscribing")
        self.last_tuned_mux = None
        wx.CallAfter(self.AbortTune)

    def OnResetTune(self, event):
        dtdebug("OnResetTune")
        self.ClearSignalInfo()
        self.parent.ClearSignalInfo()
        self.parent.ClearSignalInfo()
        self.mux = self.mux_controller.last_selected_mux.copy()
        self.muxedit_grid.Reset()
        event.Skip()

    def OnToggleBlindscan(self, event):
        self.use_blindscan_ = event.IsChecked()
        dtdebug(f"OnToggleBlindscan={self.use_blindscan_}")
        event.Skip()

    def OnClose(self, evt):
        return self.parent.OnClose(evt);

    def OnSignalInfoUpdate(self, signal_info):
        self.parent.UpdateSignalInfo(signal_info, self.tuned_)
        si_mux = signal_info.dvbs_mux

        self.si_freq_text.SetLabel(f'{si_mux.frequency/1e3:,.3f} Mhz'.replace(',', ' ') \
                                if self.signal_info.has_nit else '')
        self.si_symbolrate_text.SetLabel(f'{si_mux.symbol_rate/1e3:,.0f} kS/s'.replace(',', ' ') \
                                if self.signal_info.has_nit else '')

        mux = signal_info.dvbs_mux
        locked = signal_info.has_lock


        cn = '' if signal_info.network_id_confirmed  else "?"
        ct = '' if signal_info.ts_id_confirmed  else "?"
        stream = f' stream={mux.stream_id}' if mux.stream_id>=0 else ''
        if False:
            if signal_info.network_id_confirmed or signal_info.ts_id_confirmed or mux.stream_id>=0:
                self.dvb_ids_text.SetLabel(f'nid={mux.k.network_id}{cn}, ts={mux.k.ts_id}{ct}{stream}' if locked else '')
            else:
                self.dvb_ids_text.SetLabel(f'nid={mux.k.network_id}{cn}, ts={mux.k.ts_id}{ct}{stream}' if locked else '')
        if not locked:
            self.ClearSignalInfo()
            return
        else:
            pass
        si_mux = self.signal_info.si_mux
        sat_text = f'{pychdb.sat_pos_str(si_mux.k.sat_pos)}' if self.signal_info.has_nit else ''
        nid_text = f'{si_mux.k.network_id}' if self.signal_info.has_nit else ''
        tid_text = f'{si_mux.k.ts_id}' if self.signal_info.has_nit else ''

        self.si_nit_ids_text.SetLabel(f'{sat_text} nid={nid_text} tid={tid_text}'
                                if self.signal_info.has_nit else '')
        for key in self.status_keys:
            val = getattr(self.signal_info, f'has_{key}')
            w = getattr(self, f'has_{key}')
            if key == 'fail':
                w.SetLabel('' if val == 0 else 'fail')
            elif key == 'si_done':
                w.SetLabel('' if val == 0 else 'fin')
            else:
                w.SetForegroundColour(wx.Colour('blue' if val else 'red'))

    def ClearSignalInfo(self):
        self.si_freq_text.SetLabel('')
        self.si_symbolrate_text.SetLabel('')
        self.si_nit_ids_text.SetLabel('')

        for key in self.status_keys:
            val = 0
            w = getattr(self, f'has_{key}')
            if key in ('fail', 'si_done'):
                w.SetLabel('')
            else:
                w.SetForegroundColour(wx.Colour('blue' if val else 'red'))
        if False:
            self.dvb_ids_text.SetLabel('')
        #self.parent.ClearSignalInfo()

    def ChangeLnb(self, lnb):
        add = False
        self.lnb = lnb
        if not on_rotor(lnb) and has_network(lnb, self.sat.sat_pos):
            # no change needed
            network=get_network(self.lnb, self.sat.sat_pos)
            self.parent.SetDiseqc12Position(network.diseqc12)
        else:
            #we need to also select a different satellite
            txn = wx.GetApp().chdb.rtxn()
            mux = pychdb.lnb.select_reference_mux(txn, self.lnb, None)
            assert mux.k.sat_pos != pychdb.sat.sat_pos_none
            sat = pychdb.sat.find_by_key(txn, mux.k.sat_pos)
            if sat is None:
                mux.k.sat_pos = network.sat_pos
            elif mux.k.sat_pos == pychdb.sat.sat_pos_none:
                mux.k.sat_pos = sat.sat_pos
            del txn
            assert mux.k.sat_pos == sat.sat_pos
            self.ChangeSat(sat)
        self.lnb_changed = False
        self.parent.ChangeLnb(lnb) #update window title
        evt = LnbChangeEvent(lnb=lnb)
        wx.PostEvent(self, evt)


    def ChangeSat(self, sat):
        if sat.sat_pos == self.sat.sat_pos:
            return
        add = False
        network = get_network(self.lnb, sat.sat_pos)
        if network is not None:
            pass #allow all satellites
        elif on_rotor(self.lnb):
            #allow only network or add network
            added = ShowOkCancel("Add network?", f"Network {sat} not yet defined for lnb {self.lnb}. Add it?")
            if not added:
                return
            self.lnb_changed = True
            network = pychdb.lnb_network.lnb_network()
            network.sat_pos = sat.sat_pos
            network.usals_pos = sat.sat_pos
            dtdebug(f"Saving new lnb network: lnb={self.lnb} network={network}")
            added = pychdb.lnb.add_network(self.lnb, network)
        else:
            ShowMessage("Network unavailable",
                         f"Network {sat} not defined for lnb {self.lnb} on fixed this. Add it in lnb list first")
            return
        assert network is not None
        txn = wx.GetApp().chdb.rtxn()
        self.sat = sat
        if on_rotor(self.lnb):
            self.lnb.usals_pos = network.usals_pos
        self.mux = pychdb.dvbs_mux.find_by_key(txn,network.ref_mux)
        if self.mux is None or self.mux.k.sat_pos != self.sat.sat_pos: #The latter can happen when sat_pos of ref_mux was updated
            self.mux = pychdb.dvbs_mux.dvbs_mux()
        if self.mux.k.sat_pos == pychdb.sat.sat_pos_none:
            self.mux.k.sat_pos = self.sat.sat_pos
        del txn
        dtdebug(f"self.mux={self.mux} self.sat={self.sat}")
        assert self.mux.k.sat_pos == self.sat.sat_pos
        if network is not None:
            self.parent.SetDiseqc12Position(network.diseqc12)
        self.positioner_sat_sel.UpdateText()
        self.positioner_mux_sel.UpdateText()
        self.positioner_mux_sel.SelectSat(sat)
        self.mux_controller.SelectMux(self.mux)
        self.muxedit_grid.Reset()
        sat_pos = self.sat.sat_pos if network is None else network.usals_pos
        self.parent.ChangeSatPos(sat_pos)

    def UpdateRefMux(self, rec):
        if rec.k.sat_pos == pychdb.sat.sat_pos_none:
            rec.k.sat_pos = self.sat.sat_pos
        dtdebug(f"UpdateRefMux: rec.k.sat_pos={rec.k.sat_pos} self.sat.sat_pos={self.sat.sat_pos}")
        if rec.k.sat_pos != self.sat.sat_pos:
            dtdebug(f"changing sat_pos from={self.sat.sat_pos} to={rec.k.sat_pos}")
            rec.k.sat_pos = self.sat.sat_pos
        self.mux = rec
        self.positioner_mux_sel.UpdateText()
        self.muxedit_grid.Reset()
        for n in self.lnb.networks:
            if n.sat_pos == self.mux.k.sat_pos:
                self.lnb_changed |= not same_mux_key(n.ref_mux, self.mux.k)
                n.ref_mux = self.mux.k
                dtdebug(f"saving ref_mux={self.mux}")
                return
        dtdebug(f"network not found: lnb={self.lnb} sat_pos={self.mux.k.sat_pos}")

class SignalPanel(SignalPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.signal_info = pyreceiver.signal_info_t()
        self.SetDefaultLevels()
        self.status_keys = ('signal', 'carrier', 'fec', 'lock', 'sync')
        from neumodvb.speak import Speaker
        self.speak_signal = False
        self.speaker = Speaker()
        self.ber_accu = 0

    def Speak(self):
        if not self.speak_signal:
            return
        locked = self.signal_info.has_lock
        sat_confirmed = self.signal_info.sat_pos_confirmed
        mux = self.signal_info.dvbs_mux
        sat_pos = mux.k.sat_pos # if sat_confirmed else None
        snr = self.signal_info.stat.snr/1000 if locked else None
        self.speaker.speak(sat_pos, snr, sat_confirmed)

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
        self.snr_text.SetLabel(f'{snr:6.2f}dB')
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
        rf_level = self.signal_info.stat.signal_strength/1000
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
        mux = signal_info.dvbs_mux
        frequency_text = f'{mux.frequency/1e3:,.3f} Mhz'.replace(',', ' ') if locked else ''
        symbolrate_text = f'{mux.symbol_rate/1e3:.0f} kS/s' if locked else ''
        fec=lastdot(mux.fec).replace(' ', '/') if locked else ''
        delsys=lastdot(mux.delivery_system)
        modulation=lastdot(mux.modulation)
        fec = f'{fec}' if signal_info.has_fec else ''

        self.freq_sr_modulation_text.SetLabel(f'{frequency_text}  {symbolrate_text} '
                                              f'{delsys} - {modulation}  {fec}' if locked else '')
        self.signal_info = signal_info
        if is_tuned:
            rf_level = self.signal_info.stat.signal_strength/1000
        for key in self.status_keys:
            val = getattr(self.signal_info, f'has_{key}')
            w = getattr(self, f'has_{key}')
            if key == 'fail':
                w.SetLabel('' if val == 0 else 'fail')
            elif key == 'si_done':
                w.SetLabel('' if val == 0 else 'fin')
            else:
                w.SetForegroundColour(wx.Colour('blue' if val else 'red'))
        if not locked:
            dtdebug("SignalPanel: NO LONGER LOCKED")
            #self.ClearSignalInfo()
            self.rf_level_gauge.SetValue(rf_level)
            self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
            return False
        snr = self.signal_info.stat.snr/1000
        min_snr = self.signal_info.min_snr/1000
        self.ber_accu = 0.9*self.ber_accu + 0.1*  self.signal_info.stat.ber
        ber = self.ber_accu if self.signal_info.stat.ber> self.ber_accu else self.signal_info.stat.ber
        lber =math.log10(max(1e-9,ber))
        #self.snr_gauge.SetRange(20.0)
        #self.ref_level_gauge.SetRange(-20.0)
        snr_ranges = [0, max(min_snr, 0), max(min_snr+2, 0), self.snr_ranges[3]]
        if snr_ranges != self.snr_ranges:
            self.snr_ranges = snr_ranges
            self.snr_gauge.SetRange(snr_ranges)

        self.rf_level_gauge.SetValue(rf_level)
        self.snr_gauge.SetValue(snr)
        self.ber_gauge.SetValue(lber)
        self.snr_text.SetLabel(f'{snr:6.2f}dB')
        self.rf_level_text.SetLabel(f'{rf_level:6.2f}dB')
        self.ber_text.SetLabel(f'{ber:8.2E}')

        stream_id = mux.stream_id
        isi = ', '.join([f'<span foreground="blue">{str(i)}</span>' if i==stream_id else str(i) \
                         for i in signal_info.isi_list]) if locked else ''
        self.isi_list_text.SetLabelMarkup(isi)
        #we need the int cast, because mux.delivery_sysstem can be of dvbs, ddvbt or dvbc type
        if int(mux.delivery_system) == int(pychdb.fe_delsys_t.DVBS2) and locked:
            matype = signal_info.matype.replace("ACM/VCM", f'<span foreground="blue">ACM/VCM</span>')
        else:
            matype=""
        if locked:
            pls_mode = lastdot(str(mux.pls_mode))
            pls = f'PLS: {pls_mode} {mux.pls_code}'
        else:
            pls =''
        self.matype_pls_text.SetLabelMarkup(f'{matype} {pls}')

        sat_confirmed = signal_info.sat_pos_confirmed
        c = '' if sat_confirmed  else "?"
        self.sat_pos_text.SetForegroundColour(wx.Colour('blue' if sat_confirmed else 'red'))
        self.sat_pos_text.SetLabel(f'{pychdb.sat_pos_str(mux.k.sat_pos)}{c}' if locked else '')
        self.lnb_lof_offset_text.SetLabel(f'{self.signal_info.lnb_lof_offset:,d} kHz'.replace(',', ' ')) \
            if self.signal_info.lnb_lof_offset is not None else None

        self.freq_sr_sizer.Layout()
        return True

    def OnToggleSpeak(self, evt):
        self.GetParent().GetParent().OnToggleSpeak(evt)


class PositionerDialog(PositionerDialog_):
    def __init__(self, parent, sat, lnb, mux, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.tune_mux_panel.init(self, sat, lnb, mux)
        self.parent = parent

        self.SetTitle(f'Positioner Control - {self.tune_mux_panel.lnb}')

        self.diseqc_type_choice.controller = self.tune_mux_panel.mux_controller
        self.diseqc_type_choice.SetValue(self.lnb)
        network = get_network(self.lnb, self.sat.sat_pos)
        self.SetPosition(self.sat.sat_pos if network is None else network.usals_pos)
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
        pass
        self.Layout()

    def Close(self):
        self.tune_mux_panel.Close()

    def OnClose(self, evt):
        dtdebug("CLOSE")
        self.Close()
        wx.CallAfter(self.Destroy)
        evt.Skip()

    def OnSubscriberCallback(self, data):
        pass

    def UpdateSignalInfo(self, signal_info, tuned):
        self.signal_panel.OnSignalInfoUpdate(signal_info, tuned);
        if signal_info.constellation_samples is not None and self.update_constellation:
            self.tune_mux_panel.constellation_plot.show_constellation(signal_info.constellation_samples)

    def ClearSignalInfo(self):
        self.signal_panel.ClearSignalInfo()
        self.tune_mux_panel.constellation_plot.clear_constellation()
        self.tune_mux_panel.constellation_plot.clear_data()

    def SetPosition(self, pos):
        self.position = pos
        self.rotor_position_text_ctrl.SetValue(pychdb.sat_pos_str(self.position))

    def SetDiseqc12Position(self, idx):
        self.diseqc_position_spin_ctrl.SetValue(idx)

    def SetUsalsLocation(self, hemispheres_only=False):
        receiver = wx.GetApp().receiver
        opts =  receiver.options

        if True or not hemispheres_only:
            longitude = f'{abs(opts.usals_location.usals_longitude)/100.:3.1f}'
            self.longitude_text_ctrl.ChangeValue(longitude)

            lattitude = f'{abs(opts.usals_location.usals_lattitude)/100.:3.1f}'
            self.lattitude_text_ctrl.ChangeValue(lattitude)

        self.lattitude_north_south_choice.SetSelection(opts.usals_location.usals_lattitude<0)
        self.longitude_east_west_choice.SetSelection(opts.usals_location.usals_longitude<0)

    def SetDiseqc12(self, diseqc12):
        self.tune_mux_panel.diseqc12 = diseqc12
        self.diseqc_position_spin_ctrl.SetValue(self.tune_mux_panel.diseqc12)

    def SetStep(self, pos):
        self.step = pos
        self.rotor_step_spin_ctrl.SetValue(self.step/100)

    def SetDiseqcType(self, diseqc_type):
        self.diseqc_type_choice.SetSelection

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            dtdebug("ESCAPE")
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def ChangeLnb(self, lnb):
        self.SetTitle(f'Positioner Control - {lnb}')

    def ChangeSatPos(self, sat_pos):
        self.SetPosition(sat_pos)

    def usals_command(self, *args):
        if self.lnb.rotor_control == pychdb.rotor_control_t.ROTOR_MASTER_USALS:
            self.lnb_subscriber.positioner_cmd(*args)
            return True
        else:
            ShowMessage("Cannot control rotor",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb.rotor_control)} does not "
                        "allow moving the positioner")
        return False

    def diseqc12_command(self, *args):
        if self.lnb.rotor_control == pychdb.rotor_control_t.ROTOR_MASTER_DISEQC12:
            self.tuned_mux_subscriber.positioner_cmd(*args)
            return True
        else:
            ShowMessage("Cannot control rotor",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb.rotor_control)} does not "
                        "allow moving the positioner")
        return False

    def positioner_command(self, *args):
        if self.lnb.rotor_control in (pychdb.rotor_control_t.ROTOR_MASTER_DISEQC12,
                                      pychdb.rotor_control_t.ROTOR_MASTER_USALS):
            self.mux_subscriber.positioner_cmd(*args)
            return True
        else:
            ShowMessage("Cannot control rotor",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb.rotor_control)} does not "
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

    def UpdateUsalsPosition(self, usals_pos):
        dtdebug(f"USALS position set to {usals_pos/100}")
        self.lnb.usals_pos = usals_pos
        self.tune_mux_panel.lnb_changed = True
        for network in self.lnb.networks:
            if network.sat_pos == self.mux.k.sat_pos:
                network.usals_pos = usals_pos
                self.tune_mux_panel.lnb_changed = True
                dtdebug(f"Goto XX {usals_pos}" )
                ret=self.usals_command(pychdb.positioner_cmd_t.GOTO_XX, usals_pos)
                assert ret>=0
                return
        dtdebug(f"lnb network not found: lnb={self.lnb} sat_pos={self.mux.k.sat_pos}")

    def UpdateDiseqc12(self, diseqc12):
        dtdebug(f"DISEQC12 set to {diseqc12}")
        for n in self.lnb.networks:
            if n.sat_pos == self.mux.k.sat_pos:
                n.diseqc12 = diseqc12
                self.tune_mux_panel.lnb_changed = True
                self.tune_mux_panel.diseqc12 = diseqc12
                dtdebug(f"updated lnb diseqc12 position: lnb={self.lnb} diseqc12={diseqc12}")
                #dtdebug(f"Goto NN: {diseqc12}")
                #self.tune_mux_panel.diseqc12_command(pychdb.positioner_cmd_t.GOTO_NN, diseqc12)
                return
        dtdebug("lnb network not found: lnb={self.lnb} sat_pos={self.mux.k.sat_pos}")

    def OnDiseqcTypeChoice(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.lnb.rotor_control = self.diseqc_type_choice.GetValue()
        dtdebug(f"diseqc type set to {self.lnb.rotor_control}")
        self.tune_mux_panel.lnb_changed = True
        event.Skip()

    def OnPositionChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        val = event if type(event) == str  else event.GetString()
        from neumodvb.util import parse_longitude
        pos = parse_longitude(val)

        self.SetPosition(pos)
        self.UpdateUsalsPosition(pos)
        if type(event) != str:
            event.Skip()

    def OnGotoUsals(self, event):
        self.OnPositionChanged(self.rotor_position_text_ctrl.GetValue())
        self.UpdateUsalsPosition(self.position)


    def OnStepEast(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.position += self.step
        self.SetPosition(self.position)
        self.UpdateUsalsPosition(self.position)
        event.Skip()

    def OnStepWest(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.position -= self.step
        #self.rotor_position_text_ctrl.SetValue(pychdb.sat_pos_str(self.position))
        self.SetPosition(self.position)
        self.UpdateUsalsPosition(self.position)
        event.Skip()

    def OnStepChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        step = event.GetValue()
        dtdebug(f"USALS step changed to {step}")
        self.step = int(100*step)
        self.SetStep(self.step)
        event.Skip()

    def OnStopPositioner(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Stop Positioner")
        self.positioner_command(pychdb.positioner_cmd_t.HALT)
        event.Skip()

    def OnLattitudeChanged(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        from neumodvb.util import parse_lattitude
        val = parse_lattitude(evt.GetString())
        dtdebug(f'site lattitude changed to {val}')
        opts =  receiver.options
        opts.usals_location.usals_lattitude = val
        receiver.options = opts;
        self.SetUsalsLocation(hemispheres_only=True)
        evt.Skip()

    def OnLattitudeNorthSouthSelect(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        opts =  receiver.options
        val = abs(opts.usals_location.usals_lattitude)
        if evt.GetSelection() == 1:
            val = -val
        opts.usals_location.usals_lattitude = val
        receiver.options = opts;
        dtdebug(f'site lattitude changed to {val}')
        evt.Skip()

    def OnLongitudeChanged(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        from neumodvb.util import parse_longitude
        val = parse_longitude(evt.GetString())
        dtdebug(f'site longitude changed to {val}')
        opts =  receiver.options
        opts.usals_location.usals_longitude = val
        receiver.options = opts;
        self.SetUsalsLocation(hemispheres_only=True)
        evt.Skip()

    def OnLongitudeEastWestSelect(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        receiver = wx.GetApp().receiver
        opts =  receiver.options
        val = abs(opts.usals_location.usals_longitude)
        if evt.GetSelection() == 1:
            val = -val
        opts.usals_location.usals_longitude = val
        receiver.options = opts;
        dtdebug(f'site longitude changed to {val}')
        evt.Skip()

    def OnStorePosition(self, evt):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug(f"Diseqc12 position stored: {self.tune_mux_panel.diseqc12}")
        self.diseqc12_command(pychdb.positioner_cmd_t.STORE_NN, self.tune_mux_panel.diseqc12)
        evt.Skip()

    def OnDiseqc12PositionChanged(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        val = event.GetValue()
        self.UpdateDiseqc12(val)
        dtdebug(f"OnDiseqc12 position changed to {val}")
        event.Skip()

    def OnGotoPosition(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug(f"Goto NN: diseqc12={self.tune_mux_panel.diseqc12}")
        self.diseqc12_command(pychdb.positioner_cmd_t.GOTO_NN, self.tune_mux_panel.diseqc12)
        event.Skip()

    def OnGotoEast(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Drive east")
        self.positioner_command(pychdb.positioner_cmd_t.DRIVE_EAST)
        event.Skip()

    def OnGotoRef(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Goto ref")
        self.positioner_command(pychdb.positioner_cmd_t.GOTO_REF)
        event.Skip()

    def OnGotoSat(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        #Called from usals button "goto sat"
        dtdebug("Goto sat")
        self.tune_mux_panel.muxedit_grid.table.FinalizeUnsavedEdits()
        self.tune_mux_panel.UpdateRefMux(self.mux)
        txn = wx.GetApp().chdb.rtxn()
        lnb = pychdb.lnb.find_by_key(txn, self.lnb.k) #reread the networks
        txn.abort()
        network = get_network(lnb, self.sat.sat_pos)
        pos = network.usals_pos
        if self.lnb.rotor_control == pychdb.rotor_control_t.ROTOR_MASTER_USALS:
            self.usals_command(pychdb.positioner_cmd_t.GOTO_XX, pos)
            self.UpdateUsalsPosition(pos)
        elif self.lnb.rotor_control == pychdb.rotor_control_t.ROTOR_MASTER_DISEQC12:
            self.diseqc12_cmd(pychdb.positioner_cmd_t.GOTO_NN, network.diseqc12)
            self.SetDiseqc12(network.diseqc12)
        else:
            ShowMessage("Cannot goto sat",
                        f"Rotor control setting {neumodbutils.enum_to_str(self.lnb.rotor_control)} does not "
                        "allow moving the positioner")
            return

        self.SetPosition(pos)
        event.Skip()

    def OnGotoWest(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Drive west")
        self.positioner_command(pychdb.positioner_cmd_t.GOTO_XX)
        event.Skip()

    def OnSetEastLimit(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Set east limit")
        self.positioner_command(pychdb.positioner_cmd_t.LIMIT_EAST)
        event.Skip()

    def OnDisableLimits(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Disable limits")
        self.positioner_command(pychdb.positioner_cmd_t.LIMITS_OFF)
        event.Skip()

    def OnSetWestLimit(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        dtdebug("Set west limit")
        self.positioner_command(pychdb.positioner_cmd_t.LIMIT_WEST)
        event.Skip()

def show_positioner_dialog(caller, sat=None, lnb=None, mux=None):
    dlg = PositionerDialog(caller, sat=sat, lnb=lnb, mux=mux)
    dlg.Show()
    return None
    dlg.Close(None)
    if val == wx.ID_OK:
        try:
            pass
        except:
            pass
    dlg.Destroy()
    return None
