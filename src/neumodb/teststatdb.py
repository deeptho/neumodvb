#!/usr/bin/python3
import sys
import os
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

def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

statdb = pystatdb.statdb()
if True:
    statdb.open("/mnt/neumo/db/statdb.mdb/")
    txn=statdb.rtxn()
    zz= pystatdb.signal_stat.list_all(txn, order=pystatdb.signal_stat.signal_stat_order.key, use_index=True)
    for z in zz:
        print (z, datetime.datetime.fromtimestamp(z.k.time, tz=tz.tzlocal()).strftime("%Y%m%d %H:%M:%S.%f") )
    del txn


    print ([(z, z.k.live) for z in zz])

#txn=statdb.rtxn()
k=pystatdb.signal_stat_key.signal_stat_key()
k.live= False
k.mux = zz[-1].k.mux


txn=statdb.rtxn()
x=pystatdb.signal_stat.find_by_key(txn, k, pystatdb.find_type_t.find_geq, pystatdb.signal_stat.signal_stat_prefix.live_mux)
sort_order = pystatdb.signal_stat.subfield_from_name('k.time')<<24
ref=pystatdb.signal_stat.signal_stat()
ref.k.live = True
screen=pystatdb.signal_stat.screen(txn, sort_order=sort_order,
                                   key_prefix_type=pystatdb.signal_stat.signal_stat_prefix.live, key_prefix_data=ref)
for idx in range(screen.list_size):
    ll = screen.record_at_row(idx)
    print(f"{ll}")
