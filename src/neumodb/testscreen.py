#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
import pyepgdb

def save(lst, filename):
    with open(filename, "w") as f:
        for x in lst:
            f.write(f'{str(x)}\n')


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


if False:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    subfield = pychdb.service.subfield_from_name('ch_order')<<24
    print(subfield)
    use_index = False
    txn=db.rtxn()
    match_data = pychdb.service.service()
    match_data.media_mode = pychdb.media_mode_t.TV
    match_data.name ='bbc'
    m1 = pychdb.field_matcher.field_matcher(pychdb.service.subfield_from_name('media_mode'),
                                            pychdb.field_matcher.match_type.EQ)
    m2 = pychdb.field_matcher.field_matcher(pychdb.service.subfield_from_name('name'),
                                           pychdb.field_matcher.match_type.STARTSWITH)
    matchers = pychdb.field_matcher_t_vector()
    matchers.push_back(m1)
    matchers.push_back(m2)
    screen=pychdb.service.screen(txn, subfield,
                                 field_matchers=matchers, match_data=match_data)
    #screen=pychdb.service.screen(txn, subfield)
    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        print(f"{str(ll.media_mode).removeprefix('media_mode_t.')}: {ll.name}")



db = pychdb.chdb()
db.open("/mnt/neumo/db/chdb.mdb/")
txn=db.rtxn()

chg = None
chgm = None
sort_order = pychdb.chgm.subfield_from_name('oldchannel_id')<<24
screen = pychdb.chgm.screen(txn, sort_order=sort_order)

r=screen.record_at_row(10)
data=[ screen.record_at_row(rowno) for rowno in range(screen.list_size)]
txn.abort()
txn = db.wtxn()
for idx, d  in enumerate(data):
    d.service = d.k.oldservice
    d.k.channel_id = d.oldchannel_id
    data[idx] =d
    #pychdb.put_record(txn, d)
    if d.oldchannel_id != d.k.channel_id:
        print('BAD')
    if d.service.service_id != d.k.oldservice.service_id:
        print('BAD')

    #print(f'{d.oldchannel_id} {d.k.channel_id}')
    #print(f'{d.k.service.service_id} {d.oldservice.service_id}')
    #print(f'{d.k.oldservice} {d.service}')
txn.commit()

"""
1. init(self: pychdb.service.service_screen, db_txn: pychdb.db_txn, ref: pychdb.service.service = None, num_record: int = -1, offset: int = 0) -> None
2. init(self: pychdb.service.service_screen, db_txn: pychdb.db_txn, pos_top: int, num_records: int) -> None
"""
