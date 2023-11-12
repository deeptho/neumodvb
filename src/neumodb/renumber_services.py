#!/usr/bin/python3
import sys
import os
import shutil
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pychdb

def get_screen(txn):
    subfield = pychdb.service.subfield_from_name('ch_order')<<24
    screen= pychdb.service.screen(txn, subfield)
    return screen

chdb = pychdb.chdb()
chdb.open('/mnt/neumo/db/chdb.mdb')
wtxn = chdb.wtxn()

screen=get_screen(wtxn)
for i in range(screen.list_size):
    service = screen.record_at_row(i)
    #print (rec.k.chg.bouquet_id)
    if service.ch_order == 0:
        service.ch_order=65535
        pychdb.put_record(wtxn, service)
wtxn.commit()
