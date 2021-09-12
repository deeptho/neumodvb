# Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

from neumodvb.util import setup
setup()

import pychdb
import pyepgdb
from functools import cached_property, lru_cache
import datetime
from dateutil import tz
from neumodvb.chepglist import content_types
from neumodvb.util import dtdebug, dterror
#from dateutil import tz

from pyreceiver import set_gtk_window_name
from wx.richtext import RichTextCtrl

class GridEpgData(object):

    def __init__(self, parent):
        self.parent = parent
        self.set_start(datetime.datetime.now(tz=tz.tzlocal()))
        self.service_sort_column='ch_order'
        self.epg_sort_column = 'k.start_time'
        self.service_sort_order = pychdb.service.subfield_from_name(self.service_sort_column) << 24
        self.epg_sort_order = pyepgdb.epg_record.subfield_from_name(self.epg_sort_column) << 24
        self.chdb =  wx.GetApp().chdb
        self.epgdb = wx.GetApp().epgdb
        self.epg_screens = None
        self.ls = None
        ls = wx.GetApp().live_service_screen
        self.ls = ls
    @property
    def services_screen(self):
        return self.ls.screen

    def GetNumberRows(self):
        ret = 0 if self.services_screen is None else self.services_screen.list_size
        return ret
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
    def GetServiceAtRow(self, rowno):
        if rowno >= self.services_screen.list_size:
            return None
        ret = self.services_screen.record_at_row(rowno)
        return ret
    @lru_cache(maxsize=1) #cache the last row, because multiple columns will lookup same row
    def GetEpgScreenAtRow(self, rowno):
        service = self.GetServiceAtRow(rowno)
        if service is None:
            return None
        if self.epg_screens is None:
            self.epg_screens = pyepgdb.gridepg_screen(self.start_time_unixepoch,
                                                      self.parent.num_services_on_screen, self.epg_sort_order)
        key = service.k if type(service) == pychdb.service.service else service.k.service
        epg_screen = self.epg_screens.epg_screen_for_service(key)
        if epg_screen is None:
            txn = self.epgdb.rtxn()
            epg_screen = self.epg_screens.add_service(txn, key)
            txn.abort()
        return epg_screen

    def remove_epg_data_for_channels(self, chidx_start, chidx_end):
        for idx in range(chidx_start, chidx_end):
            service = self.GetServiceAtRow(idx)
            if service is not None:
                key = service.k if type(service) == pychdb.service.service else service.k.service
                self.epg_screens.remove_service(key)
        self.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version

    def GetChannel(self, rowno):
        if rowno < 0 or self.services_screen is None or rowno >= self.services_screen.list_size:
            return None
        return self.GetServiceAtRow(rowno)

    def GetEpgRecord(self, ch_idx, epg_idx):
        if ch_idx < 0 or ch_idx >= self.services_screen.list_size:
            return None
        epg_screen = self.GetEpgScreenAtRow(ch_idx)
        if epg_idx < 0 or epg_idx >= epg_screen.list_size:
            return None
        #print(f"GET EPG {epg_idx}")
        return epg_screen.record_at_row(epg_idx)
    def first_epg_record(self, ch_idx, start_time):
        """
        idx = channel index
        """
        epg_screen = self.GetEpgScreenAtRow(ch_idx)
        start_time = start_time
        for epg_idx in range(0, epg_screen.list_size):
            #print(f"GET EPG first ch={ch_idx} {epg_idx}")
            epg = epg_screen.record_at_row(epg_idx)
            if epg.end_time> start_time:
                return epg_idx
        return -1
    def OnSelectService(self, service):
        self.ls.SelectServiceOrChannel(service)


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
    """Text control with an icon"""

    def __init__(self, parent, id,  content, bgcolour=None, fgcolour=None, scheduled=False):
        super().__init__(parent, id)

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


class ChGridRow(object):
    """
    One row on screen, composed of a channel on the left, and zero or more epg records on the rigght
    Rows may be empty if the number of channels is too small
    rowno is the number of the row on the screen, startin at rowno=0
    """
    def __init__(self, parent, rowno, *args, **kwds):
        self.grid = parent
        self.rowno = rowno
        self.data = self.grid.data
        self.ch_cell = None
        self.ch_cell= self.add_ch_cell(span=(1, self.grid.chwidth),
                                            bgcolour=self.grid.black,
                                            fgcolour=self.grid.white)
        self.epg_cells=[]
        self.current_cell = None


    def add_ch_cell(self, span, fgcolour, bgcolour, ref_for_tab_order=None):
        cell = wx.TextCtrl(self.grid, wx.ID_ANY, "", style= wx.TE_READONLY|wx.BORDER_NONE)
        self.grid.add_cell(pos=(self.rowno+self.grid.row_offset, 0), span=span, cell=cell,
                           fgcolour=fgcolour, bgcolour=bgcolour, ref_for_tab_order=ref_for_tab_order)
        cell.data = ChannelCellData(self)
        return cell

    def add_epg_cell(self, span, colno, ref_for_tab_order=None, fgcolour=None, bgcolour=None, epg=None,
                     start_time=None):
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
            cell = EpgCell(self.grid, wx.ID_ANY, content, scheduled=scheduled)
        else:
            cell = wx.TextCtrl(self.grid, wx.ID_ANY, content, style= wx.TE_READONLY)
        #print(f'EPG: {self.rowno+self.grid.row_offset}, {colno}')
        self.grid.add_cell(pos=(self.rowno+self.grid.row_offset, colno), cell=cell, span=span,
                                  fgcolour=fgcolour, bgcolour=bgcolour,
                                  ref_for_tab_order=ref_for_tab_order)
        cell.data = EpgCellData(self, epg, start_time, colno)
        return cell

    def SetFocus(self):
        cell = self.ch_cell if self.current_cell is None else self.current_cell
        cell.SetFocus()
        self.grid.last_focused_cell = cell
    def update(self):
        """Redraw the channel and epg for this row after something changes (e.g., top_idx)
        """
        self.remove_epg()
        self.service = self.data.GetChannel(self.grid.top_idx + self.rowno)
        if self.service is None:
            print(f'NO SERVICE: {self.grid.top_idx} {self.rowno}')
            self.ch_cell.SetValue("")
        else:
            if type(self.service) == pychdb.chgm.chgm:
                self.ch_cell.SetValue(f'{self.service.chgm_order: <6} {self.service.name}')
            else:
                self.ch_cell.SetValue(f'{self.service.ch_order: <6} {self.service.name}')
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
        if getattr(self.ch_cell, 'label', self.ch_cell) == self.grid.last_focused_cell:
            self.grid.last_focused_cell = None
        self.grid.gbs.Detach(self.ch_cell)
        self.ch_cell.Destroy()
        self.ch_cell = None
        self.remove_epg()

    def move_to_row(self, rowno):
        """
        make this service appear on a different row, which must be empty
        rowno=0 means first service
        """
        pos =  self.grid.gbs.GetItemPosition(self.ch_cell)
        pos[0] = rowno + self.grid.row_offset
        self.grid.gbs.SetItemPosition(self.ch_cell, pos)
        self.rowno = rowno
        for cell in self.epg_cells:
            pos =  self.grid.gbs.GetItemPosition(cell)
            pos[0] = rowno + self.grid.row_offset
            self.grid.gbs.SetItemPosition(cell, pos)
        return self

    def is_leftmost_epg_cell(self, w):
        """
        check if window w is the left most epg cell
        """
        return w.data.colno == self.grid.chwidth
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
        col_offset = self.grid.chwidth
        colour=self.grid.green

        self.epg_cells = []
        start_time = self.data.start_time_unixepoch
        ch_idx = self.grid.top_idx+self.rowno
        epg_idx = self.data.first_epg_record(ch_idx, start_time)
        ref = self.ch_cell
        last_col = 0
        last_time = start_time
        while True:
            #add empty placeholder
            if epg_idx < 0:
                span = self.grid.num_cols - last_col
            else:
                epg_record = self.data.GetEpgRecord(ch_idx, epg_idx)
                if epg_record is None:
                    epg_idx = -1
                    continue
                start_col = max(0, (epg_record.k.start_time - start_time)//self.grid.col_duration)
                if start_col < last_col:
                    pass # print(f'{start_col}, {last_col}, {end_col}')
                if start_col < last_col:
                    epg_idx +=1
                    continue
                assert start_col >= last_col
                end_col = max(0, (epg_record.end_time - start_time)//self.grid.col_duration)
                assert end_col >= start_col
                span = min(start_col, self.grid.num_cols) - last_col
            if span>0: # we need to draw an empty cell before the next epg record
                #print(f'DRAW empty {self.rowno} {datetime.datetime.fromtimestamp(last_time, tz=tz.tzlocal())}')
                cell = self.add_epg_cell(start_time= last_time, span=(1,span), colno = col_offset + last_col,
                                         bgcolour=self.grid.gray, ref_for_tab_order=ref)
                self.epg_cells.append(cell)
                ref=cell
            if epg_idx<0 or start_col >= self.grid.num_cols:
                break #all done for this row
            last_col = min(end_col, self.grid.num_cols)
            span  = last_col - start_col
            if span != 0:
                cell=self.add_epg_cell(epg=epg_record, span=(1,span), colno = col_offset + start_col,
                                   bgcolour=self.grid.epg_colour, fgcolour = self.grid.white,
                                   ref_for_tab_order = ref)
                self.epg_cells.append(cell)
                ref=cell
                last_time = epg_record.end_time
            if last_col >=self.grid.num_cols:
                break  #all done for this row
            epg_idx +=1

    def focus_current(self, last_focused_cell):
        """
        set the focus on the cell which is closest in time
        """
        if last_focused_cell is not None:
            if last_focused_cell.data.is_ch or self.grid.focus_time is None: # we are positioned in a channel column
                self.ch_cell.SetFocus()
                return

        selected = None
        selected_start_time = None
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
        selected.SetFocus()

class GridEpgPanel(wx.Panel):
    gbsDescription = """\
    The wx.GridBagSizer is similar to the wx.FlexGridSizer except the items are explicitly positioned in a virtual cell of the layout grid, and column or row spanning is allowed.  For example, this static text is positioned at (0,0) and it spans 7 columns.
    """
    row_offset = 1 # row number at which epg_row==0 is positioned
    black=wx.Colour(0, 0, 0)
    white=wx.Colour(255, 255, 255)
    red=wx.Colour(255, 0, 0)
    green=wx.Colour(0, 255, 0)
    yellow=wx.Colour(255, 255, 0)
    gray=wx.Colour(128, 128, 128)
    epg_colour=wx.Colour()
    epg_highlight_colour=wx.Colour()
    def set_dimensions(self):
        self.chwidth = 9 #in columns
        self.num_services_on_screen = 10
        self.col_duration = 5*60 # 5 minutes
        self.num_cols = 36 # 3 hours

    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        self.allow_all = True
        self.restrict_to_sat = None
        self.make_colours()
        self.set_dimensions()
        self.data = GridEpgData(self)
        self.rows = []
        self.prepare()
        self.time_cells=[]
        self.green.Set('dark green')
        #gbs.SetDimension((-1,-1), (100, 100))
        self.Bind(wx.EVT_SHOW, self.OnShowWindow)
        self.Bind(wx.EVT_WINDOW_DESTROY, self.OnDestroyWindow)
        self.gbs = None
        self.focus_time = None
        #self.create1()
        self.created = False
        self.current_rowno = 0 # relative to top channel on screen
        self.now = datetime.datetime.now(tz=tz.tzlocal()).replace(second=0)
        self.data.start_time = self.now.replace(minute=self.now.minute - self.now.minute%30, second=0)
        self.Bind(wx.EVT_MOUSEWHEEL, self.OnMouseWheel)
        self.Bind(wx.EVT_SCROLL, self.OnScroll)
        #self.items.Bind(wx.EVT_MOUSEWHEEL, lambda e: e.Skip())
        self.wheel_count=0
        self.set_initial_top_idx()

    def set_initial_top_idx(self):
        entry = self.data.ls.selected_service_or_channel
        if entry is None:
            return 0
        entry_idx = self.data.services_screen.set_reference(entry)
        if self.data.services_screen.list_size - entry_idx < self.num_services_on_screen:
            # scroll down to fir more services on screen
            top_idx =max(self.data.services_screen.list_size - self.num_services_on_screen, 0)
        else:
            # put selected service at top of screen
            top_idx = entry_idx
        self.top_idx = top_idx


    @property
    def service_idx(self):
        """
        index in the services list of the service on this row
        """
        assert self.top_idx is not None # must be initialised
        return self.top_idx + self.current_rowno
    @service_idx.setter
    def service_idx(self, idx):
        """
        idx is the index of a channel in the current data screen
        """
        if self.top_idx is None or idx - self.top_idx < self.num_services_on_screen and idx-self.top_idx >= 0:
            self.current_rowno = idx - self.top_idx
        else:
            assert 0
    def OnScroll(self, event):
        #print(f'scroll {event.GetPosition()}')
        idx = event.GetPosition()
        if idx >=0 and idx < self.data.services_screen.list_size:
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
        if self.wheel_count < -5:
            self.scroll_down(+delta)
            self.wheel_count = 0
            #self.ScrollLines(3)
        elif self.wheel_count > 5:
            #print('scroll -3')
            self.scroll_down(-delta)
            self.wheel_count = 0
            #self.ScrollLines(-3)
    def remove_epg_data_for_rows(self, rowno_start, rowno_end):
        rowno_start = max(0, rowno_start)
        rowno_end = min(rowno_end, self.num_services_on_screen)
        if rowno_start >= rowno_end:
            return
        for row in self.rows[rowno_start:rowno_end]:
            service = row.service
            if service is not None:
                key = service.k if type(service) == pychdb.service.service else service.k.service
                self.data.epg_screens.remove_service(key)
        self.data.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version
    def check_for_new_records(self):
        if False: #todo
            txn_service = None;
            changed = self.services_screen.update(txn_service)
            if changed:
                pass
        txn_epg = self.data.epgdb.rtxn()
        any_change=False
        for rowno, row in enumerate(self.rows):
            if row.service is not None:
                key = row.service.k if type(row.service) == pychdb.service.service else row.service.k.service
                epg_screen = self.data.epg_screens.epg_screen_for_service(key)
                #changed = epg_screen.update(txn_epg)
                if epg_screen is None:
                    print('here')
                changed = epg_screen.update_between(txn_epg, self.data.start_time_unixepoch,
                                                    self.data.end_time_unixepoch)
                any_change |= changed
                if changed:
                    dtdebug(f"Updating service {row.service}")
                    #self.epg_screens.remove_service(service)
                    if self.last_focused_cell is None:
                        print('here')
                    refocus = self.last_focused_cell.data.row.rowno == row.rowno
                    row.update()
                    if refocus:
                        row.focus_current(self.last_focused_cell)
        txn_epg.abort()
        if any_change:
            self.Layout()

    def OnTimer(self, evt):
        #print('timer')
        now = datetime.datetime.now(tz=tz.tzlocal())
        now=now.replace(second=now.second - now.second%2)
        self.data.start_time = now.replace(minute=now.minute - now.minute%30, second=0)
        if self.now != now:
            self.now = now
            self.check_for_new_records()
            #print('calling refresh')
            wx.CallAfter(self.Refresh)



    def OnShowWindow(self, evt):
        if not evt.IsShown():
            return #happens also at window creation
        if not self.created:
            self.created=True
            self.create()
            self.Refresh()
        else:
            print('show window')
            self.reset()
            self.Refresh()
        evt.Skip()

    def update_scrollbar(self):
        page_size = self.num_services_on_screen
        #print(f'scrollbar: {self.top_idx} - {self.data.services_screen.list_size}')
        self.scrollbar.SetScrollbar(position=self.top_idx, thumbSize=page_size,
                                   range=self.data.services_screen.list_size,
                                   pageSize=page_size, refresh=True)

    def create(self):
        self.scrollbar = wx.ScrollBar(self, style=wx.SB_VERTICAL)
        gbs = self.gbs = wx.GridBagSizer(vgap=5, hgap=5)
        self.SetBackgroundColour(self.black)
        self.make_periods(gbs)
        self.sizer =  wx.FlexGridSizer(1, 2, 0, 0)
        self.sizer.AddGrowableCol(1)
        self.sizer.AddGrowableRow(0)
        self.sizer.Add(self.scrollbar, 1, wx.EXPAND, 0)
        self.sizer.Add(gbs, 1, wx.EXPAND, 0)
        gbs.SetSizeHints(self)
        #self.sizer.SetSizer(gbs)
        self.SetSizer(self.sizer)
        #self.scrollbar.SetScrollbar(10, 16, 50, 15)
        set_gtk_window_name(self, "gridepg") #needed to couple to css stylesheet

        gbs =self.gbs
        self.make_channels()
        self.update_periods()
        self.make_info(gbs, self.num_services_on_screen+2)
        self.gbs.AddGrowableRow(self.num_services_on_screen + 2)
        for i in range(gbs.GetCols()):
            gbs.AddGrowableCol(i)
        if False:
            for i in range(1, gbs.GetRows()): #skip the time line
                gbs.AddGrowableRow(i)
        self.last_focused_cell_ = None
        #self.Add(gbs, 1, wx.EXPAND | wx.ALL, 10)
        gbs.SetSizeHints(self)
        self.Layout()
        #self.overlay = wx.Overlay()
        #wx.CallAfter(self.OnPaint, None)
        #self.timer = wx.Timer(self)
        #self.Bind(wx.EVT_TIMER, self.OnTimer, self.timer)
        #self.timer.Start(60000)
        self.rows[self.current_rowno].SetFocus()
        self.painter =None
        self.Bind(wx.EVT_PAINT, self.OnPaint)
        #self.timer_count=0
        #self.Bind(wx.EVT_SIZE, self.OnSize)
        #self.now_brush = wx.Brush(colour=wx.Colour(0, 0, 0, 10), style=wx.BRUSHSTYLE_TRANSPARENT)
        self.now_brush = wx.Brush(colour=self.red) #for "now" vertical line
        self.time_tick_brush = wx.Brush(colour=self.white) #for "now" vertical line
        self.Bind(wx.EVT_CHILD_FOCUS, self.OnCellFocus)
        self.Bind(wx.EVT_CHAR_HOOK, self.OnKey)
        #self.Bind(wx.EVT_LEFT_CLICK, self.OnLeftClicked)
        self.gbs = gbs
        self.update_scrollbar()
    @property
    def last_focused_cell(self):
        return self.last_focused_cell_
    @last_focused_cell.setter
    def last_focused_cell(self, val):
        self.last_focused_cell_ = val
        #print(f'last_focused_cell_ set to {val}')

    def OnDestroyWindow(self, evt):
        pass
        evt.Skip()

    def make_colours(self):
        self.channel_highlight_colour = wx.Colour('cyan')
        self.epg_colour.Set('dark blue')
        self.epg_highlight_colour.Set('yellow')
    def make_channels(self):
        for idx in range(0, self.num_services_on_screen):
            self.rows.append(ChGridRow(self, idx))
        for rowno, row in enumerate(self.rows):
            row.update()

    def reset(self):
        self.data.GetEpgScreenAtRow.cache_clear() #important to avoid using stale version
        self.set_initial_top_idx()
        self.Freeze()
        num_rows = len(self.rows)
        self.remove_epg_data_for_rows(0, num_rows)
        for row in self.rows:
            row.remove()
        for rowno in range(self.num_services_on_screen):
            self.rows[rowno] = ChGridRow(self, rowno)
            self.rows[rowno].update()
        self.Thaw()

        self.rows[self.current_rowno].SetFocus()
    def update_channels(self, old_top_idx=None):
        assert old_top_idx is not None

        self.Freeze()
        if old_top_idx < self.top_idx:
            self.remove_epg_data_for_rows(0, self.top_idx - old_top_idx)
        elif old_top_idx > self.top_idx:
            self.remove_epg_data_for_rows(self.top_idx - old_top_idx + len(self.rows),
                                                   len(self.rows))
        if old_top_idx < self.top_idx:
            num_rows =self.top_idx - old_top_idx
            for row in self.rows[0:num_rows]:
                row.remove()
            for rowno, row in enumerate(self.rows[num_rows:]):
                row.move_to_row(rowno)
                self.rows[rowno] = row
                self.rows[num_rows + rowno] = None
            first = max(self.num_services_on_screen-num_rows,0)
            for rowno in range(first,self.num_services_on_screen):
                self.rows[rowno] = ChGridRow(self, rowno)
                self.rows[rowno].update()

        if old_top_idx is not None and old_top_idx > self.top_idx:
            num_rows =old_top_idx -self.top_idx
            for row in self.rows[-num_rows:]:
                row.remove()
            for rowno_, row in enumerate(self.rows[-num_rows-1::-1]):
                rowno = self.num_services_on_screen-1 -rowno_
                row.move_to_row(rowno)
                self.rows[rowno] = row
                self.rows[rowno - num_rows] = None
            for rowno in range(0, min(num_rows, self.num_services_on_screen)):
                self.rows[rowno] = ChGridRow(self, rowno)
                self.rows[rowno].update()

        self.Thaw()
        self.gbs.Layout()
        for rowno, row in enumerate(self.rows):
            assert rowno == row.rowno

    def ChannelText(self):
        pass
    def DrawCurrentTime(self):
        now = self.now
        times = self.data.periods
        t1, t2 = times[0], times[-1]
        x1=self.time_cells[0].GetPosition().Get()[0]
        x2 =self.time_cells[-1].GetPosition().Get()[0]
        x2 += self.time_cells[-1].GetSize().Get()[0]
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


    def OnUpdateOFF(self, e):
        self.DrawCurrentTime()

    def OnPaint(self, e):
        dc = wx.PaintDC(self)
        upd = wx.RegionIterator(self.GetUpdateRegion())
        rects=[]
        while upd.HaveRects():
            rect = upd.GetRect()
            rects.append(rect)
            # Repaint this rectangle
            #PaintRectangle(rect, dc)
            upd.Next()
        #print (f'PAINT {rects}')
        wx.CallAfter(self.DrawCurrentTime)

        e.Skip(True)
    def save_focus_time(self, focused_window):
        if focused_window.data.epg is not None:
            self.focus_time = focused_window.data.epg.k.start_time
            #print(f'focus time set to {datetime.datetime.fromtimestamp(self.focus_time, tz=tz.tzlocal())}')
        elif not focused_window.data.is_ch and focused_window.data.start_time is not None:
            self.focus_time = focused_window.data.start_time
            #print(f'focus time (2) set to {datetime.datetime.fromtimestamp(self.focus_time, tz=tz.tzlocal())}')
    def scroll_down(self, rows):
        old_top_idx = self.top_idx
        self.top_idx += rows
        if self.top_idx < 0:
            self.top_idx = 0
        elif self.top_idx + self.num_services_on_screen > self.data.GetNumberRows():
            self.top_idx = self.data.GetNumberRows() - self.num_services_on_screen
        self.update_channels(old_top_idx)
        wx.CallAfter(self.update_scrollbar)

    def scroll_leftright(self):
        old_top_idx = self.top_idx
        self.data.remove_epg_data_for_channels(self.top_idx, self.top_idx+self.num_services_on_screen)
        if False:
            for rowno, row in enumerate(self.rows):
                self.data.GetChannel(self.top_idx+rowno)
        for rowno, row in enumerate(self.rows):
            row.update()
        self.update_periods()
        self.Layout()
    def focus_row(self, last_focused_cell, rowno):
        """
        rowno = index of row on screen
        """
        assert rowno>=0 and rowno < self.num_services_on_screen
        #print(f'focus_row: {last_focused_cell.data.epg}')
        self.rows[rowno].focus_current(last_focused_cell)

    def OnLeftClicked(self, evt):
        #print("RESET FOCUS TIME")
        self.focus_time = None # force reset
    def rightmost_start_time(self):
        start_time = int(self.periods[-1].timestamp())
        for row in self.rows:
            end_time = max(row.rightmost_start_time(), start_time)
        return start_time
    def OnKey(self, evt):
        w = wx.Window.FindFocus()
        key = evt.GetKeyCode()
        if w.data is None or not self.IsDescendant(w) or w is self:
            evt.Skip(True)
            return
        w = wx.Window.FindFocus()
        row = w.data.row
        from neumodvb.servicelist import IsNumericKey, ask_channel_number
        # print(f'e={len(self.epg_cells)} c={len(self.channel_cells)} ch_idx={ch_idx}')
        if IsNumericKey(key):
            chno = ask_channel_number(self, key- ord('0'))
            print(f'need to move to {chno}')
            entry  = self.data.ls.entry_for_ch_order(chno)
            if entry is not None:
                old_top_idx = self.top_idx
                self.top_idx = self.data.services_screen.set_reference(entry)
                self.update_channels(old_top_idx)
                self.focus_row(w, 0)
            return
        if key == wx.WXK_RIGHT:
            if row.is_rightmost_epg_cell(w):
                print("End of line detected")
                start_cell= row.epg_cells[-1]
                start_time = self.rightmost_start_time()
                start_time = max(self.data.start_time_unixepoch +30*60, start_time)
                self.data.set_start(start_time)
                self.scroll_leftright()
                #self.focus_time=None
                row.epg_cells[-1].SetFocus()
                #evt.Skip(False)
                return
            else:
                if True:
                    to_focus = w.data.row.neighboring_cell(w, left_neighbor=False)
                    assert to_focus is not None
                    to_focus.SetFocus()
                    evt.Skip(True)
                else: #this code should work, but only works if the complete epg grid is created at startup
                      #instead of on first use
                    ctl = wx.Window.FindFocus()
                    ctl.Navigate()
                    evt.Skip(True)
        elif key == wx.WXK_LEFT:
            if evt.ControlDown():
                row.ch_cell.SetFocus()
                return
            if row.is_leftmost_epg_cell(w):
                print("Start of line detected")
                start_cell= row.epg_cells[0]
                start_time = self.data.start_time_unixepoch -30*60
                #if an epg line has at least two entries, then one of them must have epg
                if start_cell.data.epg is not None:
                    start_time = start_cell.data.epg.k.start_time-1
                self.data.set_start(start_time)
                self.scroll_leftright()
                #self.focus_time=None
                row.epg_cells[0].SetFocus()
                return
            else:
                if True:
                    to_focus = w.data.row.neighboring_cell(w, left_neighbor=True)
                    assert to_focus is not None
                    to_focus.SetFocus()
                    evt.Skip(True)
                else: #this code should work, but only works if the complete epg grid is created at startup
                      #instead of on first use
                    ctl = wx.Window.FindFocus()
                    ctl.Navigate(flags=wx.NavigationKeyEvent.IsBackward)
                    evt.Skip(True)
        elif key in (wx.WXK_DOWN, wx.WXK_PAGEDOWN):
            #sizer=w.GetContainingSizer()
            rows_to_scroll = 1 if key == wx.WXK_DOWN else self.num_services_on_screen-1
            if row.rowno < self.num_services_on_screen-1:
                if key == wx.WXK_PAGEDOWN:
                    self.focus_row(w, self.num_services_on_screen-1)
                else:
                    self.focus_row(w, row.rowno+1)
                return
            else:
                self.scroll_down(rows_to_scroll)
                self.focus_row(w, self.num_services_on_screen-1)
                return
        elif key in (wx.WXK_UP, wx.WXK_PAGEUP):
            w = wx.Window.FindFocus()
            #sizer=w.GetContainingSizer()
            rows_to_scroll = 1 if key == wx.WXK_UP else self.num_services_on_screen-1
            if row.rowno > 0:
                if key == wx.WXK_PAGEUP:
                    self.focus_row(w, 0)
                else:
                    self.focus_row(w, row.rowno-1)
                return
            else:
                self.scroll_down(-rows_to_scroll)
                self.focus_row(w, 0)
                return
        else:
            evt.Skip(True)

    def HighlightChannel(self, cell, on):
        if on:
            cell.SetForegroundColour(self.channel_highlight_colour)
        else:
            cell.SetForegroundColour(self.white)

    def HighLightCell(self, cell ,on):
        if cell.data is None:
            return False # could be another text cell which cannot be focused
        colno = cell.data.colno
        #print(f'HighlightCell: {cell.data.epg}')
        if colno >= self.chwidth:
            #print(f"highlighted cell is epg row{row}")
            if on:
                cell.SetForegroundColour(self.epg_highlight_colour)
            else:
                cell.SetForegroundColour(self.white)
        else:
            self.HighlightChannel(cell, on)
        return True
    def OnCellFocus(self, event):
        w = event.GetWindow()
        #print("CELLFOCUS...................")
        if self.last_focused_cell is not None:
            self.HighLightCell(self.last_focused_cell, False)
        if self.HighLightCell(w, True):
            self.last_focused_cell = w
            if w.data is not None and w.data.epg is not None:
                self.save_focus_time(w)
            self.set_info(None if w.data is None else w.data.epg)
            if w.data is not None:
                service = self.last_focused_cell.data.row.service
                self.data.OnSelectService(service)
    def prepare(self):
        self.font = self.GetFont()
        dc = wx.ScreenDC()
        self.font.SetPointSize(self.font.GetPointSize()+2)
        self.SetFont(self.font)
        dc.SetFont(self.font)
        w0,h0 = dc.GetTextExtent("100000")
        w1,h1 = dc.GetTextExtent("100000\n11100")
        self.text_height = h1-h0

    def add_cell(self, pos, cell, span=(1,6), bgcolour=None, fgcolour=None,
                 ref_for_tab_order=None):
        if bgcolour is not None:
            cell.SetBackgroundColour(bgcolour)
        if fgcolour is not None:
            cell.SetForegroundColour(fgcolour)
        self.gbs.Add(cell, pos, span, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
        if ref_for_tab_order is not None:
            cell.MoveAfterInTabOrder(ref_for_tab_order)
        cell.data = None
        return cell

    def add_info_cell(self, pos, span=(1,6), content="", bgcolour=None, fgcolour=None, style=0,
                 ref_for_tab_order=None, has_icon=False):

        cell = RichTextCtrl(self, wx.ID_ANY, content, style= wx.TE_READONLY|style)
        cell.data = None
        if bgcolour is not None:
            cell.SetBackgroundColour(bgcolour)
        if fgcolour is not None:
            cell.SetForegroundColour(fgcolour)
        self.gbs.Add(cell, pos, span, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
        if ref_for_tab_order is not None:
            cell.MoveAfterInTabOrder(ref_for_tab_order)
        return cell


    def make_periods(self, gbs):
        now = self.data.start_time
        self.periods = [now + datetime.timedelta(minutes=minutes) for minutes in range(0, 180, 30)]
        self.time_cells = []
        chwidth = 9 # in cells
        content=f'{now:%a %b %d}'
        cell = wx.TextCtrl(self, wx.ID_ANY, content, style= wx.TE_READONLY|wx.BORDER_NONE)
        cell.data = None
        w = self.add_cell(pos=(0, 0),  span=(1,chwidth), bgcolour = self.black,
                          fgcolour=self.channel_highlight_colour,
                          cell=cell)
        self.date_cell = w
        for idx,t in enumerate(self.periods):
            #print(f'QQQQQ {chwidth+idx*6}')
            content=f'{t:%H:%M}'
            cell = wx.StaticText(self, wx.ID_ANY, content, style= wx.TE_READONLY|wx.BORDER_NONE)
            cell.data = None
            w = self.add_cell(pos=(0, chwidth+idx*6), span=(1,6), bgcolour = self.black, fgcolour=self.white,
                              cell=cell)
            self.time_cells.append(w)
            self.numcols = chwidth+idx*6+6

    def update_periods(self):
        now = self.data.start_time
        self.periods = [now + datetime.timedelta(minutes=minutes) for minutes in range(0, 180, 30)]
        self.date_cell.SetValue(f'{now:%a %b %d}')
        for idx, cell in enumerate(self.time_cells):
            t  = self.periods[idx]
            cell.SetLabel(f'{t:%H:%M}')

    def make_info(self, gbs, row):
        startidx = self.chwidth
        pos =(row, startidx)
        colour=wx.Colour(0, 255, 0)
        #gray=wx.Colour(128, 128, 128)
        spany = 1
        spanx = gbs.GetCols() -9
        span=(spany, spanx)
        content = ""
        ref = self.rows[-1].ch_cell
        from wx.richtext import RichTextCtrl
        self.infow = self.add_info_cell(pos, span, content=content, bgcolour=self.black, fgcolour=self.white,
                      style=wx.TE_READONLY|wx.TE_RICH|wx.TE_MULTILINE|wx.TE_NO_VSCROLL|wx.TE_WORDWRAP|wx.BORDER_NONE,
                                   ref_for_tab_order=ref)
        return
    def set_info(self, rec):
        if rec is None:
            self.infow.ChangeValue("")
            return
        dt =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")
        t =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%H:%M:%S")
        e = lambda x: enum_to_str(x)
        f = self.GetFont()
        large = self.infow.GetFont()
        large.SetPointSize(int(f.GetPointSize()*2))
        self.infow.Freeze()
        self.infow.BeginSuppressUndo()
        self.infow.Clear()
        self.infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))
        self.infow.BeginTextColour(wx.GREEN)
        #font=large.Bold()))
        self.infow.WriteText(f"{rec.event_name}")
        self.infow.EndTextColour()
        #self.infow.BeginTextColour((255,255,255))
        self.infow.BeginAlignment(wx.TEXT_ALIGNMENT_RIGHT)
        self.infow.WriteText(f" {dt(rec.k.start_time)} - {t(rec.end_time)}")
        self.infow.EndAlignment()


        #self.infow.SetDefaultStyle(wx.TextAttr(wx.LIGHT_GREY, font=large))
        self.infow.WriteText(f"{content_types(rec.content_codes)}\n\n")

        #self.infow.SetDefaultStyle(wx.TextAttr(wx.WHITE, font=large))
        self.infow.WriteText(f"{rec.story}\n")

        #self.infow.SetDefaultStyle(wx.TextAttr(wx.YELLOW, font=large))
        self.infow.BeginTextColour(wx.YELLOW)
        self.infow.WriteText(str(rec.source))
        self.infow.EndTextColour()
        self.infow.EndSuppressUndo()
        self.infow.Thaw()

    def SelectService(self, service):
        if service is not None:
            print(f"SERVICE {service}")
            old_top_idx = self.top_idx
            self.top_idx = self.data.services_screen.set_reference(service)
            self.update_channels(old_top_idx)
            self.focus_row(None, 0)
            self.data.OnSelectService(service)
    def OnTune(self, event):
        assert self.last_focused_cell is not None
        service = self.last_focused_cell.data.row.service
        dtdebug(f"gridepg tune service {service}")
        wx.GetApp().ServiceTune(service)
    def OnToggleRecord(self, event):
        assert self.last_focused_cell is not None
        from record_dialog import show_record_dialog
        row = self.last_focused_cell.data.row
        service = row.service
        if self.last_focused_cell.data.is_ch:
            show_record_dialog(self, service, start_time = self.data.start_time)
            print(f'need to record service: {service}')
        elif self.last_focused_cell.data.epg is None:
            print(f'need to record anonymous t={self.last_focused_cell.data.start_time}')
            show_record_dialog(self, service, start_time = self.last_focused_cell.data.start_time)
        else:
            print(f"gridepg record {self.last_focused_cell.data.epg}")
            epg=self.last_focused_cell.data.epg
            show_record_dialog(self, service, epg=epg)
            #wx.GetApp().ServiceTune(service)
        pass
