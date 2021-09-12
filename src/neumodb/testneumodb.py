#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
import pyepgdb

def save(lst, filename):
    with open(filename, "w") as f:
        for x in lst:
            f.write(f'{str(x)}\n')


if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    if False:
        txn=db.wtxn()

        l1= pychdb.lnb.lnb()
        l1.k.sat_pos=192
        l1.diseqc_10=2

        l2= pychdb.lnb.lnb()
        l2.k.sat_pos=235
        l2.diseqc_10=1


        l3= pychdb.lnb.lnb()
        l3.k.sat_pos=282
        l3.diseqc_10=0
        pychdb.put_record(txn,l1)
        pychdb.put_record(txn,l2)

        pychdb.put_record(txn,l3)
    else:
        txn=db.rtxn()

    #q1 = pychdb.mux.list_distinct_sats(txn)
    #print( [ll for ll in q1])
    #q=pychdb.service.list_all_by_service_key(txn)
    q=pychdb.lnb.list_all_by_key(txn)
    z=[ll.k.sat_pos for ll in q]
    print (f"LEN={len(q)}")
    print( [ll for ll in q])
    txn.commit()
    #del txn
if False:
    chdb = pychdb.chdb()
    chdb.open("/mnt/neumo/db/chdb.mdb/")
    txn=chdb.rtxn()
    service = pychdb.service.find_by_ch_order(txn,702) #film1 family
    #q1 = pychdb.mux.list_distinct_sats(txn)
    #print( [ll for ll in q1])
    #q=pychdb.service.list_all_by_service_key(txn)
    #q1=pychdb.service.list_all_by_name(txn)
    #print( [ll for ll in q1])
    #TODO: if pychdb.list_all_by_key(txn) is not saved to variable: double free detected
    #z=pychdb.service.list_all_by_ch_order(txn)
    #print ([(zz.k.mux.sat_pos, zz.k.mux.ts_id, zz.k.service_id) for zz in z])
    del txn
    db = pyepgdb.epgdb()
    db.open("/mnt/neumo/db/epgdb.mdb/")
    txn=db.rtxn()
    all=pyepgdb.epg_record.list_all_by_key(txn)
    matches=[a for a in all if a.k.service.service_id == service.k.service_id]

    #import pyrecdb
    #xx=pyrecdb.make_filename(service, epg)
    #print(f"fname={xx}")

if False: #does not work
    db = pyepgdb.epgdb()
    db.open("/mnt/neumo/db/epgdb.mdb/")
    txn=db.rtxn()
    q1=pyepgdb.epg_record.list_all_by_key(txn)
    q2=pyepgdb.epg_record.list(txn, order=pyepgdb.epg_record.epg_record_order.key,
                               field_filter = 0,
                               num_records= -1,
                               offset = 0)

    del txn

if False: #works
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    filter_service = pychdb.service.service()
    filter_service.ch_order = 0
    q=pychdb.service.list(txn,
                          #order=pychdb.service.service_order.ch_order, #ok
                          #order=pychdb.service.service_order.service_key,
                          #order=pychdb.service.service_order.mux_service_id, #same as service_key
                          order=pychdb.service.service_order.name,
                          #key_prefix= pychdb.service.service_prefix.ch_order,
                          #ref= filter_service,
                          field_filter = 0,
                          #ref_filter = filter_service,
                          num_records= -1,
                          offset = 0)

    ll= len([qq for qq in q if qq.k.mux.sat_pos==282 and qq.k.mux.network_id==2 and qq.k.mux.ts_id==2094 and qq.k.mux.extra_id==65517])
    # ll = 27

if False: #works
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    filter_service = pychdb.service.service()
    #all channels on mux
    filter_service.k.mux.sat_pos=282
    filter_service.k.mux.network_id=2
    filter_service.k.mux.ts_id=2094
    filter_service.k.mux.extra_id=65517

    q=pychdb.service.list(txn,
                          #order=pychdb.service.service_order.ch_order, #ok
                          #order=pychdb.service.service_order.service_key,
                          order=pychdb.service.service_order.key,
                          #order=pychdb.service.service_order.name,
                          key_prefix= pychdb.service.service_prefix.mux,
                          ref= filter_service,
                          field_filter = 0,
                          #ref_filter = filter_service,
                          num_records= -1,
                          offset = 0)

if False: #works
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    v=pychdb.service.list_all_by_name(txn)
    print ('xxxxxxxxxxxxxxxx')
    v=pychdb.service.list_all_by_name(txn)


if True:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.service.service_screen(sort_order=pychdb.service.service_order.ch_order,
                                     key_prefix_type=pychdb.service.service_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, num_records=10, offset=2)
    screen.init(txn, num_records=10, pos_top=1)
    print( [ll.name for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.service.service_screen(sort_order=pychdb.service.service_order.name,
                                     key_prefix_type=pychdb.service.service_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, num_records=10, offset=2)
    screen.init(txn, num_records=10, pos_top=110)
    print( [ll.name for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.mux.mux_screen(sort_order=pychdb.mux.mux_order.sat_freq_pol,
                                 key_prefix_type=pychdb.mux.mux_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, num_records=10, offset=2)
    screen.init(txn, num_records=10, pos_top=0)
    print( [ll.frequency for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.dvbc_mux.dvbc_mux_screen(sort_order=pychdb.dvbc_mux.dvbc_mux_order.freq,
                                 key_prefix_type=pychdb.dvbc_mux.dvbc_mux_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, num_records=10, offset=2)
    screen.init(txn, num_records=10, pos_top=0)
    print( [ll.frequency for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.dvbs_mux.dvbs_mux_screen(sort_order=pychdb.dvbs_mux.dvbs_mux_order.sat_freq_pol,
                                 key_prefix_type=pychdb.dvbs_mux.dvbs_mux_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, num_records=10, offset=2)
    screen.init(txn, num_records=10, pos_top=0)
    print( [ll.frequency for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    screen=pychdb.mux.mux_screen(sort_order=pychdb.mux.mux_order.sat_freq_pol,
                                 key_prefix_type=pychdb.mux.mux_prefix.none, key_prefix_data=None)
    #screen.init(txn, ref = None, offset=2, num_records=10)
    screen.init(txn, num_records=10, pos_top=0)
    print( [ll.frequency for ll in screen.records])

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn=db.rtxn()
    ref=pychdb.mux.find_by_sat_freq_pol(txn, 192, 12402000, pychdb.fe_polarisation_t.V)
    screen=pychdb.mux.mux_screen(sort_order=pychdb.mux.mux_order.sat_freq_pol,
                                 key_prefix_type=pychdb.mux.mux_prefix.sat_pos, key_prefix_data=ref)
    screen.init(txn, num_records=-1, pos_top=0)
    print( [ll for ll in screen.records])
    p=screen.records[0]
    q=screen.records[1]

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn = db.wtxn()

    all_from_mux = list(set([ l.k.sat_pos for l in pychdb.mux.list_all_by_key(txn)]))
    for pos in all_from_mux:
        sat = pychdb.sat.sat()
        sat.sat_pos = pos
        sat.name = f'Satellite at {str(sat)}'
        pychdb.put_record(txn, sat)
    txn.commit()

if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    txn = db.wtxn()
    lst = pychdb.dvbs_mux.list_all_by_key(txn)
    for mux in lst:
        if mux.stream_id != -1:
            print(f'{mux.k.sat_pos} {mux.frequency} {mux.pol} {mux.stream_id} {mux.stream_id}\n')
            #pychdb.delete_record(txn, mux)
            #mux.k.stream_id = mux.stream_id
            #pychdb.put_record(txn, mux)
    txn.commit()

if False:
    db = pyepgdb.epgdb()
    db.open("/mnt/neumo/db/epgdb.mdb/")
    txn=db.rtxn()

    screen =pyepgdb.epg_record.epg_record_screen(sort_order=pyepgdb.epg_record.epg_record_order.key,
                                                 key_prefix_type=pyepgdb.epg_record.epg_record_prefix.none, key_prefix_data=None)
    q=pyepgdb.epg_record.list_all_by_key(txn)
    screen.init(txn, num_records=-1, pos_top=0)
    print(f"LEN: {len(q)} - {len(screen.records)}")
    q=screen.records[1003]
if False:
    start_time =  0
    db = pyepgdb.epgdb()
    db.open("/mnt/neumo/db/epgdb.mdb/")
    txn=db.rtxn()
    q=pyepgdb.epg_record.list_all_by_key(txn)
    service_key = pychdb.service_key.service_key()
    service_key.mux.sat_pos=282
    service_key.mux.network_id=2
    service_key.mux.ts_id=2050
    service_key.service_id=6941
    #q1 = pyepgdb.list_for_service(txn, service_key, start_time)
    ref=pyepgdb.epg_record.epg_record()
    ref.k.service=pyepgdb.epg_service.epg_service(sat_pos=282, network_id=2, ts_id=2050, service_id=6941)
    ref.k.start_time =  start_time
    screen =pyepgdb.epg_record.epg_record_screen(sort_order=pyepgdb.epg_record.epg_record_order.key,
                                                 key_prefix_type=pyepgdb.epg_record.epg_record_prefix.service,
                                                 key_prefix_data=ref, lower_limit=ref)
    screen.init(txn, num_records=-1, pos_top=0)
    save([qq for qq in q if qq.k.service.service_id==7030], "/tmp/q.txt")
    save(screen.records, "/tmp/screen.txt")
    print (f"len(q)={len(q)} len(screen.records)={len(screen.records)}")
    #save(q1, "/tmp/q1.txt")

if False:
    db = pyepgdb.epgdb()
    db.open("/mnt/neumo/db/epgdb.mdb/")
    txn=db.wtxn()
    q=pyepgdb.clean(txn, 1576339800)
    txn.commit()

"""
1. init(self: pychdb.service.service_screen, db_txn: pychdb.db_txn, ref: pychdb.service.service = None, num_record: int = -1, offset: int = 0) -> None
2. init(self: pychdb.service.service_screen, db_txn: pychdb.db_txn, pos_top: int, num_records: int) -> None
"""
