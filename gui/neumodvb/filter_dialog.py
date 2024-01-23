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
import wx.lib.mixins.gridlabelrenderer as glr
import sys
import os
import copy

from functools import lru_cache
import neumodvb.neumodbutils as neumodbutils
from collections import namedtuple
from neumodvb.util import setup, lastdot
from neumodvb.neumolist import NeumoTableBase, NeumoTable,NeumoGridBase
from neumodvb.neumo_dialogs_gui import  FilterDialog_
from neumodvb.dvbs_muxlist import DvbsMuxTable
import pychdb


class NeumoFilterTable(NeumoTableBase):

    def __init__(self, parent, parent_table, basic=False, db_t=None,
                 data_table = None, screen_getter = None,
                 initial_sorted_column = None, new_filter_colno = None, filter_value=None,
                 sort_order = 1,
                 *args, **kwds):
        """
        """
        super().__init__(parent, *args, **kwds)

        self.all_columns = parent_table.all_columns
        self.db_t = db_t
        self.parent_table = parent_table
        self.new_filtered_colnos = parent_table.filtered_colnos.copy()
        self.new_filtered_colnos.add(new_filter_colno)
        self.columns = [parent_table.columns[colno]._replace(readonly=False) for colno in self.new_filtered_colnos]
        s = parent_table.filter_record
        self.record_t = type(s)
        f = lambda x: None if x == 'icons' else type(neumodbutils.get_subfield(s, x))
        self.coltypes = [ f(col.key) for col in self.columns]
        self.sort_colno = None
        self.screen = None
        self.filter_record = self.parent_table.filter_record.copy()
        if filter_value is not None:
            neumodbutils.enum_set_subfield(self.filter_record,
                                           parent_table.columns[new_filter_colno].key, filter_value)
            self.GetRow.cache_clear()

    def SetReference(self, record):
        return 0

    def GetNumberRows(self):
        return 1
    def GetNumberCols(self):
        return len(self.columns)

    def DeleteCols(self, pos, numCols, updateLabels):
        return True
    def remove_cols(self, pos, numCols):
        for colno in range(pos, pos+numCols):
            key = self.columns[colno].key
            for parent_colno, col in enumerate(self.parent_table.columns):
                if col.key == key:
                    self.new_filtered_colnos.remove(parent_colno)
                    self.columns.pop(colno)
                    msg = wx.grid.GridTableMessage(self, wx.grid.GRIDTABLE_NOTIFY_COLS_DELETED, colno, 1)
                    self.parent.ProcessTableMessage(msg)
                    return True
        return False
    @lru_cache(maxsize=0) #cache the last row, because multiple columns will lookup same row
    def GetRow(self, rowno):
        #assert rowno == 0
        if self.record_being_edited is None  or self.row_being_edited != rowno:
            ret = self.filter_record
        else:
            ret = self.record_being_edited
            assert self.row_being_edited == rowno
        return ret


    def OnModified(self):
        """
        called when a screen changes and therefore colours of rows may change
        """
        self.parent.ForceRefresh()

    def SaveModified(self):
        self.parent.ForceRefresh()

    def __get_data__(self, use_cache=False):
        """
        retrieve the list
        """
        return self.filter_record

    def reload(self):
        dtdebug('reload')
        self.__get_data__()

    def set_sort_column(self, colno):
        need_refresh = False
        self.sort_columns = [ self.columns[colno].key ]
        self.sort_colno = colno
        self.sort_order = 1
    def get_icon_state(self, rowno, colno):
        return (False, False)
    def get_icons(self):
        return []


class FilterGrid(NeumoGridBase):

    def __init__(self, parent_table, new_filter_colno, filter_value, parent, *args, **kwds):
        """
        parent_table: table whose filter_reord we are editing
        new_filter_key: new column to add to filter
        filter_value: default value for new filter
        """
        basic = False
        readonly = False
        table = NeumoFilterTable(self, parent_table=parent_table, filter_value=filter_value,
                                 new_filter_colno=new_filter_colno)
        super().__init__(basic, readonly, table, parent, *args, **kwds)
        self.col_select_mode = True
        self.sort_order = 0
        self.sort_column = None
        self.parent_table = parent_table
        self.mux = None #currently selected mux
        h = wx.GetApp().receiver.browse_history
        self.allow_all = False
        self.num_rows_on_screen = 1
        self.GoToCell(0, len(self.table.columns)-1)
        self.SetGridCursor(0, len(self.table.columns)-1)
        self.MakeCellVisible(0, len(self.table.columns)-1)
        self.SetFocus()
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)

    def OnKeyDown(self, evt):
        """
        After editing, move cursor right
        """
        keycode = evt.GetKeyCode()
        modifiers = evt.GetModifiers()
        is_ctrl = (modifiers & wx.ACCEL_CTRL)
        if keycode == wx.WXK_RETURN and not evt.HasAnyModifiers():
            dialog = self.GetGrandParent()
            wx.PostEvent(dialog.Done, wx.CommandEvent(wx.EVT_BUTTON.typeId, dialog.Done.GetId()))
            evt.Skip(False)
        else:
            evt.Skip(True)

    def remove_col(self, colno):
        self.table.remove_cols(colno, 1)


    def CurrentGroupText(self):
        return ""



class FilterDialog(FilterDialog_):
    def __init__(self, parent_grid, basic, readonly, *args, **kwds):
        self.basic = basic
        self.parent_grid = parent_grid
        self.parent_table = parent_grid.table
        self.readonly = readonly
        super().__init__(parent_grid.GetParent(), *args, **kwds)
        self.grid = None

    def Prepare(self, new_filter_colno, filter_value):

        self.grid = FilterGrid(self.parent_table, new_filter_colno, filter_value,
                               self.filter_panel, wx.ID_ANY, size=(1, 1))
        self.grid_sizer.Add(self.grid, 1, wx.ALL | wx.EXPAND | wx.FIXED_MINSIZE, 1)
        self.Layout()

    def CheckCancel(self, event):
        if event.GetKeyCode() in [wx.WXK_ESCAPE, wx.WXK_CONTROL_C]:
            self.OnTimer(None, ret=wx.ID_CANCEL)
            event.Skip(False)
        event.Skip()

    def OnNew(self, event):
        event.Skip()

    def CmdNew(self, event):
        dtdebug("CmdNew")
        f = wx.GetApp().frame
        if not f.parent.edit_mode:
            f.SetEditMode(True)
        self.OnNew(event)

    def OnDelete(self, evt):
        remove_all = evt.Id
        if remove_all:
            self.parent_table.filtered_colnos.clear()
            self.EndModal(wx.ID_CANCEL)
        else:
            pass
            colno = self.grid.GetGridCursorCol()
            self.grid.remove_col(colno)
        evt.Skip()

    def CmdDelete(self, event):
        dtdebug("CmdDelete")
        self.OnDelete(event)
        return False

    def OnDone(self, event):
        changed = (self.parent_table.filtered_colnos != self.grid.table.new_filtered_colnos)
        changed |= (self.parent_table.filter_record != self.grid.table.filter_record)
        self.parent_table.filtered_colnos = self.grid.table.new_filtered_colnos
        self.parent_table.filter_record = self.grid.table.filter_record
        self.grid.OnDone(event)
        self.grid_sizer.Remove(1) #remove current grid
        self.filter_panel.RemoveChild(self.grid)
        grid = self.grid
        self.grid = None
        wx.CallAfter(grid.Destroy)
        if changed:
            self.parent_table.GetRow.cache_clear()
        if event is not None:
            event.Skip()

    def OnCancel(self, event):
        self.grid_sizer.Remove(1) #remove current grid
        self.filter_panel.RemoveChild(self.grid)
        self.grid.Destroy()
        self.grid = None
        event.Skip()
