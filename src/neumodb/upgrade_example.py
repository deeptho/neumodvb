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

def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/")
txn=chdb.wtxn()

statdb = pystatdb.statdb()
statdb.open("/mnt/neumo/db/statdb.mdb/")
txn_stat=statdb.wtxn()

def get_mac_address_dict(txn):
    sort_order = pychdb.fe.subfield_from_name('k.adapter_mac_address')<<24
    screen=pychdb.fe.screen(txn, sort_order=sort_order)
    ret={}
    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        ret[ll.adapter_no] = ll.k.adapter_mac_address
    return ret
mac_address_dict = get_mac_address_dict(txn)


sort_order = pychdb.lnb.subfield_from_name('k.adapter_mac_address')<<24
screen=pychdb.lnb.screen(txn, sort_order=sort_order)

for idx in range(screen.list_size):
    ll = screen.record_at_row(idx)
    #ll.adapter_no = ll.k.old_adapter_no
    #fe = pychdb.fe.find_by_adapter_no(txn, ll.adapter_no)
    #ll.k.adapter_mac_address = fe.k.adapter_mac_address
    #pychdb.put_record(txn, ll)
    pychdb.delete_record(txn, ll)
    mac_address =mac_address_dict[ll.k.old_adapter_no]
    old = ll.k.adapter_mac_address
    x = ll.k
    x.adapter_mac_address = mac_address
    ll.k = x
    pychdb.put_record(txn, ll)
    print(f"SET mac={str(ll.k.adapter_mac_address)} (was {old})  for lnb with adapter_no={str(ll.k.old_adapter_no)}")

if True:
    sort_order = pystatdb.spectrum.subfield_from_name('k.lnb_key.old_adapter_no')<<24
    screen=pystatdb.spectrum.screen(txn_stat, sort_order=sort_order)

    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        pystatdb.delete_record(txn_stat, ll)
        mac_address =mac_address_dict[ll.k.lnb_key.old_adapter_no]
        k = ll.k
        lnb_key = k.lnb_key
        lnb_key.adapter_mac_address = mac_address
        k.lnb_key = lnb_key
        ll.k = k
        pystatdb.put_record(txn_stat, ll)
        print(f'SET mac={str(ll.k.lnb_key.adapter_mac_address)} for spectrum with adapter_no={ll.k.lnb_key.old_adapter_no}')

if True:
    txn.commit()
    txn_stat.commit()
