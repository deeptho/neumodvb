#!/usr/bin/python3
import sys
import os
import time
sys.path.insert(0, '../../x86_64/target/lib/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pychdb
import pyreceiver

receiver= pyreceiver.dvb_receiver_t()

#import pyepgdb

def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

db = pychdb.chdb()
if True:
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    service=pychdb.service.find_by_ch_order(txn, 101)
    subscription_id = receiver.subscribe(service)
    del txn
time.sleep(3600)
