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
import wx.adv
import wx.lib.agw.pygauge as PG
import wx.lib.agw.peakmeter as PM
#import wx.lib.masked as masked
import wx.lib.intctrl
import wx.lib.newevent
import datetime
import re
from neumodvb.util import wxpythonversion, wxpythonversion42
from neumodvb.neumodbutils import enum_value_for_label, enum_to_str, enum_labels

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
    m=re.match(r'^(\s*(?P<hours>[0-9]{1,})h){0,1}\s*((?P<minutes>[0-9]{1,})m){0,1}\s*((?P<seconds>[0-9]{1,})s){0,1}\s*', val, re.IGNORECASE)
    if m is None:
        return None
    g=m.groupdict()
    seconds = 0 if g['seconds'] is None else int(g['seconds'])
    minutes = 0 if g['minutes'] is None else int(g['minutes'])
    hours = 0 if g['hours'] is None else int(g['hours'])
    return seconds + minutes*60 + hours*3600

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
        self.ValidInput = ['.', ':','0','1','2','3','4','5','6','7','8','9']
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
        if keycode in (wx.WXK_RIGHT, wx.WXK_LEFT, wx.WXK_BACK):
            if keycode == wx.WXK_BACK:
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
            seconds = val.seconds
        elif type(val) == int:
            seconds = val//60
        else:
            assert 0
        minutes = (seconds//60)%60
        hours = seconds//3600
        seconds = seconds%60
        ret= []
        if hours != 0:
            ret.append(f"{hours}h")
        if minutes != 0:
            ret.append(f"{minutes}m")
        if seconds != 0:
            ret.append(f"{seconds}s")
        val = " ".join(ret)
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
        size = 10 if wxpythonversion < wxpythonversion42 else 20
        super().__init__(parent, id,  style=wx.SUNKEN_BORDER, agwStyle=PM.PM_HORIZONTAL, size=(-1, size))
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
        r[0] = max(0.001, r[0])
        r[1] = max(r[0]+0.001, r[1])
        r[2] = max(r[1]+0.001, r[2])
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

    def OnClose(self, evt):
        dtdebug("OnClose called")
        self.dttimer.Stop()


class DiseqcChoice(wx.Choice):
    def __init__(self, id,  *args, **kwargs):
        from neumodvb import neumodbutils
        import pydevdb
        self. choices = neumodbutils.enum_labels(pydevdb.rotor_control_t)
        kwargs['choices'] = self.choices
        super().__init__(id, *args, **kwargs)

    def SetValue(self, lnb_connection):
        from neumodvb import neumodbutils
        if lnb_connection is None:
            self.choices.append('????')
            idx = len(self.choices)-1
        else:
            val = neumodbutils.enum_to_str(neumodbutils.get_subfield(lnb_connection, 'rotor_control')).replace('_', ' ')
            idx = self.choices.index(val)
        self.SetSelection(idx)

    def GetValue(self):
        from neumodvb import neumodbutils
        import pydevdb
        idx = self.GetCurrentSelection()
        choice = self.choices[idx]
        try:
            val = neumodbutils.enum_value_for_label(pydevdb.rotor_control_t, choice)
            return val
        except:
            return None

class ScanTypeChoice(wx.Choice):
    def __init__(self, id,  *args, **kwargs):
        from neumodvb import neumodbutils
        import pydevdb
        p_t = pydevdb.subscription_type_t
        self.choices = [ 'Scan muxes', 'Scan band', 'Acq. spectrum']
        self.values = [ p_t.MUX_SCAN, p_t.BAND_SCAN, p_t.SPECTRUM_ACQ]
        kwargs['choices'] = self.choices
        super().__init__(id, *args, **kwargs)

    def SetValue(self, subscription_type):
        from neumodvb import neumodbutils
        idx = self.values.index(subscription_type)
        self.SetSelection(idx)

    def GetValue(self):
        from neumodvb import neumodbutils
        import pydevdb
        idx = self.GetCurrentSelection()
        choice = self.values[idx]
        return choice

class StreamStateChoice(wx.Choice):
    def __init__(self, id,  *args, **kwargs):
        from neumodvb import neumodbutils
        import pydevdb
        s_t = pydevdb.stream_state_t
        self.choices = [ 'Off', 'On', 'Always on']
        self.values = [ s_t.OFF, s_t.ON]
        kwargs['choices'] = self.choices
        super().__init__(id, *args, **kwargs)

    def SetValue(self, subscription_type):
        from neumodvb import neumodbutils
        idx = self.values.index(subscription_type)
        self.SetSelection(idx)

    def GetValue(self):
        from neumodvb import neumodbutils
        import pydevdb
        idx = self.GetCurrentSelection()
        choice = self.values[idx]
        return choice

class RunType(object):
    def __init__(self):
        from neumodvb import neumodbutils
        import pydevdb
        r_t = pydevdb.run_type_t
        self.run_types = [ r_t.NEVER, r_t.ONCE, *[r_t.HOURLY]*7, r_t.DAILY, *[r_t.WEEKLY]*2, r_t.MONTHLY]

        self.intervals = [ 1, 1, *[1, 2, 3, 4, 6, 8, 12], 1, *[1, 2], 1]
        self.choices = [self.run_type_str(i,t) for i, t in zip(self.intervals, self.run_types)]
        assert len(self.run_types) == len(self.choices)
        assert len(self.intervals) == len(self.choices)

    def run_type_str(self, interval, run_type):
        import pydevdb
        r_t = pydevdb.run_type_t
        if run_type == r_t.HOURLY:
            if interval==1:
                return 'Hourly'
            else:
                return f"Every {interval} hours"
        elif run_type == r_t.WEEKLY:
            if interval==1:
                return 'Weekly'
            else:
                return f"Bi-weekly"
        return enum_to_str(run_type).capitalize()

    def str_to_runtype(self, val):
        import pydevdb
        r_t = pydevdb.run_type_t
        try:
            idx = self.choices.index(val)
            return self.run_types[idx], self.intervals[idx]
        except:
            return r_t.NEVER, 1


class RunTypeChoice(wx.Choice):
    def __init__(self, id,  *args, **kwargs):
        from neumodvb import neumodbutils
        import pydevdb
        from neumodvb.scancommandlist import run_type_str, run_type_choices
        r_t = pydevdb.run_type_t
        self.RT = RunType()

        kwargs['choices'] = self.RT.choices
        super().__init__(id, *args, **kwargs)

    def SetValue(self, run_type, interval):
        from neumodvb import neumodbutils
        try:
            idx = [*zip(self.RT.run_types, self.RT.intervals)].index((run_type, interval))
            self.SetSelection(idx)
        except:
            pass
    def GetValue(self):
        from neumodvb import neumodbutils
        import pydevdb
        idx = self.GetCurrentSelection()
        return self.RT.run_types[idx], self.RT.intervals[idx]



OnChangeEvent, EVT_VALUE_CHANGED = wx.lib.newevent.NewEvent()

class TextCtrl(wx.TextCtrl):

    def __init__(self,*args,**kwargs):
        self.old_value = None
        wx.TextCtrl.__init__(self,*args, style=wx.TE_PROCESS_ENTER, **kwargs)
        self.Bind(wx.EVT_SET_FOCUS, self.gotFocus) # used to set old value
        self.Bind(wx.EVT_KILL_FOCUS, self.lostFocus) # used to get new value
        self.Bind(wx.EVT_TEXT_ENTER, self.lostFocus) # used to get new value

    def gotFocus(self, evt):
        evt.Skip()
        self.old_value = self.GetValue()
    def lostFocus(self, evt):
        evt.Skip(False)
        if self.GetValue() != self.old_value:
            evt = OnChangeEvent(widget=self, oldValue=self.old_value, newValue=self.GetValue())
            wx.PostEvent(self, evt)

class LongitudeTextCtrl(TextCtrl):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
    def SetValue(self, floatval):
        from pychdb import sat_pos_str
        super().SetValue(sat_pos_str(int(floatval*100)))
    def GetValue(self):
        from neumodvb.util import parse_longitude
        from pychdb import sat_pos_str
        val = super().GetValue()
        floatval = parse_longitude(val)/100.
        self.ChangeValue(sat_pos_str(int(floatval*100))) #normalise display
        return floatval

class LatitudeTextCtrl(TextCtrl):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
    def StrValue(self, floatval):
        return f'{floatval}S' if floatval < 0 else f'{floatval}N'

    def SetValue(self, floatval):
        from pychdb import sat_pos_str
        super().SetValue(self.StrValue(floatval))
    def GetValue(self):
        from neumodvb.util import parse_latitude
        from pychdb import sat_pos_str
        val = super().GetValue()
        floatval = parse_latitude(val)/100.
        self.ChangeValue(self.StrValue(floatval)) #normalise display
        return floatval


class NeumoCheckListBox(wx.Panel):
    def __init__(self, parent, id, title, choices, *args, **kwds):
        kwds["style"] = kwds.get("style", 0) | wx.TAB_TRAVERSAL
        super().__init__(parent, id, *args, **kwds)
        self.main_sizer = wx.BoxSizer(wx.VERTICAL)

        self.title = wx.StaticText(self, wx.ID_ANY, title)
        self.title.SetFont(wx.Font(8, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD, 0, ""))
        self.main_sizer.Add(self.title, 0, wx.BOTTOM, 5)

        self.select_all_none_toggle = wx.CheckBox(self, wx.ID_ANY, _("Select"),
                                            style=wx.CHK_3STATE | wx.CHK_ALLOW_3RD_STATE_FOR_USER)
        self.select_all_none_toggle.Set3StateValue(wx.CHK_UNDETERMINED)
        self.main_sizer.Add(self.select_all_none_toggle, 0, 0, 0)

        self.checklistbox = wx.CheckListBox(self, wx.ID_ANY, choices=choices, style=wx.LB_MULTIPLE | wx.LB_NEEDED_SB)
        self.font=self.checklistbox.GetFont()
        self.font.SetPointSize(int(self.font.GetPointSize()*0.9))

        self.checklistbox.SetFont(self.font)
        self.main_sizer.Add(self.checklistbox, 0, 0, 0)
        self.SetSizer(self.main_sizer)
        self.main_sizer.Fit(self)
        self.Layout()

        self.select_all_none_toggle.Bind(wx.EVT_CHECKBOX, self.SelectAllNone)
        self.select_all_none_toggle.SetFont(wx.Font(6, wx.FONTFAMILY_DEFAULT,
                                                    wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD, 0, ""))
        self.checklistbox.Bind(wx.EVT_CHECKLISTBOX, self.OnCheckListChanged)

        self.seltype = 1 # 0=None selected, 1=All selected, 2 = Use user choices
        #self.last_selected = None
        self.choices = choices
        self.SelectAllNone()

    def Set(self, choices):
        self.choices = choices
        self.checklistbox.Set(choices)
        self.SelectAllNone()
        self.Refresh()

    def OnCheckListChanged(self, evt):
        #self.last_selected = self.checklistbox.GetSelections()
        self.seltype = 2
        self.select_all_none_toggle.SetLabel("Select")
        evt.Skip(False)

    def GetSelectedItems(self):
        return self.checklistbox.GetCheckedItems()

    def ForceAll(self):
        self.seltype= 1
        self.SelectAllNone()

    def SelectAllNone(self, evt=None):
        if evt is not None:
            self.seltype = evt.Selection
        button = self.select_all_none_toggle
        if self.seltype == 2:
            #if self.last_selected is not None:
            #    self.checklistbox.SetCheckedItems(self.last_selected)
            #    self.last_selected = None
            button.SetLabel("Select")
            self.checklistbox.Enabled=True
        elif self.seltype ==1:
            #if self.last_selected is None:
            #    self.last_selected = self.checklistbox.GetSelections()
            self.checklistbox.SetCheckedItems(range(len(self.choices)))
            button.SetLabel("All")
            #self.checklistbox.Enabled=False
        else:
            #if self.last_selected is None:
            #    self.last_selected = self.checklistbox.GetSelections()
            self.checklistbox.SetCheckedItems([])
            button.SetLabel("None")
            #self.checklistbox.Enabled=False

class DishesCheckListBox(NeumoCheckListBox):
    def __init__(self, parent, id,  *args, **kwargs):
        self.dishes = wx.GetApp().get_dishes()
        title = _("Allowed dishes")
        kwargs['choices'] = [f'Dish {str(d)}' for d in self.dishes]
        super().__init__(parent, id, title, *args, **kwargs)

    def selected_dishes(self):
        it=self.GetSelectedItems()
        return [self.dishes[i] for i in it]

class SatBandsCheckListBox(NeumoCheckListBox):
    def __init__(self, parent, id,  *args, **kwargs):
        import pychdb
        self.sat_bands =  list(filter(lambda x: x != 'UNKNOWN',enum_labels(pychdb.sat_band_t)))
        title = _("Allowed Bands")
        kwargs['choices'] = []
        super().__init__(parent, id, title, *args, **kwargs)

    def set_allowed_sat_bands(self, sat_bands):
        self.sat_bands = list(filter(lambda x: x != 'UNKNOWN',[enum_to_str(b) for b in sat_bands]))
        self.Set(self.sat_bands)

    def selected_sat_bands(self):
        import pychdb
        it=self.GetSelectedItems()
        return [enum_value_for_label(pychdb.sat_band_t, self.sat_bands[i]) for i in it]

class PolarisationsCheckListBox(NeumoCheckListBox):
    def __init__(self, parent, id,  *args, **kwargs):
        self.polarisations = ['H', 'V', 'L', 'R']
        title = _("Scan Polarisations")
        kwargs['choices'] = self.polarisations
        super().__init__(parent, id, title, *args, **kwargs)

    def selected_polarisations(self):
        import pychdb
        it=self.GetSelectedItems()
        return [enum_value_for_label(pychdb.fe_polarisation_t, self.polarisations[i]) for i in it]

class CardsCheckListBox(NeumoCheckListBox):
    def __init__(self, parent, id,  *args, **kwargs):
        title = _("Allowed Cards")
        cards = wx.GetApp().get_cards(available_only=True)
        kwargs['choices'] = [c for c in cards]
        self.cards = [c for c in cards.values()]
        super().__init__(parent, id, title, *args, **kwargs)

    def selected_cards(self):
        it=self.GetSelectedItems()
        return [self.cards[i] for i in it]


class DtIntCtrl(wx.lib.intctrl.IntCtrl):
    """
    Class to filter out initial value argument from wxGLade
    """
    def __init__(self, parent, id, value, *args, **kwds):
        #value = -1
        super().__init__(parent, id, value=-1, min=-1, max=40000, allow_none=True, *args, **kwds)
        TextCtrl.ChangeValue(self, '')

class NeumoProgressDialog(wx.ProgressDialog):
    def __init__(self, parent, dark_mode, title, message, duration, deactivate_parent, *args, **kwds):
        """
        Displays a progress bar on screen for duration secinds
        """
        self.fps = 2
        self.duration = duration
        self.count = 0
        self.maxcount = self.duration*self.fps
        super().__init__(title, message, *args, maximum=self.maxcount,
                         parent= parent if deactivate_parent else None, **kwds)
        self.parent = parent
        self.dark_mode = dark_mode
        self.timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.OnTimer)
        self.Keepgoing = True
        self.active = True
        self.timer.Start(1000//self.fps)        # 2 fps
        self.parent.progress_dlgs = getattr(self.parent, 'progress_dlgs', [])
        self.parent.progress_dlgs.append(self)
    def finish(self):
        self.parent.progress_dlgs.remove(self)
        wx.CallAfter(self.Destroy)

    def OnTimer(self, event):
        if self.active and self.count < self.maxcount:
            self.count += 1
            (self.active, self.skip) = self.Update(self.count)
        if not self.active:
            print(f'TIMER: end')
            self.finish()

def show_progress_dialog(parent, title, message, duration,  deactivate_parent=False, dark_mode=False,
                         style = 0
                         #| wx.PD_APP_MODAL
                         | wx.PD_CAN_ABORT
                         #| wx.PD_CAN_SKIP
                         #| wx.PD_ELAPSED_TIME
                         | wx.PD_ESTIMATED_TIME
                         | wx.PD_REMAINING_TIME
                         | wx.PD_AUTO_HIDE
                         ):
    #Note that dialog will attach itself to parent and will thus be kept alive
    NeumoProgressDialog(parent, dark_mode, title, message, duration, deactivate_parent, style=style)
