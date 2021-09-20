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
import pychdb

from neumodvb.util import dtdebug, dterror
from neumodvb.spectrum_dialog_gui import SpectrumDialog_, SpectrumButtons_, SpectrumListPanel_
from neumodvb.positioner_dialog import LnbController, SatController, MuxController
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel

import pyreceiver
from pyreceiver import get_object as get_object_


def get_object(evt):
    s = evt.GetExtraLong()
    return get_object_(s)

class SpectrumButtons(SpectrumButtons_):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.parent = parent
        wx.CallAfter(self.select_all_band_pols)

    def OnAcquireSpectrum(self, event):
        return self.Parent.OnAcquireSpectrum(event)

    def OnBlindScan(self, event):
        return self.parent.OnBlindScan(event)

    def select_all_band_pols(self):
        self.spectrum_horizontal_low.SetValue(1)
        self.spectrum_horizontal_high.SetValue(1)
        self.spectrum_vertical_low.SetValue(1)
        self.spectrum_vertical_high.SetValue(1)

    def selected_band_pols(self):
        bp_t = pychdb.fe_band_pol.fe_band_pol
        b_t = pychdb.fe_band_t
        p_t = pychdb.fe_polarisation_t
        ret = []

        hl, hh, vl, vh = self.spectrum_horizontal_low.GetValue(), \
            self.spectrum_horizontal_high.GetValue(), \
            self.spectrum_vertical_low.GetValue(), \
            self.spectrum_vertical_high.GetValue()
        if hl and hh and vl and vh:
            return [ bp_t(b_t.UNKNOWN, p_t.UNKNOWN)]

        if hl and hh:
            ret.append(bp_t(b_t.UNKNOWN, p_t.H))
        elif hl:
            ret.append(bp_t(b_t.LOW, p_t.H))
        elif hh:
            ret.append(bp_t(b_t.HIGH, p_t.H))

        if vl and vh:
            ret.append(bp_t(b_t.UNKNOWN, p_t.V))
        elif vl:
            ret.append(bp_t(b_t.LOW, p_t.V))
        elif vh:
            ret.append(bp_t(b_t.HIGH, p_t.V))
        return ret

class SpectrumListPanel(SpectrumListPanel_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent

    def initOFF(self, parent, sat, lnb,  mux):
        self.lnb, self.sat, self.mux = self.SelectInitialData(lnb, sat, mux)
        assert (mux is None and sat is None) or (sat is None and lnb is None) or (lnb is None and mux is None)
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)
        self.spectrumselect_grid.Bind ( wx.grid.EVT_GRID_SELECT_CELL, self.OnGridRowSelect)

    def OnRowSelect(self, evt):
        dtdebug(f"ROW SELECT {evt.GetRow()}")

    def SelectInitialDataOFF(self, lnb, sat, mux):
        """
        select an inital choice for lnb mux and sat
        """
        breakpoint()


class SpectrumDialog(SpectrumDialog_):

    def __init__(self, parent, mux=None, sat=None, lnb=None, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.tune_mux_panel.init(self, sat, lnb, mux)
        self.parent = parent

        self.SetTitle(f'Spectrum - {self.lnb}')

        self.start_freq = -1 # -1 means auto 10700000
        self.end_freq = -1 # -1 means auto 12750000

        self.gettting_spectrum_ = False

        bp_t = pychdb.fe_band_pol.fe_band_pol
        b_t = pychdb.fe_band_t
        p_t = pychdb.fe_polarisation_t

        self.is_blindscanning = False
        self.band_pols_to_scan = []
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)

        self.Bind(wx.EVT_CLOSE, self.OnClose) #ony if a nonmodal dialog is used

        self.app_main_frame = wx.GetApp().frame
        self.update_handler = \
            self.app_main_frame.Bind(wx.EVT_TIMER, self.OnTimer) #used to refresh list on screen
        self.tp_being_scanned = None

    def OnTimer(self, evt):
        self.grid.OnTimer(evt)
        evt.Skip(True) #ensures tat other windows also get the event

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

        wx.CallAfter(self.Destroy)
        evt.Skip()

    def OnToggleSpeak(self, evt):
        self.signal_panel.speak_signal = evt.IsChecked()
        dtdebug(f"OnToggleSpeak={self.signal_panel.speak_signal}")
        evt.Skip()

    def OnExit(self, evt):
        return wx.GetApp().frame.OnExit(evt);

    def OnSpectrumSelect(self, evt):
        rowno = self.grid.GetGridCursorRow()
        spectrum = self.grid.table.GetRow(rowno)
        self.spectrum_plot.toggle_spectrum(spectrum)

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
            self.band_pols_to_scan = self.spectrum_buttons_panel.selected_band_pols()
            if len(self.band_pols_to_scan) == 0:
                ShowMessage("Select bands", "No bands selected for spectrum")
                self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
            else:
                band_pol = self.band_pols_to_scan.pop(0)
                self.mux_subscriber.subscribe_spectrum(self.lnb, band_pol,
                                                       self.start_freq, self.end_freq,
                                                       self.sat.sat_pos)
        event.Skip()

    def EndBlindScan(self):
        self.tune_mux_panel.AbortTune()
        self.spectrum_buttons_panel.blindscan_button.SetValue(0)
        self.ClearSignalInfo()
        self.is_blindscanning = False

    def OnAbortTune(self, event):
        dtdebug(f"positioner: unsubscribing")
        self.last_tuned_mux = None
        wx.CallAfter(self.AbortTune)

    def OnBlindScan(self, event=None):
        dtdebug("Blindscan start")
        obj = event.GetEventObject()
        isPressed = obj.GetValue()
        if not isPressed:
            dtdebug("Ending blindscan all")
            self.EndBlindScan()
        else:
            dtdebug("starting blindscan all")
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
                ShowMessage("Nothing to scan",
                            "No muxes left to scan: only bands displayed on screen will be "
                            "scanned and already scanned muxes will be skipped" )
                self.spectrum_buttons_panel.blindscan_button.SetValue(0)
                self.is_blindscanning = False
            else:
                self.is_blindscanning = True
                wx.CallAfter(self.blindscan_next)
        event.Skip()

    def blindscan_next(self):
        if len(self.tps_to_scan) == 0:
            self.is_blindscanning = False
            ShowMessage("Scan done ", "No muxes left to scan" )
            self.EndBlindScan()
            return
        tp = self.tps_to_scan.pop(0)
        self.tp_being_scanned = tp
        dtdebug(f"scanning {tp}; {len(self.tps_to_scan)} tps left to scan")
        self.spectrum_plot.set_current_tp(tp)
        mux = self.OnSelectMux(tp)
        self.ClearSignalInfo()
        assert self.tune_mux_panel.mux.frequency == mux.frequency
        self.tune_mux_panel.Tune(mux, retune_mode=pyreceiver.retune_mode_t.NEVER)

    def next_stream(self, stream_id):
        tp = self.tp_being_scanned
        dtdebug(f"scanning {tp}; {len(self.tps_to_scan)} tps left to scan")
        mux = self.signal_info.dvbs_mux.copy()
        mux.stream_id = stream_id
        mux.c.is_template = True
        self.tune_mux_panel.mux = mux
        self.tune_mux_panel.muxedit_grid.Reset()
        self.spectrum_plot.reset_current_annot_status(mux)
        self.ClearSignalInfo()
        self.tune_mux_panel.Tune(mux,  retune_mode=pyreceiver.retune_mode_t.NEVER)

    def OnSelectMux(self, tp):
        spectrum = tp.spectrum.spectrum
        if spectrum.k.sat_pos != self.sat.sat_pos or \
           spectrum.k.lnb_key.adapter_no != self.lnb.k.adapter_no or \
               spectrum.k.lnb_key.dish_id != self.lnb.k.dish_id or \
                   spectrum.k.lnb_key.lnb_id != self.lnb.k.lnb_id:
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

    def OnSubscriberCallback(self, data):
        need_si = True
        if type(data) == pyreceiver.signal_info_t:
            self.signal_info = data
            if self.signal_info.has_fail or (self.signal_info.has_si_done if need_si else self.signal_info.has_lock):
                mux = self.tune_mux_panel.last_tuned_mux

                dtdebug(f"!!!!!!! TUNE DONE!!!!! mux={mux} lock={self.signal_info.has_lock} fail={self.signal_info.has_fail} done={self.signal_info.has_si_done}")
                self.spectrum_plot.set_current_annot_status(mux, self.signal_info.has_lock)
                if self.is_blindscanning:
                    tp = self.tp_being_scanned
                    if not hasattr(tp, 'isis_present'):
                        tp.isis_present = set(data.isi_list)
                        tp.isis_scanned = set((data.dvbs_mux.stream_id,))
                    else:
                        tp.isis_present = set.union(set(data.isi_list), tp.isis_present)
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
                if len(self.band_pols_to_scan) != 0:
                    band_pol = self.band_pols_to_scan.pop(0)
                    self.mux_subscriber.subscribe_spectrum(self.lnb, band_pol,
                                                           self.start_freq, self.end_freq,
                                                           self.sat.sat_pos)
                else:
                    self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)

    def ChangeLnb(self, lnb):
        self.SetTitle(f'Tuner Control - {lnb}')

    def ChangeSatPos(self, sat_pos):
        pass

    def SetDiseqc12Position(self, idx):
        pass

    def UpdateSignalInfo(self, signal_info, is_tuned):
        self.signal_panel.OnSignalInfoUpdate(signal_info, is_tuned);
        if signal_info.constellation_samples is not None:
            self.constellation_plot.show_constellation(signal_info.constellation_samples)

    def ClearSignalInfo(self):
        self.signal_panel.ClearSignalInfo()
        self.constellation_plot.clear_constellation()
        self.constellation_plot.clear_data()

    def OnInspect(self, event):
        wx.GetApp().OnInspect()
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