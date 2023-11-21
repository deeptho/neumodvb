#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/devdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/neumodb/schema')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pydeser
import pychdb
import pydevdb
import pyschemadb

chdb = pychdb.chdb()
#in the line below, we open chdb instead of devdb as that is were the data we want was stored in the past
chdb.open("/tmp/chdb.mdb/", allow_degraded_mode=True)
chdbtxn = chdb.rtxn()

d=pydeser.degraded_export(chdbtxn)
lnbs=[x for x in d if x['type']=='lnb']
fes=[x for x in d if x['type']=='fe']
usals=[x for x in d if x['type']=='usals_location']


if False:
    txn=devdb.rtxn()
    sort_order = pydevdb.lnb.subfield_from_name('k.card_mac_address')<<24
    screen=pydevdb.lnb.screen(txn, sort_order=sort_order)

    xxx = []
    for idx in range(screen.list_size):
        xxx.append(screen.record_at_row(idx))

    print([x.usals_pos for x in xxx])

def get_mac_address_dict(txn):
    ret={}
    ret1={}
    for fe in fes:
        ret [fe['data']['k']['adapter_mac_address']] = fe['data']['card_mac_address']
        ret1[fe['data']['k']['adapter_mac_address']] = fe['data']['adapter_no']
    return ret, ret1
mac_address_dict, rf_in_dict = get_mac_address_dict(chdbtxn)


newlnbs=[]
for lnb_ in lnbs:
    lnb = lnb_['data']
    k = lnb['k']
    newlnb=pydevdb.lnb.lnb()
    newlnb.k.dish_id = k['dish_id']
    newlnb.k.lnb_type = pydevdb.lnb_type_t(k['lnb_type'])
    newlnb.k.lnb_id = k['lnb_id']
    newlnb.k.rf_input = rf_in_dict.get(k['adapter_mac_address'], 0)
    card_mac_address = mac_address_dict.get(k['adapter_mac_address'], None)
    if card_mac_address is None:
        print(f"bad lnb: {k['adapter_mac_address']}")
        continue
    newlnb.k.card_mac_address = card_mac_address
    for key in [ "usals_pos", "enabled", "priority", "lof_low","lof_high", "freq_low","freq_mid",
                 "freq_high", "offset_pos", "diseqc_mini", "diseqc_10", "diseqc_11", "mtime",
                 "can_be_used", "tune_string"]:
        setattr(newlnb, key, lnb[key])
    for key in [ "pol_type", "rotor_control"]:
        setattr(newlnb, key, type(getattr(newlnb, key))(lnb[key]))
    for network in  lnb["networks"]:
        n = pydevdb.lnb_network.lnb_network()
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

newfes=[]
for fe_ in fes:
    fe = fe_['data']
    k = fe['k']
    newfe=pydevdb.fe.fe()
    newfe.k.adapter_mac_address = k['adapter_mac_address']
    newfe.k.frontend_no = k['frontend_no']
    for key in [ "rf_in", "adapter_no", "supports_neumo","present", "can_be_used",
                 "priority", "mtime", "frequency_min",
                 "frequency_max", "symbol_rate_max", "symbol_rate_min", "card_mac_address" ,
                 "card_name", "card_short_name", "adapter_name", "card_address"]:
        setattr(newfe, key, fe[key])
    for delsys in  fe["delsys"]:
        newfe.delsys.push_back(pychdb.fe_delsys_t(delsys))
    for rf_input in  fe["rf_inputs"]:
        newfe.rf_inputs.push_back(rf_input)

    for kk,v in fe['supports'].items():
        setattr(newfe.supports, kk, v)
    newfes.append(newfe)

chdbtxn.abort()

if False:
    devdb = pydevdb.devdb()
    devdb.open("/mnt/neumo/db/devdb.mdb/", allow_degraded_mode=False)
    txn=devdb.wtxn()
    for lnb in newlnbs:
        pydevdb.put_record(txn, lnb)
    for fe in newfes:
        pydevdb.put_record(txn, fe)
    txn.commit()
