#!/usr/bin/python3
import sys
import os
import time
import datetime
from dateutil import tz



sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/recdb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pyrecdb
import pyepgdb
import pychdb

db= pyrecdb.recdb()
filerec= pyrecdb.file
marker =  pyrecdb.marker
rec = pyrecdb.rec

db.open('/tmp/index.mdb')
#import pyepgdb
datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x, tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S") if x<33101382645 else "?"
millisec_fn =  lambda x: f"{x/1000:.3f}" if x<922337203685477580 else "?"

def pl(lst):
    for x in lst:
        if type(x) is filerec.file:
            print(f"file {x.fileno}={x.filename}, "
                  f"str_time: start={x.k.stream_time_start} end={x.stream_time_end} "
                  f"real_time: start={datetime_fn(x.real_time_start)}, end={datetime_fn(x.real_time_end)} "
                  f"packet: start={x.stream_packetno_start}, end={x.stream_packetno_end}")
        elif type(x) is marker.marker:
            print(f"time = {x.k.time}, "
                  f"packet: start={x.packetno_start}, end={x.packetno_end}")



idxdb = pyrecdb.recdb(db)
idxdb.open_secondary("idx")
epgdb = pyepgdb.epgdb(db)
epgdb.open_secondary("epg")


txn = idxdb.rtxn()
rec_txn=db.rtxn()
epg_txn=epgdb.rtxn()

files = filerec.list_all_by_key(txn);
markers = marker.list_all_by_key(txn)


recs = rec.list_all_by_key(rec_txn)
epgs = pyepgdb.epg_record.list_all_by_key(epg_txn)

#print(recs[0])
if False:
    m=markers[0]
    t=pyrecdb.marker_key.marker_key()
    t.time = pyrecdb.milli_seconds(10000)
    q=pyrecdb.marker.find_by_key(txn, t, pyrecdb.find_type_t.find_geq)



#print(t)
#txn.abort()

if False:
    import pyepgdb
    epgdb=pyepgdb.epgdb(db)
    epgdb.open_secondary("epg")
    epgtxn = epgdb.rtxn()
    epglist = pyepgdb.epg_record.list_all_by_key(epgtxn)

subfield = pyrecdb.marker.subfield_from_name('k.time')<<24
screen=pyrecdb.marker.screen(txn, subfield)
