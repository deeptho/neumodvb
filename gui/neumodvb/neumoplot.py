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
import warnings

import mpl_scatter_density # adds projection='scatter_density'
from matplotlib.colors import LinearSegmentedColormap
from neumodvb.scanstatus import ScanStatusTextCtrl

import numpy as np

#from sklearn.linear_model import LinearRegression
#import pandas as pd

from neumodvb.util import dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str
from neumodvb.neumo_dialogs import ShowMessage
import pyspectrum
import pystatdb
import pychdb
import pydevdb
import datetime

#horrible hack: matplotlib (in neumplot.py) uses the presence of this module to decide what backend to
#use and then refuses to use wx
if 'gi.repository.Gtk' in sys.modules:
    del sys.modules['gi.repository.Gtk']
mpl.use('WXAgg')

class CustomToolbar(NavigationToolbar):
    """
    toolbar which intercepts the readout cursor (which causes trouble)
    """
    def __init__(self, canvas):

        super().__init__(canvas)

    def mouse_move(self, event):
        self._update_cursor(event)

        if event.inaxes and event.inaxes.get_navigate():

            try:
                s = event.inaxes.format_coord(event.xdata, event.ydata)
            except (ValueError, OverflowError):
                pass
            else:
                s = s.rstrip()
                artists = [a for a in event.inaxes._mouseover_set
                           if a.contains(event)[0] and a.get_visible()]
                if artists:
                    a = cbook._topmost_artist(artists)
                    if a is not event.inaxes.patch:
                        data = a.get_cursor_data(event)
                        if data is not None:
                            data_str = a.format_cursor_data(data).rstrip()
                            if data_str:
                                s = s + '\n' + data_str
                #self.set_message(s)
        else:
            pass #self.set_message(self.mode)

def tooltips(fig):
    def update_annot(ind):

        pos = sc.get_offsets()[ind["ind"][0]]
        annot.xy = pos
        text = "{}, {}".format(" ".join(list(map(str,ind["ind"]))),
                               " ".join([names[n] for n in ind["ind"]]))
        annot.set_text(text)
        annot.get_bbox_patch().set_facecolor(cmap(norm(c[ind["ind"][0]])))
        annot.get_bbox_patch().set_alpha(0.4)


    def hover(event):
        vis = annot.get_visible()
        if event.inaxes == ax:
            cont, ind = sc.contains(event)
            if cont:
                update_annot(ind)
                annot.set_visible(True)
                fig.canvas.draw_idle()
            else:
                if vis:
                    annot.set_visible(False)
                    fig.canvas.draw_idle()

    #fig.canvas.mpl_connect("motion_notify_event", hover)


def plot_marks(marks, offset=-55, label='xxx', use_index=True):
    global ret
    n = len(ret[0][:,1])
    f = np.array(range(0,n))
    sig = marks*offset
    plt.plot(f, sig, '+', label=label)
    plt.legend()

def get_renderer(fig):
    try:
        return fig.canvas.get_renderer()
    except AttributeError:
        return fig.canvas.renderer

def get_bboxes(objs, r=None, expand=(1, 1), ax=None, transform=None):
    """

    Parameters
    ----------
    objs : list, or PathCollection
        List of objects to get bboxes from. Also works with mpl PathCollection.
    r : renderer
        Renderer. The default is None, then automatically deduced from ax.
    expand : (float, float), optional
        How much to expand bboxes in (x, y), in fractions. The default is (1, 1).
    ax : Axes, optional
        The default is None, then uses current axes.
    transform : optional
        Transform to apply to the objects, if they don't return they window extent.
        The default is None, then applies the default ax transform.
    Returns
    -------
    list
        List of bboxes.
    """
    ax = ax or plt.gca()
    r = r or get_renderer(ax.get_figure())
    try:
        return [i.get_window_extent(r).expanded(*expand) for i in objs]
    except (AttributeError, TypeError):
        try:
            if all([isinstance(obj, matplotlib.transforms.BboxBase) for obj in objs]):
                return objs
            else:
                raise ValueError("Something is wrong")
        except TypeError:
            return get_bboxes_pathcollection(objs, ax)

class Tp(object):
    def __init__(self, spectrum, freq, symbol_rate):
        self.spectrum = spectrum
        self.freq = freq
        self.symbol_rate = symbol_rate
        self.scan_ok = False
        self.scan_failed = False

    def __str__(self):
        return f'{self.freq:8.3f}{self.spectrum.pol} {self.symbol_rate}kS/s'

def find_nearest(array,value):
    import math
    idx = np.searchsorted(array, value, side="left")
    if idx > 0 and (idx == len(array) or math.fabs(value - array[idx-1]) < math.fabs(value - array[idx])):
        return idx-1
    else:
        return idx

def overlaps(a, b):
    """
    assumes that a.xmin < b.xmin
    """
    return b.xmin < a.xmax and (a.ymin <= b.ymin < a.ymax or b.ymin <= a.ymin < b.ymax )

all_rects=[]

def remove_rects():
    global all_rects
    for rect in all_rects:
        rect.remove()
    all_rects = []

def combine_ranges(a, b):
    if a is None and b is None:
        return None
    if a is None or b is None:
        return a if b is None else b
    return (min(a[0], b[0]), max(a[1], b[1]))

class Spectrum(object):
    def __init__(self, parent, spectrum, color):
        self.spectrum = spectrum
        self.parent = parent
        self.figure = self.parent.figure
        self.axes = self.parent.axes
        self.drawn = False
        self.color = color
        sat = pychdb.sat_pos_str(self.spectrum.k.sat_pos)
        date = datetime.datetime.fromtimestamp(self.spectrum.k.start_time , tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M")
        label = f'{date} {sat} {enum_to_str(self.spectrum.k.pol)} dish {self.spectrum.k.rf_path.lnb.dish_id}'
        self.label = label
        self.annots = []
        self.tps = []
        self.peak_data = None
        self.vlines = None
        self.hlines = None
        self.annot_box = ((0,0))
        self.annot_maxy = None
        self.xlimits = None # minimal and maximal frequency in this plot (initially unknown)
        self.ylimits = None # minimal and maximal signal in this plot (initially unknown)

    def __str__(self):
        sat = pychdb.sat_pos_str(self.spectrum.k.sat_pos)
        return f'{sat} {enum_to_str(self.spectrum.k.pol)} dish {self.spectrum.k.rf_path.lnb.dish_id}'

    def tps_to_scan(self) :
        return [ tp for tp in self.tps if not tp.scan_failed and not tp.scan_ok ]

    def clear(self):
        for a in self.annots:
            a.remove()
        self.annots=[]
        if self.spectrum_graph is not None:
            for a in self.spectrum_graph:
                a.remove()
        self.spectrum_graph = None
        if self.vlines is not None:
            self.vlines.remove()
            self.vlines = None

        if self.hlines is not None:
            self.hlines.remove()
            self.hlines = None

    def annot_for_freq(self, freq):
        found = None
        best = 20000000
        for annot in self.annots:
            delta= abs(annot.tp.freq - freq)
            if delta < best:
                found = annot
                best = delta
        return found, best

    def annot_for_peak(self, peak):
        found = None
        best = 20000000
        if peak is None:
            return None
        for annot in self.annots:
            delta= abs(annot.tp.freq*1000 - peak.frequency)
            if delta < best and annot.tp.spectrum.spectrum.k.pol == peak.pol:
                found = annot
                best = delta
        return found
    def show(self):
        if self.drawn:
            #dtdebug('clearing plot')
            #self.axes.clear()
            self.clear()
        receiver = wx.GetApp().receiver
        path = receiver.get_spectrum_path()
        spectrum_fname = ''.join([path, '/', self.spectrum.filename, "_spectrum.dat"])
        tps_fname = ''.join([path, '/', self.spectrum.filename, "_peaks.dat"])
        pol = self.spectrum.k.pol
        ret = self.process(spectrum_fname, tps_fname)
        self.drawn = True
        return ret

    def make_tps(self, tpsname):
        #n = len(spec[:,1])
        recompute = True
        if recompute:
            from pyspectrum import  find_spectral_peaks
            peak_freq, peak_sr = find_spectral_peaks(self.spec[:,0], self.spec[:,1])
            self.peak_data = np.vstack([peak_freq, peak_sr]).T
        else:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                self.peak_data = np.loadtxt(tpsname, ndmin=2)
        if len(self.peak_data) == 0:
            return
        f = self.peak_data[:,0]
        snr = None
        for row in self.peak_data:
            tp = Tp(spectrum=self, freq=row[0], symbol_rate=row[1]/1000.)
            self.tps.append(tp)

    def plot_spec(self, fname):
        dtdebug(f"loading spectrum {fname}")
        try:
            self.spec = np.atleast_2d(np.loadtxt(fname))
        except:
            ShowMessage(f'Could not open {fname}')
            return False
        if self.parent.do_detrend:
            self.detrend()
        t = self.spec[:,0]
        a = self.spec[:,1]
        self.spectrum_graph = self.axes.plot(t, a/1000,label=self.label, color=self.color)
        return True

    def annot_size(self):
        s = self.parent.annot_scale_factors
        xlimits, ylimits = self.parent.get_limits()
        sx, sy = s[0] * self.parent.zoom_bandwidth,  s[1] * (ylimits[1] - ylimits[0])
        dtdebug(f'annot_size: sx={sx} sy={sy} {xlimits} {ylimits}')
        return sx, sy

    def detrend_band(self, spec, lowidx, highidx):
        """
        Fit a linear curve to the minima of spectral intervals of the spectrum in spec
        The parameters of the polynomial are fit between lowidx and highidx indices. This allow excluding
        part of the spectrum near 11700Ghz in a Ku spectrum. Near that frequency it is difficult to know
        for sure if the spectrum is from the low or high lnb band (which may have an offset in its local
        oscillator)
        """
        N = 16
        num_parts = (highidx-lowidx)//N #we split the range between lowidx and highidx in 16 parts
        if num_parts == 0:
            return
        l = num_parts*N
        #t is a list of local minima
        t=  self.spec[lowidx:lowidx+l, 1].reshape([-1,num_parts]).min(axis=1)
        #f: corresponding frequencies
        f = self.spec[lowidx:lowidx+l, 0].reshape([-1,num_parts])[:,0]

        #compute polynomial fit
        p = np.polyfit(f, t, 1)

        #detrend the spectrum
        spec[:,1] -= p[0]*spec[:, 0]+p[1]

    def detrend(self):
        lowest_freq, highest_freq = self.spec[0, 0] , self.spec[-1,0]
        # the +8 is a heuristic, in case the highest frequencies of the Ku_low band would exceed 11700 due to lnb lof offset
        has_two_bands = (highest_freq > 11700+8) and (lowest_freq < 11700-8) and \
            self.spectrum.k.rf_path.lnb.lnb_type == pydevdb.lnb_type_t.UNIV
        if has_two_bands:
            mid_idx1 = np.searchsorted(self.spec[:,0], 11700-8, side='left')
            mid_idx = np.searchsorted(self.spec[mid_idx1:,0], 11700, side='left') + mid_idx1
            mid_idx2 = np.searchsorted(self.spec[mid_idx:,0], 11700+8, side='left') + mid_idx
            self.detrend_band(self.spec[:mid_idx, :], 0, mid_idx1)
            self.detrend_band(self.spec[mid_idx:, :], mid_idx2, self.spec.shape[0])
        else:
            self.detrend_band(self.spec, 0, self.spec.shape[0])

    def ann_tps(self, tpsk, spec, offset=-64, xoffset=0):
        self.annots =[]
        if len(tpsk) == 0:
            return
        f = tpsk[:,0]
        #setting this is needed to calibrate coordinate system
        l = np.min(spec[0,0])
        r = np.max(spec[-1,0])
        w, h = self.annot_size() # un units of Mhz and dB
        xscale = (len(spec[:,0])-1)/(r - l)

        n = len(spec[:,0])
        w = int(w)
        idxs =  np.searchsorted(spec[:,0], f, side='left')
        offset = h*1.5*1000
        self.pol = enum_to_str (self.spectrum.k.pol)
        annoty, lrflag = pyspectrum.find_annot_locations(spec[:,1], idxs, f, #f only used for debugging
                                                         int(w*xscale), int(h*2*1000),
                                                         offset)
        self.annot_maxy = annoty.max()/1000
        annoty /= 1000
        hlines = []
        yoffset1 = h
        yoffset2 = 2
        sig = (spec[idxs,1])/1000 + yoffset1
        vline_top = []
        bbs =[]
        for ay, s, flag, tp in zip(annoty, sig, lrflag, self.tps):
            pt=[tp.freq, s]
            pttext=[tp.freq + (0 if flag else w/10), ay]
            xoffset = 0
            txt = f"{tp.freq:8.3f}{self.pol} {int(tp.symbol_rate)}kS/s " if (flag & 2) \
                else f"{tp.freq:8.3f}{self.pol} \n{int(tp.symbol_rate)}kS/s ";
            annot=self.axes.annotate(txt, \
                                     pt, xytext=pttext, xycoords='data', \
                                     ha='right' if (flag & 1) else 'left', fontsize=8)
            annot.tp = tp
            annot.set_picker(True)  # Enable picking on the legend line.
            self.annots.append(annot)
        self.vlines = self.axes.vlines(f, sig,  annoty+h/2, color='black')
        self.vlines.set_picker(True)  # Enable picking on the legend line.
        bw =  tpsk[:,1]/2000000
        self.hlines = self.axes.hlines(sig, f-bw, f+bw, color=self.color)
        self.hlines.set_picker(True)  # Enable picking on the legend line.


    def process(self, specname, tpsname):
        if not self.plot_spec(specname):
            self.spectrum_graph = None
            return False
        frequency_step = round(1000*(self.spec[1:,0] - self.spec[:-1,0]).mean())
        sig = self.spec[:,1]
        self.make_tps(tpsname)

        #set xlimits prior to annotation to ensure the computation
        #which prevents annotations from overlapping has the proper coordinate system
        self.xlimits = int(self.spec[0,0]), int(self.spec[-1,0])
        self.ylimits = [np.min(self.spec[:,1])/1000,  np.max(self.spec[:,1])/1000]
        xlimits, ylimits = self.parent.get_limits() #takes into account all spectra

        if ylimits[1] != ylimits[0]:
            self.axes.set_ylim(ylimits)
        self.axes.set_xlim(xlimits)

        self.ann_tps(self.peak_data, self.spec)

        #set final limits
        self.xlimits = (int(self.spec[0,0]), int(self.spec[-1,0] ))
        self.ylimits = (ylimits[0], ylimits[1] if self.annot_maxy is None else self.annot_maxy )
        self.parent.get_limits.cache_clear() #needs update!
        self.ylimits = (self.ylimits[0], min(self.ylimits[1], self.ylimits[0]+30))
        xlimits, ylimits = self.parent.get_limits()

        self.axes.set_ylim(ylimits)
        self.axes.set_xlim(xlimits)
        return True

class SpectrumPlot(wx.Panel):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.xlimits = None
        self.ylimits = None
        self.zoom_bandwidth=500 #zoom all graphs to this amount of spectrum, to avoid overlapping annotations
        self.parent = parent
        self.spectrum = pystatdb.spectrum.spectrum()
        self.scrollbar = wx.ScrollBar(self)
        self.scrollbar.SetScrollbar(0, self.zoom_bandwidth, 2100, 200)
        self.scrollbar.Bind(wx.EVT_COMMAND_SCROLL, self.OnScroll)

        self.figure = mpl.figure.Figure()
        self.axes = self.figure.add_subplot(111)
        self.canvas = FigureCanvas(self, -1, self.figure)
        self.toolbar_sizer = wx.BoxSizer(wx.HORIZONTAL)
        #self.toolbar_sizer = wx.FlexGridSizer(1, 4, 0, 10)
        self.toolbar = NavigationToolbar(self.canvas)
        self.toolbar.Realize()
        self.toolbar_sizer.Add(self.toolbar, 0, wx.LEFT | wx.EXPAND)

        self.sizer = wx.BoxSizer(wx.VERTICAL)
        self.sizer.Add(self.toolbar_sizer, 0, wx.LEFT | wx.EXPAND)

        self.sizer.Add(self.canvas, proportion=1,
                        flag=wx.LEFT | wx.TOP | wx.EXPAND)
        self.sizer.Add(self.scrollbar, proportion=0,
                        flag=wx.LEFT | wx.TOP | wx.EXPAND)
        self.SetSizer(self.sizer)
        self.Bind (wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.Parent.Bind (wx.EVT_SHOW, self.OnShowHide)
        self.count =0
        from collections import OrderedDict
        self.spectra = OrderedDict()
        self.legend  = None
        self.figure.canvas.mpl_connect('pick_event', self.on_pick)
        self.figure.canvas.mpl_connect('button_press_event', self.on_button_press)
        self.shift_is_held = False
        self.ctrl_is_held = False
        self.figure.canvas.mpl_connect('key_press_event', self.set_modifiers)
        self.figure.canvas.mpl_connect('key_release_event', self.unset_modifiers)
        self.cycle_colors = plt.rcParams['axes.prop_cycle'].by_key()['color']
        self.current_annot = None #currently selected annot
        self.current_annot_vline = None
        self.do_detrend = True
        self.pan_start_freq = None
        self.add_detrend_button()
        self.add_status_box()
        self.mux_creator = None
        self.scan_status_text_ = None
        wx.CallAfter(self.compute_annot_scale_factors)

    def add_drawn_mux(self, freq, pol, symbol_rate):
        txt = f"{freq:8.3f}{pol} {int(symbol_rate)}kS/s "
        dtdebug(f'Add drawn mux {txt}')
        wx.CallAfter(self.parent.OnUpdateMux, freq, pol, symbol_rate)

    def start_draw_mux(self, default_pol):
        if self.mux_creator is not None:
            self.mux_creator.show()
            return
        from neumodvb.draw_mux import MuxSelector
        self.mux_creator = MuxSelector(self, pol=default_pol)

    def set_modifiers(self, event):
        if 'shift' in event.key:
            self.shift_is_held = True
        if 'control' in event.key:
            self.ctrl_is_held = True
    def unset_modifiers(self, event):
        if 'shift' in event.key:
            self.shift_is_held = False
        if 'control' in event.key:
            self.ctrl_is_held = False

    def on_motion(self, event):
        pass

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
        self.pan_spectrum(offset)

    def OnFix(self, event):
        self.adjust()
        self.canvas.draw()

    def draw(self):
        self.axes.clear()
        self.Fit()
        self.figure.subplots_adjust(left=0.05, bottom=0.1, right=0.98, top=0.92)
        self.axes.spines['right'].set_visible(False)
        self.axes.spines['top'].set_visible(False)
        self.axes.set_ylabel('dB')
        self.axes.set_xlabel('Frequency (MHz)')
        xlimits, ylimits = self.get_limits()
        self.axes.set_xlim(xlimits)
        self.canvas.draw()

    def add_detrend_button(self) :
        panel = wx.Panel(self, wx.ID_ANY, style=wx.BORDER_SUNKEN)
        self.toolbar_sizer.Add(panel, 0, wx.LEFT|wx.RIGHT, 10)
        sizer = wx.FlexGridSizer(3, 1, 0)
        bitmap = wx.ArtProvider.GetBitmap(wx.ART_DEL_BOOKMARK, size=(16, 16))
        button = wx.BitmapToggleButton(panel, wx.ID_ANY, label=bitmap)
        button.SetValue(self.do_detrend)
        sizer.Add(button, 1, wx.ALIGN_CENTER_VERTICAL, border=0)
        panel.SetSizer(sizer)
        self.Bind(wx.EVT_TOGGLEBUTTON, self.OnToggleDetrend, button)

    def add_status_box(self) :
        self.font_dc = wx.ScreenDC()
        w,h = self.font_dc.GetTextExtent("10745.160MHz ")
        self.SetMinSize((w,-1))
        self.status_box = wx.StaticText(self, -1)
        self.toolbar_sizer.Add(self.status_box, 0, wx.LEFT|wx.RIGHT|wx.ALIGN_CENTER|wx.EXPAND, 10)
        self.canvas.mpl_connect('motion_notify_event', self.UpdateStatusBar)
        self.canvas.Bind(wx.EVT_ENTER_WINDOW, self.ChangeCursor)

    def end_scan(self):
        if self.scan_status_text_ is not None:
            self.scan_status_text_.scan_in_progress = False

    @property
    def scan_status_text(self) :
        if self.scan_status_text_ is None:
            w,h = self.font_dc.GetTextExtent("10745.160MHz ")
            self.scan_status_text_ = ScanStatusTextCtrl(self, wx.ID_ANY, "",
                                                        style=wx.TE_MULTILINE | wx.TE_NO_VSCROLL |
                                                        wx.TE_READONLY | wx.TE_RICH)
            self.toolbar_sizer.Add(self.scan_status_text_, 1, wx.LEFT|wx.RIGHT|wx.ALIGN_CENTER|wx.EXPAND, 10)
            self.Layout()
        return self.scan_status_text_
    def set_scan_status(self, scan_status):
        self.add_scan_status_box()
        self.scan_status_text.ShowScanRecord(scan_status)

    def ChangeCursor(self, event):
        self.canvas.SetCursor(wx.Cursor(wx.CURSOR_CROSS))

    def UpdateStatusBar(self, event):
        if event.inaxes:
            self.status_box.SetLabel(
                f"{event.xdata:3.3f}Mhz\n{event.ydata:2.1f}dB")

    def add_legend_button(self, spectrum, color) :
        panel = wx.Panel(self, wx.ID_ANY, style=wx.BORDER_SUNKEN)
        if self.scan_status_text_ is None:
            self.toolbar_sizer.Add(panel, 0, wx.LEFT|wx.RIGHT, 10)
        else:
            idx = len(self.toolbar_sizer.GetChildren()) -1
            self.toolbar_sizer.Remove(idx)
            self.toolbar_sizer.Add(panel, 0, wx.LEFT|wx.RIGHT, 10)
            if self.scan_status_text_:
                if self.scan_status_text_.scan_in_progress:
                    self.toolbar_sizer.Add(self.scan_status_text_, 1, wx.LEFT|wx.RIGHT|wx.ALIGN_CENTER|wx.EXPAND, 10)
                else:
                    self.scan_status_text_.Destroy()
                    self.scan_status_text_=None

        sizer = wx.FlexGridSizer(3, 1, 0)
        static_line = wx.StaticLine(panel, wx.ID_ANY)
        static_line.SetMinSize((20, 2))
        static_line.SetBackgroundColour(wx.Colour(color))
        static_line.SetForegroundColour(wx.Colour(color))

        sizer.Add(static_line, 1, wx.ALIGN_CENTER_VERTICAL, 0)

        button = wx.ToggleButton(panel, wx.ID_ANY, _(spectrum.label))
        sizer.Add(button, 0, 0, 0)
        button.spectrum = spectrum
        button.SetValue(1)

        self.close_button = wx.Button(panel, -1, "", style=wx.BU_NOTEXT)
        self.close_button.SetMinSize((32, -1))
        self.close_button.SetBitmap(wx.ArtProvider.GetBitmap(wx.ART_CLOSE, wx.ART_OTHER, (16, 16)))
        self.close_button.spectrum = spectrum
        sizer.Add(self.close_button, 0, 0, 0)

        panel.SetSizer(sizer)

        self.Bind(wx.EVT_TOGGLEBUTTON, self.OnToggleAnnots, button)
        self.Bind(wx.EVT_BUTTON, self.OnCloseGraph, self.close_button)
        self.Layout()
        spectrum.legend_panel = panel

    def make_key(self, spectrum):
        from neumodvb.util import lastdot
        k = spectrum.k
        return f'{k}'

    def toggle_spectrum(self, spectrum):
        key = self.make_key(spectrum)
        s = self.spectra.get(key, None)
        if s is None:
            self.show_spectrum(spectrum)
        else:
            self.hide_spectrum(spectrum)

        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)

        return False

    def compute_annot_scale_factors(self):
        """
        computes self.annot_scale_factors
        when these are multiplied by the x and y limits of the the graph
        we will get the correct bounding box of double line annotions
        """
        r = self.figure.canvas.get_renderer()
        x = self.zoom_bandwidth
        y = 80*2
        self.axes.set_xlim([0, x ])
        self.axes.set_ylim([0, y ])
        t = self.axes.text(100, 10, '10841.660V/H \n10841.660V/H ', fontsize=8)
        bb = t.get_window_extent(r).transformed(self.axes.transData.inverted())
        self.annot_scale_factors = [(bb.x1-bb.x0)/x, (bb.y1-bb.y0)/y]
        self.annot_bbox = bb
        t.remove()

    def show_spectrum(self, spectrum):
        key = self.make_key(spectrum)
        s = self.spectra.get(key, None)
        if s is not None:
            self.hide_spectrum(spectrum)
            is_first = False
        else:
            is_first = len(self.spectra)==0
        self.get_limits.cache_clear()
        s = Spectrum(self, spectrum, color=self.cycle_colors[len(self.spectra)])
        self.spectra[key] = s
        self.add_legend_button(s, s.color)
        if not s.show():
            return
        if self.legend is not None:
            self.legend.remove()
        #self.pan_spectrum(0)
        self.pan_band(s.spec[0,0])
        xlimits, ylimits = self.get_limits()
        offset = 0 if self.pan_start_freq is None else self.pan_start_freq - xlimits[0]

        self.scrollbar.SetScrollbar(offset, self.zoom_bandwidth, xlimits[1] - xlimits[0], 200)
        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)

    def pan_spectrum(self, offset):
        xlimits, _ = self.get_limits()
        xmin = xlimits[0]+offset
        xmax = xmin +self.zoom_bandwidth
        self.pan_start_freq = xmin
        self.axes.set_xbound((xmin, xmax))
        self.canvas.draw()
        self.parent.Refresh()

    def pan_band(self, start):
        xmin, ymin = start, -50
        xmax, ymax = start + self.zoom_bandwidth, -45
        self.axes.set_xbound((xmin, xmax))
        self.canvas.draw()
        self.parent.Refresh()

    def hide_spectrum(self, spectrum):
        key = self.make_key(spectrum)
        s = self.spectra.get(key, None)
        if s is None:
            return
        s.legend_panel.Destroy()
        if self.scan_status_text_ is not None and not self.scan_status_text_.scan_in_progress:
            self.scan_status_text_.Destroy()
            self.scan_status_text_ = None
        self.toolbar_sizer.Layout()
        s.clear()
        del self.spectra[key]
        if self.legend is not None:
            self.legend.remove()

    @lru_cache(maxsize=None)
    def get_limits(self):
        if self.spectra is None or len(self.spectra)==0:
            return ((self.parent.start_freq/1000, self.parent.end_freq/1000), (-60.0, -40.0))
        xlimits, ylimits = None, None
        for spectrum in self.spectra.values():
            xlimits = combine_ranges(xlimits, spectrum.xlimits)
            ylimits = combine_ranges(ylimits, spectrum.ylimits)
        #-100 and +100 to allow offscreen annotations to be seen
        return (xlimits[0] - 100 , xlimits[1] +100), ylimits

    def update_matplotlib_legend(self, spectrum):
        self.legend = self.figure.legend(ncol=len(self.spectra))

        for legline, key in zip(self.legend.get_lines(), self.spectra):
            legline.set_picker(True)  # Enable picking on the legend line.
            legline.key = key

    def on_pickSHOWHIDE(self, event):
        """
        show/hide graph by clicking on legend line
        """
        # On the pick event, find the original line corresponding to the legend
        # proxy line, and toggle its visibility.
        legline = event.artist
        key = legline.key
        dtdebug(f"toggling {key} for {legline}")
        origline = self.spectra[key].spectrum_graph[0]
        visible = not origline.get_visible()
        origline.set_visible(visible)
        # Change the alpha on the line in the legend so we can see what lines
        # have been toggled.
        legline.set_alpha(1.0 if visible else 0.2)
        self.figure.canvas.draw()

    def set_current_annot(self, annot):
        if annot == self.current_annot:
            return
        if self.current_annot is not None:
            if self.current_annot.tp.scan_ok:
                color = 'green'
            elif self.current_annot.tp.scan_failed:
                color = 'red'
            else:
                color ='black'
            self.current_annot.set_color(color)
        color = 'blue'
        annot.set_color(color)
        if self.current_annot_vline is not None:
            self.current_annot_vline.remove()
        self.current_annot_vline = self.axes.axvline(x=annot.tp.freq, color='blue')
        self.current_annot = annot
        self.canvas.draw()

    def set_annot_status_(self, annot, peak, mux, locked):
        if annot is None:
            assert False
            return
        freq, symbol_rate = mux.frequency/1000, int(mux.symbol_rate/1000),
        annot.set_text(f"{freq:8.3f}{enum_to_str(mux.pol)} \n{symbol_rate}kS/s ")
        annot.tp.scan_ok = locked
        annot.tp.scan_failed = not locked
        color = 'green' if locked  else 'red'
        annot.set_color(color)
        self.canvas.draw()

    def set_annot_status(self, spectrum_key, peak, mux, locked):
        key = str(spectrum_key)
        spectrum = self.spectra[key]
        annot = spectrum.annot_for_peak(peak)
        if annot is None:
            assert False
            return
        self.set_annot_status_(annot, peak, mux, locked)

    def set_current_annot_status(self, mux, si_or_driver_mux, locked):
        if self.current_annot is None:
            return
        annot = self.current_annot
        if abs(annot.tp.freq*1000 - si_or_driver_mux.frequency) >= 0.2 * annot.tp.symbol_rate \
            or enum_to_str(si_or_driver_mux.pol) != annot.tp.spectrum.pol:
            return
        self.set_annot_status_(self.current_annot, mux, si_or_driver_mux, locked)

    def reset_current_annot_status(self, mux):
        if self.current_annot is None:
            return
        spectrum = self.current_annot.tp.spectrum
        if spectrum.annot_for_peak(mux) != self.current_annot:
            return
        self.current_annot.tp.scan_ok = False
        self.current_annot.tp.scan_failed = False
        color = 'blue'
        self.current_annot.set_color(color)
        self.canvas.draw()

    def set_current_tp(self, tp):
        """
        Highlight current tp
        """
        # On the pick event, find the original line corresponding to the legend
        # proxy line, and toggle its visibility.
        for annot in tp.spectrum.annots:
            if annot.tp == tp:
                dtdebug(f'set current_tp: spec={tp.spectrum} tp={annot.tp}')
                self.set_current_annot(annot)
                return
    def on_button_press(self, event):
        pass

    def on_pick(self, event):
        """
        show/hide graph by clicking on legend line
        """
        # On the pick event, find the original line corresponding to the legend
        # proxy line, and toggle its visibility.
        what = event.artist
        for key,spectrum  in self.spectra.items():
            if what in spectrum.annots:
                dtdebug(f'Spectrum: pick annot {spectrum} tp={what.tp}')
                #import pdb; pdb.set_trace()
                self.set_current_annot(what)
                wx.CallAfter(self.parent.OnSelectMux, what.tp)
                return
        me = event.mouseevent
        freq  = me.xdata
        best_delta =  20000000
        best_annot = None
        dtdebug(f"Spectrum: pick freq={freq}")
        for key,spectrum  in self.spectra.items():
            if what == spectrum.hlines or what == spectrum.vlines:
                ind = event.ind[0]
                verts = what.get_paths()[ind].vertices
                f = (verts[1, 0] + verts[0, 0])/2
                annot, delta = spectrum.annot_for_freq(freq)
                if delta  < best_delta:
                    best_delta = delta
                    best_annot = annot

        if best_annot is not None:
            dtdebug(f'Spectrum: pick line spectrum={spectrum} tp={best_annot.tp}')
            self.set_current_annot(best_annot)
            wx.CallAfter(self.parent.OnSelectMux, best_annot.tp)
            return

    def OnCloseGraph(self, evt):
        spectrum = evt.GetEventObject().spectrum.spectrum
        self.hide_spectrum(spectrum)
        self.canvas.draw()
        self.parent.Refresh()

    def OnToggleAnnots(self, evt):
        spectrum = evt.GetEventObject().spectrum
        if False:
            if spectrum.spectrum_graph is not None:
                origline = spectrum.spectrum_graph[0]
                visible = not origline.get_visible()
                origline.set_visible(visible)

            if spectrum.hlines is not None:
                visible = not spectrum.hlines.get_visible()
                spectrum.hlines.set_visible(visible)

        if spectrum.vlines is not None:
            visible = not spectrum.vlines.get_visible()
            spectrum.vlines.set_visible(visible)

        for a in spectrum.annots:
            visible = not a.get_visible()
            a.set_visible(visible)


        self.figure.canvas.draw()
        self.parent.Refresh()

    def OnToggleDetrend(self, evt):
        self.do_detrend =  evt.GetEventObject().GetValue()
        spectra = []
        for key,spectrum in self.spectra.items():
            spectra.append(spectrum.spectrum)
        for spectrum in spectra:
            self.hide_spectrum(spectrum)
        for spectrum in spectra:
            self.show_spectrum(spectrum)
        self.figure.canvas.draw()
        self.parent.Refresh()


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
