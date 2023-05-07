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
from wx.richtext import RichTextCtrl
from enum import Enum
from functools import cached_property, lru_cache
import datetime
from dateutil import tz

from neumodvb.util import setup, get_text_extent, wxpythonversion, wxpythonversion42
from neumodvb.neumo_dialogs import ShowMessage
from neumodvb.chepglist import content_types
from neumodvb.util import dtdebug, dterror, lastdot
from neumodvb.neumodbutils import enum_to_str
from neumodvb.record_dialog import show_record_dialog

import pychdb
import pyepgdb
import pyrecdb
from pyreceiver import set_gtk_window_name, gtk_add_window_style, gtk_remove_window_style
from pyreceiver import get_object as get_object_

def get_object(evt):
    s = evt.GetExtraLong()
    return get_object_(s)

class RowType(Enum):
    GRIDEPG = 1
    SERVICE_OR_CHANNEL = 2
    CHG = 3
    SAT = 4
    REC = 5

class GridData(object):
    def __init__(self, parent, rowtype):
        self.parent = parent
        self.ls = wx.GetApp().live_service_screen
        self.rowtype = rowtype
        if rowtype == RowType.GRIDEPG:
            self.OnSelectRow = self.OnSelectServiceOrChannel
        elif rowtype == RowType.SERVICE_OR_CHANNEL:
            self.OnSelectRow = self.OnSelectServiceOrChannel
        elif rowtype == RowType.SAT:
            self.OnSelectRow = self.OnSelectSat
        elif rowtype == RowType.CHG:
            self.OnSelectRow = self.OnSelectChg
        elif rowtype == RowType.REC:
            self.ls = wx.GetApp().live_recording_screen
            self.OnSelectRow = self.OnSelectRecording
        else:
            assert 0
    @property
    def row_screen(self):
        rowtype = self.rowtype
        if rowtype == RowType.GRIDEPG:
            return self.ls.screen
        elif rowtype == RowType.SERVICE_OR_CHANNEL:
            return self.ls.screen
        elif rowtype == RowType.SAT:
            return self.ls.sat_screen
        elif rowtype == RowType.CHG:
            return self.ls.chg_screen
        elif rowtype == RowType.REC:
            return self.ls.screen
        else:
            return None

    def GetNumberRows(self):
        ret = 0 if self.row_screen is None else self.row_screen.list_size
        return ret

    @lru_cache(maxsize=1) #cache the last row, because multiple columns will lookup same row
    def GetRecordAtRow(self, rowno):
        if rowno < 0 or rowno >= self.row_screen.list_size:
            return None
        ret = self.row_screen.record_at_row(rowno)
        return ret

    def OnSelectServiceOrChannel(self, service):
        self.ls.SelectServiceOrChannel(service)

    def OnSelectSat(self, service):
        self.ls.SelectSat(service)

    def OnSelectChg(self, service):
        self.ls.SelectChg(service)

    def OnSelectRecording(self, recording):
        self.ls.SelectRecording(recording)


class GridEpgData(GridData):

    def __init__(self, parent):
        super().__init__(parent, rowtype=RowType.SERVICE_OR_CHANNEL)
        self.set_start(datetime.datetime.now(tz=tz.tzlocal()))
        self.epg_sort_column = 'k.start_time'
        self.epg_sort_order = pyepgdb.epg_record.subfield_from_name(self.epg_sort_column) << 24
        self.epgdb = wx.GetApp().epgdb
        self.epg_screens = None

    def set_start(self, now):
        if type(now) != datetime.datetime:
            now = datetime.datetime.fromtimestamp(now, tz=tz.tzlocal())
        now = now.replace(minute=now.minute - now.minute%30, second=0)
        self.start_time = now # start of current hour
        self.start_time_unixepoch = int(self.start_time.timestamp())
        self.end_time = now + datetime.timedelta(minutes=180)
        self.end_time_unixepoch = int(self.end_time.timestamp())
        self.periods = [now + datetime.timedelta(minutes=minutes) for minutes in range(0, 180, 30)]

    @lru_cache(maxsize=1) #cache the last row, because multiple columns will lookup same row
    def GetEpgScreenAtRow(self, rowno):
        service = self.GetRecordAtRow(rowno)
        if service is None:
            return None
        if self.epg_screens is None:
            self.epg_screens = pyepgdb.gridepg_screen(self.start_time_unixepoch,
                                                      self.parent.num_rows_on_screen, self.epg_sort_order)
        key = service.k if type(service) == pychdb.service.service else service.service
        epg_screen = self.epg_screens.epg_screen_for_service(key)
        if epg_screen is None:
            txn = self.epgdb.rtxn()
            epg_screen = self.epg_screens.add_service(txn, key)
            txn.abort()
            del txn
        return epg_screen

    def remove_epg_data_for_channels(self, chidx_start, chidx_end):
        for idx in range(chidx_start, chidx_end):
            service = self.GetRecordAtRow(idx)
            if service is not None:
                key = service.k if type(service) == pychdb.service.service else service.k.service
                dtdebug(f'python remove epg service  {key}')
                self.epg_screens.remove_service(key)
        self.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version

    def GetEpgRecord(self, ch_idx, epg_idx):
        if ch_idx < 0 or ch_idx >= self.row_screen.list_size:
            return None
        epg_screen = self.GetEpgScreenAtRow(ch_idx)
        if epg_idx < 0 or epg_idx >= epg_screen.list_size:
            return None
        return epg_screen.record_at_row(epg_idx)
    def first_epg_record(self, ch_idx, start_time):
        """
        idx = channel index
        """
        epg_screen = self.GetEpgScreenAtRow(ch_idx)
        if epg_screen is None:
            dtdebug(f'ch_idx={ch_idx}')
            return -1
        start_time = start_time
        for epg_idx in range(0, epg_screen.list_size):
            epg = epg_screen.record_at_row(epg_idx)
            if epg.end_time> start_time:
                return epg_idx
        return -1


class EpgCellData(object):
    def __init__(self, row, epg, start_time, colno):
        self.is_ch = False #channel or epg?
        self.epg = epg # This is an empty (gray) epg entry
        self.row = row
        self.colno = colno # column number of epg record
        self.start_time = start_time

class ChannelCellData(object):
    def __init__(self, row):
        self.is_ch = True #channel or epg?
        self.epg = None # This is an empty (gray) epg entry
        self.row = row
        assert row is not None
        self.colno = -1 # column number of epg record


class EpgCell(wx.Panel):
    """
    Text control with an icon used to show record status
    """

    def __init__(self, parent, id,  content, bgcolour=None, fgcolour=None, scheduled=False, size=None):
        super().__init__(parent, id,  size=size)

        self.label = wx.TextCtrl(self, wx.ID_ANY, content, style= wx.TE_READONLY)

        if True:
            b= wx.GetApp().bitmaps
            bitmap = b.rec_scheduled_bitmap if scheduled else b.rec_inprogress_bitmap
            self.staticbmp = wx.StaticBitmap(self, -1, bitmap)
            self.sizer =  wx.FlexGridSizer(1, 2, 0, 0)
            self.sizer.Add(self.staticbmp, 0 , 0 , 0)
            self.sizer.Add(self.label, 1, wx.EXPAND, 0)
            self.sizer.AddGrowableRow(0)
            self.sizer.AddGrowableCol(1)
        else:
            self.sizer =  wx.FlexGridSizer(1, 1, 0, 0)
            self.sizer.Add(self.label, 1, wx.EXPAND, 0)
            self.sizer.AddGrowableRow(0)
            self.sizer.AddGrowableCol(0)
        self.SetSizer(self.sizer)

    def SetForegroundColour(self, fgcolour):
        self.label.SetForegroundColour(fgcolour)

    def SetBackgroundColour(self, bgcolour):
        self.label.SetBackgroundColour(bgcolour)
        super().SetBackgroundColour(bgcolour)

    def SetFocus(self):
        dtdebug('CALL SetFocus')
        self.label.SetFocus()

    @property
    def data(self):
        return self.label.data

    @data.setter
    def data(self, val):
        self.label.data = val

    @property
    def is_ch(self):
        assert 0
        return self.label.is_ch

    @is_ch.setter
    def is_ch(self, val):
        assert 0
        self.label.is_ch = val

    @property
    def epg(self):
        assert 0
        return self.label.epg

    @epg.setter
    def epg(self, val):
        assert 0
        self.label.epg = val

    @property
    def row(self):
        assert 0
        return self.label.row

    @epg.setter
    def row(self, val):
        assert 0
        self.label.row = val

    @property
    def colno(self):
        assert 0
        return self.label.colno

    @colno.setter
    def colno(self, val):
        assert 0
        self.label.colno = val


class GridRow(object):
    """
    One row on screen, composed of a service/chgm/chg or sat
    Rows may be empty if the number of channels is too small
    rowno is the number of the row on the screen, startin at rowno=0
    """
    def __init__(self, parent, rowno, *args, **kwds):
        self.grid = parent
        self.rowno = rowno
        self.data = self.grid.data
        self.ch_cell= self.add_ch_cell(span=(1, self.grid.chwidth),
                                            bgcolour=self.grid.black,
                                            fgcolour=self.grid.white)
        self.current_cell = None
        self.last_focused_cell = None

    @property
    def grid_focus_time(self):
        return self.grid.focus_time

    @grid_focus_time.setter
    def grid_focus_time(self, val):
        self.grid.focus_time =val

    def add_ch_cell(self, span, fgcolour, bgcolour, ref_for_tab_order=None):
        cell = wx.TextCtrl(self.grid, wx.ID_ANY, "", style= wx.TE_READONLY, size=(-1, self.grid.row_height))
        self.grid.add_cell(pos=(self.rowno+self.grid.ch_start_row, 1), span=span, cell=cell,
                           fgcolour=fgcolour, bgcolour=bgcolour, ref_for_tab_order=ref_for_tab_order)#, expand=wx.FIXED_MINSIZE)
        cell.data = ChannelCellData(self)
        self.last_focused_cell = cell
        return cell

    def SetFocus(self):
        cell = self.ch_cell if self.current_cell is None else self.current_cell
        try:
            dtdebug('CALL SetFocus')
            cell.SetFocus()
        except:
            dtdebug('exception ignored')
        dtdebug(f'SET FOCUS ON row={cell.data.row.rowno} {cell.GetParent()}')
        self.grid.last_focused_cell = cell

    def RowRecordText(self):
        """
        overide in derived class
        """
        rec = self.row_record
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M")
        if rec is None:
            return f""
        if type(rec) == pychdb.chgm.chgm:
            return f'{rec.chgm_order: <6} {rec.name}'
        elif type(rec) == pychdb.service.service:
            return f'{rec.ch_order: <6} {rec.name}'
        elif type(rec) == pychdb.chg.chg:
            return f'{rec.name}'
        elif type(rec) == pychdb.sat.sat:
            sat_pos = pychdb.sat_pos_str(rec.sat_pos)
            return f'{sat_pos: <6} {rec.name}'
        elif type(rec) == pyrecdb.rec.rec:
            if rec.real_time_start !=0:
                return f'{dt(rec.real_time_start)} {rec.epg.event_name}'
            else:
                return f'{dt(rec.epg.k.start_time)} {rec.epg.event_name}'
        return "?????"

    def update(self):
        """Redraw the current entry for this row after something changes (e.g., top_idx)
        """
        self.row_record = self.data.GetRecordAtRow(self.grid.top_idx + self.rowno)
        if self.row_record is None:
            pass
        self.ch_cell.SetValue(self.RowRecordText())

    def remove(self):
        if getattr(self.ch_cell, 'label', self.ch_cell) == self.grid.last_focused_cell:
            self.grid.last_focused_cell = None
        self.grid.gbs.Detach(self.ch_cell)
        self.ch_cell.Destroy()
        self.ch_cell = None

    def move_to_row(self, rowno):
        """
        make this service appear on a different row, which must be empty
        rowno=0 means first service
        """
        pos =  self.grid.gbs.GetItemPosition(self.ch_cell)
        pos[0] = rowno + self.grid.ch_start_row
        self.grid.gbs.SetItemPosition(self.ch_cell, pos)
        self.rowno = rowno
        return self

    def focus_currentOFF(self):
        """
        set the focus on the cell which is closest in time
        """
        if self.last_focused_cell.data.is_ch: # we are positioned in a channel column
            dtdebug('CALL SetFocus')
            self.ch_cell.SetFocus()
            return True
        return False

class ChEpgGridRow(GridRow):
    """
    One row on screen, composed of a service/channel/chgm/chg or sat on the left,
    and zero or more epg records on the rigght
    Rows may be empty if the number of channels is too small
    rowno is the number of the row on the screen, startin at rowno=0
    """
    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.epg_cells=[]

    def add_epg_cell(self, span, colno, ref_for_tab_order=None, fgcolour=None, bgcolour=None, epg=None,
                     start_time=None):
        col_offset = self.grid.epg_start_col
        has_icon = True
        content = "" if epg is None  else epg.event_name
        assert not (epg is None and start_time is None)
        use_icon = False
        if epg is not None:
            t=pyepgdb.rec_status_t
            scheduled = epg.rec_status==t.SCHEDULED
            recording =  epg.rec_status in (t.IN_PROGRESS, t.FINISHING)
            use_icon= scheduled or recording
        if use_icon:
            cell = EpgCell(self.grid, wx.ID_ANY, content, scheduled=scheduled, size=(-1, self.grid.row_height))
        else:
            cell = wx.TextCtrl(self.grid, wx.ID_ANY, content, style= wx.TE_READONLY, size=(-1, self.grid.row_height))
        self.grid.add_cell(pos=(self.rowno+self.grid.ch_start_row, col_offset+colno), cell=cell, span=span,
                                  fgcolour=fgcolour, bgcolour=bgcolour,
                                  ref_for_tab_order=ref_for_tab_order)
        cell.data = EpgCellData(self, epg, start_time, colno)
        return cell

    def SetFocus(self):
        dtdebug('CALL SetFocus')
        cell = self.ch_cell if self.current_cell is None else self.current_cell
        dtdebug(f'SET FOCUS ON row={cell.data.row.rowno} {cell.GetParent()}')
        cell.SetFocus()
        self.grid.last_focused_cell = cell

    def update(self):
        """Redraw the channel and epg for this row after something changes (e.g., top_idx)
        """
        self.remove_epg()
        super().update()
        self.update_epg()

    def remove_epg(self):
        """remove all epg cells"""
        for cell in self.epg_cells:
            if getattr(cell, 'label', cell) == self.grid.last_focused_cell:
                self.grid.last_focused_cell = None
            self.grid.gbs.Detach(cell)
            cell.Destroy()
        self.epg_cells=[]

    def remove(self):
        super().remove()
        self.remove_epg()

    def move_to_row(self, rowno):
        """
        make this service appear on a different row, which must be empty
        rowno=0 means first service
        """
        super().move_to_row(rowno)
        for cell in self.epg_cells:
            pos =  self.grid.gbs.GetItemPosition(cell)
            pos[0] = rowno + self.grid.ch_start_row
            self.grid.gbs.SetItemPosition(cell, pos)
        return self

    def is_leftmost_epg_cell(self, w):
        """
        check if window w is the left most epg cell
        """
        return w.data.colno == 0

    def neighboring_cell(self, w, left_neighbor):
        """
        return swindow w
        """
        if w == self.ch_cell:
            if left_neighbor:
                return None
            else:
                return self.epg_cells[0]
        for idx, cell in enumerate(self.epg_cells):
            if w.data.colno == cell.data.colno:
                if left_neighbor and  idx > 0:
                    return self.epg_cells[idx-1]
                elif idx < len(self.epg_cells)-1:
                    return self.epg_cells[idx+1]
                else:
                    return None
        return None
        #return w == self.epg_cells[0]

    def is_rightmost_epg_cell(self, w):
        """
        check if window w is the right most epg cell
        """
        assert len(self.epg_cells) >0
        return  w.data.colno == self.epg_cells[-1].data.colno

    def rightmost_start_time(self):
        start_cell= self.epg_cells[-1]
        #if an epg line has at least two entries, then one of them must have epg
        if start_cell.data.epg is not None:
            return start_cell.data.epg.k.start_time
        elif len(self.epg_cells)>1:
            start_cell = self.epg_cells[-2]
            if hasattr(start_cell, 'epg'):
                return start_cell.data.epg.k.start_time
        return int(self.data.start_time_unixepoch)

    def update_epg(self):
        """
        rowno = index of channel in channel list
        """
        colour=self.grid.green

        self.epg_cells = []
        start_time = self.data.start_time_unixepoch
        ch_idx = self.grid.top_idx+self.rowno
        epg_idx = self.data.first_epg_record(ch_idx, start_time)
        if epg_idx <0:
            pass #can happen when only a few records fit on screen
        ref = self.ch_cell
        covered = dict()

        while epg_idx>=0:
            epg_record = self.data.GetEpgRecord(ch_idx, epg_idx)
            if epg_record is None:
                epg_idx = -1
                break
            epg_idx +=1
            start_col = min(max(0, (epg_record.k.start_time - start_time)//self.grid.epg_duration_width),
                            self.grid.num_cols)
            end_col = min(max(0, (epg_record.end_time - start_time)//self.grid.epg_duration_width),
                          self.grid.num_cols)
            assert end_col >= start_col
            for idx in range (start_col, end_col+1):
                oldepg = covered.get(idx, None)
                if oldepg is None or oldepg.k.anonymous: #prioritize non-anonymous
                    covered[idx] = epg_record
        last_idx = 0
        end_idx = 0
        ret = []
        last_epg = None
        for idx, epg in covered.items():
            end_idx = idx
            if epg is last_epg:
                continue
            if idx > last_idx:
                ret.append((last_idx, idx, last_epg))
            last_idx = idx
            last_epg  = epg
        if last_idx < end_idx:
            ret.append((last_idx, end_idx, last_epg))
        if end_idx < self.grid.num_cols:
            ret.append((end_idx, self.grid.num_cols, None))

        last_col , last_time = 0, start_time
        for start_col, end_col, epg in ret:
            span = end_col - start_col
            if epg is None: # we need to draw an empty cell
                cell = self.add_epg_cell(start_time= last_time, span=(1,span), colno = start_col,
                                         bgcolour=self.grid.gray, ref_for_tab_order=ref)
                self.epg_cells.append(cell)
                ref=cell
            else:
                span  = end_col - start_col
                #place an actual epg cell
                cell=self.add_epg_cell(epg=epg, span=(1,span), colno = start_col,
                                   bgcolour=self.grid.epg_colour, fgcolour = self.grid.white,
                                   ref_for_tab_order = ref)
                self.epg_cells.append(cell)
                ref=cell
                last_time = epg.end_time


    def focus_current(self):
        """
        set the focus on the cell which is closest in time
        """
        selected = None
        selected_start_time = None
        if self.grid_focus_time is None:
            return False
        for cell in self.epg_cells:
            start_time = cell.data.start_time if cell.data.epg is None else cell.data.epg.k.start_time
            if selected is None:
                selected = cell
                selected_start_time = start_time
                continue
            #pick the record with the nearest start time
            assert start_time is not None
            if abs(self.grid.focus_time - selected_start_time) > abs(start_time - self.grid.focus_time):
                selected = cell
                selected_start_time = start_time
            if start_time >= self.grid.focus_time:
                break
        if selected is None:
            selected = self.epg_cells[0]
        dtdebug('CALL SetFocus')
        selected.SetFocus()
        return True

class GroupSelectPanel(wx.Panel):
    def __init__(self, parent, *args, **kwds):
        self.controller = parent
        super().__init__(parent, *args, **kwds)
        dtdebug('GroupSelectPanel')
        self.header_font = self.GetFont()
        self.header_font.SetPointSize(self.header_font.GetPointSize()+2)
        self.create_entries()
        self.group_select_in_progress = False
        self.grouptype_idx = self.initial_group_idx()
        self.sorttype_idx = self.initial_sort_idx()
        self.last_rowtype = None
        self.created =False
        self.create()

    @property
    def sort_selected(self):
        if wx.Window.FindFocus() == self.sorttype_text:
            return True
        return False

    @sort_selected.setter
    def sort_selected(self, val):
        now = wx.Window.FindFocus() != self.grouptype_text
        if now == val:
            return
        if val:
            dtdebug('CALL SetFocus')
            self.sorttype_text.SetFocus()
        else:
            dtdebug('CALL SetFocus')
            self.grouptype_text.SetFocus()

    @property
    def num_grouptypes(self):
        return len(self.grouptypes)

    @property
    def num_grouptypes(self):
        return len(self.grouptypes)

    @property
    def num_sorttypes(self):
        return len(self.sorttypes)

    def OnEnter(self, evt):
        dtdebug("OnEnter")

    def Navigate(self, focused_widget, modifier, key):
        if key in (wx.WXK_DOWN, wx.WXK_UP):
            self.controller.grid_panel.set_active()
            dtdebug('CALL SetFocus')
            self.controller.grid_panel.SetFocus()
            return True
        elif key in (wx.WXK_LEFT, wx.WXK_RIGHT):
            is_ctrl = (modifier & wx.ACCEL_CTRL)
            #if is_ctrl:
            #    return False
            delta = -1 if key == wx.WXK_LEFT else 1
            if wx.Window.FindFocus() == self.grouptype_text:
                if is_ctrl:
                    pass
                    dtdebug('CALL SetFocus')
                    self.sorttype_text.SetFocus()
                else:
                    self.grouptype_idx = min(max(self.grouptype_idx + delta,0), self.num_grouptypes -1)
                    self.display_grouptype()
            else:
                if is_ctrl:
                    pass
                    dtdebug('CALL SetFocus')
                    self.grouptype_text.SetFocus()
                else:
                    self.sorttype_idx = min(max(self.sorttype_idx + delta,0), self.num_sorttypes -1)
                    self.display_sorttype()
            return True
        if key == wx.WXK_RETURN:
            #TODO: also implement aborting group slect by pressing escape key
            if wx.Window.FindFocus() == self.grouptype_text:
                self.activate_group()
            else:
                self.activate_sort()
            return True
        return False

    def initial_group_idx(self):
        for idx, gt in enumerate(self.grouptypes):
            if gt[-1] ==  self.list_filter_type:
                return idx
        return 0

    def initial_sort_idx(self):
        cols =tuple(self.ls.get_sort_columns())
        for idx, gt in enumerate(self.sorttypes):
            if cols == gt[1]:
                return idx
        return 0

    def display_grouptype(self):
        idx = self.grouptype_idx
        t = pychdb.list_filter_type_t
        ls = self.controller.app.live_service_screen
        self.grouptype_idx = idx
        txt, cmd, record_type = self.grouptypes[self.grouptype_idx]
        self.grouptype_text.SetValue(txt)
        w,h = get_text_extent(txt, self.header_font, extra="**", compensate=False)
        w = max(w, self.grouptype_text_size[0])
        h = max(h+10, self.grouptype_text_size[1])
        self.grouptype_text.SetMinSize((w, h))
        wx.CallAfter(self.grouptype_text.Refresh)

    def display_sorttype(self):
        idx = self.sorttype_idx
        self.sorttype_idx = idx
        if  self.sorttype_idx >= self.num_sorttypes:
            self.sorttype_idx = 0
        txt, sort_keys = self.sorttypes[self.sorttype_idx]
        self.sorttype_text.SetValue(txt)
        w,h = self.sorttype_text_size
        self.sorttype_text.SetMinSize((w, h))
        wx.CallAfter(self.sorttype_text.Refresh)

    def activate_group(self):
        """
        Caled when user presses return to confirm selection
        """
        idx = self.grouptype_idx
        self.last_rowtype = self.controller.grid_panel.rowtype
        txt, rowtype, record_type = self.grouptypes[self.grouptype_idx]
        t = pychdb.list_filter_type_t
        self.group_select_in_progress = True
        self.OnSelectGroup()
        wx.CallAfter(self.grouptype_text.Refresh)

    def activate_sort(self):
        idx = self.sorttype_idx
        txt, sort_keys = self.sorttypes[self.sorttype_idx]

        dtdebug(f'set_sort_columns {sort_keys}')
        self.ls.set_sort_columns(sort_keys)
        self.display_sorttype()
        dtdebug(f'activate_sort: sorttype={sort_keys}')
        wx.CallAfter(self.controller.show_grid_panel, rowtype= self.last_rowtype, focus_it=True, recreate=True)
        wx.CallAfter(self.sorttype_text.Refresh)

    def OnSelectGroup(self):
        self.grouptype_idx = self.initial_group_idx()
        self.group_select_in_progress = False
        self.display_grouptype()
        dtdebug(f'OnSelectGroup: rowtype={self.last_rowtype}')
        #return to main list
        wx.CallAfter(self.controller.show_grid_panel, rowtype= self.last_rowtype, focus_it=True, recreate=True)
        self.last_rowtype = None

    def OnFocus(self, event):
        if self.group_select_in_progress:
            dtdebug('CALL SetFocus')
            self.controller.grid_panel.SetFocus()
            return
        self.controller.set_active(self)
        w = wx.Window.FindFocus()
        if w == self.grouptype_text:
            self.sort_selected = False
            self.sorttype_idx = self.initial_sort_idx()
            pass
        elif w == self.sorttype_text:
            if self.group_select_in_progress:
                self.group_select_in_progress = False
                wx.CallAfter(self.controller.show_grid_panel, rowtype= self.last_rowtype, focus_it=True, recreate=True)
                dtdebug('CALL SetFocus')
                wx.CallAfter(w.SetFocus)
            self.sort_selected = True
            self.grouptype_idx = self.initial_group_idx()
            pass
        else:
            assert 0
        self.display_grouptype()
        self.display_sorttype()
        wx.CallAfter(self.Refresh)
        return

    def set_active(self):
        dtdebug("group_select_in_progress enabled")
        self.display_grouptype()
        self.display_sorttype()

    def set_inactive(self):
        self.sorttype_idx = self.initial_sort_idx()
        if not self.group_select_in_progress:
            self.grouptype_idx = self.initial_group_idx()
            dtdebug('INACTIVE')
        else:
            dtdebug('INACTIVE2')
        self.display_grouptype()
        self.display_sorttype()

    def create(self):
        assert not self.created
        self.created = True
        self.top_sizer =  wx.FlexGridSizer(1, 4, 0, 0)
        grouptype_text = wx.TextCtrl(self, wx.ID_ANY, "", style= wx.TE_READONLY)
        grouptype_text.SetForegroundColour(wx.WHITE)
        grouptype_text.SetFont(self.header_font)

        sorttype_text = wx.TextCtrl(self, wx.ID_ANY, "", style= wx.TE_READONLY)
        sorttype_text.SetForegroundColour('yellow')
        sorttype_text.SetFont(self.header_font)

        self.top_sizer.Add(grouptype_text, proportion=0, flag=wx.EXPAND, border=0)
        self.top_sizer.Add((10,10), 1, 0, border=0)
        self.top_sizer.Add(sorttype_text, 0, wx.EXPAND, 0)
        self.top_sizer.Add((10,10), 1, 0, 0)
        self.grouptype_text = grouptype_text
        self.sorttype_text = sorttype_text
        self.SetSizerAndFit(self.top_sizer)
        self.Bind(wx.EVT_CHILD_FOCUS, self.OnFocus)
        self.display_grouptype()
        self.display_sorttype()


class SatBouquetGroupSelectPanel(GroupSelectPanel):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)

    def create_entries(self):
        h = self.controller.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        self.list_filter_type = h.h.list_filter_type
        self.ls = self.controller.app.live_service_screen
        self.grouptypes = [
            (_("All Services"), None, t.ALL_SERVICES),
            (_("Select Sat"), RowType.SAT, t.SAT_SERVICES),
            #(_("All Channels"), None,  t.ALL_CHANNELS),
            (_("Select Bouquet"), RowType.CHG, t.BOUQUET_CHANNELS)
        ]
        self.sorttypes_service = (
            (_("Sorted by channel number"), ('ch_order',)),
            (_("Sorted by name"), ('name',)),
            (_("Sorted by modification time"), ('mtime',)),
            (_("Sorted by sat/mux"), ('frequency', 'pol', 'k.service_id'))
        )
        self.sorttypes_chgm = (
            (_("Sort by channel number"), ('chgm_order',)),
            (_("Sort by name"), ('name',)),
            (_("Sort by modification time"), ('mtime',))
        )
        w, h = 0, 0
        for g in self.grouptypes:
            w1,h1 = get_text_extent(g[0], self.header_font, compensate=True)
            w, h =max(w, w1), max(h, h1)
        self.grouptype_text_size = (w, h)
        w, h = 0, 0
        for g in list(self.sorttypes_service)+ list(self.sorttypes_chgm) :
            w1,h1 = get_text_extent(g[0], self.header_font, compensate=True)
            w, h =max(w, w1), max(h, h1)
        self.sorttype_text_size = (w, h)

    @property
    def sorttypes(self):
        t = pychdb.list_filter_type_t
        txt, cmd, record_type = self.grouptypes[self.grouptype_idx]
        if record_type in (t.SAT_SERVICES, t.ALL_SERVICES):
            return self.sorttypes_service
        else:
            return self.sorttypes_chgm

    def display_grouptype(self):
        idx = self.grouptype_idx
        t = pychdb.list_filter_type_t
        self.grouptype_idx = idx
        txt, cmd, record_type = self.grouptypes[self.grouptype_idx]
        if record_type == t.SAT_SERVICES:
            txt = txt if self.group_select_in_progress or self.grouptype_text.HasFocus() else str(self.ls.filter_sat)
        elif record_type == t.BOUQUET_CHANNELS:
            txt =  txt if self.group_select_in_progress or self.grouptype_text.HasFocus()  else  str(self.ls.filter_chg)
        self.grouptype_text.SetValue(txt)
        w,h = get_text_extent(txt, self.header_font, compensate=True)
        w = max(w, self.grouptype_text_size[0])
        h = max(h, self.grouptype_text_size[1])
        self.grouptype_text.SetMinSize((w, h))
        wx.CallAfter(self.grouptype_text.Refresh)

    def activate_group(self):
        idx = self.grouptype_idx
        self.last_rowtype = self.controller.grid_panel.rowtype
        if not self.group_select_in_progress and self.last_rowtype not in (RowType.GRIDEPG, RowType.SERVICE_OR_CHANNEL):
            dtdebug(f'{self.last_rowtype} {self.controller.grid_panel}')
        assert self.group_select_in_progress or self.last_rowtype in (RowType.GRIDEPG, RowType.SERVICE_OR_CHANNEL)
        txt, rowtype, record_type = self.grouptypes[self.grouptype_idx]
        t = pychdb.list_filter_type_t
        self.group_select_in_progress = True
        if record_type == t.ALL_SERVICES:
            self.ls.set_sat_filter(None)
            self.OnSelectGroup()
        elif record_type == t.ALL_CHANNELS:
            self.ls.set_chg_filter(None)
            self.OnSelectGroup()
        else: #show a group select list
            dtdebug(f'activate_group: rowtype={rowtype}')
            wx.CallAfter(self.controller.show_grid_panel, rowtype= rowtype, focus_it=True, recreate=True)
            #wx.CallAfter(self.SetFocus)
        wx.CallAfter(self.grouptype_text.Refresh)

class RecGroupSelectPanel(GroupSelectPanel):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)

    def create_entries(self):
        h = self.controller.app.receiver.rec_browse_history
        t = pyrecdb.list_filter_type_t
        self.list_filter_type = h.h.list_filter_type
        self.ls = self.controller.app.live_recording_screen
        self.grouptypes = [
            (_("All recordings"), None, t.ALL_RECORDINGS),
            (_("Scheduled recordings"), None, t.SCHEDULED_RECORDINGS),
            (_("Completed recordings"), None, t.COMPLETED_RECORDINGS),
            (_("In progress recordings"), None, t.IN_PROGRESS_RECORDINGS),
            (_("Failed recordings"), None, t.FAILED_RECORDINGS)
        ]
        self.sorttypes = (
            (_("Sorted by date"), ('real_time_start',)),
            (_("Sorted by name"), ('epg.event_name',)),
            (_("Sorted by service"), ('service.name',))
        )
        w, h = 0, 0
        for g in self.grouptypes:
            w1,h1 = get_text_extent(g[0], self.header_font, compensate=True)
            w, h =max(w, w1), max(h, h1)
        self.grouptype_text_size = (w, h)
        w, h = 0, 0
        for g in list(self.sorttypes):
            w1,h1 = get_text_extent(g[0], self.header_font, compensate=True)
            w, h =max(w, w1), max(h, h1)
        self.sorttype_text_size = (w, h)

    def OnSelectGroup(self):
        filter_type = self.grouptypes[self.grouptype_idx][2]
        self.ls.set_recordings_filter(filter_type)
        self.group_select_in_progress = False
        self.display_grouptype()
        dtdebug(f'OnSelectGroup: rowtype={self.last_rowtype}')
        #return to main list
        wx.CallAfter(self.controller.show_grid_panel, rowtype= self.last_rowtype, focus_it=True, recreate=True)
        self.last_rowtype = None


class MosaicPanel(wx.Panel):
    def __init__(self, controller, *args, **kwds):
        self.controller = controller
        super().__init__(*args, **kwds)
        self.Bind(wx.EVT_CHILD_FOCUS, self.OnFocus)
        self.focus_idx = 0

    @property
    def current_mpv_player(self):
        return self.mpv_players[0 if self.focus_idx < 0 else self.focus_idx]

    def ChangeVolume(self, step):
        self.current_mpv_player.change_audio_volume(step)

    def OnFocus(self, evt):
        self.controller.update_active(self)
        w = evt.GetWindow()
        idx = self.glcanvases.index(w)
        self.focus_idx = idx
        assert idx >=0
        self.Focus()

    def AcceptsFocus(self):
        return True

    def AcceptsFocusRecursively(self):
        return True

    def set_active(self):
        self.Focus()

    def set_inactive(self):
        self.SetBackgroundColour(wx.Colour(0,0,0,0))

    def set_noborder(self):
        self.SetBackgroundColour(wx.Colour(0,0,0,0))

    def create(self):
        gtk_add_window_style(self, 'mosaic_background')
        self.mpv_players=[]
        self.glcanvases=[]
        self.AddMpvPlayer()
        self.SetSize((1200,800))
        wx.CallAfter(self.Refresh)

    @property
    def num_entries(self):
        return len(self.mpv_players)

    @property
    def current_mpv(self):
        return None if self.focus_idx is None else \
            self.mpv_players[0 if self.focus_idx <0 else self.focus_idx]

    @property
    def num_cols(self):
        if  1 < self.num_entries <=2:
            return 1
        elif  1 < self.num_entries <=4:
            return 2
        elif self.num_entries >4:
            return 3
        return 1

    def Focus(self):
        self.SetBackgroundColour('yellow')
        self.HighlightMpvPlayer(self.focus_idx)

    def Navigate(self, focused_widget, modifier, key):
        is_ctrl = (modifier & wx.ACCEL_CTRL)
        if False:
            if is_ctrl:
                self.set_inactive()
                return False
        if not is_ctrl:
            if key == wx.WXK_LEFT:
                if self.controller.hidden:
                    self.controller.CmdJumpBack()
            elif key == wx.WXK_RIGHT:
                if self.controller.hidden:
                    self.controller.CmdJumpForward()
            return True
        if key in (wx.WXK_LEFT, wx.WXK_RIGHT):
            focus_idx = self.focus_idx
            focus_idx += -1 if key == wx.WXK_LEFT else 1
            if focus_idx < 0 or focus_idx >= len(self.glcanvases):
                return False
            self.focus_idx = max( min(focus_idx, len(self.glcanvases) -1), 0)
            assert self.focus_idx < len(self.glcanvases)
            self.HighlightMpvPlayer(self.focus_idx)
            return True
        elif key in (wx.WXK_UP, wx.WXK_DOWN):
            self.focus_idx += -self.num_cols if key == wx.WXK_UP else self.num_cols
            self.focus_idx = max(min(self.focus_idx, len(self.glcanvases) -1),0)
            self.HighlightMpvPlayer(self.focus_idx)
            return True
        self.SetBackgroundColour(wx.Colour(0,0,0,0))
        return False

    def OnClose(self, evt):
        for player in self.mpv_players:
            player.stop_play()
        while len(self.glcanvases)>0:
            glcanvas = self.glcanvases[-1]
            self.RemoveMpvPlayer(glcanvas, force=True)
        evt.Skip()
    def OnSubscriberCallback(self, evt):
        data = get_object(evt)
        if type(data) == str:
            ShowMessage("Subscription failed", data)
    def AddMpvPlayer(self):
        import pyneumompv
        mpv_player = pyneumompv.MpvPlayer(self.controller.app.receiver, self)
        glcanvas = mpv_player.glcanvas
        glcanvas.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)
        self.controller.mosaic_sizer.Add(glcanvas, 1, wx.EXPAND|wx.ALL)
        self.mpv_players.append(mpv_player)
        self.glcanvases.append(glcanvas)
        self.controller.mosaic_sizer.SetCols(self.num_cols)
        if self.num_entries > 1 and self.focus_idx is None:
            self.focus_idx = 0
        dtdebug(f"Added glcanvas={glcanvas} mpv_player={mpv_player} focus_idx={self.focus_idx} l={len(self.mpv_players)}/{len(self.glcanvases)}")
        self.HighlightMpvPlayer()
        wx.PostEvent(glcanvas, wx.WindowCreateEvent())

    def HighlightMpvPlayer(self, focus_idx=None):
        if self.controller.hidden and  len(self.glcanvases) == 1:
            focus_idx = -1 # do not show coloured highlight
        if focus_idx is None:
            focus_idx = self.focus_idx
        if focus_idx is not None:
            #self.focus_idx = focus_idx if self.num_entries > 1 or focus_idx > self.num_entries else None
            self.focus_idx = focus_idx
        for idx, glcanvas in enumerate(self.glcanvases):
            self.controller.mosaic_sizer.GetItem(glcanvas).SetBorder(5 if idx == focus_idx else 0)
        self.controller.Layout()
        wx.CallAfter(self.controller.Refresh)

    def RemoveMpvPlayer(self, glcanvas, force=False):
        if not force and len(self.mpv_players) == 1:
            dterror("cannot remove last mpv player")
        for idx, w in enumerate(self.glcanvases):
            if w == glcanvas:
                mpv_player=self.mpv_players.pop(idx)
                self.controller.mosaic_sizer.Remove(idx)
                self.glcanvases.pop(idx)
                mpv_player.close()
                glcanvas.Destroy()
                break

        self.controller.mosaic_sizer.SetCols(self.num_cols)
        if self.num_entries == 1:
            self.focus_idx = 0
        elif self.focus_idx is None:
            self.focus_idx = 0
        else:
            self.focus_idx= min(self.focus_idx, self.num_entries -1)
        self.HighlightMpvPlayer()

    def OnStop( self, evt):
        dtdebug(f'OnStop {len(self.mpv_players)}')
        if self.focus_idx is None or self.focus_idx <0 or self.focus_idx >= len(self.mpv_players):
            dterror(f'self.focus_idx={self.focus_idx} out of range: max={len(self.mpv_players)}')
            return
        assert self.focus_idx >=0 and self.focus_idx < len(self.mpv_players)
        player = self.mpv_players[self.focus_idx]
        glcanvas = self.glcanvases[self.focus_idx]
        player.stop_play()
        if len(self.mpv_players) > 1 :
            self.RemoveMpvPlayer(glcanvas, force=True)

    def ServiceTune(self, service_or_chgm, replace_running=True):
        ls = self.controller.app.live_service_screen
        service = ls.Tune(service_or_chgm) #save info on the last tuned channel/service
        if service is None:
            ShowMessage(f'Cannot tune to service {service_or_chgm}')
            return
        assert ls.app.receiver is not None
        if not replace_running:
            self.AddMpvPlayer()
            self.focus_idx = len(self.mpv_players) -1
            dtdebug(f'ADDED PLAYER {self.mpv_players[self.focus_idx]} {self.focus_idx}')
            wx.CallLater(2000, self.mpv_players[self.focus_idx].play_service, service)
        else:
            if self.focus_idx is not None:
                dtdebug(f'USED PLAYER {self.mpv_players[self.focus_idx]} {self.focus_idx}')
                self.mpv_players[self.focus_idx].play_service(service)

class RecordPanel(wx.Panel):
    black=wx.Colour(128, 128, 0, 0)
    white=wx.Colour(255, 255, 255)
    red=wx.Colour(255, 0, 0)
    green=wx.Colour(0, 255, 0)
    yellow=wx.Colour(255, 255, 0)
    gray=wx.Colour(128, 128, 128)
    epg_colour=wx.Colour()
    epg_highlight_colour=wx.Colour()

    def set_dimensions(self):
        self.num_rows_on_screen = None # will be auto computed
        self.chwidth = 9 # in columns
        self.ch_start_col = 1
        self.ch_start_row = 1 # row number at which epg_row==0 is positioned
        self. num_cols = 36 # 3 hours

    def __init__(self, controller, rowtype,  *args, **kwds):
        self.created = False
        self.set_dimensions()
        self.now = datetime.datetime.now(tz=tz.tzlocal()).replace(second=0)
        self.controller = controller
        super().__init__(*args, **kwds)
        self.allow_all = True
        self.restrict_to_sat = None
        self.make_colours()
        self.rowtype = rowtype
        if rowtype == RowType.GRIDEPG:
            self.data = GridEpgData(self)
            self.RowClass = ChEpgGridRow
        elif  rowtype == RowType.SERVICE_OR_CHANNEL:
            self.data = GridData(self, rowtype=rowtype)
            self.RowClass = GridRow
        elif  rowtype == RowType.SAT:
            self.data = GridData(self, rowtype=rowtype)
            self.RowClass = GridRow
        elif  rowtype == RowType.CHG:
            self.data = GridData(self, rowtype=rowtype)
            self.RowClass = GridRow
        elif  rowtype == RowType.REC:
            self.data = GridData(self, rowtype=rowtype)
            self.RowClass = GridRow
        self.rows = []
        self.gbs = None
        self.last_focused_rowno = None # relative to top channel on screen
        self.Bind(wx.EVT_MOUSEWHEEL, self.OnMouseWheel)
        self.Bind(wx.EVT_SCROLL, self.OnScroll)
        self.wheel_count=0
        self.testfont = self.GetFont()
        self.font = self.GetFont()
        self.font.SetPointSize(self.font.GetPointSize()+1)
        self.SetFont(self.font)

    @property
    def selected_row_entry(self):
        return self.data.ls.selected_service_or_channel

    def OnDestroy(self):
        dtdebug('OnDestroy (NOOP)')
        pass

    def set_active(self):
        pass

    def set_inactive(self):
        pass

    def HighlightCell(self, cell, on):
        if cell.data is None:
            return False # could be another text cell which cannot be focused
        if on:
            cell.SetForegroundColour(self.channel_highlight_colour)
        else:
            cell.SetForegroundColour(self.white)
        return True

    def OnFocus(self, event):
        self.controller.update_active(self)
        w = event.GetWindow()
        rowno = w.data.row.rowno
        past_end = rowno + self.top_idx >= self.data.row_screen.list_size
        if past_end:
            row =  self.data.row_screen.list_size-1-self.top_idx
            if row>= 0:
                self.focus_row(w, self.data.row_screen.list_size-1-self.top_idx)
            else:
                dtdebug('CALL SetFocus')
                self.controller.top_panel.SetFocus()
            return
        self.controller.set_active(self)
        if self.last_focused_cell is not None:
            self.HighlightCell(self.last_focused_cell, False)
        if self.HighlightCell(w, True):
            self.last_focused_cell = w
            self.update_info(w)
            if w.data is not None:
                record = self.last_focused_cell.data.row.row_record
                self.last_focused_rowno = self.last_focused_cell.data.row.rowno
                self.data.OnSelectRow(record)

    def update_info(self, w):
        assert 0 # needs to be overridden in derived class

    def set_initial_top_idx(self):
        entry = self.selected_row_entry
        if entry is None:
            dtdebug('set_initial_top_idx=0')
            self.top_idx = 0
            self.last_focused_rowno = 0
            return 0
        entry_idx = self.data.row_screen.set_reference(entry)
        if self.last_focused_rowno is not None:
            top_idx = max(entry_idx -self.last_focused_rowno, 0)
            dtdebug(f'set_initial_top_idx={top_idx}')
            self.last_focused_rowno = entry_idx - top_idx
        elif False and self.data.row_screen.list_size - entry_idx < self.num_rows_on_screen:
            # scroll down to fit more rows on screen
            top_idx =max(self.data.row_screen.list_size - self.num_rows_on_screen, 0)
            dtdebug(f'set_initial_top_idx={top_idx}')
        else:
            # put selected row at top of screen
            top_idx = entry_idx if entry_idx>=0 else 0
            dtdebug(f'set_initial_top_idx={top_idx}')
            self.last_focused_rowno = 0
        self.top_idx = top_idx

    @property
    def row_idx(self):
        """
        index in the full rows list (including offscreen records) of the current row
        """
        assert self.top_idx is not None # must be initialised
        return self.top_idx + self.last_focused_rowno

    @row_idx.setter
    def row_idx(self, idx):
        """
        idx is the index of a channel in the current data screen
        """
        if self.top_idx is None or idx - self.top_idx < self.num_rows_on_screen and idx-self.top_idx >= 0:
            self.last_focused_rowno = idx - self.top_idx
        else:
            assert 0

    def OnScroll(self, event):
        idx = event.GetPosition()
        if idx >=0 and idx < self.data.row_screen.list_size:
            self.scroll_down(idx -self.top_idx)

    def OnMouseWheel(self, event):
        evtObj = event.GetEventObject()
        wr = event.GetWheelRotation()
        self.wheel_count += (1 if wr>0 else -1)
        ms = wx.GetMouseState()
        ctrlDown = ms.ControlDown()
        shiftDown = ms.ShiftDown()
        altDown = ms.AltDown()
        delta= 5 if not ctrlDown and not altDown and shiftDown else 1
        if self.wheel_count < -10:
            self.scroll_down(+delta)
            self.wheel_count = 0
        elif self.wheel_count > 10:
            self.scroll_down(-delta)
            self.wheel_count = 0

    def check_for_new_records(self):
        txn = self.data.ls.chdb.rtxn()
        changed = self.data.ls.screen.update(txn)
        txn.commit()
        del txn
        if changed:
            dtdebug(f"Updating live service screen")
            old_record = self.selected_row_entry
            self.data.GetRecordAtRow.cache_clear()
            self.SelectRow(old_record)
            #wx.CallAfter(self.Refresh)

    def on_timer(self):
        self.check_for_new_records()

    def OnTimer(self, evt):
        now = datetime.datetime.now(tz=tz.tzlocal())
        now = now.replace(second=now.second - now.second%2)
        self.data.start_time = now.replace(minute=now.minute - now.minute%30, second=0)
        if self.now != now:
            self.now = now
            self.on_timer()
            wx.CallAfter(self.Refresh)

    def update_scrollbar(self):
        page_size = self.num_rows_on_screen
        self.scrollbar.SetScrollbar(position=self.top_idx, thumbSize=page_size,
                                    range=self.data.row_screen.list_size,
                                    pageSize=page_size, refresh=True)

    def create(self):
        if self.created:
            return
        self.created = True
        gtk_add_window_style(self, "active")
        self.set_initial_top_idx()
        self.row_gap = 8 if wxpythonversion < wxpythonversion42 else 4
        gbs = self.gbs = wx.GridBagSizer(vgap=self.row_gap, hgap=5)
        self.sizer =  wx.FlexGridSizer(1, 2, 0, 0)
        self.sizer.AddGrowableRow(0)
        self.sizer.AddGrowableCol(0)
        self.sizer.Add(gbs, 0, wx.EXPAND, 0)
        gbs.SetSizeHints(self)
        self.SetSizer(self.sizer)
        gbs =self.gbs

        ##The following is needed to handle a wx(?) bug: when scrolling down and thus adding
        ##cells to the grid, row heights seme to increase leading to the last entry moving off screeen
        w,h = get_text_extent("Test", self.GetFont(), compensate=False)
        self.row_height= ((h+1)//2)*2 +self.row_gap
        dtdebug(f'ROW HEIGHT: {self.row_height}')
        self.make_rows()
        self.scrollbar = wx.ScrollBar(self, style=wx.SB_VERTICAL)

        self.gbs.Add(self.scrollbar, (1,0), (self.num_rows_on_screen,1), wx.EXPAND)
        for i in range(1, gbs.GetCols()):
            gbs.AddGrowableCol(i)
        self.last_focused_cell_ = None
        gbs.SetSizeHints(self)
        self.Layout()
        self.Bind(wx.EVT_CHILD_FOCUS, self.OnFocus)
        if self.last_focused_rowno < len(self.rows):
            dtdebug('CALL SetFocus')
            wx.CallAfter(self.rows[self.last_focused_rowno].SetFocus)
        self.gbs = gbs
        self.update_scrollbar()
        gbs.SetFlexibleDirection(wx.VERTICAL)
        gbs.SetNonFlexibleGrowMode(wx.FLEX_GROWMODE_ALL)
        self.Layout()
    @property
    def last_focused_cell(self):
        return self.last_focused_cell_

    @last_focused_cell.setter
    def last_focused_cell(self, val):
        self.last_focused_cell_ = val

    def MoveToChOrder(self, w, chno):
        entry  = self.data.ls.entry_for_ch_order(chno)
        if entry is not None:
            old_top_idx = self.top_idx
            self.top_idx = self.data.row_screen.set_reference(entry)
            self.update_rows(old_top_idx)
            self.focus_row(w, 0)

    def OnDestroyWindow(self, evt):
        dtdebug(f'OnDestroyWindow {evt.GetWindow()}')
        pass

    def make_colours(self):
        self.channel_highlight_colour = wx.Colour('cyan')
        self.epg_colour.Set('dark blue')
        self.epg_highlight_colour.Set('yellow')
        self.green.Set('dark green')

    def make_rows(self):
        ss=self.GetParent().GetSize()
        ss1= self.GetParent().GetParent().GetSize()
        grid_height = (ss1[1]*6)//10
        x = grid_height // self.row_height
        self.num_rows_on_screen = x
        for idx in range(0, self.num_rows_on_screen):
            row = self.RowClass(self, idx)
            self.rows.append(row)
            row.update()

    def reset(self):
        self.set_initial_top_idx()
        self.Freeze()
        num_rows = len(self.rows)
        for row in self.rows:
            row.remove()
        self.rows=[]
        self.make_rows()
        self.Thaw()
        if False:
            self.rows[self.last_focused_rowno].SetFocus()
        self.gbs.SetItemSpan(self.scrollbar, (self.num_rows_on_screen,1))

    def SetFocus(self):
        dtdebug('CALL SetFocus')
        self.rows[self.last_focused_rowno].SetFocus()

    def update_rows(self, old_top_idx=None):
        assert old_top_idx is not None
        self.Freeze()
        self.update_rows_(old_top_idx = old_top_idx)
        self.Thaw()
        self.gbs.Layout()
        for rowno, row in enumerate(self.rows):
            assert rowno == row.rowno

    def update_rows_(self, old_top_idx=None):
        if old_top_idx < self.top_idx:
            num_rows =self.top_idx - old_top_idx
            for row in self.rows[0:num_rows]:
                row.remove()
            for rowno, row in enumerate(self.rows[num_rows:]):
                row.move_to_row(rowno)
                self.rows[rowno] = row
                self.rows[num_rows + rowno] = None
            first = max(self.num_rows_on_screen-num_rows,0)
            for rowno in range(first,self.num_rows_on_screen):
                self.rows[rowno] = self.RowClass(self, rowno)
                self.rows[rowno].update()

        if old_top_idx is not None and old_top_idx > self.top_idx:
            num_rows =old_top_idx -self.top_idx
            for row in self.rows[-num_rows:]:
                row.remove()
            for rowno_, row in enumerate(self.rows[-num_rows-1::-1]):
                rowno = self.num_rows_on_screen-1 -rowno_
                row.move_to_row(rowno)
                self.rows[rowno] = row
                self.rows[rowno - num_rows] = None
            for rowno in range(0, min(num_rows, self.num_rows_on_screen)):
                self.rows[rowno] = self.RowClass(self, rowno)
                self.rows[rowno].update()


    def scroll_down(self, rows):
        old_top_idx = self.top_idx
        self.top_idx += rows
        if self.top_idx < 0:
            self.top_idx = 0
        elif self.top_idx + self.last_focused_rowno >= self.data.GetNumberRows():
            if True:
                self.top_idx = max(self.data.GetNumberRows() -1 - self.last_focused_rowno, 0)
            else:
                self.top_idx = old_top_idx
        self.update_rows(old_top_idx)
        self.focus_row(None, self.last_focused_rowno)
        wx.CallAfter(self.update_scrollbar)

    def scroll_leftright(self):
        pass

    def focus_row(self, last_focused_cell, rowno):
        """
        rowno = index of row on screen
        """
        rowno = self.last_focused_rowno if rowno is None else rowno
        assert rowno is None or rowno>=0 and rowno < self.num_rows_on_screen
        idx = max(min(rowno+self.top_idx, self.data.row_screen.list_size -1), 0)
        rowno = idx - self.top_idx
        dtdebug('CALL SetFocus')
        self.rows[rowno].SetFocus()

    def rightmost_start_time(self):
        start_time = int(self.periods[-1].timestamp())
        for row in self.rows:
            end_time = max(row.rightmost_start_time(), start_time)
        return start_time

    def Navigate(self, w, modifier, key):
        """
        returns False if command is not handled here
        """
        is_ctrl = (modifier & wx.ACCEL_CTRL)
        if not hasattr(w, 'data'):
            return False
        row = w.data.row
        if key == wx.WXK_RIGHT:
            if is_ctrl:
                return False
            return True
        elif key == wx.WXK_LEFT:
            if is_ctrl:
                return False
            if is_ctrl:
                return False
            return True
        elif key in (wx.WXK_DOWN, wx.WXK_NUMPAD_DOWN):
            if is_ctrl:
                return False
            if row.rowno < self.num_rows_on_screen-1:
                self.focus_row(w, row.rowno+1)
                return True
            else:
                self.scroll_down(1)
                self.focus_row(w, self.num_rows_on_screen-1)
                return True
        elif key in (wx.WXK_PAGEDOWN, wx.WXK_NUMPAD_PAGEDOWN):
            if is_ctrl:
                return False
            rows_to_scroll = 1 if key == wx.WXK_DOWN else self.num_rows_on_screen-1
            if row.rowno < self.num_rows_on_screen-1:
                self.focus_row(w, self.num_rows_on_screen-1)
                return True
            else:
                self.scroll_down(self.num_rows_on_screen-1)
                self.focus_row(w, self.num_rows_on_screen-1)
                return True
        elif key in (wx.WXK_UP, wx.WXK_NUMPAD_UP):
            if is_ctrl:
                return False
            if row.rowno > 0:
                self.focus_row(w, row.rowno-1)
                return True
            else:
                self.scroll_down(-1)
                self.focus_row(w, 0)
                return True
        elif key in (wx.WXK_PAGEUP, wx.WXK_NUMPAD_PAGEUP):
            if is_ctrl:
                return False
            if row.rowno > 0:
                self.focus_row(w, 0)
                return True
            else:
                self.scroll_down(-(self.num_rows_on_screen-1))
                self.focus_row(w, 0)
                return True
        return False

    def OnKey(self, evt):
        w = wx.Window.FindFocus()
        key = evt.GetKeyCode()
        w = wx.Window.FindFocus()
        row = w.data.row

        modifiers = evt.GetModifiers()
        is_ctrl = (modifiers & wx.ACCEL_CTRL)
        is_shift = (modifiers & wx.ACCEL_SHIFT)
        if not (is_ctrl or is_hft) and IsNumericKey(key):
            from neumodvb.servicelist import IsNumericKey, ask_channel_number
            chno = ask_channel_number(self, key- ord('0'))
            entry  = self.data.ls.entry_for_ch_order(chno)
            if entry is not None:
                self.SelectRow(entry)
            return
        else:
            evt.Skip(True)

    def add_cell(self, pos, cell, span=(1,6), bgcolour=None, fgcolour=None,
                 ref_for_tab_order=None, expand=wx.EXPAND):
        if bgcolour is not None:
            cell.SetBackgroundColour(bgcolour)
        if fgcolour is not None:
            cell.SetForegroundColour(fgcolour)
        it = self.gbs.Add(cell, pos, span, expand|wx.ALIGN_CENTER_VERTICAL)
        #set a very small column size; the call gbs.SetNonFlexibleGrowMode... elsewhere will expand the columns
        it.SetMinSize((4,self.row_height))
        if ref_for_tab_order is not None:
            cell.MoveAfterInTabOrder(ref_for_tab_order)
        cell.data = None
        gtk_add_window_style(cell, 'cell')
        return cell


    def SelectRow(self, record):
        if record is not None:
            old_top_idx = self.top_idx
            self.top_idx = self.data.row_screen.set_reference(record)
            self.update_rows(old_top_idx)
            self.focus_row(None, 0)
            self.data.OnSelectRow(record)



    def OnTune(self, event, replace_running):
        assert self.last_focused_cell is not None
        record = self.last_focused_cell.data.row.row_record
        dtdebug(f"gridepg tune record {record} replace_running={replace_running}")
        if self.controller.top_panel.group_select_in_progress:
            assert 0
            self.controller.top_panel.OnSelectGroup(record)
        else:
            wx.GetApp().ServiceTune(record, replace_running)

    def OnPlay(self, evt, replace_running):
        assert self.last_focused_cell is not None
        record = self.last_focused_cell.data.row.row_record
        dtdebug(f"gridepg play record {record} replace_running={replace_running}")
        wx.GetApp().PlayRecording(record)

    def OnToggleRecord(self, event):
        assert self.last_focused_cell is not None
        row = self.last_focused_cell.data.row
        record = row.row_record
        if self.last_focused_cell.data.is_ch:
            start_time = datetime.datetime.now(tz=tz.tzlocal())
            show_record_dialog(self, record, start_time = start_time)
            return True
        return False


class GridEpgPanel(RecordPanel):

    def set_dimensions(self):
        super().set_dimensions()
        self.epg_start_col = 1 + self.chwidth
        self.epg_duration_width = 5*60 # 5 minutes
        self.ch_start_row = 1 # row number at which epg_row==0 is positioned

    def __init__(self, controller, *args, **kwds):
        rowtype = RowType.GRIDEPG
        super().__init__(controller, rowtype, *args, **kwds)
        self.time_cells=[]
        self.focus_time = None
        self.data.start_time = self.now.replace(minute=self.now.minute - self.now.minute%30, second=0)
        self.create()

    def remove_epg_data_for_rows(self, rowno_start, rowno_end):
        rowno_start = max(0, rowno_start)
        rowno_end = min(rowno_end, self.num_rows_on_screen)
        if rowno_start >= rowno_end:
            return
        for row in self.rows[rowno_start:rowno_end]:
            service = row.row_record
            if service is not None:
                key = service.k if type(service) == pychdb.service.service else service.service
                dtdebug(f'python remove epg service  {key}')
                self.data.epg_screens.remove_service(key)
        self.data.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version

    def check_for_new_epg_records(self):
        if False: #todo
            txn_service = None;
            changed = self.row_screen.update(txn_service)
            if changed:
                pass
        txn_epg = self.data.epgdb.rtxn()
        any_change=False
        for rowno, row in enumerate(self.rows):
            if row.row_record is not None:
                key = row.row_record.k if type(row.row_record) == pychdb.service.service else row.row_record.service
                epg_screen = self.data.epg_screens.epg_screen_for_service(key)
                #changed = epg_screen.update(txn_epg)
                changed = epg_screen.update_between(txn_epg, self.data.start_time_unixepoch,
                                                    self.data.end_time_unixepoch)
                any_change |= changed
                if changed:
                    dtdebug(f"Updating service {row.row_record}")
                    #self.epg_screens.remove_service(service)
                    refocus = self.last_focused_cell is not None and self.last_focused_cell.data.row.rowno == row.rowno
                    row.update()
                    if refocus:
                        row.focus_current()
        txn_epg.abort()
        del txn_epg
        if any_change:
            self.Layout()

    def on_timer(self):
        super().on_timer()
        self.check_for_new_epg_records()

    def create(self):
        if self.created:
            return
        super().create()
        self.make_periods(self.gbs)
        self.update_periods()
        self.painter =None
        self.Bind(wx.EVT_PAINT, self.OnPaint)
        self.now_brush = wx.Brush(colour=self.red) #for "now" vertical line
        self.time_tick_brush = wx.Brush(colour=self.white) #for "now" vertical line

    def OnDestroy(self):
        if self.created:
            dtdebug('UNBIND')
            self.Unbind(wx.EVT_PAINT)
        else:
            dtdebug('UNBIND skipped (not created)')

    def reset(self):
        self.data.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version
        last_was_ch = True if self.last_focused_cell is not None and self.last_focused_cell.data.is_ch else False
        self.set_initial_top_idx()
        self.Freeze()
        num_rows = len(self.rows)
        self.remove_epg_data_for_rows(0, num_rows)
        for row in self.rows:
            row.remove()
        self.rows=[]
        self.make_rows()
        self.Thaw()
        self.last_focused_rowno = min(self.last_focused_rowno, self.num_rows_on_screen-1)
        self.gbs.SetItemSpan(self.scrollbar, (self.num_rows_on_screen,1))

    def SetFocusOFF(self):
        if last_was_ch:
            self.rows[self.last_focused_rowno].SetFocus()
        else:
            self.rows[self.last_focused_rowno].focus_current()

    def update_rows_(self, old_top_idx=None):
        if old_top_idx < self.top_idx:
            self.remove_epg_data_for_rows(0, self.top_idx - old_top_idx)
        elif old_top_idx > self.top_idx:
            self.remove_epg_data_for_rows(self.top_idx - old_top_idx + len(self.rows),
                                                   len(self.rows))
        super().update_rows_(old_top_idx = old_top_idx)

    def DrawCurrentTime(self):
        now = self.now
        times = self.data.periods
        t1, t2 = times[0], times[-1]
        x1 = self.time_cells[0].GetPosition().Get()[0]
        x2 =self.time_cells[-1].GetPosition().Get()[0]
        y1 = self.time_cells[0].GetPosition().Get()[1]
        y2 = self.rows[-1].ch_cell.GetPosition().Get()[1]
        y2 += self.rows[-1].ch_cell.GetSize().Get()[1]
        if now >= t1 and now < t2:
            x=int((now-t1)/(t2-t1)*(x2-x1) + x1)
            dc = wx.ClientDC(self)
            dc.SetBackground(self.now_brush)
            dc.SetClippingRegion(x, y1, 2, y2-y1)
            dc.Clear()
        for cell in self.time_cells:
            x, y= cell.GetPosition().Get()
            w, h = cell.GetSize().Get()
            dc = wx.ClientDC(self)
            dc.SetBackground(self.time_tick_brush)
            dc.SetClippingRegion(x-2, y, 2, h)
            dc.Clear()

    def OnPaint(self, e):
        dc = wx.PaintDC(self)
        upd = wx.RegionIterator(self.GetUpdateRegion())
        rects=[]
        while upd.HaveRects():
            rect = upd.GetRect()
            rects.append(rect)
            upd.Next()
        wx.CallAfter(self.DrawCurrentTime)
        e.Skip(True)

    def save_focus_time(self, focused_window):
        oldrow = self.last_focused_rowno
        self.last_focused_rowno =  focused_window.data.row.rowno
        moved_left_right =  self.last_focused_rowno == oldrow
        if self.focus_time is not None and not moved_left_right:
            return
        if focused_window.data.epg is not None:
            self.focus_time = focused_window.data.epg.k.start_time
        elif not focused_window.data.is_ch and focused_window.data.start_time is not None:
            self.focus_time = focused_window.data.start_time

    def scroll_leftright(self):
        old_top_idx = self.top_idx
        self.data.remove_epg_data_for_channels(self.top_idx, self.top_idx+self.num_rows_on_screen)
        for rowno, row in enumerate(self.rows):
            row.update()
        self.update_periods()
        self.Layout()

    def focus_row(self, last_focused_cell, rowno):
        """
        rowno = index of row on screen
        """
        rowno = self.last_focused_rowno if rowno is None else rowno
        assert rowno is None or rowno>=0 and rowno < self.num_rows_on_screen
        last_was_ch = True if self.last_focused_cell is not None and \
            self.last_focused_cell.data.is_ch else False
        if last_was_ch:
            dtdebug('CALL SetFocus')
            self.rows[rowno].SetFocus()
        else:
            self.rows[rowno].focus_current()

    def rightmost_start_time(self):
        start_time = int(self.periods[-1].timestamp())
        for row in self.rows:
            end_time = max(row.rightmost_start_time(), start_time)
        return start_time

    def Navigate(self, w, modifier, key):
        """
        returns False if command is not handled here
        """
        is_ctrl = (modifier & wx.ACCEL_CTRL)
        row = w.data.row
        if key not in (wx.WXK_LEFT, wx.WXK_RIGHT):
            return super().Navigate(w, modifier, key)
        if key == wx.WXK_RIGHT:
            if is_ctrl:
                if not w.data.is_ch:
                    return False
            if row.is_rightmost_epg_cell(w):
                start_cell= row.epg_cells[-1]
                start_time = self.rightmost_start_time()
                start_time = max(self.data.start_time_unixepoch +30*60, start_time)
                self.data.set_start(start_time)
                self.scroll_leftright()
                dtdebug('CALL SetFocus')
                row.epg_cells[-1].SetFocus()

                return True
            else:
                to_focus = w.data.row.neighboring_cell(w, left_neighbor=False)
                assert to_focus is not None
                dtdebug('CALL SetFocus')
                to_focus.SetFocus()
                return True
        elif key == wx.WXK_LEFT:
            if is_ctrl:
                dtdebug('CALL SetFocus')
                row.ch_cell.SetFocus()
                return True
            if row.is_leftmost_epg_cell(w):
                start_cell= row.epg_cells[0]
                start_time = self.data.start_time_unixepoch -30*60
                #if an epg line has at least two entries, then one of them must have epg
                if start_cell.data.epg is not None:
                    start_time = start_cell.data.epg.k.start_time-1
                self.data.set_start(start_time)
                self.scroll_leftright()
                dtdebug('CALL SetFocus')
                row.epg_cells[0].SetFocus()
                return True
            else:
                if is_ctrl:
                    return False
                to_focus = w.data.row.neighboring_cell(w, left_neighbor=True)
                assert to_focus is not None
                dtdebug('CALL SetFocus')
                to_focus.SetFocus()
                return True
        return False

    def HighlightCell(self, cell ,on):
        if cell.data is None:
            return False # could be another text cell which cannot be focused
        colno = cell.data.colno
        if colno >= 0:
            if on:
                cell.SetForegroundColour(self.epg_highlight_colour)
            else:
                cell.SetForegroundColour(self.white)
        else:
            return super().HighlightCell(cell, on)
        if on and cell.data is not None:
            self.save_focus_time(cell)
        return True

    def make_periods(self, gbs):
        now = self.data.start_time
        self.periods = [now + datetime.timedelta(minutes=minutes) for minutes in range(0, 180, 30)]
        self.time_cells = []
        chwidth = self.chwidth # in cells
        content=f'{now:%a %b %d}'
        cell = wx.StaticText(self, wx.ID_ANY, content, style= wx.TE_READONLY|wx.BORDER_NONE)
        cell.data = None
        w = self.add_cell(pos=(0, 0),  span=(1,chwidth), bgcolour = self.black,
                          fgcolour=self.channel_highlight_colour,
                          cell=cell)
        self.date_cell = w
        for idx,t in enumerate(self.periods):
            content=f'{t:%H:%M}'
            cell = wx.StaticText(self, wx.ID_ANY, content, style= wx.TE_READONLY|wx.BORDER_NONE)
            cell.data = None
            w = self.add_cell(pos=(0, self.epg_start_col+idx*6), span=(1,6), bgcolour = self.black, fgcolour=self.white,
                              cell=cell)
            self.time_cells.append(w)

    def update_periods(self):
        now = self.data.start_time
        self.periods = [now + datetime.timedelta(minutes=minutes) for minutes in range(0, 180, 30)]
        self.date_cell.SetLabel(f'{now:%a %b %d}')
        for idx, cell in enumerate(self.time_cells):
            t  = self.periods[idx]
            cell.SetLabel(f'{t:%H:%M}')

    def OnToggleRecord(self, event):
        if super().OnToggleRecord(event):
            return True
        row = self.last_focused_cell.data.row
        service = row.row_record
        assert not self.last_focused_cell.data.is_ch
        if self.last_focused_cell.data.epg is None:
            show_record_dialog(self, service, start_time = self.last_focused_cell.data.start_time)
            return True
        else:
            epg=self.last_focused_cell.data.epg
            show_record_dialog(self, service, epg=epg)
            return True
        return False

    def update_info(self, w):
        rec = None if w.data is None else w.data.epg
        infow = self.controller.infow
        if rec is None:
            infow.ChangeValue("")
            return

        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        t =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        infow.Freeze()
        infow.BeginSuppressUndo()
        infow.Clear()

        infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))
        infow.BeginTextColour(wx.GREEN)

        infow.WriteText(f"{rec.event_name}")
        infow.EndTextColour()
        infow.BeginAlignment(wx.TEXT_ALIGNMENT_RIGHT)
        infow.WriteText(f" {dt(rec.k.start_time)} - {t(rec.end_time)}")
        infow.EndAlignment()

        infow.WriteText(f"{content_types(rec.content_codes)}\n\n")

        infow.WriteText(f"{rec.story}\n")
        infow.BeginTextColour(wx.YELLOW)
        infow.WriteText(f'{str(rec.source)}; {dt(rec.mtime)}')
        infow.EndTextColour()
        infow.EndSuppressUndo()
        infow.Thaw()

    def CmdTune(self, event):
        """
        Also handles Play
        """
        return self.OnTune(event, replace_running=True)

    def CmdTuneAdd(self, event):
        """
        Also handles Play Add
        """
        return self.OnTune(event, replace_running=False)

class ServiceChannelPanel(RecordPanel):
    def __init__(self, controller, *args, **kwds):
        rowtype = RowType.SERVICE_OR_CHANNEL
        super().__init__(controller, rowtype, *args, **kwds)
        self.create()

    def update_info(self, w):
        x = self.last_focused_cell
        row = None if w is None or w.data is None else w.data.row
        rec = None if row is None else row.data.GetRecordAtRow(self.top_idx+ row.rowno)
        infow = self.controller.infow
        if rec is None:
            infow.ChangeValue("")
            return
        service = self.data.ls.service_for_entry(rec)
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*1.5))
        infow.Freeze()
        infow.BeginSuppressUndo()
        infow.Clear()

        infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))

        infow.BeginTextColour(wx.GREEN)
        if type(rec) == pychdb.chgm.chgm:
            infow.WriteText(f"{str(rec.chgm_order)}: {rec.name} ")
            rec = rec
        elif type(rec) == pychdb.service.service:
            infow.WriteText(f"{str(rec.ch_order)}: {rec.name} ")
        infow.EndTextColour()

        infow.BeginAlignment(wx.TEXT_ALIGNMENT_RIGHT)
        if service is None:
            infow.WriteText(f" Missing service! ")
        else:
            pol = lastdot(service.pol).replace('POL','')
            stream = '' if service.k.mux.stream_id < 0 else f'-{service.k.mux.stream_id}'
            t2mi = '' if service.k.mux.t2mi_pid < 0 else f' T{service.k.mux.t2mi_pid}'

            infow.WriteText(f"{service.frequency/1000.:9.3f}{pol}{stream}{t2mi} " \
                            f"nid={service.k.network_id} tid={service.k.ts_id} ")
        infow.EndAlignment()
        if service is not None:
            infow.WriteText(f"sid={service.k.service_id} pmt={service.pmt_pid}\n\n")

            infow.WriteText(f"Provider: {service.provider}\n")
            infow.WriteText(f"Modified: {dt(service.mtime)}\n")
            exp = "Yes" if service.expired else "No"
            enc = "Yes" if service.encrypted else "No"
            infow.WriteText(f"Encrypted: {enc} Expired: {exp}\n")
        infow.EndSuppressUndo()
        infow.Thaw()

    def CmdTune(self, event):
        """
        Also handles Play
        """
        return self.OnTune(event, replace_running=True)

    def CmdTuneAdd(self, event):
        """
        Also handles Play Add
        """
        return self.OnTune(event, replace_running=False)


class ChgPanel(RecordPanel):

    def __init__(self, controller, *args, **kwds):
        rowtype = RowType.CHG
        super().__init__(controller, rowtype, *args, **kwds)
        self.create()

    @property
    def selected_row_entry(self):
        chg = self.data.ls.selected_chg
        return None if chg is None or chg.k.bouquet_id == 0 else chg

    def update_info(self, w):
        row = None if w is None or w.data is None else w.data.row
        rec = None if row is None else row.data.GetRecordAtRow(self.top_idx+ row.rowno)
        infow = self.controller.infow
        if rec is None:
            infow.ChangeValue("")
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*2))
        infow.Freeze()
        infow.BeginSuppressUndo()
        infow.Clear()

        infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))

        infow.BeginTextColour(wx.GREEN)
        from neumodvb.neumodbutils import enum_to_str

        infow.WriteText(f"{rec.name}: {enum_to_str(rec.k.group_type)}")
        infow.EndTextColour()
        if False:
            infow.BeginAlignment(wx.TEXT_ALIGNMENT_RIGHT)
            infow.WriteText(f"xxxx ")
            infow.EndAlignment()

            infow.WriteText(f"xxxx")
            infow.WriteText(f"Provider: {rec.provider}\n")
            infow.WriteText(f"Modified: {dt(rec.mtime)}\n")
            exp = "Yes" if rec.expired else "No"
            enc = "Yes" if rec.encrypted else "No"
            infow.WriteText(f"Encrypted: {enc} Expired: {exp}\n")
        infow.EndSuppressUndo()
        infow.Thaw()

    def Navigate(self, focused_widget, modifier, key):
        if key != wx.WXK_RETURN:
            return super().Navigate(focused_widget, modifier, key)
        record = self.last_focused_cell.data.row.row_record
        ls = self.controller.app.live_service_screen
        ls.set_chg_filter(record)
        self.controller.top_panel.OnSelectGroup()


class SatPanel(RecordPanel):
    def __init__(self, controller, *args, **kwds):
        rowtype = RowType.SAT
        super().__init__(controller, rowtype, *args, **kwds)
        self.create()

    @property
    def selected_row_entry(self):
        def selected_row_entry(self):
            sat = self.data.ls.selected_sat
            return None if sat.sat_pos == pychdb.sat.sat_pos_none else sat

    def update_info(self, w):
        row = None if w is None or w.data is None else w.data.row
        rec = None if row is None else row.data.GetRecordAtRow(self.top_idx+ row.rowno)
        infow = self.controller.infow
        if rec is None:
            infow.ChangeValue("")
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*2))
        infow.Freeze()
        infow.BeginSuppressUndo()
        infow.Clear()

        infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))

        infow.BeginTextColour(wx.GREEN)
        infow.WriteText(f"{pychdb.sat_pos_str(rec.sat_pos)} {rec.name} ")
        infow.EndTextColour()
        if False:
            infow.BeginAlignment(wx.TEXT_ALIGNMENT_RIGHT)
            infow.WriteText(f"xxxx ")
            infow.EndAlignment()

            infow.WriteText(f"xxxx")
            infow.WriteText(f"Provider: {rec.provider}\n")
            infow.WriteText(f"Modified: {dt(rec.mtime)}\n")
            exp = "Yes" if rec.expired else "No"
            enc = "Yes" if rec.encrypted else "No"
            infow.WriteText(f"Encrypted: {enc} Expired: {exp}\n")
        infow.EndSuppressUndo()
        infow.Thaw()

    def Navigate(self, focused_widget, modifier, key):
        if key != wx.WXK_RETURN:
            return super().Navigate(focused_widget, modifier, key)
        record = self.last_focused_cell.data.row.row_record
        ls = self.controller.app.live_service_screen
        ls.set_sat_filter(record)
        self.controller.top_panel.OnSelectGroup()

class RecordingsPanel(RecordPanel):
    def __init__(self, controller, *args, **kwds):
        rowtype = RowType.REC
        super().__init__(controller, rowtype, *args, **kwds)
        self.create()

    @property
    def selected_row_entry(self):
        def selected_row_entry(self):
            return 0  #todo

    def update_info(self, w):
        row = None if w is None or w.data is None else w.data.row
        rec = None if row is None else row.data.GetRecordAtRow(self.top_idx+ row.rowno)
        infow = self.controller.infow
        if rec is None:
            infow.ChangeValue("")
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M")
        t =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%H:%M")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*2))
        infow.Freeze()
        infow.BeginSuppressUndo()
        infow.Clear()

        infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))

        infow.BeginTextColour(wx.GREEN)
        infow.WriteText(f"{rec.epg.event_name}\n")
        infow.EndTextColour()

        infow.BeginTextColour(wx.YELLOW)
        infow.WriteText(f"{rec.service.name} ")
        infow.EndTextColour()
        rs = enum_to_str(rec.epg.rec_status).capitalize()
        if rec.real_time_start !=0:
            infow.WriteText(f"{dt(rec.real_time_start)} -{t(rec.real_time_end)} ({rs})\n")
        else:
            infow.BeginTextColour(wx.BLUE)
            infow.WriteText(f"{dt(rec.epg.k.start_time)} -{t(rec.epg.end_time)} ({rs})\n")
            infow.EndTextColour()
        infow.WriteText(f"{rec.epg.story}\n")

        infow.EndSuppressUndo()
        infow.Thaw()

    def check_for_new_records(self):
        txn = self.data.ls.recdb.rtxn()
        changed = self.data.ls.screen.update(txn)
        txn.commit()
        del txn
        if changed:
            dtdebug(f"Updating live service screen")
            old_record = self.selected_row_entry
            self.data.GetRecordAtRow.cache_clear()
            self.SelectRow(old_record)
            #wx.CallAfter(self.Refresh)

    def Navigate(self, focused_widget, modifier, key):
        if key != wx.WXK_RETURN:
            return super().Navigate(focused_widget, modifier, key)
        record = self.last_focused_cell.data.row.row_record
        ls = self.controller.app.live_service_screen
        ls.set_sat_filter(record)
        self.controller.top_panel.OnSelectGroup()

    def CmdPlay(self, event):
        return self.OnPlay(event, replace_running=True)

    def CmdPlayAdd(self, event):
        return self.OnPlay(event, replace_running=False)


class LivePanel(wx.Panel):
    def __init__(self, parent, *args, **kwds):
        self.show_epg_cells = True
        self.parent = parent
        self.app = wx.GetApp()
        super().__init__(parent, *args, **kwds)
        self.created = False
        self.mosaic_panel = None
        set_gtk_window_name(self, "gridepg") #needed to couple to css stylesheet
        self.focused_panel_ = None
        self.Bind(wx.EVT_SHOW, self.OnShowWindow)
        self.parent.Bind(wx.EVT_CLOSE, self.OnClose)
        self.hidden = False
        self.grid_panel = None

    @property
    def focused_panel(self):
        return self.focused_panel_

    @focused_panel.setter
    def focused_panel(self, val):
        self.focused_panel_=val

    def OnFocus(self, evt):
        w = evt.GetWindow()
    def OnClose(self, evt):
        if self.mosaic_panel is not None:
            self.mosaic_panel.OnClose(evt)
        evt.Skip()

    def create(self, rowtype):
        self.font = self.GetFont()
        self.dc = wx.ScreenDC()
        self.font.SetPointSize(self.font.GetPointSize()+2)
        self.SetFont(self.font)
        self.dc.SetFont(self.font)
        w0,h0 = self.dc.GetTextExtent("100000")
        w1,h1 = self.dc.GetTextExtent("100000\n11100")
        self.text_height = h1-h0
        self.overall_layout(rowtype)
        self.size = None
        self.Bind(wx.EVT_CHAR_HOOK, self.OnKey)
        self.Bind(wx.EVT_CHILD_FOCUS, self.OnFocus)
        dtdebug('CALL SetFocus')
        self.Bind(wx.EVT_SET_FOCUS, self.OnSetFocus)
    def OnSetFocus(self, evt):
        pass
    def OnDestroy(self, evt):
        dtdebug (f'OnDestroy Called {evt.GetWindow()}')

    def set_active(self, w):
        gtk_remove_window_style(w, "inactive")
        gtk_add_window_style(w, "active")
        w.set_active()
        self.focused_panel = w

    def set_inactive(self,w):
        gtk_remove_window_style(w, "active")
        gtk_add_window_style(w, "inactive")
        w.set_inactive()

    def update_active(self, panel):
        assert panel is not None
        if self.focused_panel != panel:
            if self.focused_panel is not None:
                self.set_inactive(self.focused_panel)
            self.focused_panel = panel
            self.set_active(self.focused_panel)

    def populate_grid_panel(self):
        self.Layout()

    def OnShowWindow(self, evt):
        if False or not evt.IsShown():
            return #happens also at window creation
        if self.created:
            self.grid_panel.reset()
            self.grid_panel.set_active()
            dtdebug('CALL SetFocus')
            self.grid_panel.SetFocus()
            self.Refresh()
        evt.Skip()

    def resize(self):
        if self.hidden:
            return

        #self.grid_panel.reset()

        self.Layout()
        self.Refresh()

    def OnResize(self, evt):
        if not self.hidden:
            size =  evt.GetSize()
            if self.size != size and self.size is not None:
                self.Layout()
                wx.CallAfter(self.resize)
            self.size = size
        evt.Skip()

    def top_panel_layout(self):
        pass #self.top_panel.create()

    def mosaic_panel_layout(self):
        self.mosaic_sizer = wx.GridSizer(cols=1, vgap=0, hgap=0)
        self.mosaic_panel = MosaicPanel(self, self.middle_panel, wx.ID_ANY)
        self.mosaic_panel.SetSizer(self.mosaic_sizer)
        self.parent.Layout()
        self.mosaic_panel.create()

    def create_grid_panel(self, rowtype=None):
        if rowtype is None:
            rowtype =self.current_rowtype
        else:
            self.current_rowtype = rowtype
        if rowtype == RowType.GRIDEPG:
            self.grid_panel = GridEpgPanel(self, self.middle_panel, wx.ID_ANY)
            self.middle_sizer.Insert(0, self.grid_panel, 6, wx.EXPAND, 0)
        elif rowtype == RowType.SERVICE_OR_CHANNEL:
             self.grid_panel = ServiceChannelPanel(self, self.middle_panel, wx.ID_ANY)
             self.middle_sizer.Insert(0, self.grid_panel, 1, wx.EXPAND, 0)
        elif rowtype == RowType.SAT:
            self.grid_panel = SatPanel(self, self.middle_panel, wx.ID_ANY)
            self.middle_sizer.Insert(0, self.grid_panel, 1, wx.EXPAND, 0)
        elif rowtype == RowType.CHG:
            self.grid_panel = ChgPanel(self, self.middle_panel, wx.ID_ANY)
            self.middle_sizer.Insert(0, self.grid_panel, 1, wx.EXPAND, 0)
        elif rowtype == RowType.REC:
            self.grid_panel = RecordingsPanel(self, self.middle_panel, wx.ID_ANY)
            self.middle_sizer.Insert(0, self.grid_panel, 1, wx.EXPAND, 0)
        else:
            assert 0

    def create_top_panel(self, rowtype=None):
        if rowtype is None:
            rowtype =self.current_rowtype
        if rowtype == RowType.REC:
            self.top_panel = RecGroupSelectPanel(self, wx.ID_ANY)
        else:
            self.top_panel = SatBouquetGroupSelectPanel(self, wx.ID_ANY)

    def show_grid_panel(self, rowtype, focus_it=True, recreate=False):
        if self.grid_panel is not None:
            #self.grid_panel.Hide()
            if self.grid_panel.rowtype == rowtype  and not recreate:
                return # panel already on screen
            panel = self.grid_panel
            dtdebug('CallAfter Destroy')
            wx.CallAfter(panel.Destroy)
            panel.OnDestroy()
            self.grid_panel = None
        self.create_grid_panel(rowtype = rowtype)
        self.grid_panel.Show()
        if focus_it:
            dtdebug(f'FOCUS on {self.grid_panel}')
            self.grid_panel.SetFocus()
            self.focused_panel = self.grid_panel
        else:
            dtdebug(f'NO FOCUS on {self.grid_panel}')
        self.parent.Layout()
        wx.CallAfter(self.parent.Layout)

    def show_top_panel(self, rowtype, recreate=False):
        if self.top_panel is not None:
            current = type(self.top_panel) == RecGroupSelectPanel
            desired = rowtype == RowType.REC
            if current == desired  and not recreate:
                return # panel already on screen
            panel = self.top_panel
            wx.CallAfter(panel.Destroy)
            self.top_panel = None
        self.create_top_panel(rowtype = rowtype)
        self.main_sizer.Insert(0, self.top_panel, 0, wx.EXPAND|wx.BOTTOM, border=5)
        self.top_panel.Show()
        wx.CallAfter(self.parent.Layout)

    def middle_panel_layout(self, rowtype):
        self.middle_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.create_grid_panel(rowtype = rowtype)
        self.mosaic_panel_layout()
        self.middle_sizer.Add(self.mosaic_panel, 2, wx.ALIGN_CENTER_VERTICAL|wx.ALIGN_CENTER_HORIZONTAL|wx.EXPAND, 0)
        self.middle_panel.SetSizer(self.middle_sizer)

    def bottom_panel_layout(self):
        self.infow = RichTextCtrl(self.bottom_panel, wx.ID_ANY, "", style=wx.TE_READONLY)
        bgcolour =wx.Colour(128,128,128,0)
        fgcolour = wx.WHITE
        self.infow.SetBackgroundColour(bgcolour)
        self.infow.SetForegroundColour(fgcolour)

        bottom_sizer = wx.BoxSizer(wx.HORIZONTAL)
        bottom_sizer.Add((20, 20), self.grid_panel.chwidth, wx.EXPAND)
        bottom_sizer.Add(self.infow, self.grid_panel.num_cols, wx.EXPAND)
        self.bottom_panel.SetSizer(bottom_sizer)

    def overall_layout(self, rowtype):
        dtdebug ('created top panel')
        self.create_top_panel(rowtype)
        self.middle_panel = wx.Panel(self, wx.ID_ANY)
        self.bottom_panel = wx.Panel(self, wx.ID_ANY)

        main_sizer = wx.BoxSizer(wx.VERTICAL)
        main_sizer.Add(self.top_panel, 0, wx.EXPAND|wx.BOTTOM, border=5)
        main_sizer.Add(self.middle_panel, 2, wx.EXPAND)
        main_sizer.Add(self.bottom_panel, 1, wx.EXPAND)

        self.top_panel_layout()
        self.middle_panel_layout(rowtype)
        self.bottom_panel_layout()

        self.SetSizer(main_sizer)
        self.main_sizer = main_sizer
        self.Layout()

    def OnTimer(self, evt):
        if self.grid_panel is not None:
            self.grid_panel.OnTimer(evt)

    def make_accels(self):
        """
        intercept the arrow keys
        """
        entries=[]
        self.keys={}
        for mod in (wx.ACCEL_NORMAL, wx.ACCEL_CTRL):
            for key in (wx.WXK_RIGHT, wx.WXK_LEFT,
                        wx.WXK_UP, wx.WXK_NUMPAD_UP,
                        wx.WXK_DOWN, wx.WXK_NUMPAD_DOWN,
                        wx.WXK_PAGEDOWN, wx.WXK_NUMPAD_PAGEDOWN,
                        wx.WXK_PAGEUP, wx.WXK_NUMPAD_PAGEUP):
                key_id = wx.NewIdRef()
                entries.append((mod, key, key_id))
                self.keys[key_id] = (mod, key)
                self.Bind(wx.EVT_MENU, self.Navigate, key_id)
        mod = wx.ACCEL_NORMAL
        for key in (wx.WXK_RETURN,):
            key_id = wx.NewIdRef()
            entries.append((mod, key, key_id))
            self.keys[key_id] = (mod, key)
            self.Bind(wx.EVT_MENU, self.Navigate, key_id)

        accel_tbl = wx.AcceleratorTable(entries)
        self.SetAcceleratorTable(accel_tbl)

    def Navigate(self, evt):
        focused = wx.Window.FindFocus()
        w = focused
        modifier, key = self.keys[evt.GetId()]
        is_ctrl = (modifier & wx.ACCEL_CTRL)

        if self.focused_panel is None:
            while w is not None:
                if w in (self.top_panel, self.mosaic_panel, self.grid_panel):
                    self.focused_panel = w
                w = w.GetParent()

        if self.focused_panel is not None:
            if self.focused_panel.Navigate(focused, modifier, key):
                self.set_active(self.focused_panel)
                return
        self.set_inactive(self.focused_panel)
        if is_ctrl:
            #This is a global navigation event
            if self.focused_panel == self.top_panel and key == wx.WXK_DOWN:
                self.grid_panel.focus_row(focused, None)
                self.focused_panel = self.grid_panel
            elif self.focused_panel == self.grid_panel:
                if key == wx.WXK_UP:
                    self.top_panel.SetFocus()
                    self.focused_panel = self.top_panel
                elif key == wx.WXK_RIGHT:
                    self.mosaic_panel.Focus()
                    self.focused_panel = self.mosaic_panel
            elif self.focused_panel == self.mosaic_panel:
                if key == wx.WXK_UP:
                    self.top_panel.SetFocus()
                    self.focused_panel = self.top_panel
                elif key == wx.WXK_LEFT:
                    self.grid_panel.focus_row(None,None)
                    self.focused_panel = self.grid_panel
                else:
                    self.focused_panel = self.mosaic_panel
        self.set_active(self.focused_panel)

    def OnKey(self, evt):
        w = wx.Window.FindFocus()
        key = evt.GetKeyCode()
        if not self.IsDescendant(w):
            return
        from neumodvb.servicelist import IsNumericKey, ask_channel_number
        modifiers = evt.GetModifiers()
        is_ctrl = (modifiers & wx.ACCEL_CTRL)
        is_shift = (modifiers & wx.ACCEL_SHIFT)
        if not (is_ctrl or is_shift) and IsNumericKey(key):
            chno = ask_channel_number(self, key- ord('0'))
            dtdebug(f'need to move to {chno}')
            if chno is not None:
                self.grid_panel.MoveToChOrder(w, chno)
            return
        evt.Skip(True)

    def show_gui(self, rowtype):
        if not self.created:
            self.created=True
            dtdebug('creating gui')
            self.create(rowtype)
            self.Bind(wx.EVT_SIZE, self.OnResize)
            self.make_accels()
        else:
            dtdebug('show_gui')
            self.show_top_panel(rowtype)
            self.show_grid_panel(rowtype)
        if self.hidden:
            self.top_panel.Show()
            self.bottom_panel.Show()
            self.grid_panel.Show()
            self.hidden = False

    def toggle_gui(self):
        if self.hidden:
            self.top_panel.Show()
            self.bottom_panel.Show()
            self.grid_panel.Show()
            self.hidden = False
            self.mosaic_panel.set_inactive()
            self.SetCursor(wx.NullCursor)
            self.focused_panel.SetFocus()
        else:
            self.top_panel.Hide()
            self.bottom_panel.Hide()
            self.grid_panel.Hide()
            self.hidden = True
            self.mosaic_panel.set_noborder()
            cursor = wx.Cursor(wx.CURSOR_BLANK)
            self.SetCursor(cursor)
        self.Layout()

    def CmdLiveScreen(self, evt):
        self.show_gui(self.grid_panel.rowtype if self.grid_panel is not None else RowType.SERVICE_OR_CHANNEL)


    def CmdLiveChannels(self, evt):
        self.show_gui(RowType.SERVICE_OR_CHANNEL)

    def CmdLiveRecordings(self, evt):
        self.show_gui(RowType.REC)

    def ShowSatList(self):
        self.show_gui(RowType.SAT)

    def ShowChgList(self):
        self.show_gui(RowType.CHG)

    def CmdLiveEpg(self, evt):
        self.show_gui(RowType.GRIDEPG)
        pass

    def CmdToggleGui(self, evt):
        self.toggle_gui()

    def CmdToggleRecord(self, event):
        if self.hidden:
            self.OnToggleLiveRecord()
        else:
            return self.grid_panel.OnToggleRecord(event)

    def OnToggleLiveRecord(self):
        ls = wx.GetApp().live_service_screen
        service = ls.selected_service
        start_time = datetime.datetime.now(tz=tz.tzlocal())
        show_record_dialog(self, service, start_time = start_time)

    def ServiceTune(self, *args, **kwds):
        dtdebug(f'ServiceTune {args} {kwds}')
        self.mosaic_panel.ServiceTune(*args, **kwds)

    def CmdFullScreen(self, evt):
        return self.parent.CmdFullScreen(evt)

    def CmdStop(self, event):
        dtdebug('CmdStop')
        return self.mosaic_panel.OnStop(event)

    def CmdJumpForward(self, event=None):
        dtdebug('CmdJumpForward')
        return wx.GetApp().Jump(60)

    def CmdJumpBack(self, event=None):
        dtdebug('CmdJumpBack')
        return wx.GetApp().Jump(-60)

    def CmdVolumeUp(self, evt):
        dtdebug('CmdVolumeUp')
        self.mosaic_panel.ChangeVolume(+1)

    def CmdVolumeDown(self, evt):
        dtdebug('CmdVolumeDown')
        self.mosaic_panel.ChangeVolume(-1)

    def CmdFullScreen(self, evt):
        wx.GetApp().frame.FullScreen()
        after = wx.Window.FindFocus()
