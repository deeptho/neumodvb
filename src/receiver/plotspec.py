#!/usr/bin/python3
import os
import sys
import time
import math
import numpy as np
import matplotlib.pyplot as plt
import os
import regex as re


def load_blindscan(fname):
    x=np.loadtxt(fname, dtype={'names': ('standard', 'frequency', 'polarisation',
                                         'symbol_rate','rolloff', 'modulation'),
                               'formats': ('S1', 'i8', 'S1', 'i8', 'S1', 'S1')})
    return x['frequency']

def load_blindscan_old(fname):
    x=np.loadtxt(fname, dtype={'names': ('frequency', 'bandwidth'),
                               'formats': ('i8', 'i8')})
    return x['frequency']

def plotspec(fname, pol, lim=None):
    fig, ax= plt.subplots();
    fig.canvas.set_window_title(fname)
    have_blindscan = False
    x=np.loadtxt(fname)
    print (f"Loading {fname}")
    ret=dict()
    for method in [load_blindscan, load_blindscan_old]:
        try:
            #x[:,0] = np.array(range(x.shape[0]))
            f=x[:,0]
            spec = x[:,1]/1000.
            ax.plot(f, spec, label="spectrum (dB)")
            ret[fname] = spec
            tps=method(fname.replace('spectrum', 'blindscan'))
            f1= tps/1000
            spec1 = tps*0+-70
            ax.plot( f1, spec1,  '+', label="Found TPs")
            have_blindscan = True
        except:
            pass
        if have_blindscan:
            break
    if have_blindscan:
        title='Blindscan result - {fname}'
    else:
        title='Spectrum - {fname}'
    plt.title(title.format(pol=pol, fname=fname));
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return ret

def plotspec1(fname1, fname2, pol, lim=None):
    fig, ax= plt.subplots();
    fig.canvas.set_window_title(fname1)
    have_blindscan = False
    x1=np.loadtxt(fname1)
    x2=np.loadtxt(fname2)
    print (f"Loading {fname1} {fname2}")
    for method in [load_blindscan, load_blindscan_old]:
        try:
            #x[:,0] = np.array(range(x.shape[0]))
            f=x1[:,0]
            spec1 = x1[:,1]/1000.
            spec2 = x2[:,1]/1000.
            ax.plot(f, spec1, label="spectrum1 (dB)")
            ax.plot(f, spec2, label="spectrum2 (dB)")

            tps=method(fname.replace('spectrum', 'blindscan'))
        except:
            pass
        if have_blindscan:
            break
    if have_blindscan:
        title='Blindscan result - {fname}'
    else:
        title='Spectrum - {fname}'
    plt.title(title.format(pol=pol, fname=fname));
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return



rx = re.compile('(spectrum)(_a[0-9]+_)*([HV]).dat$')
def find_data(d='/tmp/'):
    ret=[]
    for root, dirs, files in os.walk(d):
      for file in files:
          res = re.match(rx, file)
          if res is not None:
              fname = os.path.join(root, file)
              #print(fname)
              ret.append((fname, res. group(3)))
    return ret


d=os.getcwd()

files=find_data(d)

ret =dict()
for file, pol in files:
    ret = {**ret , **plotspec(file, pol=pol)}



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
