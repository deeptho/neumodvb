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

from neumodvb.preferences_dialog_gui import  PreferencesDialog_
from neumodvb.neumowidgets import DurationTextCtrl, DtIntCtrl
from neumodvb.config import save_config, get_config



class PreferencesDialog(PreferencesDialog_):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        self.parent = parent
        self.app = wx.GetApp()
        self.opts = self.app.receiver.get_options()
        self.popuplate_from_config_obj()
        for e in dir(self):
            w = getattr(self,e)
            if e in self.cfg_vars:
                v = self.cfg_vars[e][1]
            else:
                v= getattr(self.opts, e, None)
                if v is None:
                    continue
            if type(w) == DurationTextCtrl:
                w.SetValueTime(v)
            elif type(w) == wx.TextCtrl:
                w.SetValue(str(v))
            elif type(w) == DtIntCtrl:
                w.SetValue(int(v))
            elif type(w) == wx.CheckBox:
                w.SetValue(int(v))

    def popuplate_from_config_obj(self):
        c = get_config()
        self.configobj = c
        self.cfg_vars = {}
        for sec in  ['PATHS']:
            for k,v in c[sec].items():
                self.cfg_vars[k] = (sec, v)


    def OnSave(self, evt):
        cfg = get_config()
        for e in dir(self):
            if e in ['config_path', 'log4cxx']:
                continue
            w = getattr(self,e)
            if type(w) == DurationTextCtrl:
                v = datetime.timedelta(seconds=w.GetSeconds())
            elif type(w) == wx.TextCtrl:
                v = w.GetValue()
            elif type(w) == DtIntCtrl:
                v = w.GetValue()
            elif type(w) == wx.CheckBox:
                v= w.GetValue()
            else:
                continue
            if e in self.cfg_vars:
                sec=self.cfg_vars[e][0]
                self.configobj[sec][e] = v
            else:
                setattr(self.opts, e, v)
        devdb_wtxn = self.app.receiver.devdb.wtxn()
        self.opts.save_to_db(devdb_wtxn)
        devdb_wtxn.commit()
        save_config(self.configobj)
        self.app.receiver.set_options(self.opts)
        wx.CallAfter(self.Destroy)

def show_preferences_dialog(parent):
    dlg = PreferencesDialog(parent, wx.ID_ANY, "")
    dlg.Show()
    return None
