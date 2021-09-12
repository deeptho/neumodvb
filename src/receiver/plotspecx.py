#!/usr/bin/python3
import os
import sys
import time
import math
import numpy as np
import matplotlib.pyplot as plt
import os
import regex as re
from sklearn.linear_model import LinearRegression

def load_blindscan(fname):
    x=np.loadtxt(fname, dtype={'names': ('standard', 'frequency', 'polarisation',
                                         'symbol_rate','rolloff', 'modulation'),
                               'formats': ('S1', 'i8', 'S1', 'i8', 'S1', 'S1')})
    return x['frequency']

def plotspec(fname, pol, lim=None):
    fig, ax= plt.subplots();
    have_blindscan = False
    try:
        x=np.loadtxt(fname)
        f=x[:,0]
        spec = x[:,1]
        ax.plot(f, spec, label="spectrum (dB)")

        tps=load_blindscan(fname.replace('spectrum', 'blindscan'))
        f1= tps/1000
        spec1 = tps*0+-70000
        ax.plot( f1, spec1,  '+', label="Found TPs")
        have_blindscan = True
    except:
        pass
    if have_blindscan:
        title='Blindscan result - {fname}'
    else:
        title='Spectrum - {fname}'
    plt.title(title.format(pol=pol, fname=fname));
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return

def n(s,x):
    return 10*s - 10*s.mean() +x[:,1].mean()


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


def x(fname):
    a=np.loadtxt(fname);
    fig, ax= plt.subplots();
    plt.title(fname);
    ax.plot(a[:,0], a[:,1], label="spec")
    ax.plot(a[:,0], a[:,2], label="band")
    plt.legend()

def y(fnames):
    ret=[]
    fig, ax= plt.subplots();
    plt.title("spec");
    for fname in fnames:
        x=np.loadtxt(fname)
        t=x[:,0]
        a=x[:,1]
        b=x[:,2]
        ax.plot(t, a/1000, label=fname)
        ret.append(x)
    plt.legend()
    ax.set_xlim([11430,11500])
    return ret

    ax.set_xlim([11430,11500])
def z(fname1, fname2):
    a1=np.loadtxt(fname1);
    a2=np.loadtxt(fname2);
    fig, ax= plt.subplots();
    plt.title("band");
    ax.plot(a1[:,0], a1[:,1], label=fname1)
    ax.plot(a2[:,0], a2[:,2], label=fname2)
    plt.legend()

fnames= [
    "/tmp/spectrumH256_100.dat", "/tmp/spectrumH256_50.dat",
    #"/tmp/spectrumH256_1000_sweep.dat", "/tmp/spectrumH256_500_sweep.dat"
    ]
fnames= [
    "/tmp/spectrumH512_50.dat", "/tmp/spectrumH256_50.dat",
    "/tmp/spectrumH512_100.dat",
    "/tmp/spectrumH256_100.dat",
    #"/tmp/spectrumH256_500_sweep.dat"
    ]
offsets =[ -6000-3216, -3216, 0, 0]
offsets =[ -60351-6000, -60351, 0, 0]
offsets =[ 0, 0, 0, 0]
subsample =[ 10, 10, 5,5]
#x("/tmp/spectrumH256_100.dat") #23s
#x("/tmp/spectrumH256_50.dat") #45s
#x("/tmp/spectrumH512_50.dat") #41s

ret=y(fnames)


if __name__ == "__main__":
    import signal
    from matplotlib import pyplot as plt
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    try:
        import sys, inspect
        if sys.ps1: interpreter = True
    except AttributeError:
        interpreter = False
        if sys.flags.interactive: interpreter = True
    if interpreter:
        plt.ion()
        plt.show()
    else:
        plt.ioff()
        plt.show()
