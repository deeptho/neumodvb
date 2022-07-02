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
import datetime
from dateutil import tz

import pychdb

from neumodvb.util import dtdebug, dterror
from neumodvb.spectrum_dialog_gui import SpectrumDialog_, SpectrumButtons_, SpectrumListPanel_
from neumodvb.positioner_dialog import LnbController, SatController, MuxController
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
import pyreceiver
from pyreceiver import get_object as get_object_

def lnb_matches_spectrum(lnb,  spectrum):
    start_freq, end_freq = pychdb.lnb.lnb_frequency_range(lnb)
    return (start_freq <= spectrum.start_freq <= end_freq) and \
        (start_freq <= spectrum.end_freq <= end_freq)

def get_object(evt):
    s = evt.GetExtraLong()
    return get_object_(s)

class SpectrumButtons(SpectrumButtons_):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.parent = parent
        wx.CallAfter(self.select_range_and_pols)

    def OnAcquireSpectrum(self, event):
        return self.Parent.OnAcquireSpectrum(event)

    def OnBlindScan(self, event):
        return self.parent.OnBlindScan(event)

    def select_start_end(self, lnb):
        rng = pychdb.lnb.lnb_frequency_range(lnb)
        start_freq, end_freq = rng
        if start_freq <= self.parent.start_freq <= end_freq and \
           start_freq <= self.parent.end_freq <= end_freq:
            return
        dtdebug(f'changing spectral scan range: {lnb} {rng}')
        self.parent.start_freq, self.parent.end_freq = rng
        self.start_freq_text.SetValue(str(self.parent.start_freq//1000))
        self.end_freq_text.SetValue(str(self.parent.end_freq//1000))
        is_circ = lnb.pol_type in (pychdb.lnb_pol_type_t.LR, pychdb.lnb_pol_type_t.RL)
        if is_circ:
            self.spectrum_horizontal.SetLabel('L')
            self.spectrum_vertical.SetLabel('R')
        else:
            self.spectrum_horizontal.SetLabel('H')
            self.spectrum_vertical.SetLabel('V')
    def select_range_and_pols(self):
        self.spectrum_horizontal.SetValue(1)
        self.spectrum_vertical.SetValue(1)
        self.start_freq_text.SetValue(str(self.parent.start_freq//1000))
        self.end_freq_text.SetValue(str(self.parent.end_freq//1000))

    def get_range_and_pols(self):
        p_t = pychdb.fe_polarisation_t
        ret = []
        self.parent.start_freq = int(self.start_freq_text.GetValue())*1000
        self.parent.end_freq = int(self.end_freq_text.GetValue())*1000
        is_circ = self.parent.lnb.pol_type in (pychdb.lnb_pol_type_t.LR, pychdb.lnb_pol_type_t.RL)
        h, v, = self.spectrum_horizontal.GetValue(), \
            self.spectrum_vertical.GetValue()
        if v and h:
            self.parent.pols_to_scan = [ p_t.L, p_t.R ] if is_circ else [ p_t.H, p_t.V ]
        elif h:
            self.parent.pols_to_scan = [ p_t.L ] if is_circ else [ p_t.H ]
        elif v:
            self.parent.pols_to_scan = [ p_t.R ] if is_circ else [ p_t.V ]

class SpectrumListPanel(SpectrumListPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent


    def OnRowSelect(self, evt):
        dtdebug(f"ROW SELECT {evt.GetRow()}")



class SpectrumDialog(SpectrumDialog_):

    def __init__(self, parent, mux=None, sat=None, lnb=None, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.tune_mux_panel.init(self, sat, lnb, mux)

        from neumodvb.positioner_dialog import EVT_LNB_CHANGE, EVT_ABORT_TUNE
        self.tune_mux_panel.Bind(EVT_LNB_CHANGE, self.OnChangeLnb)
        self.Bind(EVT_ABORT_TUNE, self.OnAbortTune)

        self.parent = parent

        self.SetTitle(f'Spectrum - {self.lnb}')

        self.start_freq = 10700000
        self.end_freq = 12750000
        self.spectrum_buttons_panel.select_start_end(self.lnb)
        self.gettting_spectrum_ = False

        bp_t = pychdb.fe_band_pol.fe_band_pol
        b_t = pychdb.fe_band_t
        p_t = pychdb.fe_polarisation_t

        self.is_blindscanning = False
        self.done_count = 0
        self.pols_to_scan = []
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)

        self.Bind(wx.EVT_CLOSE, self.OnClose) #ony if a nonmodal dialog is used

        self.app_main_frame = wx.GetApp().frame
        self.update_handler = \
            self.app_main_frame.Bind(wx.EVT_TIMER, self.OnTimer) #used to refresh list on screen
        self.tp_being_scanned = None
        self.blindscan_num_muxes = 0
        self.blindscan_num_locked_muxes = 0
        self.blindscan_num_nonlocked_muxes = 0
        self.blindscan_num_si_muxes =0
        self.update_constellation = True
        self.tune_mux_panel.constellation_toggle.SetValue(self.update_constellation)
        self.signal_info = None

    def OnTimer(self, evt):
        self.grid.OnTimer(evt)
        evt.Skip(True) #ensures tat other windows also get the event

    def OnChangeLnb(self, evt):
        ret = self.spectrum_buttons_panel.select_start_end(evt.lnb)
        return ret

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
    def grid(self):
        return self.spectrumlist_panel.spectrumselect_grid

    def Close(self):
        self.tune_mux_panel.Close()
        self.app_main_frame.Unbind(wx.EVT_TIMER, handler=self.OnTimer) #used to refresh list on screen
        self.spectrumlist_panel.Close()

    def OnClose(self, evt):
        dtdebug("CLOSE DIALOG")
        self.Close()
        self.tune_mux_panel.AbortTune()
        wx.CallAfter(self.Destroy)
        evt.Skip()

    def OnToggleSpeak(self, evt):
        self.signal_panel.speak_signal = evt.IsChecked()
        dtdebug(f"OnToggleSpeak={self.signal_panel.speak_signal}")
        evt.Skip()

    def OnToggleConstellation(self, evt):
        self.update_constellation = evt.IsChecked()
        dtdebug(f"OnToggleConstellation={self.update_constellation}")
        evt.Skip()

    def CmdExit(self, evt):
        return wx.GetApp().frame.CmdExit(evt);

    def OnSpectrumSelect(self, evt):
        rowno = self.grid.GetGridCursorRow()
        spectrum = self.grid.table.GetRow(rowno)
        self.spectrum_plot.toggle_spectrum(spectrum)

    def OnDrawMux(self, evt):
        self.spectrum_plot.start_draw_mux()

    def OnToggleBlindscan(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.use_blindscan = event.IsChecked()
        dtdebug(f"OnToggleBlindscan={self.use_blindscan}")
        event.Skip()

    def OnAcquireSpectrum(self, event=None):
        obj = event.GetEventObject()
        isPressed = obj.GetValue()
        if not isPressed:
            dtdebug("Ending blindscan")
            self.EndBlindScan()
        else:
            dtdebug("starting spectrum acquisition")
            self.spectrum_buttons_panel.get_range_and_pols()
            if (self.end_freq < self.start_freq):
                ShowMessage("Select range", "Invalid range for spectrum")
                self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
            elif len(self.pols_to_scan) == 0:
                ShowMessage("Select bands", "No Polarisations selected for spectrum")
                self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
            else:
                pol = self.pols_to_scan.pop(0)
                self.mux_subscriber.subscribe_spectrum(self.lnb, pol,
                                                       self.start_freq, self.end_freq,
                                                       self.sat.sat_pos)
        event.Skip()

    def EndBlindScan(self):
        self.tune_mux_panel.AbortTune()
        self.spectrum_buttons_panel.blindscan_button.SetValue(0)
        self.ClearSignalInfo()
        self.is_blindscanning = False
        self.blindscan_num_muxes = 0
        self.blindscan_num_locked_muxes = 0
        self.blindscan_num_nonlocked_muxes = 0
        self.blindscan_num_si_muxes =0

    def OnAbortTune(self, event):
        dtdebug(f"positioner: unsubscribing")
        self.last_tuned_mux = None
        if self.is_blindscanning:
            self.EndBlindScan()
            self.spectrum_buttons_panel.blindscan_button.SetValue(0)

    def OnBlindScan(self, event=None):
        dtdebug("Blindscan start")

        obj = event.GetEventObject()
        isPressed = obj.GetValue()
        if not isPressed:
            dtdebug("Ending blindscan all")
            self.EndBlindScan()
        else:
            dtdebug("starting blindscan all")
            self.blindscan_start = datetime.datetime.now(tz=tz.tzlocal())
            self.tps_to_scan = []
            self.tune_mux_panel.use_blindscan = True
            for key, spectrum in self.spectrum_plot.spectra.items():
                self.tps_to_scan += spectrum.tps_to_scan()
            if False:
                #debugging only
                for key, spectrum in self.spectrum_plot.spectra.items():
                    annot,_ = spectrum.annot_for_freq(11509)
                    idx = self.tps_to_scan.index(annot.tp)
                    self.tps_to_scan = self.tps_to_scan[idx:]
                    break
            if len(self.tps_to_scan) == 0:
                self.blindscan_end = datetime.datetime.now(tz=tz.tzlocal())
                m, s =  divmod(round((self.blindscan_end -  self.blindscan_start).total_seconds()), 60)
                ShowMessage("No spectral peaks available scan: only bands displayed on screen will be "
                            "scanned and already scanned muxes will be skipped" )
                self.spectrum_buttons_panel.blindscan_button.SetValue(0)
                self.is_blindscanning = False
            else:
                self.is_blindscanning = True
                wx.CallAfter(self.blindscan_next)
        event.Skip()

    def blindscan_next(self):
        self.done_count = 0
        if len(self.tps_to_scan) == 0:
            self.is_blindscanning = False
            self.blindscan_end = datetime.datetime.now(tz=tz.tzlocal())
            m, s =  divmod(round((self.blindscan_end -  self.blindscan_start).total_seconds()), 60)
            title = "Blindscan spectrum finished"
            msg = f"Scanned {self.blindscan_num_muxes} (Locked: {self.blindscan_num_locked_muxes}; " \
                f"DVB: {self.blindscan_num_si_muxes}) muxes in {m}min {s}s"
            dtdebug(msg)
            ShowMessage(title, msg)
            self.spectrum_buttons_panel.blindscan_button.SetValue(0)
            self.is_blindscanning = False
            self.EndBlindScan()
            return
        tp = self.tps_to_scan.pop(0)
        self.tp_being_scanned = tp
        dtdebug(f"scanning {tp}; {len(self.tps_to_scan)} tps left to scan")
        self.spectrum_plot.set_current_tp(tp)
        mux = self.OnSelectMux(tp)
        self.ClearSignalInfo()
        assert self.tune_mux_panel.mux.frequency == mux.frequency
        if not self.tune_mux_panel.Tune(mux, retune_mode=pyreceiver.retune_mode_t.NEVER, silent=True):
            if self.tp_being_scanned is None:
                title = "Blindscan failed"
                msg = f"Blindscan failed: {self.tune_mux_panel.mux_subscriber.error_message}"
                ShowMessage(title, msg)
                self.EndBlindScan()
            else:
                dtdebug("Moving on after tuning failed")
                self.OnSubscriberCallback(False)


    def next_stream(self, stream_id):
        tp = self.tp_being_scanned
        dtdebug(f"scanning {tp}; {len(self.tps_to_scan)} tps left to scan")
        mux = self.mux.copy()
        mux.stream_id = stream_id
        mux.c.tune_src = pychdb.tune_src_t.TEMPLATE
        self.tune_mux_panel.mux = mux
        self.tune_mux_panel.muxedit_grid.Reset()
        self.spectrum_plot.reset_current_annot_status(mux)
        #self.ClearSignalInfo()
        if not self.tune_mux_panel.Tune(mux,  retune_mode=pyreceiver.retune_mode_t.NEVER, silent=True):
            #attempt retune once
            if not self.tune_mux_panel.Tune(mux,  retune_mode=pyreceiver.retune_mode_t.NEVER, silent=True):
                self.OnSubscriberCallback(self.signal_info)
    def OnSelectMux(self, tp):
        spectrum = tp.spectrum.spectrum
        #if spectrum.k.sat_pos != self.sat.sat_pos or \
        #   spectrum.k.lnb_key.adapter_no != self.lnb.k.adapter_no or \
        #       spectrum.k.lnb_key.dish_id != self.lnb.k.dish_id or \
        #           spectrum.k.lnb_key.lnb_id != self.lnb.k.lnb_id:

        if spectrum.k.sat_pos != self.sat.sat_pos or \
           not lnb_matches_spectrum(self.lnb, spectrum):
            txn = wx.GetApp().chdb.rtxn()
            sat = pychdb.sat.find_by_key(txn, spectrum.k.sat_pos)
            lnb = pychdb.lnb.find_by_key(txn, spectrum.k.lnb_key)
            txn.abort()
        else:
            lnb = self.lnb
            sat = self.sat
        self.tune_mux_panel.ChangeLnb(lnb)
        self.tune_mux_panel.ChangeSat(sat)
        mux = self.tune_mux_panel.mux
        mux.frequency = int(tp.freq*1000)
        mux.symbol_rate=  int(tp.symbol_rate*1000)
        mux.stream_id = -1
        mux.pls_mode = pychdb.fe_pls_mode_t.ROOT
        mux.pls_code = 1
        mux.pol = tp.spectrum.spectrum.k.pol
        #to test
        mux.k.sat_pos = sat.sat_pos
        self.tune_mux_panel.muxedit_grid.Reset()
        return mux

    def OnUpdateMux(self, freq, pol, symbol_rate):
        lnb = self.lnb
        sat = self.sat
        mux = pychdb.dvbs_mux.dvbs_mux()
        mux.frequency = int(freq*1000)
        mux.symbol_rate=  int(symbol_rate*1000)
        mux.stream_id = -1
        mux.pls_mode = pychdb.fe_pls_mode_t.ROOT
        mux.pls_code = 1
        p_t = pychdb.fe_polarisation_t
        mux.pol = getattr(p_t, pol)
        mux.k.sat_pos = sat.sat_pos
        mux.c.tune_src = pychdb.tune_src_t.TEMPLATE
        self.tune_mux_panel.mux = mux
        self.tune_mux_panel.muxedit_grid.Reset()
        return mux

    def OnSubscriberCallback(self, data):
        if type(data) == str:
            ShowMessage("Error", data)
            return
        need_si = True
        if data == False:
            mux = self.tune_mux_panel.last_tuned_mux
            self.blindscan_num_muxes += 1
            self.blindscan_num_nonlocked_muxes += 1
            dtdebug(f"TUNE DONE mux={mux} ERROR")
            self.spectrum_plot.set_current_annot_status(mux, mux, False)
            if self.is_blindscanning:
                 tp = self.tp_being_scanned
                 self.blindscan_next()
        elif type(data) == pyreceiver.signal_info_t:
            self.signal_info = data
            need_si = not self.signal_info.has_no_dvb
            si_done = self.signal_info.has_si_done if need_si else self.signal_info.has_lock
            self.done_count +=1
            if self.done_count >= 3:
                if self.signal_info.has_fail or si_done:
                    mux = self.tune_mux_panel.last_tuned_mux
                    self.blindscan_num_muxes += 1
                    self.blindscan_num_locked_muxes += self.signal_info.has_lock
                    self.blindscan_num_nonlocked_muxes += not self.signal_info.has_lock
                    self.blindscan_num_si_muxes += (self.signal_info.has_nit or self.signal_info.has_sdt or self.signal_info.has_pat)
                    dtdebug(f"TUNE DONE mux={mux} lock={self.signal_info.has_lock} fail={self.signal_info.has_fail} done={self.signal_info.has_si_done}")
                    self.spectrum_plot.set_current_annot_status(mux, self.signal_info.si_mux, self.signal_info.has_lock)
                    if self.is_blindscanning:
                        tp = self.tp_being_scanned
                        if not hasattr(tp, 'isis_present'):
                            tp.isis_present = set(data.isi_list)
                            #add both si_mux.stream_id dvbs_mux.stream_id in case drivers change stream_id (which is bug)
                            tp.isis_scanned = set((data.si_mux.stream_id,data.dvbs_mux.stream_id))
                        else:
                            tp.isis_present = set.union(set(data.isi_list), tp.isis_present)
                            #add both si_mux.stream_id dvbs_mux.stream_id in case drivers change stream_id (which is bug)
                            tp.isis_scanned.add(data.dvbs_mux.stream_id)
                            tp.isis_scanned.add(data.si_mux.stream_id)
                        tp.isis_to_scan =  tp.isis_present -  tp.isis_scanned
                        if len(tp.isis_to_scan)>0:
                            stream_id = min(tp.isis_to_scan)
                            tp.isis_to_scan.discard(stream_id)
                            self.next_stream(stream_id)
                        else:
                            self.blindscan_next()
            else:
                mux = self.tune_mux_panel.last_tuned_mux

        if type(data) ==type(self.spectrum_plot.spectrum):
            self.spectrum_plot.show_spectrum(data)
            if data.is_complete:
                if len(self.pols_to_scan) != 0:
                    pol = self.pols_to_scan.pop(0)
                    self.mux_subscriber.subscribe_spectrum(self.lnb, pol,
                                                           self.start_freq, self.end_freq,
                                                           self.sat.sat_pos)
                else:
                    self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
                    self.tune_mux_panel.AbortTune()

    def ChangeLnb(self, lnb):
        self.tune_mux_panel.positioner_lnb_sel.UpdateText()
        self.SetTitle(f'Spectrum analysis - {lnb}')

    def ChangeSatPos(self, sat_pos):
        pass

    def SetDiseqc12Position(self, idx):
        pass

    def UpdateSignalInfo(self, signal_info, is_tuned):
        self.signal_panel.OnSignalInfoUpdate(signal_info, is_tuned);
        if signal_info.constellation_samples is not None:
            self.tune_mux_panel.constellation_plot.show_constellation(signal_info.constellation_samples)

    def ClearSignalInfo(self):
        self.signal_panel.ClearSignalInfo()
        self.tune_mux_panel.constellation_plot.clear_constellation()
        self.tune_mux_panel.constellation_plot.clear_data()

    def OnInspect(self, event):
        wx.GetApp().CmdInspect()
        event.Skip()


def show_spectrum_dialog(caller, sat=None, lnb=None , mux=None):
    dlg = SpectrumDialog(caller, sat=sat, lnb=lnb, mux=mux)
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
