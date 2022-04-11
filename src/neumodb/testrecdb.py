#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/recdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pyrecdb
import pychdb
import pyepgdb

if True:
    recdb = pyrecdb.recdb()
    recdb.open("/mnt/neumo/db/recdb.mdb/")
    #recdb.open("/tmp/recdb.mdb/")
    if False:
        epgdb = pyepgdb.epgdb(recdb)
        epgdb.open_secondary("epg")
    #epgdb=pyepgdb.epgdb(recdb)
    #epgdb.open_secondary("epg")
    txn=recdb.rtxn()
    q1=pyrecdb.rec.list_all_by_key(txn)
    #epgtxn=epgdb.rtxn()
    #q2=pyepgdb.epg_record.list_all_by_key(epgtxn)
    for rec in q1:
        if rec.epg.k.anonymous:
            print(f"================={rec.epg.k.event_id}")
            print(rec)
            print(f'key={rec.epg}')
            print(f'status={rec.epg.rec_status}')
    if False:
        print("Files in this recording\n")
        q2=pyrecdb.live_service.list_all_by_key(txn)
        for qq in q2:
            print (f'     {qq.dirname} {qq.service}')
