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
import pyepgdb


epgdb_name = "/tmp/epgdb.mdb/"

epgdb = pyepgdb.epgdb()
epgdb.open(epgdb_name)
e=pyepgdb.epg_record_t()
e.k.service.sat_pos=2820
e.k.service.network_id=2
e.k.service.ts_id=2061
e.k.service.service_id=8931
e.k.start_time = 1625421600
e..event_id =2189
