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
from collections import namedtuple, OrderedDict
import numbers
import datetime
from dateutil import tz
import regex as re

from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, MyColLabelRenderer,  GridPopup, screen_if_t
from neumodvb.neumo_dialogs import ShowMessage
from pyreceiver import set_gtk_window_name, gtk_add_window_style, gtk_remove_window_style
import pychdb

class service_language_screen_t(object):
    def __init__(self, parent):
        self.parent = parent
        self.for_subtitles = self.parent.for_subtitles
        self.data =[]
        if self.for_subtitles:
            if self.parent.service is None:
                self.data = self.parent.mpv_player.subtitle_languages()
            else:
                self.data = self.parent.mpv_player.subtitle_languages() #todo
        else:
            if self.parent.service is None:
                self.data = self.parent.mpv_player.audio_languages()
            else:
                self.data = self.parent.mpv_player.audio_languages() #todo

    @property
    def list_size(self):
        return len(self.data)

    def record_at_row(self, rowno):
        assert(rowno>=0)
        if rowno >= self.list_size:
            assert(rowno == self.list_size)
        assert rowno < self.list_size
        return self.data[rowno]

    def update(self, txn):
        return True

    def set_reference(self, rec):
        for i in range(len(self.data)):
            l = self.data[i]
            if l.lang1 == rec.lang1 and l.lang2 == rec.lang2 and \
               l.lang3 == rec.lang3 and l.position == rec.position:
                return i
        return -1

class LanguageTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    all_columns = \
        [CD(key='lang1',  label='Language', dfn= lambda x: str(x[0]),
            noresize=False, example='Languagexxxx')
        ]

    def __init__(self, parent, mpv_player, for_subtitles=False, basic=False, *args, **kwds):
        initial_sorted_column = 'lang1'
        data_table= pychdb.language_code
        self.service = None
        self.changed = False
        self.for_subtitles = for_subtitles
        self.mpv_player = mpv_player
        super().__init__(*args, parent=parent, basic=basic, db_t=pychdb, data_table = data_table,
                         record_t=pychdb.language_code.language_code,
                         screen_getter = self.screen_getter,
                         initial_sorted_column = initial_sorted_column,
                         **kwds)
    def screen_getter(self, txn, subfield):
        """
        txn is not used; instead we use self.service
        """
        self.screen = screen_if_t(service_language_screen_t(self), self.sort_order==2)

class LanguageGrid(NeumoGridBase):
    def __init__(self, dialog, parent, basic, readonly, *args, dark_mode=False, for_subtitles=False,
                 **kwds):
        self.dark_mode = True
        self.parent = parent
        self.dialog = dialog
        self.dark_mode = dark_mode
        mpv = wx.GetApp().current_mpv_player
        table = LanguageTable(self, mpv, for_subtitles= for_subtitles)
        assert readonly
        assert basic
        super().__init__(basic, readonly, table, *args, dark_mode=dark_mode, fontscale=1.5, **kwds)
        self.sort_order = 0
        self.sort_column = None
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.for_subtitles = for_subtitles
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        gtk_add_window_style(self, 'language_grid')
        set_gtk_window_name(self, 'language_grid')

    def InitialRecord(self):
        if self.for_subtitles:
            ret= self.table.mpv_player.get_current_subtitle_language()
        else:
            ret = self.table.mpv_player.get_current_audio_language()
        dtdebug(f"POPUP initial lang={ret}")
        return ret

    def OnKeyDown(self, evt):
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN  and not evt.HasAnyModifiers():
            self.dialog.selected_row = self.GetGridCursorRow()
            self.dialog.EndModal(wx.ID_OK)
            evt.Skip(False)
        else:
            evt.Skip(True)
