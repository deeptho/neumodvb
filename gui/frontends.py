#!/usr/bin/python3
import sys
import os
import time
import neumodvb #forces setting up paths
from neumodvb.config import options
#import pyneumodb
import pychdb

chdb = pychdb.chdb()
chdb.open(options.chdb)

def ps(screen):
    for i in range(screen.list_size):
        rec = screen.record_at_row(i)
        #print (rec.k.chg.bouquet_id)
        if rec.k.service.mux.sat_pos == 282:
            print(rec, rec.mtime)
    print("="*10)

def get_screen():
    txn = chdb.rtxn()
    subfield = pychdb.fe.subfield_from_name('k.adapter_no')<<24 | \
        pychdb.fe.subfield_from_name('k.frontend_no')<<16
    screen= pychdb.fe.screen(txn, subfield)
    txn.abort()
    return screen

s= get_screen()
print('Frontends:')
for i in range(s.list_size):
    print(f'{s.record_at_row(i)}')
