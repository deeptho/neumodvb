#!/usr/bin/python3
import sys
import os
import wx
import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.dates import DateFormatter, AutoDateFormatter, WeekdayLocator,DayLocator, HourLocator, AutoDateLocator, MONDAY
from cycler import cycler
mpl.use('WXAgg')
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/neumodb/devdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pystatdb
import pyepgdb
import pychdb
import pydevdb
import datetime
from dateutil import tz
import time
import numpy as np

def get_adapters():
    txn = devdb.rtxn()
    ret={}
    for a in  pydevdb.fe.list_all_by_adapter_no(txn):
        ret[a.k.adapter_mac_address] = f'{a.adapter_no}: {a.adapter_name}'
    txn.abort()
    return ret



statdb = pystatdb.statdb()
statdb.open("/mnt/neumo/db/statdb.mdb/")
devdb = pydevdb.devdb()
devdb.open("/mnt/neumo/db/devdb.mdb/")

chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/")
txn=statdb.rtxn()

adapters=get_adapters()


chdb_rtxn=chdb.rtxn()


mux = pychdb.dvbs_mux.find_by_sat_pol_freq(chdb_rtxn, sat_pos=2820, pol= pychdb.fe_polarisation_t.V,
                                         frequency=11778000,  key_prefix = pychdb.dvbs_mux.dvbs_mux_prefix.none)
chdb_rtxn.abort()


sort_order = (pystatdb.signal_stat.subfield_from_name('k.time')<<24) | \
    (pystatdb.signal_stat.subfield_from_name('k.lnb.card_mac_address')<<16) | \
    (pystatdb.signal_stat.subfield_from_name('k.lnb.rf_input')<<8)


ref=pystatdb.signal_stat.signal_stat()
ref.k.live = False
ref.k.mux = mux.k

screen=pystatdb.signal_stat.screen(txn, sort_order=sort_order,
                                   key_prefix_type=pystatdb.signal_stat.signal_stat_prefix.live_mux, key_prefix_data=ref)

ret=[]
for idx in range(screen.list_size):
    ss = screen.record_at_row(idx) # record for one tuned session
    t =[]
    snr = []
    addr = ss.k.lnb.card_mac_address
    for idx, st in enumerate(ss.stats):
        t1 = datetime.datetime.fromtimestamp(ss.k.time + idx*300, tz=tz.tzlocal())
        t.append(t1)
        snr.append(st.snr)
    t=np.array(t)
    snr=np.array(snr)
    ret.append((t, snr, adapters.get(addr, hex(addr))))
txn.abort()

plt.ion()
fig , axes = plt.subplots()
labeled = set()

c = (cycler(color=['r', 'g', 'b', 'y']) + \
     cycler(linestyle=['-', '--', ':', '-.']))

for t, snr, label in ret:
    if label in labeled:
        label=None
    else:
        labeled.add(label)
    axes.plot(t, snr/1000., color='black', label=label)
axes.set_prop_cycle(c)
axes.legend()
#dayFormatter = DateFormatter('%Y%m%d', tz=tz.tzlocal())
alldays = AutoDateLocator(minticks=1, maxticks=20,tz=tz.tzlocal())
#fmt = DateFormatter(fmt='%Y-%m-%d', tz=tz.tzlocal())
#axes.xaxis.set_major_locator(alldays)
#axes.xaxis.set_major_formatter(fmt)
