#!/usr/bin/python3
import sys
import os
import wx
import matplotlib as mpl
import matplotlib.pyplot as plt
mpl.use('WXAgg')
#del sys.modules['gi.repository.Gtk']
import numpy as np
import numpy as np
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/receiver')
sys.path.insert(0, '../../build/src/stackstring/')
sys.path.insert(0, '../../gui/')
from pyspectrum import  find_spectral_peaks, make_kernels
from neumodvb.config import options
from matplotlib.widgets import Slider
plt.ion()

#specfname=f'{options.spectrum_path}/30.0W/2022-11-07_20:49:34_H_dish2_C92ab2200_RF1_spectrum.dat'
specfname=f'{options.spectrum_path}/30.0W/2022-11-07_20:49:48_V_dish2_C92ab2200_RF1_spectrum.dat'
windows = np.array([2,    4,    6,    8,   10,   12,   14,   16,   18,   20,   22,
	                  24,   26,   28,   30,   32,   34,   36,   38,   40,   42,   44,
	                  46,   48,   50,   52,   54,   56,   58,   60,   62,   64,   66,
	                  68,   72,   74,   76,   80,   82,   86,   88,   92,   94,   98,
	                  102,  106,  108,  112,  116,  120,  126,  130,  134,  140,  144,
	                  150,  154,  160,  166,  172,  178,  184,  190,  198,  204,  212,
	                  220,  228,  236,  244,  252,  262,  270,  280,  290,  300,  312,
	                  322,  334,  346,  358,  370,  384,  398,  412,  426,  442,  456,
	                  474,  490,  508,  526,  544,  564,  584,  604,  626,  648,  670,
	                  694,  720,  744,  772,  798,  826,  856,  886,  918,  950,  984,
	                  1020, 1056, 1094, 1132, 1172, 1214, 1256, 1302, 1348, 1396, 1444,
	                  1496, 1548, 1604, 1660, 1720, 1780, 1844, 1910, 1976, 2048
                    ])


data = np.atleast_2d(np.loadtxt(specfname))
spec = data[:,1]
freq = data[:,0]
w=200

annotymin, annotymax = spec.min(), spec.max()
fig , axes = plt.subplots()
axes.plot(freq, spec)

vlines1, vlines2 = None, None

def make_plot(idx=10):
    global falling_idxs, rising_idxs, falling_resp, rising_resp
    global vlines1, vlines2, fig, axes
    w=windows[idx]
    w=idx
    print(f'w={w}')
    peak_marks, responses = make_kernels(freq, spec, w)
    falling, rising = peak_marks
    falling_resp, rising_resp = responses
    falling_idxs = np.where(falling!=0)[0]
    rising_idxs = np.where(rising!=0)[0]
    if vlines1 is not None:
        vlines1.remove()
    if vlines2 is not None:
        vlines2.remove()
    vlines1=axes.vlines(freq[falling_idxs], annotymin, annotymin+ falling_resp[falling_idxs],
                        color='red')
    vlines2=axes.vlines(freq[rising_idxs], annotymin, annotymin+rising_resp[rising_idxs],
                        color='blue')

make_plot()
#axes.vlines(freq[np.where(rising!=0)], annotymin, annotymax, color='blue')

def update(val):
    w = int(val);
    make_plot(w)
    fig.canvas.draw_idle()

axslider = fig.add_axes([0.25, -0, 0.65, 0.03])
#slider = Slider(ax=axslider, label='w', valmin=0, valmax=windows.shape[0]-1, valinit=10)
slider = Slider(ax=axslider, label='w', valmin=2, valmax=200, valinit=98)

# register the update function with each slider
slider.on_changed(update)



if False:
    peak_freq, peak_sr = find_spectral_peaks(spec[:,0], spec[:,1])
    np.savetxt(f'{options.spectrum_path}/19.2E/2022-06-28_01:13:49_H_dish0_adapter0_peaks.dat', np.vstack([peak_freq, peak_sr]).T)


    print(tpsgt[2])
    print(peak_freq[2], peak_sr[2])
