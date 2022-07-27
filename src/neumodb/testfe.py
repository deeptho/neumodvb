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
import time
def mac_fn(mac):
    return mac.to_bytes(6, byteorder='little').hex(":")


def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

chdb = pychdb.chdb()

chdb.open("/mnt/neumo/db/chdb.mdb/")
txn=chdb.rtxn()

sort_order = pychdb.fe.subfield_from_name('k.adapter_mac_address')<<24
screen=pychdb.fe.screen(txn, sort_order=sort_order)
for idx in range(screen.list_size):
    ll = screen.record_at_row(idx)
    if ll.present:
        print(f"{mac_fn(ll.k.adapter_mac_address)}: {ll.adapter_name}: {ll.adapter_no}.{ll.k.frontend_no}: {ll.master_adapter_mac_address}"
              f' present={ll.present}')

print("===================================")
sort_order = pychdb.fe.subfield_from_name('card_mac_address')<<24
screen=pychdb.fe.screen(txn, sort_order=sort_order)
ret={}
for idx in range(screen.list_size):
    ll = screen.record_at_row(idx)
    if ll.present:
        ret[ll.card_mac_address] = ll.card_name
for mac,name in ret.items():
    print(f"{mac_fn(mac)}: {name}")
