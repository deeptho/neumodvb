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
#from collections import namedtuple, OrderedDict
#import numbers
import datetime
from dateutil import tz
import regex as re
from functools import cached_property

from neumodvb.util import setup, lastdot
from neumodvb import neumodbutils
from neumodvb.neumolist import NeumoTable, NeumoGridBase, IconRenderer, screen_if_t, MyColLabelRenderer
from neumodvb.util import setup, lastdot, dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str

#import pychdb
import pydevdb


class StreamTable(NeumoTable):
    CD = NeumoTable.CD
    bool_fn = NeumoTable.bool_fn
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    all_columns = \
        [CD(key='stream_id',  label='id', basic=True, readonly=True),
         CD(key='stream_state',  label='state', basic=True, readonly=False, dfn=lambda x: lastdot(x),
            example='OFF '),
         CD(key='autostart',  label='auto\n start', basic=True, readonly=False, dfn=bool_fn),
         CD(key='preserve',  label='preserve', basic=True, readonly=False, dfn=bool_fn),
         CD(key='content',  label='mux/svc', basic=True, readonly=False,
            example="[101] 10817.500V - BBC One Lon HDxxx"),
         CD(key='dest_host',  label='dest host', basic=True, readonly=False),
         CD(key='dest_port',  label='port', basic=True, readonly=False),
         CD(key='subscription_id',  label='subs', basic=True, readonly=False),
         CD(key='stream_pid',  label='pid', basic=True, readonly=False),
         CD(key='mtime',  label='Modified', dfn=datetime_fn, example=' Wed Jun 15 00:00xxxx')
        ]

    def __init__(self, parent, basic=False, *args, **kwds):
        initial_sorted_column = 'stream_id'
        data_table= pydevdb.stream
        screen_getter = lambda txn, subfield: self.screen_getter_xxx(txn, subfield)

        super().__init__(*args, parent=parent, basic=basic, db_t=pydevdb, data_table = data_table,
                         screen_getter = screen_getter,
                         record_t=pydevdb.stream.stream, initial_sorted_column = initial_sorted_column,
                         **kwds)
        self.do_autosize_rows = True
    def screen_getter_xxx(self, txn, sort_field):
        match_data, matchers = self.get_filter_()
        screen = pydevdb.stream.screen(txn, sort_order=sort_field,
                                   field_matchers=matchers, match_data = match_data)
        self.screen = screen_if_t(screen, self.sort_order==2)

    def __save_record__(self, txn, record):
        dtdebug(f'saving {record.stream_id}')
        pydevdb.put_record(txn, record)
        return record

    def __new_record__(self):
        ret=self.record_t()
        return ret

class StreamGridBase(NeumoGridBase):
    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            self.MoveCursorRight(False)
            evt.Skip(False)
        else:
            evt.Skip(True)

    def __init__(self, basic, readonly, *args, **kwds):
        table = StreamTable(self, basic)
        super().__init__(basic, readonly, table, *args, **kwds)
        self.sort_order = 0
        self.sort_column = None

    def OnShowHide(self, event):
        #Ensure that multiline rows are shown fully
        if event.Show:
            wx.CallAfter(self.AutoSizeRows)
        return super().OnShowHide(event)


class BasicStreamGrid(StreamGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(True, True, *args, **kwds)
        if False:
            self.SetSelectionMode(wx.grid.Grid.GridSelectionModes.GridSelectRows)
        else:
            self.SetSelectionMode(wx.grid.Grid.SelectRows)

class StreamGrid(StreamGridBase):
    def __init__(self, *args, **kwds):
        super().__init__(False, False, *args, **kwds)
