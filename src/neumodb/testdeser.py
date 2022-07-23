#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pydeser
import pychdb
chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/", allow_degraded_mode=True)
txn=chdb.rtxn()

if False:
    sort_order = pychdb.service.subfield_from_name('k.service_id')<<24
    screen=pychdb.service.screen(txn, sort_order=sort_order)

    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)


#d=pydeser.test_export(txn)
schema = pydeser.schema_map(txn)
