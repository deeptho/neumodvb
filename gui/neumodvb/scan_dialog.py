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

import sys
import os
import copy
import datetime
from dateutil import tz
from enum import Enum


from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb.neumo_dialogs_gui import ScanDialog_, ScanParameters_, SchedulingParameters_
from pydevdb import subscription_type_t
import pychdb
import pydevdb

class SchedulingParameters(SchedulingParameters_):
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
    def init(self, parent, scan_command):
        self.parent = parent
        self.scan_command = scan_command
        self.set_scheduling_parameters()

    def set_scheduling_parameters(self):
        self.start_time = self.scan_command.start_time
        d= wx.DateTime.FromTimeT(self.start_time)
        self.startdate_datepicker.SetValue(d)
        t = datetime.datetime.fromtimestamp(self.start_time, tz=tz.tzlocal())
        self.starttime_text.SetValue(t.strftime('%H:%M'))
        self.run_type_choice.SetValue(self.scan_command.run_type, self.scan_command.interval)
        self.max_duration_text.SetValueTime(self.scan_command.max_duration)
        self.catchup_checkbox.SetValue(True)

    def get_scheduling_parameters(self):
        d = self.startdate_datepicker.GetValue()

        start_time = self.startdate_datepicker.GetValue().GetTicks()
        start_time += self.starttime_text.GetSeconds()
        self.scan_command.start_time = start_time
        self.scan_command.run_type, self.scan_command.interval = \
            self.run_type_choice.GetValue()
        self.scan_command.max_duration = self.max_duration_text.GetSeconds()
        self.scan_command.catchup = self.catchup_checkbox.GetValue()

    def OnDone(self):
        self.get_scheduling_parameters()

class ScanParameters(ScanParameters_):
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)

    def init(self, parent, scan_command, allow_band_scan, allow_band_scan_for_muxes):
        p_t = pychdb.fe_polarisation_t
        self.parent = parent
        self.allow_band_scan = allow_band_scan
        self.allow_band_scan_for_muxes = allow_band_scan_for_muxes
        self.scan_command = scan_command
        if len(scan_command.sats) > 0:
            sat_bands = set()
            for sat in scan_command.sats:
                sat_bands.add(sat.sat_band)
        elif len(scan_command.dvbs_muxes) > 0 and self.allow_band_scan_for_muxes:
            sat_bands = set()
            txn = wx.GetApp().chdb.rtxn()
            sat=pychdb.sat.sat()
            for mux in scan_command.dvbs_muxes:
                if type(mux) != pychdb.dvbs_mux.dvbs_mux:
                    continue
                band, sub_band = pychdb.sat_band_for_freq(mux.frequency)
                sat = pychdb.sat.find_by_key(txn, mux.k.sat_pos, band)
                if sat:
                    sat_bands.add(sat.sat_band)
            txn.abort()
        else:
            sat_bands = None

        self.receiver = wx.GetApp().receiver
        self.scan_command.tune_options.subscription_type = subscription_type_t.BAND_SCAN if allow_band_scan \
            else subscription_type_t.MUX_SCAN
        self.scan_command.tune_options = self.receiver.get_default_tune_options(
            subscription_type = self.scan_command.tune_options.subscription_type)
        self.scan_command.tune_options.use_blind_tune = allow_band_scan #must use blind tune when spectrum scanning
        self.scan_command.tune_options.propagate_scan = not self.band_scan
        if sat_bands is not None:
            self.allowed_sat_bands_checklistbox.set_allowed_sat_bands(list(sat_bands))

    @property
    def band_scan(self):
        return self.scan_command.tune_options.subscription_type != subscription_type_t.MUX_SCAN

    def Prepare(self):
        if self.allow_band_scan:
            pass
        else:
            self.band_scan_save_spectrum_checkbox.Disable()
            self.band_scan_save_spectrum_checkbox.Hide()
            if not self.allow_band_scan_for_muxes:
                self.allowed_sat_bands_panel.Hide()
                self.allowed_pols_panel.Hide()
        scan_epg = self.scan_command.tune_options.scan_target == pydevdb.scan_target_t.SCAN_FULL_AND_EPG
        self.scan_epg_checkbox.SetValue(scan_epg)
        self.propagate_scan_checkbox.SetValue(self.scan_command.tune_options.propagate_scan)
        self.blind_tune_checkbox.SetValue(self.scan_command.tune_options.use_blind_tune)
        self.may_move_dish_checkbox.SetValue(self.scan_command.tune_options.may_move_dish)
        if self.band_scan:
            self.propagate_scan_checkbox.Disable()
            self.blind_tune_checkbox.Disable()

        #self.Layout()
        #wx.CallAfter(self.Layout)

    def CheckCancel(self, event):
        event.Skip()

    def set_scan_type(self, subscription_type):
        self.scan_command.tune_options.subscription_type = subscription_type
        self.scan_command.tune_options = self.receiver.get_default_tune_options(
            subscription_type= self.scan_command.tune_options.subscription_type)
        if self.band_scan:
            self.scan_command.tune_options.use_blind_tune = True
            self.blind_tune_checkbox.SetValue(self.scan_command.tune_options.use_blind_tune)
            self.propagate_scan_checkbox.Disable()
            self.band_scan_save_spectrum_checkbox.Enable()
        else:
            self.propagate_scan_checkbox.Enable()
            self.blind_tune_checkbox.Enable()
            self.band_scan_save_spectrum_checkbox.SetValue(False)
            self.band_scan_save_spectrum_checkbox.Disable()

    def OnDone(self):
        allowed_sat_bands=self.allowed_sat_bands_checklistbox.selected_sat_bands()
        self.scan_command.tune_options.allowed_dish_ids = pydevdb.int8_t_vector()
        for dish in self.allowed_dishes_checklistbox.selected_dishes():
            self.scan_command.tune_options.allowed_dish_ids.push_back(dish)
        self.scan_command.tune_options.allowed_card_mac_addresses = pydevdb.int64_t_vector()
        for c in self.allowed_cards_checklistbox.selected_cards():
            self.scan_command.tune_options.allowed_card_mac_addresses.push_back(c)
        pols = self.allowed_pols_checklistbox.selected_polarisations()
        scan_epg = self.scan_epg_checkbox.GetValue()
        self.scan_command.tune_options.scan_target= \
											pydevdb.scan_target_t.SCAN_FULL_AND_EPG if scan_epg else pydevdb.scan_target_t.SCAN_FULL
        self.scan_command.tune_options.propagate_scan = self.propagate_scan_checkbox.GetValue()
        self.scan_command.tune_options.use_blind_tune = self.blind_tune_checkbox.GetValue()
        self.scan_command.tune_options.may_move_dish = self.may_move_dish_checkbox.GetValue()

        start_freq = self.start_freq_textctrl.GetValue()
        end_freq = self.end_freq_textctrl.GetValue()
        start_freq = start_freq*1000 if start_freq is not None and start_freq != -1 else -1
        end_freq = end_freq*1000 if end_freq is not None and end_freq != -1 else -1
        o = self.scan_command.band_scan_options
        o.start_freq, o.end_freq = start_freq, end_freq
        o.pols = pols
        v = o.pols
        if len(self.scan_command.sats) > 0:
            self.scan_command.sats = [ sat for sat in self.scan_command.sats if sat.sat_band in allowed_sat_bands ]
        if len(self.scan_command.dvbs_muxes) > 0 and self.allow_band_scan_for_muxes:
            self.scan_command.dvbs_muxes = [ mux for mux in self.scan_command.dvbs_muxes \
                           if pychdb.sat_band_for_freq(mux.frequency)[0] in allowed_sat_bands]

class ScanJobDialog_(ScanDialog_):

    def __init__(self, parent, with_schedule, allow_band_scan, allow_band_scan_for_muxes,
                 title, scan_command, *args, **kwds):
        p_t = pychdb.fe_polarisation_t
        super().__init__(parent, *args, **kwds)
        self.scan_command = scan_command
        self.with_schedule = with_schedule
        self.scan_parameters_panel.init(parent, self.scan_command, allow_band_scan, allow_band_scan_for_muxes)
        if with_schedule:
            if scan_command.id < 0: # not inited yet
                now = int(datetime.datetime.now(tz=tz.tzlocal()).timestamp())
                self.scan_command.start_time= now
            self.scheduling_parameters_panel.init(parent, self.scan_command)
        else:
            self.scheduling_parameters_panel.Hide()
        if title is not None:
            self.title_label.SetLabel(title)
            self.SetTitle(title)

    def Prepare(self):
        if self.scan_parameters_panel.allow_band_scan:
            self.scan_type_choice.Enable()
        else:
            self.scan_type_choice.Disable()
            self.scan_type_choice.Hide()
        t = self.scan_command.tune_options.subscription_type

        self.scan_type_choice.SetValue(t)
        self.scan_parameters_panel.Prepare()
        self.SetSizerAndFit(self.main_sizer)

    def get_scan_type_choice(self):
        self.scan_command.tune_options.subscription_type = self.scan_type_choice.GetValue()
        return self.scan_command.tune_options.subscription_type

    def OnScanTypeChoice(self, evt=None):
        subscription_type = self.get_scan_type_choice()
        self.scan_parameters_panel.set_scan_type(subscription_type)
        if self.scan_parameters_panel.allow_band_scan:
            self.scan_type_choice.Enable()
        else:
            self.scan_type_choice.Disable()

    def OnCancel(self):
        dtdebug("OnCancel")

    def OnDone(self):
        self.scan_command.tune_options.subscription_type = self.get_scan_type_choice()
        if self.with_schedule:
            self.scheduling_parameters_panel.OnDone()
        self.scan_parameters_panel.OnDone()
        return self.scan_command

class ScanDialog(ScanJobDialog_):
    def __init__(self, parent, with_schedule, allow_band_scan, allow_band_scan_for_muxes,
                 title, scan_command, *args, **kwds):
        super().__init__(parent, with_schedule, allow_band_scan, allow_band_scan_for_muxes,
                         title, scan_command, *args, **kwds)

def service_for_key(service_key):
    txn = wx.GetApp().chdb.rtxn()
    service = pychdb.service.find_by_key(txn, service_key.mux, service_key.service_id)
    txn.abort()
    del txn
    return service


def show_scan_dialog(parent, title='Scan muxes', with_schedule=False, allow_band_scan=False,
                     allow_band_scan_for_muxes=False,
                     sats = None, dvbs_muxes = None, dvbc_muxes=None, dvbt_muxes=None):
    """
    create a dialog for creating or editing a scan
    allow_band_scan: allow a spectrum scan; if False then disable the related options
    sats: if not None, initial list of satellites to scan; this list can be further reduced
          by the user disabling specific satellite bands
    muxes: it not None, scan these specific muxes
    exactly one of muxes or sats should be not None

    """
    band_scan_options = None
    scan_command = pydevdb.scan_command.scan_command()
    if sats is not None:
        scan_command.sats = sats
    if dvbs_muxes is not None:
        scan_command.dvbs_muxes = dvbs_muxes
    if dvbc_muxes is not None:
        scan_command.dvbc_muxes = dvbc_muxes
    if dvbt_muxes is not None:
        scan_command.dvbt_muxes = dvbt_muxes


    dlg = ScanDialog(parent.GetParent(), with_schedule, allow_band_scan, allow_band_scan_for_muxes,
                     title, scan_command = scan_command)
    dlg.Prepare()
    dlg.Fit()
    ret = dlg.ShowModal()
    if ret == wx.ID_OK:
        scan_command = dlg.OnDone()
    else:
        dlg.OnCancel()
        scan_command = None
    dlg.Destroy()
    return scan_command
