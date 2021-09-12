#!/usr/bin/python3
import os
import sys
import time
#sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/receiver/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pyspectrum
import math
import numpy as np
import matplotlib.pyplot as plt
import os
#from sklearn.linear_model import LinearRegression
import pandas as pd
from pyspectrum import find_tps, find_kernel_tps, rising, falling, morpho

frequency_step =100
plt.ion()
#q=np.loadtxt(fname)
#v[:,1]*=10
def n(s,x):
    return 10*s - 10*s.mean() +x[:,1].mean()

def load_blindscan(fname):
    x=np.loadtxt(fname, dtype={'names': ('standard', 'frequency', 'polarisation',
                                         'symbol_rate','rolloff', 'modulation'),
                               'formats': ('S1', 'i8', 'S1', 'i8', 'S1', 'S1')})
    return np.vstack([x['frequency'], x['symbol_rate']/1000]).T


#y=np.loadtxt("/tmp/spectrum1.dat")
#z=np.loadtxt("/mnt/scratch/tbs/spectrum_28.2EH.dat")
def moving_average(x, w):
    return np.convolve(x, np.ones(w), 'same') / w

if False:
    plt.figure();plt.plot(x[:,0], moving_average(x[:,1],10))
    #plt.figure();plt.plot(y[:,0], moving_average(y[:,1],100)*3/64.)
    plt.figure();plt.plot(z[:,0], moving_average(z[:,1],10))
elif False:
    #plt.figure();
    #plt.plot(q[:,0], q[:,1], label="new")
    #plt.legend()
    #plt.figure();
    plt.plot(x[:,0], x[:,1], label="stid-g")
    plt.plot(y[:,0], y[:,1], label="stidfft-g")
    plt.legend()
    plt.figure();
    plt.plot(z[:,0], moving_average(z[:,1],10), label="stid-w")
    #plt.plot(w[:,0], w[:,1], label="stidfft-w")
    plt.plot(v[:,0], moving_average(v[:,1],10), label="stv-w")
    plt.legend()

    #plt.figure();
    #plt.plot(y[:,0], y[:,1], label="stidfft-g")
    #plt.plot(w[:,0], w[:,1], label="stidfft-w")
    #plt.legend()
#plt.figure();plt.plot(z[:,0], z[:,1])
def ann(f,v, s, xoffset=3, yoffset=0):
    pt=[f,v];
    ax.annotate(f"{f}H {s}kS/s", pt, xytext=(pt[0]+xoffset, pt[1]+yoffset), arrowprops=dict(facecolor='red', arrowstyle='->'))

if False:
    a0=np.loadtxt(fname0);
    a1=np.loadtxt(fname1);
    a2=np.loadtxt(fname2);
    fig, ax= plt.subplots();
    ax.plot(a0[:,0], a0[:,1], label="256")
    ax.plot(a1[:,0], a1[:,1], label="256_50kHz")
    ax.plot(a2[:,0], a2[:,1], label="1024")

    ax.set_xlim([11430,11500])
    ann(11456,-37193, 2400, -8, 1000)
    ann(11457,-35417, 256, -3, 3000)
    ann(11458,-34651, 542, 3, 1000)
    ann(11465,-36042, 667, 3, 1000)
    plt.legend()
if False:
    plt.plot(q[:,0], q[:,1], label=fname);plt.legend()

    #fft size 256@100kHz => 23s fft size 1024 => 23s (poorer resolution?)
    #fft size 256@50kHz => 44s


def y(fname,lim):
    ret=[]
    fig, ax= plt.subplots();
    plt.title("spec");
    x=np.loadtxt(fname)
    t=x[:,0]
    a=x[:,1]
    b=x[:,2]
    ax.plot(t, a/1000,label=fname)
    ret.append(x)
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return ret

def y1(fname,lim):
    ret=[]
    fig, ax= plt.subplots();
    plt.title("spec");
    x=np.loadtxt(fname)
    a=x[:,1]
    b=x[:,2]
    ax.plot(a/1000,label=fname)
    ret.append(x)
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return ret

def plot_tps(tpsk, label='Candidate TP', offset=-60, offset1=-40, use_index=False):
    global ret
    n = len(ret[0][:,1])
    f = tpsk[:,0]
    snr = None
    if use_index:
        g= ret[0][:,0]*1000
        scale=n/(g.max() - g.min())
        f = (f -g.min())* scale
        bw =  tpsk[:,1]/2 *scale
        #snr = tpsk[:,2]/1000 +offset1 if tpsk.shape[1]>=3 else None
    else:
        f= f/1000
        bw =  tpsk[:,1]/2000
        #snr =  tpsk[:,2]/1000 +offset1 if tpsk.shape[1]>=3 else None
    sig = tpsk[:,0]*0+offset + np.array(range(len(tpsk[:,0])))%5
    plt.title("Scan results1");
    plt.errorbar(f, sig,  xerr=bw, fmt='+', label=label)
    if snr is not None:
        plt.plot(f, snr,  '+', label="snr")
    plt.legend()

def plot_peaks(peaks, label='peaks', offset=-60):
    global ret
    n = len(ret[0][:,1])
    snr = None
    #snr =  tpsk[:,2]/1000 +offset1 if tpsk.shape[1]>=3 else None
    sig = peaks*0+offset
    plt.plot(peaks, sig,  '+', label=label)
    plt.legend()


def plot_marks(marks, offset=-55, label='xxx', use_index=True):
    global ret
    n = len(ret[0][:,1])
    f = np.array(range(0,n))
    sig = marks*offset
    plt.plot(f, sig, '+', label=label)
    plt.legend()

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
    specname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/spectrum_a0_H.dat"
    tpsname = "/home/philips/sat/neumodvb_data/20210126_33e/13_50/blindscan_a0_H.dat"
    frequency_step = 200
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=load_blindscan(tpsname)

if True:
    specname = "/home/philips/sat/neumodvb_data/20210127_42e/spectrum_a2_V.dat"
    tpsname =  "/home/philips/sat/neumodvb_data/20210127_42e/blindscan_a2_V.dat"
    frequency_step = 117
    #os.system(f"scp streacomold.mynet:{specname} {specname}")
    #os.system(f"scp streacomold.mynet:{tpsname} {tpsname}")
    tps=load_blindscan(tpsname)



lim= [11430,11500]
lim =None

#ret=z(fnames, [11430,11500])

ret=y(specname, lim)


sig = ret[0][:,1]
f=ret[0][:,0]
w1=31
w2=17
mincount=1
thresh=3000
maxagc2level=np.hstack([np.zeros(w1-1), np.array(pd.Series(sig).rolling(w1).max().dropna().tolist())])
minagc2level=np.hstack([np.zeros(w1-1), np.array(pd.Series(sig).rolling(w1).min().dropna().tolist())])
midagc2level=np.hstack([np.zeros(w1-1), np.array(pd.Series(sig).rolling(w1).mean().dropna().tolist())])
maxagc2level2=np.hstack([np.array(pd.Series(sig).rolling(w2).max().dropna().tolist()), np.zeros(w2-1)])
minagc2level2=np.hstack([np.array(pd.Series(sig).rolling(w2).min().dropna().tolist()), np.zeros(w2-1)])
midagc2level2=np.hstack([np.array(pd.Series(sig).rolling(w2).mean().dropna().tolist()), np.zeros(w2-1)])



tst1=maxagc2level-sig>thresh
tst2= np.logical_and(maxagc2level2-sig>thresh , sig-minagc2level>thresh)
tst2=maxagc2level2-sig>thresh

fallingx= np.logical_and(tst1,  np.logical_not(np.roll(tst1,1)))
risingx=np.logical_and(tst2,  np.logical_not(np.roll(tst2,-1)))

#x= find_tps(sig, w1, thresh, mincount)


tpsk = find_kernel_tps(sig, w1, thresh, mincount, frequency_step)

#transponder bands
bands=np.vstack([tpsk[:,0]-tpsk[:,1], tpsk[:,0]+tpsk[:,1]]).T

tpsk[:,0] = tpsk[:,0]+10700000

idx=find_kernel_tps(sig, w1, thresh, mincount, frequency_step)//100


#z(x*-50000, f, label="test")
plot_tps(tps, 'found TP', offset=-58)
plot_tps(tpsk, 'Candidate TP')


#y1(specname, lim=[7400, 8400])
y1(specname, lim=None)
plot_tps(tpsk, 'Candidate TP', use_index=True)
#z1(tpsk[:,2])
#ri= rising(sig, w1,thresh)
#fa= falling(sig, w1,thresh)
#plot_marks(ri, -57, label='rising')
#plot_marks(fa, -55, label='falling')


#z(fa*-40000,     list(range(len(f))), label='falling')
#z(ri*-41000,     list(range(len(f))), label='rising')


#z1(x*-50000, f, label="test")

if False:
    X=morpho(sig, 1000, 5000)
    peaks1=np.where(X[1,:-1] - X[1, 1:]> 2000)[0]
    peaks2=np.where(X[1,1:] - X[1, :-1]> 2000)[0]
    plot_peaks(peaks1, label='p1', offset=-55)
    plot_peaks(peaks2, label='p2', offset=-55)
