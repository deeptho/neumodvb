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

from neumodvb.util import setup, lastdot
from neumodvb.neumo_dialogs_gui import  LanguageDialog_
from neumodvb.servicelanguagelist import LanguageGrid, LanguageTable
from pyreceiver import set_gtk_window_name, gtk_add_window_style, gtk_remove_window_style

class LanguageDialog(LanguageDialog_):
    def __init__(self, parent, for_subtitles, basic, readonly, *args, dark_mode=True, **kwds):
        self.parent= parent
        self.basic = True
        self.readonly = True
        self.dark_mode = dark_mode
        self.for_subtitles = for_subtitles
        kwds['title'] = "Subtitle Language" if for_subtitles else "Audio language"
        kwds['style'] = wx.BORDER_NONE
        super().__init__(parent, *args, **kwds)
        self.languagegrid = None
        #self.languagegrid_sizer.Remove(0) #remove empty slot
        gtk_add_window_style(self, 'language_dialog')
        set_gtk_window_name(self, 'language_dialog')
    def Prepare(self, lnbgrid):
        self.languagegrid = LanguageGrid(self, self.parent, self.basic, self.readonly, self.languagelist_panel, \
                                         wx.ID_ANY, size=(-1, -1), dark_mode = self.dark_mode,
                                         for_subtitles = self.for_subtitles)
        self.languagegrid_sizer.Add(self.languagegrid, 1, wx.ALL | wx.EXPAND | wx.FIXED_MINSIZE, 1)
        self.languagegrid.SetFocus()
        #num_cols = self.languagegrid.table.GetNumberCols()
        #width = self.GetClientSize().GetWidth()
        #self.languagegrid.SetColSize(num_cols-1, width*2)
        self.Layout()
        #self.Fit()
    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()
    def OnDone(self, event):
        self.selected_row = self.languagegrid.GetGridCursorRow()
        self.languagegrid_sizer.Remove(1) #remove current grid
        self.languagelist_panel.RemoveChild(self.languagegrid)
        languagegrid = self.languagegrid
        self.languagegrid = None
        wx.CallAfter(languagegrid.Destroy)
        #self.Close()

        event.Skip()

    def OnCancel(self, event):
        self.languagegrid_sizer.Remove(1) #remove current grid
        self.languagelist_panel.RemoveChild(self.languagegrid)
        self.languagegrid.Destroy()
        self.languagegrid = None
        #self.Close()
        event.Skip()
    def GetAdjustedSizeOFF(self, minWidth, prefHeight, maxHeight):
        w = self.languagegrid.GetWidth()
        return w, 200


def show_language_dialog(caller, servicegrid, for_subtitles, dark_mode):
    """
    show dialog as child of main frame.
    optional servicegrid could be used later if language data is stored in service record
    """
    dlg = LanguageDialog(caller, for_subtitles, basic=False, readonly=True, dark_mode=dark_mode)
    title= _("Select Subtitle Language") if for_subtitles else _("Select Audio Language")
    dlg.title.SetLabel(title)
    dlg.Prepare(caller)
    val = dlg.ShowModal()
    row = -1
    if val == wx.ID_OK:
        mpv = wx.GetApp().current_mpv_player
        if for_subtitles:
            mpv.set_subtitle_language(dlg.selected_row)
        else:
            mpv.set_audio_language(dlg.selected_row)
    else:
        pass
    dlg.Destroy()
    return row


def show_audio_language_dialog(caller, dark_mode, servicegrid=None):
    """
    show dialog as child of main frame.
    """
    return show_language_dialog(caller=caller, servicegrid=servicegrid, for_subtitles=False, dark_mode=dark_mode)

def show_subtitle_language_dialog(caller, dark_mode, servicegrid=None):
    """
    show dialog as child of main frame.
    """
    return show_language_dialog(caller=caller, servicegrid=servicegrid, for_subtitles=True, dark_mode=dark_mode)
