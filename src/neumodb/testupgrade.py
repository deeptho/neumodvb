#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/neumodb/schema')
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

chdb = pychdb.chdb()

chdb.open("/mnt/neumo/db/chdb.mdb/", allow_degraded_mode=True)
txn=chdb.rtxn()

sort_order = pychdb.lnb.subfield_from_name('k.adapter_mac_address')<<24
screen=pychdb.lnb.screen(txn, sort_order=sort_order)

for idx in range(screen.list_size):
    ll = screen.record_at_row(idx)
    #ll.adapter_no = ll.k.old_adapter_no
    #fe = pychdb.fe.find_by_adapter_no(txn, ll.adapter_no)
    #ll.k.adapter_mac_address = fe.k.adapter_mac_address
    #pychdb.put_record(txn, ll)
    print(f"{str(ll.k.adapter_mac_address)}: {str(ll.adapter_no)}")
#txn.commit()
