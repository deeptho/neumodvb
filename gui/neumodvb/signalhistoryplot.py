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
import warnings
import os
import sys
import time
import datetime
import regex as re
from dateutil import tz
import matplotlib as mpl
from matplotlib.backends.backend_wxagg import FigureCanvasWxAgg as FigureCanvas
from matplotlib.backends.backend_wxagg import NavigationToolbar2WxAgg as NavigationToolbar
import matplotlib.pyplot as plt
from matplotlib import cm
from matplotlib.colors import Normalize, LogNorm
from scipy.interpolate import interpn
import warnings
import itertools
import mpl_scatter_density # adds projection='scatter_density'
from cycler import cycler
from enum import Enum

import numpy as np

class SignalType(Enum):
    SNR = 0
    STRENGTH = 1
    BER = 2


#from sklearn.linear_model import LinearRegression
#import pandas as pd

from neumodvb.util import dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str
from neumodvb.neumoplot import combine_ranges

import pyspectrum
import pystatdb
import pychdb
import pydevdb

#horrible hack: matplotlib (in neumplot.py) uses the presence of this module to decide what backend to
#use and then refuses to use wx
if 'gi.repository.Gtk' in sys.modules:
    del sys.modules['gi.repository.Gtk']
mpl.use('WXAgg')


class SignalHistory(object):
    def __init__(self, parent, mux, color, signal_type):
        self.signal_type = signal_type
        self.mux = mux
        self.parent = parent
        self.figure = self.parent.figure
        self.axes = self.parent.axes
        self.drawn = False
        self.color = color
        self.label = str(self)
        self.xlimits = None # minimal and maximal frequency in this plot (initially unknown)
        self.ylimits = None # minimal and maximal signal in this plot (initially unknown)
        self.markers = itertools.cycle(('o', 'v', '^', '<', '>', '+', '*'))

    def __str__(self):
        sat = pychdb.sat_pos_str(self.mux.k.sat_pos)
        return f'{sat} {self.mux.frequency/1000:.3f}{enum_to_str(self.mux.pol)}'

    def clear(self):
        if self.curves is not None:
            for g in self.curves:
                for a in g:
                    a.remove()
        self.curves = None

    def show(self):
        if self.drawn:
            dtdebug('clearing plot')
            self.axes.clear()
            self.clear()
        self.drawn = True
        self.plot_sighist(self.signal_type)

    def next_prop(self):
        for c in plt.rcParams['axes.prop_cycle']:
            yield c

    def plot_sighist(self, signal_type):
        self.signal_type = signal_type
        mux = self.mux
        dtdebug(f"Plotting {self.mux}")
        sort_order = (pystatdb.signal_stat.subfield_from_name('k.time')<<24) | \
            (pystatdb.signal_stat.subfield_from_name('k.lnb.card_mac_address')<<16) | \
            (pystatdb.signal_stat.subfield_from_name('k.lnb.rf_input')<<8)

        receiver = wx.GetApp().receiver
        cards = { c: k for k,c in wx.GetApp().get_cards_with_rf_in().items()}
        txn = receiver.statdb.rtxn()
        stats = pystatdb.signal_stat.get_by_mux_fuzzy(txn, mux.k.sat_pos, mux.pol, mux.frequency)
        txn.abort()
        signals={}
        if self.signal_type == SignalType.SNR:
            minsignal, maxsignal = 0, 20
        elif self.signal_type == SignalType.STRENGTH:
            minsignal, maxsignal = 0, -80
        elif self.signal_type == SignalType.BER:
            minsignal, maxsignal = None, None
        for ss in stats:
            t =[]
            values = []
            for idx, st in enumerate(ss.stats):
                t1 = datetime.datetime.fromtimestamp(ss.k.time + idx*300)
                t.append(t1)
                if self.signal_type == SignalType.SNR:
                    values.append(st.snr/1000.)
                elif self.signal_type == SignalType.STRENGTH:
                    values.append(st.signal_strength/1000.)
                elif self.signal_type == SignalType.BER:
                    values.append(st.ber)
            t=np.array(t)
            values = np.array(values)
            rf_path = f'D{ss.k.rf_path.lnb.dish_id}' + cards.get((ss.k.rf_path.card_mac_address, ss.k.rf_path.rf_input), '???')
            signal = signals.get(rf_path, [])
            signal.append((t, values))
            signals[rf_path] = signal

        #it = iter(color_cycler)
        curves = []
        mint, maxt= None, None
        for rf_path, signal in signals.items():
            marker = next(self.markers)
            name = f'{mux.frequency/1000:.3f}{enum_to_str(mux.pol)} {rf_path}'
            for t, values in  signal:
                mint = min(t) if mint is None else min(min(t), mint)
                maxt = max(t) if maxt is None else max(max(t), maxt)
                minsignal = values.min() if minsignal is None else minsignal
                maxsignal =values.max() if maxsignal is None else maxsignal
                curves.append(self.axes.plot(t, values, label=name, color=self.color, marker=marker))
                name = None
        self.curves = curves
        self.legend = self.axes.legend()
        if mint is not None:
            self.xlimits = [mint, maxt]
        else:
            self.xlimits = [0, datetime.timedelta(seconds=60)]
        if self.xlimits[1] == self.xlimits[0]:
            self.xlimits[0] -= datetime.timedelta(seconds=60)

        self.ylimits = [minsignal, maxsignal]
        if maxsignal <= minsignal:
            self.ylimits[1] = self.ylimits[0] + 1e-6
        self.axes.set_ylim(self.ylimits)
        self.axes.xaxis_date(tz=tz.tzlocal())
        self.axes.xaxis.set_major_formatter(mpl.dates.DateFormatter('%Y-%m-%d\n %H:%M:%S'))

class SignalHistoryPlot(wx.Panel):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.xlimits = None
        self.ylimits = None
        self.zoom_time= 3600 #zoom all graphs to this amount of time
        self.start_time = datetime.datetime.now(tz = tz.tzlocal())
        self.parent = parent
        self.spectrum = pystatdb.spectrum.spectrum()
        self.use_scrollbar = False
        if self.use_scrollbar:
            self.scrollbar = wx.ScrollBar(self)
            self.scrollbar.SetScrollbar(int(self.start_time.timestamp())-120, self.zoom_time, int(self.start_time.timestamp()),
                                        60)
            self.scrollbar.Bind(wx.EVT_COMMAND_SCROLL, self.OnScroll)
        self.colors = itertools.cycle(plt.rcParams['axes.prop_cycle'].by_key()['color'])
        self.figure = mpl.figure.Figure()
        self.axes = self.figure.add_subplot(111)
        self.canvas = FigureCanvas(self, -1, self.figure)
        self.toolbar_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.toolbar = NavigationToolbar(self.canvas)
        self.toolbar.Realize()
        self.toolbar_sizer.Add(self.toolbar, 0, wx.LEFT | wx.EXPAND)

        self.sizer = wx.BoxSizer(wx.VERTICAL)
        self.sizer.Add(self.toolbar_sizer, 0, wx.LEFT | wx.EXPAND)

        self.sizer.Add(self.canvas, proportion=1,
                        flag=wx.LEFT | wx.TOP | wx.EXPAND)
        if self.use_scrollbar:
            self.sizer.Add(self.scrollbar, proportion=0,
                           flag=wx.LEFT | wx.TOP | wx.EXPAND)
        self.SetSizer(self.sizer)
        self.Bind ( wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.Parent.Bind ( wx.EVT_SHOW, self.OnShowHide )
        self.count =0
        from collections import OrderedDict
        self.signals = OrderedDict()
        self.legend  = None
        self.shift_is_held = False
        self.ctrl_is_held = False

    def OnShowHide(self,event):
        if event.IsShown():
            self.draw()

    def OnWindowCreate(self,event):
        if event.GetWindow() == self:
            #self.start_freq, self.end_freq = self.parent.start_freq, self.parent.end_freq
            self.draw()
        else:
            pass

    def OnScroll(self, event):
        pos = event.GetPosition()
        offset =pos
        self.pan_signal(datetime.timedelta(seconds=offset))

    def draw(self):
        self.axes.clear()
        self.Fit()
        self.figure.subplots_adjust(left=0.05, bottom=0.1, right=0.98, top=0.92)
        self.axes.spines['right'].set_visible(False)
        self.axes.spines['top'].set_visible(False)
        self.axes.set_ylabel('dB')
        self.axes.set_xlabel('Date\n')
        xlimits, ylimits = self.get_limits()
        if False:
            self.axes.set_xlim(xlimits)
        self.canvas.draw()


    def add_legend_button(self, signal, color) :
        panel = wx.Panel(self, wx.ID_ANY, style=wx.BORDER_SUNKEN)
        self.toolbar_sizer.Add(panel, 0, wx.LEFT|wx.RIGHT, 10)
        sizer = wx.FlexGridSizer(3, 1, 0)
        static_line = wx.StaticLine(panel, wx.ID_ANY)
        static_line.SetMinSize((20, 2))
        static_line.SetBackgroundColour(wx.Colour(color))
        static_line.SetForegroundColour(wx.Colour(color))

        sizer.Add(static_line, 1, wx.ALIGN_CENTER_VERTICAL, 0)

        button = wx.ToggleButton(panel, wx.ID_ANY, _(signal.label))
        sizer.Add(button, 0, 0, 0)
        button.signal = signal
        button.SetValue(1)

        self.close_button = wx.Button(panel, -1, "", style=wx.BU_NOTEXT)
        self.close_button.SetMinSize((32, -1))
        self.close_button.SetBitmap(wx.ArtProvider.GetBitmap(wx.ART_CLOSE, wx.ART_OTHER, (16, 16)))
        self.close_button.signal = signal
        sizer.Add(self.close_button, 0, 0, 0)

        panel.SetSizer(sizer)

        self.Bind(wx.EVT_TOGGLEBUTTON, self.OnToggleGraph, button)
        self.Bind(wx.EVT_BUTTON, self.OnCloseGraph, self.close_button)
        self.Layout()
        signal.legend_panel = panel

    def make_key(self, spectrum):
        return str(spectrum.k)

    def toggle_signal(self, mux, signal_type):
        self.signal_type = signal_type
        key = self.make_key(mux)
        s = self.signals.get(key, None)
        if s is None:
            self.show_signal(mux, signal_type)
        else:
            self.hide_signal(mux)

        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)

        return False

    def pan_signal(self, offset):
        xlimits, _ = self.get_limits()
        xmin = xlimits[0]+offset
        xmax = xmin + datetime.timedelta(seconds=self.zoom_time)
        self.pan_start_freq = xmin
        self.axes.set_xbound((xmin, xmax))
        self.canvas.draw()
        self.parent.Refresh()

    def props(self):
        for c in plt.rcParams['axes.prop_cycle']:
            yield c

    def show_signal(self, mux, signal_type):
        key = self.make_key(mux)
        s = self.signals.get(key, None)
        if s is not None:
            self.hide_signal(mux)
            is_first = False
        else:
            is_first = len(self.signals)==0
        self.get_limits.cache_clear()
        color = next(self.colors)
        s = SignalHistory(self, mux, color=color, signal_type=signal_type)
        self.signals[key] = s
        self.add_legend_button(s, s.color)
        s.show()
        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)

    def hide_signal(self, signal):
        key = self.make_key(signal)
        s = self.signals.get(key, None)
        if s is None:
            return
        s.legend_panel.Destroy()
        self.toolbar_sizer.Layout()
        s.clear()
        del self.signals[key]
        if self.legend is not None:
            self.legend.remove()
        self.legend = self.axes.legend()

    @lru_cache(maxsize=None)
    def get_limits(self):
        if self.signals is None or len(self.signals)==0:
            return ((self.start_time, self.start_time - datetime.timedelta(seconds=3600)), (0, 20.0))
        xlimits, ylimits = None, None
        for signal in self.signals.values():
            xlimits = combine_ranges(xlimits, signal.xlimits)
            ylimits = combine_ranges(ylimits, signal.ylimits)
        #-100 and +100 to allow offscreen annotations to be seen
        return (xlimits[0] - datetime.timedelta(seconds=30), xlimits[1] + datetime.timedelta(seconds=30)), ylimits

    def update_matplotlib_legend(self, spectrum):
        self.legend = self.figure.legend(ncol=len(self.spectra))

        for legline, key in zip(self.legend.get_lines(), self.spectra):
            legline.set_picker(True)  # Enable picking on the legend line.
            legline.key = key

    def OnCloseGraph(self, evt):
        mux = evt.GetEventObject().signal.mux
        self.hide_signal(mux)
        self.canvas.draw()
        self.parent.Refresh()

    def OnToggleGraph(self, evt):
        signal = evt.GetEventObject().signal
        for curve in signal.curves:
            for a in curve:
                visible = not a.get_visible()
                a.set_visible(visible)

        self.figure.canvas.draw()
        self.parent.Refresh()
    def change_signal_type(self, signal_type):
        for signal in self.signals.values():
            self.show_signal(signal.mux, signal_type)

"""
solution to layout text boxes (freq,pol,symrate) suhc that they do not overlap with the vertical lines
pointing to spectral peaks, the horizontal lines describing bandwidth, and the curve itself
-boxes are either left aligned and then start at the vertical line, or rigth aligned and then end
 at the vertical line

-step 1 is to compute a baseline version which is above the curve and horizontal lines, has only left aligned text
 but can have overlap between boxes

-step 2 is to compute two overlap-free versions:
--2.a "increasing" version in which all boxes are right aligned and overlapping
  boxes are moved above their left neighbor. This requires a single pass over the data
--2.b "decreasing" version in which all boxes are left aligned and overlapping
  boxes are moved above their right neighbor. This requires a single pass over the data (but starting from the end)

-step 3 is to merge the increasing and decreasing versions as follows:
--start at the left wit the version having the lowest height; call this "current" version
--skip to the next (right) element. when the alternate version would be lower than the current version, attempt to
  switch version as follows (if switching is not possible, simply keep the current version)
  -switching from increasing to decreasing is always allowed as it creates no additional conflict (if the
   left element was increasing, it was right aligned and cannot cause overlap with the right element, which is
   left aligned in this case)
  -switching  from decreasing to increasing is allowed only the the current element will not overlap with  the increasing
   (=left aligned) version of its right neighbor. This means that swicthing will only be allowed between sepearated clusters
   of overlapping elements.


"""
