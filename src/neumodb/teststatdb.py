#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
#sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pystatdb
import pyepgdb
import datetime
from dateutil import tz

def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

db = pystatdb.statdb()
if True:
    db.open("/mnt/neumo/db/statdb.mdb/")
    txn=db.rtxn()
    zz= pystatdb.signal_stat.list_all(txn, order=pystatdb.signal_stat.signal_stat_order.key, use_index=True)
    for z in zz:
        print (z, datetime.datetime.fromtimestamp(z.time, tz=tz.tzlocal()).strftime("%Y%m%d %H:%M:%S.%f") )
    del txn
