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
from neumodvb.neumo_dialogs_gui import ScanDialog_
from pyreceiver import subscription_type_t
from pydevdb import tune_options_t
import pychdb

class ScanDialog(ScanDialog_):
    def __init__(self, parent, allow_band_scan, allowed_sat_bands, title, *args, **kwds):
        p_t = pychdb.fe_polarisation_t
        super().__init__(parent, *args, **kwds)
        self.receiver = wx.GetApp().receiver
        self.tune_options = self.receiver.get_default_tune_options(
            subscription_type= subscription_type_t.SPECTRUM_BAND_SCAN if allow_band_scan \
            else subscription_type_t.MUX_SCAN)
        self.allow_band_scan = allow_band_scan
        self.band_scan = allow_band_scan #use spectrum scan by default in this case
        self.tune_options.use_blind_tune = self.band_scan #must use blind tune when spectrum scanning
        self.tune_options.propagate_scan = not self.band_scan
        self.band_scan_options = dict(low_freq=-1, high_freq=-1, pols=[p_t.H, p_t.V])
        if title is not None:
            self.title_label.SetLabel(title)
            self.SetTitle(title)
        if allowed_sat_bands is not None:
            self.allowed_sat_bands_checklistbox.set_allowed_sat_bands(allowed_sat_bands)
    def OnScanTypeChoice(self, evt):
        self.band_scan = self.scan_type_choice.GetSelection()==0
        if self.allow_band_scan:
            self.scan_type_choice.Enable()
        else:
            self.scan_type_choice.Disable()
        if self.band_scan:
            self.tune_options.use_blind_tune = True
            self.blind_tune_checkbox.SetValue(self.tune_options.use_blind_tune)
            self.propagate_scan_checkbox.Disable()
            self.band_scan_save_spectrum_checkbox.Enable()
            #self.blind_tune_checkbox.Disable()
            #self.allowed_sat_bands_checklistbox.Enable()
        else:
            self.propagate_scan_checkbox.Enable()
            self.blind_tune_checkbox.Enable()
            self.band_scan_save_spectrum_checkbox.SetValue(False)
            self.band_scan_save_spectrum_checkbox.Disable()
            #self.allowed_sat_bands_checklistbox.ForceAll()
            #self.allowed_sat_bands_checklistbox.Disable()

    def Prepare(self):
        if self.allow_band_scan:
            self.scan_type_choice.Enable()
        else:
            self.scan_type_choice.Disable()
            self.scan_type_choice.Hide()
            self.band_scan_save_spectrum_checkbox.Disable()
            self.band_scan_save_spectrum_checkbox.Hide()
            self.allowed_sat_bands_checklistbox.Hide()
            self.allowed_pols_checklistbox.Hide()
        self.scan_type_choice.SetSelection(0 if self.band_scan else 1)
        self.scan_epg_checkbox.SetValue(self.tune_options.scan_epg)
        self.propagate_scan_checkbox.SetValue(self.tune_options.propagate_scan)
        self.blind_tune_checkbox.SetValue(self.tune_options.use_blind_tune)
        self.may_move_dish_checkbox.SetValue(self.tune_options.may_move_dish)
        if self.band_scan:
            self.propagate_scan_checkbox.Disable()
            self.blind_tune_checkbox.Disable()
        self.SetSizerAndFit(self.main_sizer)
        #self.Layout()
        #wx.CallAfter(self.Layout)

    def CheckCancel(self, event):
        event.Skip()

    def OnCancel(self):
        dtdebug("OnCancel")

    def OnDone(self):
        pols=self.allowed_pols_checklistbox.selected_polarisations()
        sat_bands=self.allowed_sat_bands_checklistbox.selected_sat_bands()
        import pydevdb
        self.tune_options.allowed_dish_ids = pydevdb.int8_t_vector()
        for dish in self.allowed_dishes_checklistbox.selected_dishes():
            self.tune_options.allowed_dish_ids.push_back(dish)
        self.tune_options.allowed_card_mac_addresses = pydevdb.int64_t_vector()
        for c in self.allowed_cards_checklistbox.selected_cards():
            self.tune_options.allowed_card_mac_addresses.push_back(c)
        self.allowed_pols = pols
        self.allowed_sat_bands = sat_bands
        self.band_scan = self.scan_type_choice.GetSelection()==0
        self.tune_options.scan_epg = self.scan_epg_checkbox.GetValue()
        self.tune_options.propagate_scan = self.propagate_scan_checkbox.GetValue()
        self.tune_options.use_blind_tune = self.blind_tune_checkbox.GetValue()
        self.tune_options.may_move_dish = self.may_move_dish_checkbox.GetValue()
        self.band_scan_options = dict(low_freq=10700000, high_freq=12750000, pols=pols,
                                      sat_bands=sat_bands, )

        return self.tune_options, self.band_scan_options if self.band_scan else None

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
        tune_options, band_scan_options = dlg.OnDone()
    else:
        dlg.OnCancel()
        tune_options = None
        band_scan_options = None
        band_scan = False
    dlg.Destroy()
    return tune_options, band_scan_options
