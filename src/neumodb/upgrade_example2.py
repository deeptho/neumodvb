#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/neumodb/schema')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pydeser
import pychdb
import pyschemadb
chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/", allow_degraded_mode=True)
txn=chdb.rtxn()

d=pydeser.degraded_export(txn)
lnbs=[x for x in d if x['type']=='lnb']



if False:
    sort_order = pychdb.service.subfield_from_name('k.service_id')<<24
    screen=pychdb.lnb.screen(txn, sort_order=sort_order)

    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        print(ll)

def get_mac_address_dict(txn):
    sort_order = pychdb.fe.subfield_from_name('k.adapter_mac_address')<<24
    screen=pychdb.fe.screen(txn, sort_order=sort_order)
    ret={}
    ret1={}
    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        ret[ll.k.adapter_mac_address] = ll.card_mac_address
        ret1[ll.k.adapter_mac_address] = ll.adapter_no
    return ret, ret1
mac_address_dict, rf_in_dict = get_mac_address_dict(txn)


newlnbs=[]
for lnb_ in lnbs:
    lnb = lnb_['data']
    k = lnb['k']
    newlnb=pychdb.lnb.lnb()
    newlnb.k.dish_id = k['dish_id']
    newlnb.k.lnb_type = pychdb.lnb_type_t(k['lnb_type'])
    newlnb.k.lnb_id = k['lnb_id']
    newlnb.k.rf_input = rf_in_dict[k['adapter_mac_address']]
    newlnb.k.card_mac_address = mac_address_dict[k['adapter_mac_address']]
    for key in [ "usals_pos", "enabled", "priority", "lof_low","lof_high", "freq_low","freq_mid",
                 "freq_high", "offset_pos", "diseqc_mini", "diseqc_10", "diseqc_11", "mtime",
                 "can_be_used", "tune_string", "name" ]:
        setattr(newlnb, key, lnb[key])
    for key in [ "pol_type", "rotor_control"]:
        setattr(newlnb, key, type(getattr(newlnb, key))(lnb[key]))
    for network in  lnb["networks"]:
        n = pychdb.lnb_network.lnb_network()
        for kk,v in network.items():
            if kk == 'ref_mux':
                r = network['ref_mux']
                n.ref_mux.extra_id, n.ref_mux.network_id, n.ref_mux.sat_pos, \
                     n.ref_mux.t2mi_pid, n.ref_mux.ts_id = \
                         r['extra_id'], r['network_id'], r['sat_pos'], r['t2mi_pid'], r['ts_id']
            else:
                setattr(n, kk, type(getattr(n, kk))(network[kk]))
        newlnb.networks.push_back(n)
    for o in lnb['lof_offsets']:
        newlnb.lof_offsets.push_back(o)
    newlnbs.append(newlnb)

txn.commit()

chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/", allow_degraded_mode=False)
txn=chdb.wtxn()
for lnb in newlnbs:
    pychdb.put_record(txn, lnb)
txn.commit()
