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

chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/")
txn=chdb.rtxn()
sat_pos=-3000
lnb = pydevdb.lnb.lnb()
mux = pychdb.select_reference_mux(txn, lnb, sat_pos)
print(f'mux={mux}')
