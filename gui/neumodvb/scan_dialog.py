#!/usr/bin/python3
# Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
        self.repeat_type_choice.SetValue(self.scan_command.repeat_type, self.scan_command.interval)
        self.max_duration_text.SetValueTime(self.scan_command.max_duration)
        self.catchup_checkbox.SetValue(True)

    def get_scheduling_parameters(self):
        d = self.startdate_datepicker.GetValue()

        start_time = self.startdate_datepicker.GetValue().GetTicks()
        start_time += self.starttime_text.GetSeconds()
        self.scan_command.start_time = start_time
        self.scan_command.repeat_type, self.scan_command.interval = \
            self.repeat_type_choice.GetValue()
        self.scan_command.max_duration = self.max_duration_text.GetSeconds()
        self.scan_command.catchup = self.catchup_checkbox.GetValue()

    def OnDone(self):
        self.get_scheduling_parameters()

class ScanParameters(ScanParameters_):
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)

    def init(self, parent, allow_band_scan, allowed_sat_bands):
        p_t = pychdb.fe_polarisation_t
        self.allow_band_scan = allow_band_scan
        self.allow_band_scan_for_muxes = allow_band_scan_for_muxes
        self.band_scan = allow_band_scan #use spectrum scan by default in this case
        self.tune_options.use_blind_tune = self.band_scan #must use blind tune when spectrum scanning
        self.tune_options.propagate_scan = not self.band_scan
        self.band_scan_options = dict(low_freq=-1, high_freq=-1, pols=[p_t.H, p_t.V])
        if allowed_sat_bands is not None:
            self.allowed_sat_bands_checklistbox.set_allowed_sat_bands(allowed_sat_bands)

    def Prepare(self):
        if self.allow_band_scan:
            pass
        else:
            self.band_scan_save_spectrum_checkbox.Disable()
            self.band_scan_save_spectrum_checkbox.Hide()
            if not self.allow_band_scan_for_muxes:
                self.allowed_sat_bands_panel.Hide()
                self.allowed_pols_panel.Hide()
        self.scan_epg_checkbox.SetValue(self.tune_options.scan_epg)
        self.propagate_scan_checkbox.SetValue(self.tune_options.propagate_scan)
        self.blind_tune_checkbox.SetValue(self.tune_options.use_blind_tune)
        self.may_move_dish_checkbox.SetValue(self.tune_options.may_move_dish)
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
        sat_bands=self.allowed_sat_bands_checklistbox.selected_sat_bands()
        import pydevdb
        import pychdb
        self.tune_options.allowed_dish_ids = pydevdb.int8_t_vector()
        for dish in self.allowed_dishes_checklistbox.selected_dishes():
            self.tune_options.allowed_dish_ids.push_back(dish)
        self.tune_options.allowed_card_mac_addresses = pydevdb.int64_t_vector()
        for c in self.allowed_cards_checklistbox.selected_cards():
            self.tune_options.allowed_card_mac_addresses.push_back(c)
        pols = self.allowed_pols_checklistbox.selected_polarisations()
        sat_bands = sat_bands
        self.tune_options.scan_epg = self.scan_epg_checkbox.GetValue()
        self.tune_options.propagate_scan = self.propagate_scan_checkbox.GetValue()
        self.tune_options.use_blind_tune = self.blind_tune_checkbox.GetValue()
        self.tune_options.may_move_dish = self.may_move_dish_checkbox.GetValue()

        start_freq = self.start_freq_textctrl.GetValue()
        end_freq = self.end_freq_textctrl.GetValue()
        start_freq = start_freq*1000 if start_freq is not None and start_freq != -1 else -1
        end_freq = end_freq*1000 if end_freq is not None and end_freq != -1 else -1
        self.band_scan_options = dict(low_freq=start_freq, high_freq=end_freq, pols=pols, sat_bands=sat_bands)

        return self.tune_options, self.band_scan_options, self.band_scan

class ScanJobDialog_(ScanDialog_):
    def __init__(self, parent, with_schedule, allow_band_scan, allowed_sat_bands, title, *args, **kwds):
        p_t = pychdb.fe_polarisation_t
        super().__init__(parent, *args, **kwds)
        self.with_schedule = with_schedule
        self.scan_parameters_panel.init(parent, allow_band_scan, allowed_sat_bands)
        if with_schedule:
            import pydevdb
            self.scan_command = pydevdb.scan_command.scan_command()
            now = int(datetime.datetime.now(tz=tz.tzlocal()).timestamp())
            self.scan_command.start_time= now
            self.scan_command.interval=3
            self.scan_command.repeat_type = pydevdb.repeat_type_t.HOURLY
            self.scan_command.max_duration= 60*(60+10)
            self.scan_command.catchup = True
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
        self.band_scan = self.scan_type_choice.GetSelection()==0
        if self.with_schedule:
            self.scheduling_parameters_panel.OnDone()
        return self.scan_parameters_panel.OnDone()

class ScanDialog(ScanJobDialog_):
    def __init__(self, parent, allow_band_scan, allowed_sat_bands, title, *args, **kwds):
        with_schedule = True
        super().__init__(parent, with_schedule, allow_band_scan, allowed_sat_bands, title, *args, **kwds)

def service_for_key(service_key):
    txn = wx.GetApp().chdb.rtxn()
    service = pychdb.service.find_by_key(txn, service_key.mux, service_key.service_id)
    txn.abort()
    del txn
    return service


def show_scan_dialog(parent, title='Scan muxes', allow_band_scan=False, allowed_sat_bands=None):
    """
    create a dialog for creating or editing an scan
    record can be of type service, rec, or scan
    in addition, for type service, epg can be set to provide defaults for
    a new scan
    """
    band_scan_options = None
    dlg = ScanDialog(parent.GetParent(), allow_band_scan, allowed_sat_bands, title)
    dlg.Prepare()
    dlg.Fit()
    ret = dlg.ShowModal()
    if ret == wx.ID_OK:
        tune_options, band_scan_options, is_band_scan = dlg.OnDone()
    else:
        dlg.OnCancel()
        tune_options = None
        band_scan_options = None
        band_scan = False
        is_band_scan = False
    dlg.Destroy()
    return tune_options, band_scan_options, is_band_scan
