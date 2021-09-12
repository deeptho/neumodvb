#!/usr/bin/python3
import os
import sys
import time
import math
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import os
import regex as re



def plotspec(fname, pol, lim=None):
    fig, ax= plt.subplots();
    have_blindscan = False
    try:
        x=np.loadtxt(fname)
        ax.imshow(pd.crosstab(x[:,0], x[:,1]), cmap='Blues',  label="IQ Samples")
    except:
        pass
    title='IQ samples - {fname}'
    plt.title(title.format(pol=pol, fname=fname));
    plt.legend()
    if lim is not None:
        ax.set_xlim(lim)
    return



rx = re.compile('(iq)([HV]).dat$')
def find_data(d='/tmp/'):
    ret=[]
    for root, dirs, files in os.walk(d):
      for file in files:
          res = re.match(rx, file)
          if res is not None:
              fname = os.path.join(root, file)
              print(fname)
              ret.append((fname, res.group(2)))
    return ret


d=os.getcwd()


files=find_data(d)
for file, pol in files:
    plotspec(file, pol=pol)

#fname='/tmp/iq.dat'
#iq=np.loadtxt(fname)


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
