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
import datetime
from dateutil import tz

import pydevdb
import pychdb

from neumodvb.util import dtdebug, dterror, is_circ, get_last_scan_text_dict
from neumodvb.spectrum_dialog_gui import SpectrumDialog_, SpectrumButtons_, SpectrumListPanel_
from neumodvb.neumo_dialogs import ShowMessage, ShowOkCancel
import pyreceiver
from pyreceiver import get_object as get_object_

def lnb_matches_spectrum(lnb,  spectrum):
    start_freq, end_freq = pydevdb.lnb.lnb_frequency_range(lnb)
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
        self.parent.OnBlindScan(event)

    def select_start_end(self, lnb):
        rng = pydevdb.lnb.lnb_frequency_range(lnb)
        start_freq, end_freq = rng
        if start_freq <= self.parent.start_freq <= end_freq and \
           start_freq <= self.parent.end_freq <= end_freq:
            return
        dtdebug(f'changing spectral scan range: {lnb} {rng}')
        self.parent.start_freq, self.parent.end_freq = rng
        self.start_freq_text.SetValue(str(self.parent.start_freq//1000))
        self.end_freq_text.SetValue(str(self.parent.end_freq//1000))
        if is_circ(lnb):
            self.spectrum_horizontal.SetLabel('L')
            self.spectrum_vertical.SetLabel('R')
        else:
            self.spectrum_horizontal.SetLabel('H')
            self.spectrum_vertical.SetLabel('V')

    def select_range_and_pols(self):
        lnb = self.parent.lnb
        if lnb is None:
            return
        if lnb.pol_type in (pydevdb.lnb_pol_type_t.VH, pydevdb.lnb_pol_type_t.HV,
                            pydevdb.lnb_pol_type_t.LR, pydevdb.lnb_pol_type_t.RL,
                            pydevdb.lnb_pol_type_t.L, pydevdb.lnb_pol_type_t.H):
            self.spectrum_horizontal.SetValue(1)
            self.spectrum_horizontal.Enable(True)
        else:
            self.spectrum_horizontal.SetValue(0)
            self.spectrum_horizontal.Enable(False)
        if lnb.pol_type in (pydevdb.lnb_pol_type_t.VH, pydevdb.lnb_pol_type_t.HV,
                            pydevdb.lnb_pol_type_t.LR, pydevdb.lnb_pol_type_t.RL,
                            pydevdb.lnb_pol_type_t.R, pydevdb.lnb_pol_type_t.V):
            self.spectrum_vertical.SetValue(1)
            self.spectrum_vertical.Enable(True)
        else:
            self.spectrum_vertical.SetValue(0)
            self.spectrum_vertical.Enable(False)

        self.start_freq_text.SetValue(str(self.parent.start_freq//1000))
        self.end_freq_text.SetValue(str(self.parent.end_freq//1000))

    def get_range_and_pols(self):
        p_t = pychdb.fe_polarisation_t
        ret = []
        self.parent.start_freq = int(self.start_freq_text.GetValue())*1000
        self.parent.end_freq = int(self.end_freq_text.GetValue())*1000
        if self.parent.lnb is None:
            dterror("self.parent.lnb=None")
        h, v, = self.spectrum_horizontal.GetValue(), \
            self.spectrum_vertical.GetValue()
        lnb = self.parent.lnb
        if v and h:
            self.parent.pols_to_scan = [ p_t.L, p_t.R ] if is_circ(lnb) else [ p_t.H, p_t.V ]
        elif h:
            self.parent.pols_to_scan = [ p_t.L ] if is_circ(lnb) else [ p_t.H ]
        elif v:
            self.parent.pols_to_scan = [ p_t.R ] if is_circ(lnb) else [ p_t.V ]

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

        from neumodvb.positioner_dialog import EVT_LNB_SELECT, EVT_ABORT_TUNE
        self.tune_mux_panel.Bind(EVT_LNB_SELECT, self.OnSelectLnb)
        self.tune_mux_panel.Bind(EVT_ABORT_TUNE, self.OnAbortTune)

        self.parent = parent

        self.SetTitle(f'Spectrum - {self.lnb}')

        self.start_freq = 10700000
        self.end_freq = 12750000
        self.spectrum_buttons_panel.select_start_end(self.lnb)
        self.gettting_spectrum_ = False

        bp_t = pychdb.sat_sub_band_pol
        b_t = pychdb.sat_band_t
        p_t = pychdb.fe_polarisation_t

        self.is_blindscanning = False
        self.done_time = None
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
        self.blindscan_num_si_muxes = 0
        self.update_constellation = True
        self.tune_mux_panel.constellation_toggle.SetValue(self.update_constellation)
        self.signal_info = None

    def OnTimer(self, evt):
        self.grid.OnTimer(evt)
        evt.Skip(True) #ensures tat other windows also get the event

    def OnSelectLnb(self, evt):
        ret = self.spectrum_buttons_panel.select_start_end(evt.lnb)
        evt.Skip(True)
        return ret

    @property
    def lnb(self):
        return self.tune_mux_panel.lnb

    @property
    def rf_path(self):
        return self.tune_mux_panel.rf_path

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
        lnb = self.lnb
        if lnb.pol_type in (pydevdb.lnb_pol_type_t.VH, pydevdb.lnb_pol_type_t.HV, pydevdb.lnb_pol_type_t.H):
            default_pol = 'H'
        elif lnb.pol_type in (pydevdb.lnb_pol_type_t.VH, pydevdb.lnb_pol_type_t.HV, pydevdb.lnb_pol_type_t.V):
            default_pol = 'V'
        elif lnb.pol_type in (pydevdb.lnb_pol_type_t.RL, pydevdb.lnb_pol_type_t.LR, pydevdb.lnb_pol_type_t.L):
            default_pol = 'L'
        elif lnb.pol_type in (pydevdb.lnb_pol_type_t.R):
            default_pol = 'R'
        else:
            default_pol = 'H'
        self.spectrum_plot.start_draw_mux(default_pol)

    def OnToggleBlindscan(self, event):  # wxGlade: PositionerDialog_.<event_handler>
        self.use_blindscan = event.IsChecked()
        dtdebug(f"OnToggleBlindscan={self.use_blindscan}")
        event.Skip()
    def OnAcquireSpectrum(self, event=None):
        obj = event.GetEventObject()
        isPressed = obj.GetValue()
        if not isPressed:
            dtdebug("Ending spectrum aquisition")
            self.EndBlindScan()
        else:
            if not wx.GetApp().neumo_drivers_installed:
                ShowMessage("Unsupported", "Spectrum not supported (neumo drivers not installed)")
                return
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
                #reread usals in case we are part of spectrum_dialog and positioner_dialog has changed them
                self.tune_mux_panel.lnb = self.tune_mux_panel.read_lnb_from_db()
                self.mux_subscriber.subscribe_spectrum_acquisition(self.rf_path, self.lnb, pol,
                                                                   self.start_freq, self.end_freq,
                                                                   self.sat)
        event.Skip()

    def EndBlindScan(self):
        self.tune_mux_panel.AbortTune()
        self.spectrum_buttons_panel.blindscan_button.SetValue(0)
        self.ClearSignalInfo()
        self.is_blindscanning = False
        self.blindscan_num_muxes = 0
        self.blindscan_num_locked_muxes = 0
        self.blindscan_num_nonlocked_muxes = 0
        self.blindscan_num_si_muxes = 0

    def OnAbortTune(self, event):
        dtdebug(f"positioner: unsubscribing")
        self.last_tuned_mux = None
        if self.is_blindscanning:
            self.EndBlindScan()
            self.spectrum_buttons_panel.blindscan_button.SetValue(0)
            self.spectrum_plot.end_scan()

    def OnBlindScan(self, event=None):
        dtdebug("Blindscan start parallel")

        obj = event.GetEventObject()
        isPressed = obj.GetValue()
        if not isPressed:
            dtdebug("Ending blindscan all")
            self.EndBlindScan()
        else:
            if not wx.GetApp().neumo_drivers_installed:
                ShowMessage("Unsupported", "Blindscan not supported (neumo drivers not installed)")
                return
            dtdebug("starting blindscan all - parallel")
            self.blindscan_start = datetime.datetime.now(tz=tz.tzlocal())
            self.tps_to_scan = []
            self.tune_mux_panel.use_blindscan = True
            self.is_blindscanning = True
            self.blindscan_all()
        event.Skip()


    def blindscan_all(self):
        subscriber = self.tune_mux_panel.mux_subscriber
        lnb, sat  = self.tune_mux_panel.lnb, self.tune_mux_panel.sat
        new_entries=[]
        for key, spectrum in self.spectrum_plot.spectra.items():
            k = spectrum.spectrum.k
            if k.rf_path.lnb != lnb.k:
                k.rf_path.lnb = lnb.k # in case user has overridden
                newkey = self.spectrum_plot.make_key(spectrum.spectrum)
                new_entries.append((newkey,  spectrum))
            subscriber.scan_spectral_peaks(self.rf_path, k, spectrum.peak_data[:,0], spectrum.peak_data[:,1])
        for key, spectrum in new_entries:
            self.spectrum_plot.spectra[key] = spectrum
    def OnSelectMux(self, tp):
        """
        called when user clicks mux in spectrum plot
        """
        spectrum = tp.spectrum.spectrum
        if spectrum.k.sat_pos != self.sat.sat_pos or \
           not lnb_matches_spectrum(self.lnb, spectrum):
            txn = wx.GetApp().chdb.rtxn()
            sat = pychdb.sat.find_by_key(txn, spectrum.k.sat_pos)
            txn.abort()
            del txn
            txn = wx.GetApp().devdb.rtxn()
            rf_path = spectrum.k.rf_path
            lnb = pydevdb.lnb.find_by_key(txn, rf_path.lnb)
            txn.abort()
            del txn
        else:
            rf_path = self.rf_path
            lnb = self.lnb
            sat = self.sat
        self.tune_mux_panel.SelectLnb(lnb)
        self.tune_mux_panel.ChangeRfPath(rf_path)
        self.tune_mux_panel.ChangeSat(sat)
        mux = self.tune_mux_panel.mux
        if tp is not None:
            mux.frequency = int(tp.freq*1000)
            mux.symbol_rate=  int(tp.symbol_rate*1000)
            mux.pol = tp.spectrum.spectrum.k.pol
        mux.k.stream_id = -1
        mux.pls_mode = pychdb.fe_pls_mode_t.ROOT
        mux.pls_code = 1
        #to test
        mux.k.sat_pos = sat.sat_pos
        self.tune_mux_panel.muxedit_grid.Reset()
        return mux

    def OnUpdateMux(self, freq, pol, symbol_rate):
        rf_path, lnb = self.rf_path, self.lnb
        sat = self.sat
        mux = pychdb.dvbs_mux.dvbs_mux()
        mux.frequency = int(freq*1000)
        mux.symbol_rate=  int(symbol_rate*1000)
        mux.k.stream_id = -1
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
            self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
            self.EndBlindScan()
            return
        need_si = True
        if type(data) == pyreceiver.scan_mux_end_report_t: #called from scanner
            assert data.mux is not None
            has_lock = data.mux.c.scan_result != pychdb.scan_result_t.NOLOCK
            self.spectrum_plot.set_annot_status(data.spectrum_key, data.peak.peak, data.mux, has_lock)

        elif type(data) == pydevdb.scan_stats.scan_stats: #called from scanner
            scan_stats = data
            self.spectrum_plot.scan_status_text.ShowScanRecord(scan_stats)

            if scan_stats.finished:
                self.is_blindscanning = False
                self.blindscan_end = datetime.datetime.now(tz=tz.tzlocal())
                m, s =  divmod(round((self.blindscan_end -  self.blindscan_start).total_seconds()), 60)
                title = "Blindscan spectrum finished"
                msg = f"Scanned: {scan_stats.finished_muxes} (Locked: {scan_stats.locked_muxes}; " \
                    f"DVB: {scan_stats.si_muxes}; Failed: {scan_stats.failed_muxes}) muxes in {m}min {s}s"
                dtdebug(msg)
                ShowMessage(title, msg)
                self.spectrum_buttons_panel.blindscan_button.SetValue(0)
                self.is_blindscanning = False
                self.EndBlindScan()
                return

        elif type(data) == pyreceiver.signal_info_t:
            self.signal_info = data
            need_si = not self.signal_info.has_no_dvb
            si_done = self.signal_info.has_si_done if need_si else self.signal_info.has_lock
            if self.signal_info.has_fail or si_done:
               if self.done_time is None:
                    self.done_time = datetime.datetime.now(tz=tz.tzlocal())
                    self.mis_scan_time = 20 #minimum time to scan multistreams
               elif datetime.datetime.now(tz=tz.tzlocal()) - self.done_time >= datetime.timedelta(seconds=1): # show result for at least 1 second
                    mux = self.tune_mux_panel.last_tuned_mux
                    dtdebug(f"TUNE DONE mux={mux} lock={self.signal_info.has_lock} fail={self.signal_info.has_fail} done={self.signal_info.has_si_done} no_dvb={self.signal_info.has_no_dvb}")
                    if not self.is_blindscanning:
                        self.spectrum_plot.set_current_annot_status(mux, self.signal_info.driver_mux,
                                                                    self.signal_info.has_lock)
            else:
                mux = self.tune_mux_panel.last_tuned_mux

        if type(data) ==type(self.spectrum_plot.spectrum):
            self.spectrum_plot.show_spectrum(data)
            if data.is_complete:
                if len(self.pols_to_scan) != 0:
                    pol = self.pols_to_scan.pop(0)
                    self.mux_subscriber.subscribe_spectrum_acquisition(self.rf_path, self.lnb, pol,
                                                                       self.start_freq, self.end_freq,
                                                                       self.sat)
                else:
                    self.spectrum_buttons_panel.acquire_spectrum.SetValue(0)
                    self.tune_mux_panel.AbortTune()

    def SetWindowTitle(self, lnb, lnb_connection, sat):
        self.SetTitle(f'Spectrum analysis - {lnb.k} {lnb_connection} {sat}')

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


def show_spectrum_dialog(caller, sat=None, rf_path=None, lnb=None , mux=None):
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
