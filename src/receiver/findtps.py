#!/usr/bin/python3
import os
import sys
import time
import regex as re

os.environ['XDG_CURRENT_DESKTOP'] = 'none'
def maindir():
    dir=os.path.dirname(os.path.realpath(__file__))
    return dir

dir=os.path.realpath(os.path.join(maindir(),'../../build/src'))
sys.path.insert(0, os.path.join(dir, 'viewer/'))
sys.path.insert(0, os.path.join(dir, 'receiver'))
sys.path.insert(0, os.path.join(dir, 'stackstring'))
sys.path.insert(0, os.path.join(dir, 'neumodb'))
sys.path.insert(0, os.path.join(dir, 'neumodb/chdb'))
sys.path.insert(0, os.path.join(dir, 'neumodb/statdb'))
sys.path.insert(0, os.path.join(dir, 'neumodb/epgdb'))
sys.path.insert(0, os.path.join(dir, 'neumodb/recdb'))
sys.path.insert(0, os.path.join(dir, 'stackstring/'))

import math
import numpy as np
import matplotlib.pyplot as plt
import os
#from sklearn.linear_model import LinearRegression
#import pandas as pd
import pyspectrum1
import pyspectrum
from pyspectrum import find_annot_locations

#import matplotlib
#matplotlib.use('wxAgg')

plt.ion()
#q=np.loadtxt(fname)
#v[:,1]*=10
def nxxxx(s,x):
    return 10*s - 10*s.mean() +x[:,1].mean()

def load_blindscan(fname):
    x=np.loadtxt(fname, dtype={'names': ('standard', 'frequency', 'polarisation',
                                         'symbol_rate','rolloff', 'modulation'),
                               'formats': ('S1', 'i8', 'S1', 'i8', 'S1', 'S1')})
    if False:
        f, s = convert(x['frequency']/1000, x['symbol_rate']/1000)
    else:
        f, s = x['frequency']/1000, x['symbol_rate']/1000
    return np.vstack([f,s ]).T


#y=np.loadtxt("/tmp/spectrum1.dat")
#z=np.loadtxt("/mnt/scratch/tbs/spectrum_28.2EH.dat")
def moving_average(x, w):
    return np.convolve(x, np.ones(w), 'same') / w

def ann(f,v, s, xoffset=3, yoffset=0):
    pt=[f,v];
    ax.annotate(f"{f}H/V {s}kS/s", pt, xytext=(pt[0]+xoffset, pt[1]+yoffset), arrowprops=dict(facecolor='red', arrowstyle='->'))

def convert(t, a):
    l=len(np.where (t<11700)[0])
    t = 5150 -(t[:l] - 9750)
    a = a[:l]
    return t[::-1],a[::-1]

def plot_spec(fname,lim):
    fig, ax= plt.subplots(figsize=(15,5));
    plt.title("spec");
    print(f"loading {fname}")
    x=np.loadtxt(fname)
    t=x[:,0]
    a=x[:,1]
    b=x[:,2]
    #t, a = convert(t, a)
    b = b[:len(a)][::-1]
    x = np.vstack([t,a,b]).T
    ax.plot(t, a/1000,label=fname)
    #plt.legend()
    mng = plt.get_current_fig_manager()
    #s=mng.window.screen().availableSize()
    #mng.resize(s.width(), s.height()/2)
    fig.canvas.toolbar.push_current() # save the 'un zoomed' view to stac
    #ax.set_xlim([min(t), min(t)+400])
    ax.set_ylim([min(a)/1000, max(a)/1000+8])
    if lim is not None:
        ax.set_xlim(lim)
    return x, fig, ax

def plot_tps(tpsk, spec, label='Candidate TP', yoffset=2):
    #n = len(spec[:,1])
    f = tpsk[:,0]
    snr = None
    bw =  tpsk[:,1]/2000
    #snr =  tpsk[:,2]/1000 +offset1 if tpsk.shape[1]>=3 else None
    idxs= np.searchsorted(spec[:,0], f, side='left')
    sig = spec[idxs,1]/1000 +yoffset
    plt.errorbar(f, sig,  xerr=bw, fmt='+', label=label)
    if snr is not None:
        plt.plot(f, snr,  '+', label="snr")
    #plt.legend()
    return idxs

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

    fig.canvas.mpl_connect("motion_notify_event", hover)

def annot_size(figure, axes):
    r = figure.canvas.get_renderer()
    t = axes.text(-50, 11000, '10841.660V/H\n10841.660V/H')
    bb = t.get_window_extent(renderer=r).transformed(axes.transData.inverted())
    t.remove()
    print(f"Box: {bb.width} x {bb.height}")
    return bb


def ann_tps(tpsk, spec, ax, offset=-64, xoffset=0, yoffset=1):
    #n = len(spec[:,1])
    f = tpsk[:,0]
    snr = None
    bw =  tpsk[:,1]/2
    i =0
    annots =[]


    idxs =  np.searchsorted(spec[:,0], f, side='left')
    scale = (len(spec[:,0])-1)/(spec[-1,0] - spec[0,0])

    bb = annot_size(ax.figure, ax)
    w = int(scale*bb.width)
    h = bb.height*1000 *1.1
    print(f"scale={scale} W={w} H={h}")
    offset=4000
    annoty, lr, a, b=find_annot_locations(spec[:,1], idxs, w, h, offset)

    for f1, bw1, idx,annoty,l in zip(f, bw, idxs, annoty, lr):
        pt=[f1, annoty/1000.]
        offset = 0 #yoffset  if (i%2) else yoffset-5
        xoffset = 0
        offset = 0
        annot=ax.annotate(f"{f1:8.3f}H/V\n{int(bw1*2.25)}kS/s", \
                          pt, xytext=(pt[0]+xoffset, pt[1]+offset), \
                          ha='right' if l else 'left')
       #                           arrowprops=dict(facecolor='red', arrowstyle='->'), \
        #annot.set_visible(False)
        annots.append(annot)
        i=i+1
    ax.set_ylim([a/1000, b/1000])
    #ax.set_ylim([min(spec[:,1])/1000, max(annoty, max(spec[:,1]))/1000+h/1000])
    return annots

def plot_peaks(peaks, label='peaks', offset=-60):
    global ret
    n = len(ret[0][:,1])
    snr = None
    #snr =  tpsk[:,2]/1000 +offset1 if tpsk.shape[1]>=3 else None
    sig = peaks*0+offset
    plt.plot(peaks, sig,  '+', label=label)
    #plt.legend()


def plot_marks(marks, offset=-55, label='xxx', use_index=True):
    global ret
    n = len(ret[0][:,1])
    f = np.array(range(0,n))
    sig = marks*offset
    plt.plot(f, sig, '+', label=label)
    #plt.legend()

if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/5.0W/6909x_wall/spectrumH.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/5.0W/6909x_wall/blindscanH.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/5.0W/6909x_wall/spectrumV.dat"
    tpsname = "/home/philips/sat/neumosdvb_data/spectra/5.0W/6909x_wall/blindscanV.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/26.0E/6909x_wavefrontier/spectrumH.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/26.0E/6909x_wavefrontier/blindscanH.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/26.0E/6909x_wavefrontier/spectrumV.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/26.0E/6909x_wavefrontier/blindscanV.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/28.2E/6909x_wavefrontier/spectrumH.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/28.2E/6909x_wavefrontier/blindscanH.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/28.2E/6909x_wavefrontier/spectrumV.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/28.2E/6909x_wavefrontier/blindscanV.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=np.loadtxt(tpsname)
if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/51.5E/6909x_garden/spectrumH.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/51.5E/6909x_garden/blindscanH.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=load_blindscan(tpsname)


if False:
    specname = "/home/philips/sat/neumodvb_data/spectra/51.5E/6909x_garden/spectrumV.dat"
    tpsname = "/home/philips/sat/neumodvb_data/spectra/51.5E/6909x_garden/blindscanV.dat"
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=load_blindscan(tpsname)

if False:
    specname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/spectrum_a0_V.dat"
    tpsname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/blindscan_a0_V.dat"

if False:
    specname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/spectrum_a0_H.dat"
    tpsname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/blindscan_a0_H.dat"

if False:
    specname = "/home/philips/sat/neumodvb_data/20210127_42e/spectrum_a2_V.dat"
    tpsname =  "/home/philips/sat/neumodvb_data/20210127_42e/blindscan_a2_V.dat"

if False:
    specname = "/home/philips/sat/neumodvb_data/20210303_5.0w/spectrum_a2_H.dat"
    tpsname =  "/home/philips/sat/neumodvb_data/20210303_5.0w/blindscan_a2_H.dat"

if False:
    specname = "/home/philips/sat/neumodvb_data/20210304_5.0w/spectrum_a2_H.dat"
    tpsname =  "/home/philips/sat/neumodvb_data/20210304_5.0w/blindscan_a2_H.dat"
if True:
    specname = "/tmp/spectrum_a2_H.dat"
    tpsname =  "/tmp/blindscan_a2_H.dat"



lim= [11430,11500]
lim= [10700,11700]
lim =None

#ret=z(fnames, [11430,11500])

w1=31
mincount=1
thresh1=3000
thresh2=3000

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



def process(specname, tpsname, pol):
    spec, fig, ax, =plot_spec(specname, lim)
    frequency_step = round(1000*(spec[1:,0]-spec[:-1,0]).mean())
    print(f"STEP={frequency_step}")
    #tps=load_blindscan(tpsname)
    sig = spec[:,1]
    #f=spec[:,0]
    if False:
        #tpsk1 = pyspectrum1.find_kernel_tps(sig, w1, thresh1, mincount, frequency_step)
        tpsk2 = pyspectrum.find_kernel_tps(sig, w1, thresh2, mincount, frequency_step)
        #tpsk1[:,0] = tpsk1[:,0]+10700000
        tpsk2[:,0] = tpsk2[:,0]+10700000
        if False:
            f,s = convert (tpsk2[:,0]/1000, tpsk2[:,1])
        else:
            f,s = tpsk2[:,0]/1000, tpsk2[:,1]
        tpsk2 =  np.vstack([f,s ]).T
    else:
        #tpsk1 = load_blindscan(tpsname)
        tpsk2 = load_blindscan(tpsname)
    l2 = len(tpsk2[:,0])
    plt.title(f"{specname}");
    #plot_tps(tps, spec, 'found TP', offset=-58)
    #plot_tps(tpsk1, spec, 'Algo 1', offset=-62)
    annotx = plot_tps(tpsk2, spec, 'Algo 2', yoffset=2)
    if True:
        w = 200
        h = 2000
        offset=2000
        fig.annots = ann_tps(tpsk2, spec, ax, w, h, offset)
        #tooltips(fig)
        #fig.canvas.toolbar.push_current()

    return fig,ax,spec, annotx

rx = re.compile('(spectrum)(_a[0-9]+_)*([HV]).dat$')
def find_data(d='/tmp/'):
    ret=[]
    for root, dirs, files in os.walk(d):
      for file in files:
          res = re.match(rx, file)
          if res is not None:
              specname = os.path.join(root, file)
              file=file.replace('spectrum', 'blindscan')
              tpsname =  os.path.join(root, file)
              #print(tpsname)
              ret.append((specname, tpsname, res. group(3)))
    return ret


d=os.getcwd()
d='/home/philips/sat/neumodvb_data/20210304_5.0w'
files=find_data(d)
ret =dict()

def x(ax):
    bb = get_bboxes(ax.figure.annots)
    import matplotlib.patches as patches
    trf = ax.transData.inverted()
    for b in bb:
        b=b.transformed(trf)
        rect = patches.Rectangle((b.xmin, b.ymin), b.width, b.height, linewidth=1, edgecolor='r', fill=None)
        ax.add_patch(rect)


def overlaps(a, b):
    """
    assumes that a.xmin < b.xmin and a.ymin <= b.ymin
    """
    return b.xmin < a.xmax and b.ymin < a.ymax

all_rects=[]

def remove_rects():
    global all_rects
    for rect in all_rects:
        rect.remove()
    all_rects = []

def adjust(ax):
    remove_rects()
    bb = get_bboxes(ax.figure.annots)
    import matplotlib.patches as patches
    trf = ax.transData.inverted()
    for left, right in zip (bb[:-1], bb[1:]):
        if overlaps(left,right):
            print ((left, right))
            bl=left.transformed(trf)
            br=right.transformed(trf)
            rect = patches.Rectangle((bl.xmin, bl.ymin), bl.width, bl.height, linewidth=1, edgecolor='r', fill=None)
            ax.add_patch(rect); all_rects.append(rect)
            rect = patches.Rectangle((br.xmin, br.ymin), br.width, br.height, linewidth=1, edgecolor='b', fill=None)
            ax.add_patch(rect); all_rects.append(rect)


axs=[]
for specname, tpsname, pol in files[1:]:
    fig, ax, spec, annotx =process(specname, tpsname, pol=pol)
    axs.append(ax)

def doit(axs):
    for ax in axs:
        ax.figure.show()
        x(ax)

#doit(axs)
if False:
    w=200
    h=2
    offset=6

    annoty=find_annot_locations(spec[:,1], annotx, w, h, offset)
    tpsk = load_blindscan(tpsname)
    f = tpsk[:,0]
    idxs=annotx
    sig = spec[idxs,1]/1000
    bw =  tpsk[:,1]/2000
    plt.errorbar(f, sig,  xerr=bw, fmt='+', label='test')
    plt.show()
    plt.legend()


#doit(axs)
#process(specname, tpsname)
"""
Problem is that boxes should not overlap other boxex, the graph and the vertical lines connecting the text box
to the graph and ending on the frequency axis

start by laying out each box (left aligned) above the gaph. Assume vertical positions quantized in units of height

Now clusters of (horizontally) overlapping boxes will appear. Try and swap the leftmost box in each cluster
from left to righ alignment if this is possible without causing overlap to the gaph or left neighboring boxes.

Then nove all othe boxes one level. Again find clusters (note that some problems may have disappeared at this level!)
and retry
"""
