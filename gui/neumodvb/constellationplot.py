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
import matplotlib as mpl
from matplotlib.backends.backend_wxagg import FigureCanvasWxAgg as FigureCanvas
import matplotlib.pyplot as plt
from matplotlib import cm
from matplotlib.colors import Normalize, LogNorm
from scipy.interpolate import interpn
import warnings

import mpl_scatter_density # adds projection='scatter_density'
from matplotlib.colors import LinearSegmentedColormap

import numpy as np

#from sklearn.linear_model import LinearRegression
#import pandas as pd

from neumodvb.util import dtdebug, dterror
from neumodvb.neumodbutils import enum_to_str

#horrible hack: matplotlib (in neumplot.py) uses the presence of this module to decide what backend to
#use and then refuses to use wx
if 'gi.repository.Gtk' in sys.modules:
    del sys.modules['gi.repository.Gtk']
mpl.use('WXAgg')

white_viridis = LinearSegmentedColormap.from_list('white_viridis', [
    (0, '#ffffff00'),
    (1e-20, '#440053'),
    (0.2, '#404388'),
    (0.4, '#2a788e'),
    (0.6, '#21a784'),
    (0.8, '#78d151'),
    (1, '#fde624'),
], N=256)


def density_scatter( x , y, ax = None, sort = True, bins = 20, **kwargs )   :
    """
    Scatter plot colored by 2d histogram
    """
    if ax is None :
        fig , ax = plt.subplots()
    data , x_e, y_e = np.histogram2d( x, y, bins = bins, density = True )
    z = interpn( ( 0.5*(x_e[1:] + x_e[:-1]) , 0.5*(y_e[1:]+y_e[:-1]) ) , data , np.vstack([x,y]).T , method = "splinef2d", bounds_error = False)

    #To be sure to plot all data
    z[np.where(np.isnan(z))] = 0.0

    # Sort the points by density, so that the densest points are plotted last
    if sort :
        idx = z.argsort()
        x, y, z = x[idx], y[idx], z[idx]

    ax.scatter( x, y, c=z, **kwargs )

    norm = Normalize(vmin = np.min(z), vmax = np.max(z))
    cbar = fig.colorbar(cm.ScalarMappable(norm = norm), ax=ax)
    cbar.ax.set_ylabel('Density')

    return ax


def log_transform(im):
    '''returns log(image) scaled to the interval [0,1]'''
    try:
        (min, max) = (im[im > 0].min(), im.max())
        if (max > min) and (max > 0):
            return 255*(np.log(im.clip(min, max)) - np.log(min)) / (np.log(max) - np.log(min))
    except:
        pass
    return im


class ConstellationPlotBase(wx.Panel):
    def __init__(self, parent, figsize, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        self.xlimits = None
        self.ylimits = None
        self.parent = parent

        self.figure = mpl.figure.Figure(figsize=figsize)
        self.axes = self.figure.add_subplot(111, projection='scatter_density')
        self.canvas = FigureCanvas(self, -1, self.figure)
        #print(f'BLIT={FigureCanvas.supports_blit}')
        self.sizer = wx.BoxSizer(wx.VERTICAL)
        self.sizer.Add(self.canvas, proportion=1,
                        flag=wx.LEFT | wx.TOP | wx.EXPAND)
        #self.sizer.Add(self.scrollbar, proportion=0,
        #                flag=wx.LEFT | wx.TOP | wx.EXPAND)
        self.SetSizer(self.sizer)
        self.Fit()
        self.Bind (wx.EVT_WINDOW_CREATE, self.OnWindowCreate)
        self.Parent.Bind (wx.EVT_SHOW, self.OnShowHide)
        self.legend  = None
        self.cycle_colors = plt.rcParams['axes.prop_cycle'].by_key()['color']
        self.constellation_graphs=[]
        self.min = None
        self.max = None
        self.samples = None

    def OnShowHide(self,event):
        if event.IsShown():
            self.draw()

    def OnWindowCreate(self,event):
        if event.GetWindow() == self:
            #self.start_freq, self.end_freq = self.parent.start_freq, self.parent.end_freq
            self.draw()
        else:
            pass

    def draw(self):
        self.axes.clear()
        self.figure.patch.set_alpha(0.2)
        self.Fit()
        #self.figure.subplots_adjust(left=0.15, bottom=0.15, right=0.95, top=0.95)
        self.figure.subplots_adjust(left=0, bottom=0, right=1, top=1)
        self.axes.set_axis_off()
        #self.axes.spines['right'].set_visible(False)
        #self.axes.spines['top'].set_visible(False)
        #self.plot_constellation()
        #self.axes.set_ylabel('dB')
        #self.axes.set_xlabel('Frequency (Mhz)')
        self.axes.set_xlim((-120, 120))
        self.axes.set_ylim((-120, 120))
        self.canvas.draw()

    def clear_data(self):
        self.samples = None

    def show_constellation(self, samples):
        import timeit
        if False:
            maxsize = 32*1024
            if self.samples is not None:
                if self.samples.shape[1] + samples.shape[1] < maxsize:
                    self.samples = np.hstack([self.samples, samples])
                else:
                    self.samples = np.hstack([self.samples[:,samples.shape[1]:], samples])
            else:
                self.samples = samples
        else:
            self.samples = samples
        if len(self.constellation_graphs)>0:
            self.constellation_graphs[0].remove()
            self.constellation_graphs=[]
        #dtdebug(f'constellation: plotting {self.samples.shape} samples')
        start =timeit.timeit()
        with warnings.catch_warnings():
            warnings.filterwarnings('ignore', r'All-NaN (slice|axis) encountered')
            graph = self.axes.scatter_density(self.samples[0,:], self.samples[1,:], vmin=0, vmax= 10., cmap=white_viridis)

        end =timeit.timeit()

        self.constellation_graphs.append(graph)
        if len(self.constellation_graphs) > 5:
            old = self.constellation_graphs.pop(0)
            old.remove()
        self.axes.set_xlim((-120, 120))
        self.axes.set_ylim((-120, 120))
        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)

    def clear_constellation(self):
        #dtdebug(f'clearing constellation samples')
        for x in self.constellation_graphs:
            x.remove()
        self.constellation_graphs = []
        self.canvas.draw()
        wx.CallAfter(self.parent.Refresh)


class SmallConstellationPlot(ConstellationPlotBase):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, (1.5,1.5), *args, **kwargs)

class ConstellationPlot(ConstellationPlotBase):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, (1.5, 1.5), *args, **kwargs)
