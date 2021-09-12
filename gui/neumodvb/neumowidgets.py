#!/usr/bin/python3
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
import wx.adv
import wx.lib.agw.pygauge as PG
import wx.lib.agw.peakmeter as PM
import wx.lib.masked as masked
import datetime
import re

def _set_textctrl_size_by_chars(self, tc, w, h):
    sz = tc.GetTextExtent('X')
    sz = wx.Size(sz.x * w, sz.y * h)
    tc.SetInitialSize(tc.GetSizeFromTextSize(sz))

    """
GTK+ 3.6 introduced caching of widget styling information and this seems to be the culprit with my sizing issue. The workaround suggested on ticket 16088 (using wxEVT_SHOW) did not work for my project. However, I was able to find my own workaround. For the benefit of others who are experiencing a problem similar to mine, try this:
    """
def _get_size_by_chars(self, w, h):
    sz = self.GetTextExtent('X')
    sz = wx.Size(sz.x * w, sz.y * h)
    return sz


def add_table(self):
    rows = 3
    cols = 2
    labels=['frequency', 'symbolrate', 'polarisation']
    table = self.WriteTable(rows, cols)
    for col in range(cols):
        for row in range(rows):
            cell = table.GetCell(row, col)
            cell.Clear()
            attr = wx.richtext.RichTextAttr(style=wx.NO_BORDER)
            cell.SetStyle(attr)
            if col == 0:
                cell.AddParagraph(labels[row])
            else:
                cell.AddParagraph("This is the cell at Row {0}, Column {1}".format(row, col))


def _set_textctrl_size_by_chars(self, tc, w, h):
    sz = tc.GetTextExtent('X')
    sz = wx.Size(sz.x * w, sz.y * h)
    tc.SetInitialSize(tc.GetSizeFromTextSize(sz))

def _get_size_by_chars(self, w, h):
    sz = self.GetTextExtent('X')
    sz = wx.Size(sz.x * w, sz.y * h)
    return sz

def parse_duration(val):
    dummy='00:00'
    if len(val) > 5:
        return False;
    val+dummy[len(val):]
    m=re.match(r'^([0-9]+):([0-9]+)', val)
    if m is None:
        return None
    if int(m.groups()[1])>=60:
        return None
    return (int(m.groups()[1]) + 60 * int(m.groups()[0]))*60

def parse_time(val, is_duration=False):
    dummy='00:00'
    if len(val) > 5:
        return False;
    val+dummy[len(val):]
    m=re.match(r'^([0-9]+):([0-9]+)', val)
    if m is None:
        return None
    if not is_duration and int(m.groups()[0])>=24:
        return None
    if int(m.groups()[1])>=60:
        return None
    return (int(m.groups()[1]) + 60 * int(m.groups()[0]))*60

class TimeValidator(wx.Validator): # Create a validator subclass
    def __init__(self):
        wx.Validator.__init__(self)
        self.ValidInput = ['.','0','1','2','3','4','5','6','7','8','9']
        self.StringLength = 0
        self.Bind(wx.EVT_CHAR,self.OnCharChanged) # bind character input event
    def is_valid(self, x):
        if len(x)==0:
            return True
        if x[0] not in (0,1,2):
            return False;
        if len(x) < 2:
            return True

    def OnCharChanged(self, event):
        # Get the ASCII code of the input character
        keycode = event.GetKeyCode()
        # Backspace (ASCII code is 8), delete a character.
        if keycode == 8:
            self.StringLength -= 1
            # Event continues to pass
            event.Skip()
            return

        # Convert ASII code into characters
        InputChar = chr(keycode)

        if InputChar in self.ValidInput:
            # The first character is ., illegal, intercept the event, will not be successfully entered
            if InputChar == '.' and self.StringLength == 0:
                return False
            # In the allowed input range, continue to deliver the event.
            else:
                event.Skip()
                self.StringLength += 1
                return True
        return False

    def Clone(self):
        return TimeValidator()

    def Validate(self,win): #1 Use validator method
        textCtrl = self.GetWindow()
        text = textCtrl.GetValue()
        valid_text = ''
        for i in text:
            if i in self.ValidInput:
                valid_text += i
        textCtrl.SetValue(valid_text)
        return True

    def TransferToWindow(self):
        return True

    def TransferFromWindow(self):
        return True

class DatePickerCtrl(wx.adv.DatePickerCtrl):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds)
        sz = self.GetTextExtent("23:59  ")
        self.SetInitialSize(self.GetSizeFromTextSize(sz))

class TimePickerCtrlOFF(wx.TextCtrl):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds, validator=TimeValidator())
        sz = self.GetTextExtent("23:59 ")
        self.SetInitialSize(self.GetSizeFromTextSize(sz))
        #self.SetValidator(TimeValidator)

class DurationTextCtrl(wx.TextCtrl):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds, validator=TimeValidator())
        sz = self.GetTextExtent("23:59 ")
        self.SetInitialSize(self.GetSizeFromTextSize(sz))

    def GetSeconds(self):
        return parse_duration(self.GetValue())

    def SetValueTime(self, val):
        if type(val) == datetime.timedelta:
            minutes = val.seconds//60
        elif type(val) == int:
            minutes = val//60
        else:
            assert 0
        val = f"{minutes // 60:02}:{minutes % 60:02}"
        self.SetValue(val)

class TimeTextCtrl(wx.TextCtrl):
    def __init__(self, parent, *args, **kwds):
        super().__init__(parent, *args, **kwds, validator=TimeValidator())
        sz = self.GetTextExtent("23:59 ")
        self.SetInitialSize(self.GetSizeFromTextSize(sz))

    def GetSeconds(self):
        return parse_duration(self.GetValue())


class BarGauge(PM.PeakMeterCtrl):
    def __init__(self, parent, id, range, *args, **kwargs):
        self.parent = parent
        super().__init__(parent, id,  style=wx.SUNKEN_BORDER, agwStyle=PM.PM_HORIZONTAL, size=(-1,10))
        self.SetMeterBands(1, 25)
        self._clrNormal, self._clrHigh = self._clrHigh, self._clrNormal

    def SetValue(self, value):
        value = self.ranges[0] if value is None else min(max(self.ranges[0], value), self.ranges[3])
        value = int((value- self.ranges[0])*self.scale)
        self.SetData([value], 0, 1)

    def SetRange(self, ranges):
        self.ranges = ranges
        l = ranges[-1] - ranges[0]
        assert l>0
        self.scale = 100/l
        r = [int((x - ranges[0])*self.scale) for x in ranges[1:] ]
        self.SetRangeValue(r[0], r[1], r[2])

class BarGaugeOFF(PG.PyGauge):
    def __init__(self, parent, id, range, *args, **kwargs):
        self.parent = parent
        super().__init__(parent, id, *args, **kwargs)
        self.SetBackgroundColour(wx.RED)
        self.SetBarColor([wx.Colour(162,255,178),wx.Colour(159,176,255)])
        #make widget fit to parent
        wx.CallAfter(self.tst)

    def DoGetBestSize(self):
        return wx.Window.DoGetBestSize(self)

    def tst(self):
        self.SetRange(100)
        self.xxx=10
        self.SetValue([self.xxx, self.xxx+20])

    def OnXXX(self,evt):
        if self.xxx >=80:
            self.xxx=0
            self.SetValue([self.xxx, self.xxx+20])
        else:
            self.xxx +=10
            self.SetValue([self.xxx, self.xxx+20])
        self.Refresh()

    def OnClose(self, evt):
        dtdebug("OnClose called")
        self.dttimer.Stop()


class DiseqcChoice(wx.Choice):
    def __init__(self, id,  *args, **kwargs):
        from neumodvb import neumodbutils
        import pychdb
        self. choices = neumodbutils.enum_labels(pychdb.rotor_control_t)
        kwargs['choices'] = self.choices
        super().__init__(id, *args, **kwargs)

    def SetValue(self, lnb) :
        from neumodvb import neumodbutils
        import pychdb
        val = neumodbutils.enum_to_str(neumodbutils.get_subfield(lnb, 'rotor_control'))
        idx = self.choices.index(val)
        self.SetSelection(idx)

    def GetValue(self) :
        from neumodvb import neumodbutils
        import pychdb
        idx = self.GetCurrentSelection()
        choice = self.choices[idx]
        val = neumodbutils.enum_value_for_label(pychdb.rotor_control_t, choice)
        return val
