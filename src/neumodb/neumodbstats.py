#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
#import pyepgdb

db = pychdb.chdb()
db.open("/mnt/neumo/db/chdb.mdb/")
db.stats()
