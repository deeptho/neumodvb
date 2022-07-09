#!/usr/bin/python3
import numpy as np
import sys
import os
import matplotlib as mpl
from numpy.linalg import lstsq

mpl.use("WXAgg")
os.environ['NO_AT_BRIDGE'] = '1'
import matplotlib.pyplot as plt
plt.ion()


sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pystatdb
import pyepgdb
import pychdb
import datetime
from dateutil import tz
import time


def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

statdb = pystatdb.statdb()
if True:
    statdb.open("/mnt/neumo/db/statdb.mdb/")
    txn=statdb.rtxn()
    zz= pystatdb.signal_stat.list_all(txn, order=pystatdb.signal_stat.signal_stat_order.key, use_index=True)
    ret=[]
    ret1 =[]
    if True:
        for z in zz:
            if z.locktime_ms >0 and z.symbol_rate>0:
                #ret.append((z.k.mux.sat_pos, z.k.frequency, z.k.pol, z.symbol_rate, z.locktime_ms))
                ret.append((z.symbol_rate, z.locktime_ms))
                ret1.append(z)
        del txn
ret =np.array(ret)

fig=plt.figure()
ax1 = plt.subplot(111)
ax1.plot(ret[:,0]/1000, ret[:,1], '.')
ax1.set_xlabel('symbol rate (kHz)')
ax1.set_ylabel('lock time (ms)')
