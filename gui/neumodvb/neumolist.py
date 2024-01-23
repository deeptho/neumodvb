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
from functools import lru_cache
import wx
import wx.grid
import wx.lib.mixins.gridlabelrenderer as glr
import sys
import os
import copy
from collections import namedtuple
import numbers
import datetime
from dateutil import tz
import regex as re
from inspect import currentframe
from packaging import version
from neumodvb.util import dtdebug, dterror, get_text_extent, setup, lastdot, wxpythonversion, wxpythonversion42
from neumodvb import neumodbutils

import pyepgdb
import pydevdb
import pychdb
import pyrecdb
import pystatdb

is_old_wx = version.parse(wx.version().split(' ')[-1]) <= version.parse("3.1")

def lnb_network_str(lnb_networks):
    return '; '.join([ pychdb.sat_pos_str(network.sat_pos) for network in lnb_networks])

def NeumoGetBestSize(self, grid, attr, dc, row, col):
    text = grid.GetCellValue(row, col)
    dc.SetFont(attr.GetFont())
    from wx.lib import wordwrap
    text = wordwrap.wordwrap(text, grid.GetColSize(col), dc, breakLongWords = False)
    w, h = dc.GetMultiLineTextExtent(text)
    return wx.Size(w, h)

#path buggy wxpython code to fix extra space at end
#wx.grid.GridCellAutoWrapStringRenderer.GetBestSize = NeumoGetBestSize

class screen_if_t(object):
    def __init__(self, screen, invert_rows):
        self.screen = screen
        #if True: translate all incoming/outgoing row_numbers to turn screen upside down
        self.invert_rows = invert_rows
        self.editing_record_ = None

    @property
    def editing_record(self):
        return self.editing_record_

    @property
    def has_editing_record(self):
        return self.editing_record_ is not None

    @editing_record.setter
    def editing_record(self, val):
        self.editing_record_ = val

    @property
    def list_size(self):
        return self.screen.list_size

    def record_at_row(self, rowno):
        assert(rowno>=0)
        if rowno == self.list_size and self.has_editing_record:
            return self.editing_record
        if self.list_size == 0:
            return None

        assert(rowno < self.list_size)
        if self.invert_rows:
            rowno = self.list_size -1 - rowno
        return self.screen.record_at_row(rowno)

    def update(self, txn):
        return self.screen.update(txn)

    def set_reference(self, rec):
        rowno = self.screen.set_reference(rec)
        if self.invert_rows:
            if rowno < 0: return rowno
            return self.screen.list_size - 1 - rowno
        else:
            return rowno

def get_linenumber():
    cf = currentframe()
    return cf.f_back.f_lineno
def todo(x):
    print(x)



class NeumoChoiceEditor(wx.grid.GridCellChoiceEditor):
    def __init__(self, col,  *args, **kwds):
        self.col = col
        super().__init__(*args, **kwds)

    def SetSize(self, rect):
        extra = self.Control.GetParent().GetParent().combobox_extra_width
        self.Control.SetSize(rect.x, rect.y, rect.width+extra, rect.height, wx.SIZE_ALLOW_MINUS_ONE)

    def SetChoices(self, choices):
        self.SetParameters(','.join(choices))
        self.Reset()

    def Show(self, *args):
        choices = None
        if self.col.cfn is not None:
            table = self.Control.GetParent().GetParent().GetTable()
            choices= self.col.cfn(table)
        elif self.col.key.endswith('_pos'):
            #recompute each time, because data may have changed
            sats = wx.GetApp().get_sat_poses()
            choices= [pychdb.sat_pos_str(x) for x in sats]
        elif self.col.key.endswith('adapter_mac_address'):
            #recompute each time, because data may have changed
            d = wx.GetApp().get_adapters()
            choices= list(d.keys())
        elif self.col.key.endswith('card_mac_address'):
            #recompute each time, because data may have changed
            d = wx.GetApp().get_cards()
            choices= list(d.keys())
        elif self.col.key.endswith('rf_input'):
            #recompute each time, because data may have changed
            d = wx.GetApp().get_cards_with_rf_in()
            choices= list(d.keys())
        size = self.Control.GetParent().GetParent().GetFont().GetPointSize()
        f = self.Control.GetFont()
        f.SetPointSize(size)
        ret = super().Show(*args)
        self.Control.SetFont(f)
        if choices is not None:
            if is_old_wx:
                self.combobox.Clear()
                self.combobox.Append(choices)
            else:
                wx.CallAfter(self.SetChoices, choices)
        return ret


class NeumoNumberEditor(wx.grid.GridCellNumberEditor):
    def __init__(self, col, *args, **kwds):
        self.col = col
        super().__init__(*args, **kwds)

    def Show(self, *args):
        size = self.Control.GetParent().GetParent().GetFont().GetPointSize()
        f = self.Control.GetFont()
        f.SetPointSize(size)
        ret = super().Show(*args)
        self.Control.SetFont(f)
        return ret

class NeumoFloatEditor(wx.grid.GridCellFloatEditor):
    def __init__(self, col, *args, **kwds):
        self.col = col
        super().__init__(*args, **kwds)

    def Show(self, *args):
        size = self.Control.GetParent().GetParent().GetFont().GetPointSize()
        f = self.Control.GetFont()
        f.SetPointSize(size)
        ret = super().Show(*args)
        self.Control.SetFont(f)
        return ret


class NeumoBoolEditor(wx.grid.GridCellBoolEditor):
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)

    def EndEditOFF(self, row, col, grid, oldval):
        ret=super(wx.grid.GridCellBoolEditor,self).EndEdit(row, col, grid, oldval)
        return ret


class IconRenderer(wx.grid.GridCellRenderer):
    def __init__(self, table,  *args, **kwds):
        super().__init__(*args, **kwds)
        #import images
        self.table = table
        if False:
            #self._bmp =wx.ArtProvider.GetBitmap(wx.ART_TIP, wx.ART_OTHER, (32, 32))
            self._bmp =wx.ArtProvider.GetBitmap(wx.ART_DEL_BOOKMARK, wx.ART_OTHER, (32, 32))
        #self._bmp = images.Smiles.getBitmap()

    @property
    def icons(self):
        """
        get icons and their horizontal positions
        """
        #get items to show and their geometry
        icons_ = self.table.get_icons()
        xspace = self.rect_width
        icons = []
        for i in icons_:
            w = i.GetWidth()
            h = i.GetHeight()
            xspace -= w
            icons.append(dict(icon=i, w=w, h =h))
        if xspace <0:
            #dtdebug(f"not enough room for icons: {xspace}")
            xspace=0
        else:
            xspace = xspace //(len(icons)+1)
        x = xspace
        for icon in icons:
            icon['x'] = x
            x += icon['w'] + xspace
        return icons
    def GetBestSize(self, grid, attr, dc, row, col):
        return wx.Size(5, 5)
    def Draw(self, grid, attr, dc, rect, rowno, colno, isSelected):
        self.rect_width = rect.width
        #this is a list of tuples: (icon, draw_or_not)
        dc.SetPen(wx.Pen("grey",style=wx.TRANSPARENT))
        dc.SetBrush(wx.Brush(attr.GetBackgroundColour(), wx.SOLID))
        dc.DrawRectangle(rect.x,rect.y, rect.width, rect.height)
        #col = self.columns[colno]
        if rowno < 0 or rowno >= self.table.GetNumberRows():
            state = None
            dtdebug(f"ILLEGAL rowno: rowno={rowno} len={self.table.GetNumberRows()}")
        else:
            state = self.table.get_icon_state(rowno, colno)
            for onoff, icon in zip(state, self.icons):
                if onoff:
                    #dc.SetBrush(wx.Brush('red', wx.SOLID))
                    #dc.SetTextForeground('white')
                    x = rect.x + icon['x']
                    y = rect.top + (rect.height - icon['h']) // 2
                    dc.DrawRectangle(x,y, icon['w'], icon['h'])
                    dc.DrawBitmap(icon['icon'], x, y, True)

class MyRendererOFF(wx.grid.GridCellRenderer):

    def __init__(self, table,  *args, **kwds):
        super().__init__(*args, **kwds)
        self.table = table

    def Draw(self, grid, attr, dc, rect, rowno, colno, isSelected):
        dc.SetBrush(wx.Brush(wx.WHITE))
        dc.SetPen(wx.TRANSPARENT_PEN)
        text = f'row={rowno} col={colno}'
        hAlign, vAlign = grid.GetColLabelAlignment()
        #GetTextLines(grid,dc,attr,rect,row,col)
        grid.DrawTextRectangle(dc, text, rect, hAlign, vAlign)


class MyColLabelRenderer(glr.GridLabelRenderer):
    sort_icons = None
    def __init__(self, bgcolour, fgcolour):
        #super().__init__()
        self.bgcolour_ = bgcolour
        self.highlight_color = wx.Colour('#e0ffe0')
        self.fgcolour_ = fgcolour
        if MyColLabelRenderer.sort_icons is None:
             MyColLabelRenderer.sort_icons = [
                 None,
                 wx.ArtProvider.GetBitmap(wx.ART_GO_DOWN, wx.ART_MENU),
                 wx.ArtProvider.GetBitmap(wx.ART_GO_UP, wx.ART_MENU)
            ]
        self.sort_bitmap_width = self.sort_icons[1].Width

    def Draw(self, grid, dc, rect, col):
        if (grid.col_select_mode and col == grid.table.sort_colno) or col in grid.table.filtered_colnos:
            dc.SetBrush(wx.Brush(self.highlight_color ))
            #dc.SetPen(wx.TRANSPARENT_PEN)
            dc.DrawRectangle(rect)
        else:
            dc.SetBrush(wx.Brush(self.bgcolour_ ))
            dc.SetPen(wx.TRANSPARENT_PEN)
            dc.DrawRectangle(rect)
        hAlign, vAlign = grid.GetColLabelAlignment()
        text = grid.GetColLabelValue(col)

        self.DrawBorder(grid, dc, rect)
        self.DrawText(grid, dc, rect, text, hAlign, vAlign)
        if col == grid.table.sort_colno:
            bitmap = self.sort_icons[ grid.table.sort_order]
            if bitmap is not None:
                x = rect.x + rect.Width - bitmap.Width
                y = (rect.Height - bitmap.Height)//2
                dc.DrawBitmap(bitmap, x, y)

        self.DrawBorder(grid, dc, rect)
        self.DrawText(grid, dc, rect, text, hAlign, vAlign)
        if col == grid.table.sort_colno:
            bitmap = self.sort_icons[ grid.table.sort_order]
            if bitmap is not None:
                x = rect.x + rect.Width - bitmap.Width
                y = (rect.Height - bitmap.Height)//2
                dc.DrawBitmap(bitmap, x, y)


class NeumoTableBase(wx.grid.GridTableBase):
    def __init__(self, parent, *args, **kwds):
        super().__init__(*args, **kwds)
        self.row_being_edited = None
        self.new_rows = set()
        self.undo_list = []
        self.unsaved_edit_undo_list = []
        self.filtered_colnos = set()
        self.parent = parent
        self.last_edited_colno = None
        self.do_autosize_rows = False

    @property
    def record_being_edited(self):
        return None if self.screen is None else self.screen.editing_record

    @record_being_edited.setter
    def record_being_edited(self, val):
        if self.screen is not None:
            self.screen.editing_record =val

    def highlight_colour(self, record):
         return None
    def SetReference(self, record):
        assert 0
    def AppendRows(self, numRows=1, updateLabels=True):
        return False

    def DeleteRows(self, pos=0, numRows=1, updateLabels=True):
        return false

    def GetNumberRows(self):
        assert 0

    def GetNumberCols(self):
        return len(self.columns)

    def IsEmptyCell(self, rowno, colno):
        return False

    def GetTypeName(self, rowno, colno):
        if self.coltypes[colno] == bool:
            return wx.grid.GRID_VALUE_BOOL
        elif neumodbutils.is_enum(self.coltypes[colno]):
            return wx.grid.GRID_VALUE_CHOICE
        else:
            return wx.grid.GRID_VALUE_STRING

    def GetRow(self, rowno):
        assert 0

    def CurrentlySelectedRecord(self):
        rowno = self.parent.GetGridCursorRow()
        if rowno < 0:
            return None
        rec = self.GetRow(rowno)
        return rec

    def GetValue(self, rowno, colno):
        return self.GetValue_text(rowno, colno)
        try:
            return self.GetValue_text(rowno, colno)
        except:
            return colno if colno is None else '?????'

    def GetValue_text(self, rowno, colno):
        if rowno <0 or rowno >= self.GetNumberRows():
            dtdebug(f"ILLEGAL rowno: rowno={rowno} len={self.GetNumberRows()}")
            return "???"
        rec = self.GetRow(rowno)
        if rec is None:
            return "???"
        if colno is None:
            return rec
        col = self.columns[colno]
        if col.key == 'icons':
            return ''
        field = neumodbutils.get_subfield(rec, col.key) if col.cfn is None else None
        if self.coltypes[colno] == bool:
            return "1" if field else ""
        txt = str(field) if col.dfn is None else str(col.dfn((rec, field, self)) )
        return txt

    def GetValue_raw(self, rowno, colno):
        if rowno <0 or rowno > self.GetNumberRows():
            dtdebug(f"ILLEGAL rowno: rowno={rowno} len={len(data)}")
        rec = self.GetRow(rowno)
        if colno is None:
            return rec
        col = self.columns[colno]
        if col.key == 'icons':
            return ''
        field = neumodbutils.get_subfield(rec, col.key)
        return field

    def SetValue(self, rowno, colno, val):
        try:
            if rowno == self.row_being_edited :
                rec = self.record_being_edited if self.record_being_edited is not None else self.GetRow(rowno)
            else:
                rec =  self.GetRow(rowno)
        except:
            dterror(f"row {rowno} out of range {self.GetNumberRows()}")
            return
        col = self.columns[colno]
        self.last_edited_colno = colno
        if col.sfn is not None:
            oldrecord = None if rec is None else rec.copy()
            rec =  col.sfn((rec, val, self))
        else:
            key = col.key
            before = None if rec is None else copy.copy(neumodbutils.get_subfield(rec, key))
            coltype = type(neumodbutils.get_subfield(self.record_t(), key))
            newval = None
            try:
                if neumodbutils.is_enum(coltype):
                    newval = neumodbutils.enum_value_for_label(before, val)
                elif coltype == str:
                    newval = val
                elif coltype == bool:
                    newval = True if val =='1' else False
                elif issubclass(coltype, numbers.Integral):
                    if key.endswith('frequency'):
                        newval = int (1000*float(val))
                    elif key.startswith('freq_') or key.startswith('lof_'):
                        newval = int (1000*float(val)) if float(val)>=0 else -1
                    elif key.endswith('symbol_rate'):
                        newval = int (1000*int(val))
                    elif key.endswith("sat_pos") or key.endswith("lnb_pos") or key.endswith("usals_pos"):
                        from neumodvb.util import parse_longitude
                        newval = parse_longitude(val)
                    elif key.endswith("adapter_mac_address"):
                        d = wx.GetApp().get_adapters()
                        newval = d.get(val, None)
                    elif key.endswith("card_mac_address"):
                        d = wx.GetApp().get_cards()
                        newval = d.get(val, None)
                    elif key.endswith("rf_input"):
                        d = wx.GetApp().get_cards_with_rf_in()
                        newval = d.get(val, None)
                    else:
                        newval = int(val)
            except:
                dtdebug(f'ILLEGAL VALUE val={val}')
                newval = None
            if newval is None:
                dtdebug("ILLEGAL new value")
                return
            oldrecord = None if rec is None else rec.copy()
            if key.endswith("rf_input"):
                if rec is None:
                    pass
                elif type(rec) == pydevdb.lnb_connection.lnb_connection:
                    rec.card_mac_address, rec.rf_input = newval
                else:
                    rec.k.card_mac_address, rec.k.rf_input = newval
                    rec.connection_name = "" # will be reset by display code
            else:
                neumodbutils.enum_set_subfield(rec, key, newval)
        # be careful: self.data[rowno].field will operate on a copy of self.data[rowno]
        # we cannot use return value policy reference for vectors (data moves in memory on resize)
        #self.data[rowno] =rec
        self.record_being_edited = rec
        self.row_being_edited = rowno
        self.Backup("edit", rowno, oldrecord, rec)
        self.GetRow.cache_clear()


    def GetColLabelValue(self, colno):
        return self.columns[colno].label

    def Backup(self, operation, rowno, oldrecord, newrecord):
        pass

    def FinalizeUnsavedEdits(self):
        assert False

    def Undo(self):
        """ Undo's the last operation
        """
        pass

    def OnModified(self):
        """
        """
        pass

    def SaveModified(self):
        pass

    def DeleteRows(self, rows):
        assert false

    def new_row(self):
        return None

    def __save_record__(self, txn, record):
        pass

    def __delete_record__(self, txn, record):
        pass

    def set_sort_column(self, colno):
        pass

    def __get_data__(self, use_cache=False):
        """
        retrieve the list
        """
        assert 0
        return None

    def reload(self):
        pass

    def export(self, csvfile):
        num_rows, num_cols = self.GetNumberRows(), self.GetNumberCols()
        import csv
        writer = csv.writer(csvfile, dialect='excel', delimiter='\t')
        row = []
        for colno in range(num_cols):
            row.append(self.GetColLabelValue(colno).replace('-', '').replace('\n',''))
        writer.writerow(row)
        for rowno in range(num_rows):
            row = []
            for colno in range(num_cols):
                row.append(self.GetValue_text(rowno, colno))
            writer.writerow(row)

class NeumoTable(NeumoTableBase):
    #label: to show in header
    #dfn: display function
    CD = namedtuple('ColumnDescriptor',
                    'key label dfn basic example no_combo readonly allow_others noresize sort cfn, sfn')
    CD.__new__.__defaults__=(None, None, None, False, None, False, False, False, False, None, None, None)
    BCK = namedtuple('BackupRecord', 'operation oldrow oldrecord newrecord')
    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
    bool_fn =  lambda x: 0 if False else 1

    all_columns = []

    def __init__(self, parent, basic=False, db_t=None, record_t=None,
                 data_table = None, screen_getter = None,
                 initial_sorted_column = None,
                 sort_order = 0,
                 *args, **kwds):
        """
        """
        super().__init__(parent, *args, **kwds)
        self.record_t = record_t
        self.db_t = db_t
        self.filter_record = self.record_t()
        self.columns = [col for col in self.all_columns if col.basic ] if basic else self.all_columns
        self.data_table = data_table
        self.sort_columns =  [initial_sorted_column] if type(initial_sorted_column) == str \
            else list(initial_sorted_column)
        self.old_sort_columns = []
        self.sort_colno  = None
        self.sort_order = sort_order
        self.cache = dict()
        self.screen = None
        s = self.record_t()
        f = lambda x: None if x == 'icons' else type(neumodbutils.get_subfield(s, x))
        self.coltypes = [ f(col.key) if col.cfn is None else None for col in self.columns]
        if db_t == pyepgdb:
            self.db = wx.GetApp().epgdb
        elif db_t == pychdb:
            self.db = wx.GetApp().chdb
        elif db_t == pydevdb:
            self.db = wx.GetApp().devdb
        elif db_t == pyrecdb:
            self.db = wx.GetApp().recdb
        elif db_t == pystatdb:
            self.db = wx.GetApp().statdb
        else:
            assert(0)
        #self.data = None #start off with an empty table. Table will be populated in OnWindowCreate
        self.red_bg = wx.grid.GridCellAttr()
        self.red_bg.SetBackgroundColour('red')
        self.screen_getter = screen_getter
        self.parent = parent
        if initial_sorted_column:
            if type(initial_sorted_column) == str:
                self.set_initial_sort_column(initial_sorted_column)
            else:
                self.set_initial_sort_column(initial_sorted_column[0])

    def highlight_colour(self, record):
        """
        to be overridden in derived tables
        Should return True or False or None
        None means; leave current colour alone, wich is mainly useful to speed things up
        when no the table does not use highlights
        """
        return None

    def SetReference(self, record):
        return self.screen.set_reference(record)

    def AppendRows(self, numRows=1, updateLabels=True):
        return True

    def DeleteRows(self, pos=0, numRows=1, updateLabels=True):
        return True

    def GetNumberRows(self):
        if self.screen is None:
            return 0
        return self.screen.list_size + len(self.new_rows)


    @lru_cache(maxsize=30) #cache the last row, because multiple columns will lookup same row
    def GetRow(self, rowno):
        if self.record_being_edited is None  or self.row_being_edited != rowno:
            if rowno >= self.screen.list_size:
                return None
            #import time
            ret = self.screen.record_at_row(rowno)
        else:
            ret = self.record_being_edited
            assert self.row_being_edited == rowno
        colour =self.highlight_colour(ret)
        self.parent.ColourRow(rowno, colour=colour)
        return ret

    def get_filter_(self):
        if len(self.filtered_colnos) == 0:
            return None, None
        match_data = self.filter_record
        matchers = pydevdb.field_matcher_t_vector()
        for colno in self.filtered_colnos:
            col = self.columns[colno]
            coltype = type(neumodbutils.get_subfield(self.record_t(), col.key))
            matchtype = pydevdb.field_matcher.match_type.EQ
            if coltype == str:
                matchtype = pydevdb.field_matcher.match_type.CONTAINS
            m = pydevdb.field_matcher.field_matcher(self.data_table.subfield_from_name(col.key),
                                                       matchtype)

            matchers.push_back(m)
        return match_data, matchers


    def Backup(self, operation, rowno, oldrecord, newrecord):
        if operation == 'edit':
            self.unsaved_edit_undo_list.append(self.BCK(operation, rowno, oldrecord, newrecord))
        else:
            self.undo_list.append(self.BCK(operation, rowno, oldrecord, newrecord))

    def PeekUnsavedEdits(self):
        ret = None
        if len(self.unsaved_edit_undo_list) ==0 :
            return None, None, None, None
        first = self.unsaved_edit_undo_list[0]
        last = self.unsaved_edit_undo_list[-1]
        if first.oldrow in self.new_rows:
            operation = 'new'
        else:
            operation = 'replace'

        firstrec = None if operation =='new' else first.oldrecord
        return ret, firstrec, last.newrecord, first.oldrow

    def FinalizeUnsavedEdits(self):
        ret = None
        if len(self.unsaved_edit_undo_list) ==0 :
            return None, None, None, None
        first = self.unsaved_edit_undo_list[0]
        last = self.unsaved_edit_undo_list[-1]
        for x in self.unsaved_edit_undo_list:
            assert x.oldrow == first.oldrow
        if first.oldrow in self.new_rows:
            operation = 'new'
            self.new_rows.remove(first.oldrow)
        else:
            operation = 'replace'

        self.unsaved_edit_undo_list.clear()
        firstrec = None if operation =='new' else first.oldrecord
        self.undo_list.append(self.BCK(operation, first.oldrow, firstrec, last.newrecord))
        self.record_being_edited = None
        self.row_being_edited = None
        return ret, firstrec, last.newrecord, first.oldrow


    def Undo(self):
        """ Undo's the last operation
        """
        if len(self.unsaved_edit_undo_list)>0:
            last = self.unsaved_edit_undo_list.pop()
            #self.data[last.oldrow] = last.oldrecord
            assert last.oldrow == self.row_being_edited
            self.record_being_edited = last.oldrecord
            self.GetRow.cache_clear()
            return 0
        elif len(self.undo_list)>0:
            last = self.undo_list.pop()
            if type(last) is list:
                txn = self.db.wtxn()
                for l in last:
                    assert l.operation == 'delete'
                    self.__save_record__(txn, l.oldrecord)
                changed = self.screen.update(txn)
                txn.commit()
                if changed:
                    self.GetRow.cache_clear()
                    if self.do_autosize_rows:
                        self.parent.AutoSizeRows()
                    self.parent.SelectRecord(l.oldrecord)
                    self.parent.ForceRefresh()
                #self.data = self.__get_data__()
                return len(last)
            elif last.operation == 'replace':
                assert type(last) is not list
                txn = self.db.wtxn()
                self.__save_record__(txn, last.oldrecord)
                changed = self.screen.update(txn)
                txn.commit()
                if changed:
                    self.GetRow.cache_clear()
                    if self.do_autosize_rows:
                        self.parent.AutoSizeRows()
                    self.parent.SelectRecord(last.oldrecord)
                    self.parent.ForceRefresh()
                assert last.oldrow < self.GetNumberRows()
                if self.row_being_edited is not None:
                    assert last.oldrow == self.row_being_edited
                    self.record_being_edited = last.oldrecord
                return 0
            elif last.operation == 'new':
                assert type(last) is not list
                txn = self.db.wtxn()
                self.__delete_record__(txn, last.newrecord)
                changed = self.screen.update(txn)
                txn.commit()
                if changed:
                    self.GetRow.cache_clear()
                    if self.do_autosize_rows:
                        self.parent.AutoSizeRows()
                    self.parent.ForceRefresh()
                assert last.oldrow == self.GetNumberRows()
                #self.data.pop()
                return -1
        return 0

    def OnModified(self):
        """
        called when a screen changes and therefore colours of rows may change
        """
        self.GetRow.cache_clear()
        self.parent.ForceRefresh()
    def on_screen_changed(self):
        """
        Called when a screen for this table has changed
        """

    def SaveModified(self):
        """
        TODO: when record changes affect sort order, list should be reordered
        """
        op, old, new, rowno = self.PeekUnsavedEdits() # merge all unsaved edits into one (they are in the same row)
        if new is None:
            return False
        txn = self.db.wtxn()
        #delete followed by put ensures that we have the correct result also when the primary key is changed
        #Note that this does not handle dependent records of other types
        if old is None:
            dtdebug(f"SAVE NEW: {op}: {new}")
        else:
            dtdebug(f"SAVE MODIFIED: {op}: {old} {new}")
            self.__delete_record__(txn, old)
        saved = self.__save_record__(txn, new)
        error = saved is None
        if error:
            txn.commit()
            self.parent.MakeCellVisible(rowno, 0 if self.last_edited_colno is None else self.last_edited_colno)
            wx.CallAfter(self.parent.SetGridCursor, rowno, 0 if self.last_edited_colno is None else self.last_edited_colno)
        else:
            changed = self.screen.update(txn)
            txn.commit()
            del txn
            idx =0
            self.FinalizeUnsavedEdits() # merge all unsaved edits into one (they are in the same row)
            self.GetRow.cache_clear()
            if changed:
                self.parent.sync_rows()
                if self.do_autosize_rows:
                    self.parent.AutoSizeRows()
                self.parent.SelectRecord(new)
        self.parent.ForceRefresh()
        return True

    def DeleteRows(self, rows):
        txn = None

        tobackup =[]

        dtdebug(f"DELETE: {rows}")
        changed = False;
        for row in rows:
            if row in self.new_rows:
                self.screen.editing_record = None
                changed = True
            else:
                record = self.GetRow(row).copy()
                tobackup.append(self.BCK('delete', row, record, None))
                txn = self.db.wtxn() if txn is None else txn
                self.__delete_record__(txn, record)
        if len(tobackup)>0:
            self.undo_list.append(tobackup)
        txn = self.db.wtxn() if txn is None else txn
        if self.screen.update(txn):
            changed = True
            if self.do_autosize_rows:
                self.parent.AutoSizeRows()
        else:
            pass
        if txn is not None:
            txn.commit()
            del txn

        self.parent.sync_rows()
        if changed:
            self.GetRow.cache_clear()
            rowno = max(min(rows) - 1, 0)
            colno = self.parent.GetGridCursorCol()
            if self.GetNumberRows() > 0:
                dtdebug(f'GotoCell {rowno}/{self.GetNumberRows()} {colno}/{self.GetNumberCols()}')
                self.parent.GoToCell(rowno, colno)
                self.parent.SetGridCursor(rowno, colno)
                wx.CallAfter(self.parent.MakeCellVisible, rowno, colno)
            self.parent.ForceRefresh()

    def new_row(self):
        rowno = self.GetNumberRows()
        self.selected_row = rowno
        rec = self.__new_record__()
        #self.data[n] = rec
        self.record_being_edited = rec
        self.row_being_edited = rowno
        self.new_rows.add(rowno)
        return rec

    def __save_record__(self, txn, record):
        assert false #must be overridden in derived class
        pass

    def __delete_record__(self, txn, record):
        self.db_t.delete_record(txn, record)

    def set_sort_column(self, colno):
        #need_refresh = False
        need_new_data = False
        if 0 <= colno <= len(self.columns):
            if self.columns[colno].sort is not None:
                sort_columns = list(self.columns[colno].sort)
            else:
                sort_columns = [ self.columns[colno].key ]

            self.sort_colno = colno
            key = self.columns[colno].key
            if self.sort_columns[0: len(sort_columns)] == sort_columns:
                self.sort_order = 1 if self.sort_order != 1 else 2
                #need_refresh = (self.sort_order !=2)
            else:
                need_new_data = True
                self.sort_columns = sort_columns + self.sort_columns
                if len(self.sort_columns) > 4:
                    #keep newest 4 items; very newest is in front
                    self.sort_columns= self.sort_columns[:4]
                self.sort_order = 2 if key.endswith('time') else 1
        if need_new_data:
            self.__get_data__()
        self.screen.invert_rows = self.sort_order == 2
        self.GetRow.cache_clear()

    def set_initial_sort_column(self, key):
        #need_refresh = False
        need_new_data = False
        try:
            colno = next ( idx for (idx, col) in enumerate(self.columns) if col.key == key)
            if 0 <= colno <= len(self.columns):
                if self.columns[colno].sort is not None:
                    sort_columns = list(self.columns[colno].sort)
                else:
                    sort_columns = [ self.columns[colno].key ]

                self.sort_colno = colno
                self.sort_order = 2 if key.endswith('time') else 1
        except:
            pass

    def __get_data__(self, use_cache=False):
        """
        retrieve the list
        """
        s = self.screen.screen if self.screen is not None else None
        if use_cache and self.screen is not None:
            return self.screen
        txn = self.db.rtxn()
        #The following code ensures that sorting is performed according to the selected order,
        #but then also - for duplicates - according to previously selected order
        subfield=0
        shift = 24
        for c in self.sort_columns:
            if c == 'icons':
                col = self.get_icon_sort_key()
                v = self.data_table.subfield_from_name(col)
            else:
                v = self.data_table.subfield_from_name(c)
            if v == 0:
                continue
            subfield |= (v<<shift)
            shift -= 8
            if shift < 0 :
                break
        assert subfield !=0
        if self.screen_getter is None:
            self.screen = screen_if_t(self.data_table.screen(txn, sort_order=subfield))
            self.screen.invert_rows = self.sort_order == 2
        else:
            self.screen_getter(txn, subfield)
        txn.abort()
        del txn
        return

    def reload(self):
        dtdebug('reload')
        self.__get_data__()

class NeumoGridBase(wx.grid.Grid, glr.GridWithLabelRenderersMixin):
    default_highlight_colour='#ffcfd4'

    def __init__(self, basic, readonly, table, *args, dark_mode=False, fontscale=1, **kwds):
        super().__init__(*args, **kwds)
        panel = args[0]
        panel.grid = self
        self. num_rows_on_screen = 0
        self.dark_mode = dark_mode
        if self.dark_mode:
            self.SetBackgroundColour(wx.Colour('black'))
            self.SetDefaultCellBackgroundColour(wx.Colour('black'))
            self.SetDefaultCellTextColour(wx.Colour('white'))
        self.col_select_mode = False
        self.infow = None
        self.coloured_rows= set()
        glr.GridWithLabelRenderersMixin.__init__(self)
        self.readonly = readonly
        self.basic  = basic
        self.app = wx.GetApp()
        if basic:
            self.ShowScrollbars(wx.SHOW_SB_NEVER,wx.SHOW_SB_ALWAYS)
        else:
            self.ShowScrollbars(wx.SHOW_SB_NEVER,wx.SHOW_SB_DEFAULT)
        # Then we call CreateGrid to set the dimensions of the grid
        # (100 rows and 10 columns in this example)
        #grid.CreateGrid(100, 10)
        self.created = False
        self.table = table
        self.icon_renderer = IconRenderer(self.table)
        #self.cell_renderer = MyRenderer(self.table)
        self.selected_row = None if self.table.GetNumberRows() == 0 else 0
        self.SetTable(self.table, takeOwnership=True)
        self.font = self.GetFont()
        if fontscale != 1:
            font = self.GetDefaultCellFont()
            font.SetPointSize(int(font.GetPointSize()*fontscale))
            self.SetDefaultCellFont(font)
        self.cellfont = self.GetDefaultCellFont()
        self.header_font = self.GetFont()
        extrasize = 2 if wxpythonversion < wxpythonversion42 else 0
        self.header_font.SetPointSize(self.header_font.GetPointSize() + extrasize)
        self.dc =  wx.ScreenDC()
        self.header_dc =  wx.ScreenDC()
        self.header_font.SetWeight(wx.BOLD)
        self.SetLabelFont(self.header_font)
        self.cellfont.SetPointSize(self.cellfont.GetPointSize()+ extrasize)
        self.SetDefaultCellFont(self.cellfont)
        self.dc.SetFont(self.font) # for estimating label sizes
        self.header_dc.SetFont(self.header_font) # for estimating label sizes
        self.combobox_extra_width , _ = self.header_dc.GetTextExtent(f"XXX")
        self.SetGridLineColour(wx.Colour('lightgray'))
        fg = self.GetLabelTextColour()
        bg = self.GetBackgroundColour()
        if self.dark_mode:
            self.SetLabelTextColour(wx.Colour('white'))
            self.SetLabelBackgroundColour(bg)
        self.my_col_label_renderer = MyColLabelRenderer(bg, fg)

        self.HideRowLabels()
        #self.SetDefaultCellBackgroundColour('yellow')
        self.Bind ( wx.EVT_WINDOW_CREATE, self.OnWindowCreate )
        self.Parent.Bind ( wx.EVT_SHOW, self.OnShowHide )
        self.Bind ( wx.grid.EVT_GRID_SELECT_CELL, self.OnGridCellSelect)
        self.Bind ( wx.grid.EVT_GRID_EDITOR_HIDDEN, self.OnGridEditorHidden)
        if is_old_wx:
            self.Bind ( wx.grid.EVT_GRID_EDITOR_CREATED, self.OnGridEditorCreated)
        self.Bind ( wx.grid.EVT_GRID_EDITOR_SHOWN, self.OnGridEditorShown)
        self.__make_columns__()
        extrasize = 0 if wxpythonversion < wxpythonversion42 else 10
        self.SetColLabelSize(self.GetColLabelSize() + extrasize)
        self.Bind(wx.grid.EVT_GRID_LABEL_RIGHT_CLICK, self.OnColumnMenu)
        self.Bind(wx.grid.EVT_GRID_CELL_RIGHT_CLICK, self.OnColumnMenu)
        self.Bind(wx.grid.EVT_GRID_LABEL_LEFT_CLICK, self.OnToggleSort)

    def InitialRecord(self):
        return None

    def sync_rows(self):
        """
        add or remove rows on screen to reflect vchanges in underlying table
        """
        num_rows  = self.table.GetNumberRows()
        if num_rows == self.num_rows_on_screen:
            return
        self.BeginBatch()
        if num_rows > self.num_rows_on_screen:
            num = num_rows - self.num_rows_on_screen
            #self.AppendRows(num)
            msg = wx.grid.GridTableMessage(self.table,
                                           wx.grid.GRIDTABLE_NOTIFY_ROWS_APPENDED, num)
            self.ProcessTableMessage(msg)
        elif num_rows < self.num_rows_on_screen:
            num = self.num_rows_on_screen - num_rows
            dtdebug(f'removing {num} rows')
            msg = wx.grid.GridTableMessage(self.table,
                                           wx.grid.GRIDTABLE_NOTIFY_ROWS_DELETED,
                                           self.num_rows_on_screen-num, num)
            self.ProcessTableMessage(msg)
        self.EndBatch()
        self.num_rows_on_screen = num_rows

    def OnDone(self, event):
        self.EnableCellEditControl(enable=False)
        pass

    def OnShowHide(self, event):
        dtdebug(f'SHOW/HIDE {self} {event.IsShown()}')
        self.table.SaveModified()

    def OnClose(self):
        self.table.SaveModified()

    def OnGridEditorHidden(self, event):
        dtdebug("OnGridEditorHidden")
        #wx.GetApp().frame.set_accelerators(True)

    def OnGridEditorCreated(self, event):
        dtdebug("OnGridEditorCreated")
        control = event.GetControl()
        if type(control) == wx._core.ComboBox:
            grid = control.GetParent().GetParent()
            col = event.GetCol()
            row = event.GetRow()
            editor = grid.GetCellEditor(row, col)
            #horrible hack https://github.com/wxWidgets/Phoenix/issues/627
            #to prevent wx._core.wxAssertionError: C++ assertion "GetEventHandler() == this" failed at
            #../src/common/wincmn.cpp(477) in ~wxWindowBase(): any pushed event handlers must have been removed

            editor.DecRef()

            #add comboxbox to editor so that our gridcelleditor code can repopulate the list when needed
            editor.combobox = control

    def OnGridEditorShown(self, event):
        dtdebug("OnGridEditorShown")
        #wx.GetApp().frame.set_accelerators(False)

    def ColourRow(self, row, colour=default_highlight_colour): #light red
        attr = wx.grid.GridCellAttr()
        if colour is None:
            colour = self.GetDefaultCellBackgroundColour()
            self.coloured_rows.discard(row)
        else:
            self.coloured_rows.add(row)
        attr.SetBackgroundColour(colour)
        self.SetRowAttr(row, attr)

    def UnColourRow(self, row):
        if row in self.coloured_rows:
            attr = wx.grid.GridCellAttr()
            attr.SetBackgroundColour(self.GetDefaultCellBackgroundColour())
            self.SetRowAttr(row, attr)
            self.coloured_rows.discard(row)

    def UnColourAllRows(self):
        for row in list(self.coloured_rows):
            self.UnColourRow(row)

    def check_screen_update(self, screen):
        if screen is not None:
            txn=self.table.db.rtxn()
            changed = self.table.screen.update(txn)
            txn.abort()
            del txn
            return changed
        return False

    def OnTimer(self, evt):
        changed = False
        if False:
            for screen in self.table.aux_screens:
                changed |= self.check_screen_update(screen)
        changed |= self.check_screen_update(self.table.screen)
        if changed:
            self.table.on_screen_changed()
            self.table.GetRow.cache_clear()
            if self.table.do_autosize_rows:
                self.AutoSizeRows()
            self.OnRefresh(None, None)
            if self.infow is not None:
                self.infow.ShowRecord(self.table, self.table.CurrentlySelectedRecord())
            return changed
        return False

    def OnToggleSort(self, evt):
        sort_column = evt.GetCol()
        editing_record_selected  = self.GridCursorRow in self.table.new_rows
        rec = self.table.CurrentlySelectedRecord()
        self.table.set_sort_column(sort_column)
        if self.table.do_autosize_rows:
            self.AutoSizeRows()
        self.Refresh()
        if editing_record_selected:
            rowno = self.GridCursorRow
            colno = self.GetGridCursorCol()
            self.GoToCell(rowno, colno)
            self.SetGridCursor(rowno, colno)
            wx.CallAfter(self.MakeCellVisible, rowno, colno)
        elif rec is not None:
            self.SelectRecord(rec)

    def __make_column__(self, idx, col, coltype):
        self.SetColLabelAlignment(wx.ALIGN_LEFT, wx.ALIGN_CENTRE)
        attr = wx.grid.GridCellAttr()
        editor = None
        renderer = None
        readonly = self.readonly or col.readonly
        if col.cfn is not None:
            choices = []
            editor = NeumoChoiceEditor(col=col, choices=choices, allowOthers=False)
        elif col.key == 'icons':
            renderer = self.icon_renderer
        elif col.key in ('networks',):
            editor = None
            readonly = True
            renderer = wx.grid.GridCellAutoWrapStringRenderer()
        elif col.key in ('connections',):
            editor = None
            readonly = True
            renderer = wx.grid.GridCellAutoWrapStringRenderer()
        elif col.key.endswith('story'):
            editor = None
            renderer = wx.grid.GridCellAutoWrapStringRenderer()
        elif col.key.endswith('lang'):
            readonly = True
        elif not readonly:
            if neumodbutils.is_enum(coltype):
                #see https://stackoverflow.com/questions/54843888/wxpython-how-to-set-an-editor-for-a-column-of-a-grid
                choices= neumodbutils.enum_labels(coltype)
                editor = NeumoChoiceEditor(col=col, choices=choices, allowOthers=False)
            elif coltype== bool:
                #editor = wx.grid.GridCellEnumEditor(choices="off,on")
                editor = wx.grid.GridCellBoolEditor()
                pass
            elif issubclass(coltype, numbers.Integral):
                if col.key == 'mtime':
                    readonly = True
                elif col.key.endswith('frequency') or col.key.startswith('freq_') or col.key.startswith('lof_'):
                    editor = NeumoFloatEditor(col, precision=3)
                elif col.key.endswith('sat_pos') or col.key.endswith('lnb_pos') \
                     or col.key.endswith('usals_pos') or col.key.endswith('offset_angle'):
                    #Note that the following code line depends on satlist_panel being the first
                    #panel to be initialised (so: on the order of panels in neumoviewer.wxg)
                    choices=[] # will be computed in NeumoChoiceEditor.Show()
                    if col.no_combo:
                        editor = None
                    else:
                        editor = NeumoChoiceEditor(col=col, choices=choices, allowOthers=col.key.endswith('lnb_pos') or col.allow_others)
                elif col.key.endswith('_mac_address'):
                    #Note that the following code line depends on satlist_panel being the first
                    #panel to be initialised (so: on the order of panels in neumoviewer.wxg)
                    if col.no_combo:
                        editor = None
                    else:
                        choices = [] # will be computed in NeumoChoiceEditor.Show()
                        editor = NeumoChoiceEditor(col=col, choices=choices, allowOthers=False)
                elif col.key.endswith('rf_input'):
                    #Note that the following code line depends on satlist_panel being the first
                    #panel to be initialised (so: on the order of panels in neumoviewer.wxg)
                    if col.no_combo:
                        editor = None
                    else:
                        choices = [] # will be computed in NeumoChoiceEditor.Show()
                        editor = NeumoChoiceEditor(col=col, choices=choices, allowOthers=False)
                else:
                    editor = NeumoNumberEditor(col)
            else:
                editor = None
        if readonly:
            attr.SetReadOnly(True)
        if renderer is not None:
            attr.SetRenderer(renderer)
        else:
            pass #attr.SetRenderer(self.cell_renderer)
        if editor is not None:
            attr.SetEditor(editor)
        if editor is not None or renderer is not None or readonly:
            self.SetColAttr(idx, attr)
        return self.textwidth(col, type(editor) == NeumoChoiceEditor)

    def textwidth(self, col, has_combobox_editor):
        example = ""
        if col.example is not None:
            example = col.example
        elif col.key.endswith('pos'):
            example="23.5E" #leave room for huge text editor box
        elif col.key.endswith('_id'):
            example="65535"
        elif col.key.endswith('_pid'):
            example="65535"
        elif col.key.endswith('event_name'):
            example="0"*64
        elif col.key.find('_time')>=0:
            example="1997-12-11 18:43:01"
        if example is not None:
            if True:
                #in the first line below we really should have self.cellfont, but that seems to produce the wrong result
                w,h = get_text_extent(f"{example}", self.cellfont, extra="")
                w1,h1 = get_text_extent(f"{col.label}", self.header_font, extra="")
            else:
                w,h = self.header_dc.GetTextExtent(f"{example}")
                w1,h1 = self.header_dc.GetTextExtent(f"{col.label}")
            extra = self.my_col_label_renderer.sort_bitmap_width + 2 #+2 to compensate for (rounding?) error in wx
            w=max(w, w1+extra)
            return w

        return None

    def __make_columns__(self):
        f = self.GetFont()
        dc = wx.ScreenDC()
        dc.SetFont(f)
        self.gridwidth=0
        for idx, col in enumerate(self.table.columns):
            textwidth = self.__make_column__(idx, col, self.table.coltypes[idx])
            if col.noresize:
                continue
            self.SetColLabelRenderer(idx+0, self.my_col_label_renderer)
            w = textwidth
            if w is not None:
                self.SetColSize(idx, w)
                self.gridwidth += w
            else:
                self.gridwidth += self.GetColSize(idx)

    def __unmake_columns__(self):
        """
        make sure no cell editors remain; otherwise program will crash
        """
        self.DisableCellEditControl()
        for idx, col in enumerate(self.table.columns):
            if True:
                attr = wx.grid.GridCellAttr()
                attr.SetEditor(None)
                self.SetColAttr(idx, attr)

    def OnColumnMenu(self, evt):
        """(col, evt) -> display a popup menu when a column label is
        right clicked"""
        row, col = evt.GetRow(), evt.GetCol()
        if self.table.columns[col].key == 'icons':
            return
        x = self.GetColSize(col)/2
        menu = wx.Menu()
        self.Refresh()
        it = wx.MenuItem(None, text="Filter Column", id=col)
        it.rowno=row
        menu.Append(it)

        it = wx.MenuItem(None, text="Remove Filter", id=col+100)
        it.rowno=row
        menu.Append(it)

        self.Bind(wx.EVT_MENU, self.ShowFilter)

        self.PopupMenu(menu)
        menu.Destroy()
        return

    def ShowFilter(self, event):
        from neumodvb.filter_dialog import FilterDialog
        menu = event.GetEventObject()
        rowno=menu.FindItemById(event.Id).rowno
        remove = event.Id >= 100
        colno = event.Id if event.Id < 100 else (event.Id - 100)
        filter_value = self.table.GetValue_raw(rowno, colno) if not remove and rowno>=0 else None
        if remove:
            self.table.filtered_colnos.remove(colno)
        else:
            dlg = FilterDialog(self, title="Filter Column",
                               basic=True, readonly=False)

            dlg.Prepare(new_filter_colno = colno, filter_value=filter_value)
            dlg.ShowModal()
            dlg.Destroy()
            del dlg
        self.OnRefresh(evt=None)

    def OnRowSelect(self, rowno):
        self.selected_row = rowno
        if self.infow is not None:
            rec = self.table.GetValue(rowno, None)
            self.infow.ShowRecord(self.table, rec)

    def OnGridCellSelect(self, evt):
        has_selection = any(True for _ in self.GetSelectedBlocks())

        if self.table.row_being_edited is not None and evt.GetRow() != self.table.row_being_edited:
            self.OnRowSelect(evt.GetRow())
            #if len(self.table.unsaved_edit_undo_list) > 0:
            wx.CallAfter(self.table.SaveModified)
        elif has_selection and self.table.row_being_edited is None:
            self.OnRowSelect(evt.GetRow())
            return #This can be a multi-row selection event (pressing control key)
        elif evt.GetRow() != self.GetGridCursorRow():
            self.OnRowSelect(evt.GetRow())
        else:
            pass
        wx.CallAfter(self.SelectRow,evt.GetRow())

    def OnWindowCreate(self, evt):
        if self.created:
            #prevent multiple calls of this function (bug in wxPython?)
            evt.Skip()
            return
        self.created = True
        self.Parent.Bind(wx.EVT_SHOW, self.OnShow)
        rec = self.InitialRecord()
        self.SetSelectionMode(wx.grid.Grid.SelectRows)
        dtdebug(f'OnWindowCreate rec_to_select={rec}')
        self.OnRefresh(None, rec_to_select=rec)
        #self.SelectRecord(rec)
        #evt.Skip()

    def SelectRecord(self, rec):
        if rec is not None:
            dtdebug(f"SelectRecord {rec}")
            rowno = self.table.SetReference(rec)
            if rowno >=0:
                colno = self.GetGridCursorCol()
                if colno <0 :
                    colno = 0
                self.GoToCell(rowno, colno)
                self.SetGridCursor(rowno, colno)
                wx.CallAfter(self.MakeCellVisible, rowno, colno)

    def OnShow(self, evt):
        evt.Skip()

    def OnDestroyWindowOFF(self, evt):
        self.__unmake_columns__()
        evt.Skip()

    def OnSort(self, event):
        pass

    def OnNew(self, evt):
        dtdebug(f"new record row={self.GetGridCursorRow()} col={self.GetGridCursorCol()}")
        oldn = self.table.GetNumberRows()
        if self.table.screen.has_editing_record:
            saved = self.table.SaveModified()
            if not saved:
                #this happens when the user has not yet pressed enter on a selected value
                return
        rec= self.table.new_row()
        self.table.screen.editing_record = rec
        n = self.table.GetNumberRows()
        self.table.GetRow.cache_clear()
        self.sync_rows()
        self.MakeCellVisible(self.GetNumberRows()-1, 0)
        wx.CallAfter(self.SetGridCursor, self.GetNumberRows()-1, 0)

    def CmdNew(self, event):
        dtdebug("CmdNew")
        f = wx.GetApp().frame
        if not f.edit_mode:
            f.SetEditMode(True)
        self.OnNew(event)

    def OnDelete(self, evt):
        dtdebug(f"delete record row={self.GetGridCursorRow()} col={self.GetGridCursorCol()}; " \
                "SELECTED ROWS={self.GetSelectedRows()}")
        wx.CallAfter(self.table.DeleteRows, self.GetSelectedRows())

    def CmdDelete(self, event):
        dtdebug("CmdDelete")
        self.OnDelete(event)
        return False

    def OnEditMode(self, evt):
        self.app.frame.ToggleEditMode()

    def OnUndo(self, evt):
        num_rows_added = self.table.Undo()
        self.sync_rows()
        self.ForceRefresh()

    def OnRefresh(self, evt, rec_to_select=None):
        oldlen = self.GetNumberRows() #important: do not use self.table.GetNumberRows()
        self.table.__get_data__()
        if oldlen >0 and rec_to_select is None:
            rec_to_select = self.table.CurrentlySelectedRecord()
        self.sync_rows()
        if self.infow is not None:
            self.infow.ShowRecord(self.table, self.table.CurrentlySelectedRecord())
        self.ForceRefresh()
        if rec_to_select is None:
            rec_to_select = self.table.GetRow(0)
        if rec_to_select is not None:
            self.SelectRecord(rec_to_select)
    def CmdExport(self, evt):
        with wx.FileDialog(self, "Open XYZ file", wildcard="CSV files (*.csv)|*.csv",
                           style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT) as fileDialog:
            if fileDialog.ShowModal() == wx.ID_CANCEL:
                return     # the user changed their mind

            # Proceed loading the file chosen by the user
            pathname = fileDialog.GetPath()
            try:
                with open(pathname, 'w') as file:
                    self.table.export(file)
            except IOError:
                wx.LogError("Cannot open file '%s'." % pathname)

class GridPopup(wx.ComboPopup):
    def __init__(self, grid_class, *args, **kwds):
        self.grid_class = grid_class
        super().__init__(*args, **kwds)
        self.popup_grid = None
        self.popup_panel = None
        self.popup_height = None
    def OnMotion(self, evt):
        pass

    def OnLeftDown(self, evt):
        dtdebug('LEFTDOWN')
        self.value = self.curitem
        self.Dismiss()

    # Create the popup child control.  Return true for success.
    def Create(self, parent):
        #called when user clicks on channel selector
        self.parent = parent
        if  parent.Parent.window_for_computing_width is not None:
            width, height = parent.Parent.window_for_computing_width.GetSize()
            self.popup_height = height//2
        sizer = wx.BoxSizer(wx.HORIZONTAL)
        assert self.popup_grid is None
        assert self.popup_panel is None
        self.popup_panel = wx.Panel(parent, wx.ID_ANY)
        self.popup_grid = self.grid_class(self.popup_panel, wx.ID_ANY, style=wx.LC_HRULES |
                                         wx.LC_NO_HEADER | wx.LC_REPORT | wx.LC_VIRTUAL | wx.LC_VRULES)
        sizer.Add(self.popup_grid, 1, wx.ALL | wx.EXPAND | wx.FIXED_MINSIZE, 0)
        self.popup_panel.SetSizer(sizer)
        self.popup_grid.SetFocus()
        return True

    def IsCreated(self):
        return hasattr(self, popup_panel)

    # Return the widget that is to be used for the popup
    def GetControl(self):
        return self.popup_panel

    def GetStringValue(self):
        row = self.popup_grid.selected_row
        return self.parent.Parent.CurrentGroupText()
        return "Not set"

    # Called immediately after the popup is shown
    def OnPopup(self):
        rec_to_select = self.popup_grid.InitialRecord()
        wx.CallAfter(self.popup_grid.OnRefresh, None, rec_to_select)
        wx.ComboPopup.OnPopup(self)

    # Called when popup is dismissed
    def OnDismiss(self):
        wx.ComboPopup.OnDismiss(self)

    # This is called to custom paint in the combo control itself
    # (ie. not the popup).  Default implementation draws value as
    # string.
    def PaintComboControl(self, dc, rect):
        wx.ComboPopup.PaintComboControl(self, dc, rect)

    # Receives key events from the parent ComboCtrl.  Events not
    # handled should be skipped, as usual.
    def OnComboKeyEvent(self, event):
        wx.ComboPopup.OnComboKeyEvent(self, event)

    # Implement if you need to support special action when user
    # double-clicks on the parent wxComboCtrl.
    def OnComboDoubleClick(self):
        wx.ComboPopup.OnComboDoubleClick(self)

    # Return final size of popup. Called on every popup, just prior to OnPopup.
    # minWidth = preferred minimum width for window
    # prefHeight = preferred height. Only applies if > 0,
    # maxHeight = max height for window, as limited by screen size
    #   and should only be rounded down, if necessary.
    def GetAdjustedSize(self, minWidth, prefHeight, maxHeight):
        extra = wx.SystemSettings.GetMetric(wx.SYS_VSCROLL_X)
        extra *= 10 #wx.SYS_VSCROLL_X seems to much too small (only 2 pixels; gnome3 craziness?)
        w = self.ComboCtrl.popup.popup_grid.gridwidth + extra
        if self.popup_height is None:
            self.popup_height = maxHeight
        return w, self.popup_height #wx.ComboPopup.GetAdjustedSize(self, minWidth, prefHeight, maxHeight)

    # Return true if you want delay the call to Create until the popup
    # is shown for the first time. It is more efficient, but note that
    # it is often more convenient to have the control created
    # immediately.
    # Default returns false.
    def LazyCreate(self):
        return True #wx.ComboPopup.LazyCreate(self)
