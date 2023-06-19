#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/devdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pydevdb
import pychdb
import pyepgdb

devdb = pydevdb.devdb()
devdb.open("/mnt/neumo/db/devdb.mdb/")
txn=devdb.rtxn()
sort_order = pydevdb.lnb.subfield_from_name('k.lnb_id')<<24
screen=pydevdb.lnb.screen(txn, sort_order=sort_order)
for idx in range(screen.list_size):
    lnb = screen.record_at_row(idx)
    print(f"{str(lnb.k)} a={lnb.can_be_used}")
