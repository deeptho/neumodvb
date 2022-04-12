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


#t=pyrecdb.marker_key.marker_key()
#print(t)

#db.open('/mnt/neumo/live/A00_ts02035_sid06201_20200523_03:13:29/index.mdb')
#db.open('/mnt/neumo/live/A02_ts00006_sid01537_20201018_11:48:44/index.mdb')
#db.open('/mnt/neumo/live/A02_ts00004_sid08588_20201105_22:16:28')
#db.open('/mnt/neumo/live/A02_ts00004_sid08588_20201105_23:59:01')
#db.open('/mnt/neumo/live/A02_ts00004_sid08588_20201106_00:04:22/index.mdb')
#db.open("/mnt/neumo/recordings/Domenica In - Rai 1 HD - 2020-11-08 14:00/index.mdb")
#db.open('/mnt/neumo/live/A02_ts00004_sid08588_20201108_17:14:35/index.mdb')
#db.open("/mnt/neumo/live/A00_ts02024_sid05109_20201113_23:53:31/index.mdb/")
#db.open("/tmp/mpm.mdb/")
#db.open("/mnt/neumo/recordings/Domenica In - Rai 1 HD - 2020-11-15 14:00/index.mdb/")
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
