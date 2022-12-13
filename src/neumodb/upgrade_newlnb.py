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
import pystatdb
import pyschemadb

devdb = pydevdb.devdb()
devdb.open("/mnt/neumo/db/devdb.mdb/", allow_degraded_mode=True)
devdbtxn = devdb.rtxn()


d=pydeser.degraded_export(devdbtxn)
lnbs=[x for x in d if x['type']=='lnb']
fes=[x for x in d if x['type']=='fe']



newlnbs=[]
for lnb_ in lnbs:
    lnb = lnb_['data']
    k = lnb['k']
    newlnb=pydevdb.lnb.lnb()
    newlnb.k.dish_id = k['dish_id']
    newlnb.k.lnb_type = pydevdb.lnb_type_t(k['lnb_type'])
    newlnb.k.lnb_id = k['lnb_id']
    newlnb.k.offset_pos = lnb['offset_pos']

    for key in ["can_be_used", "enabled", "freq_mid", "lof_low", "usals_pos",
                "freq_high", "freq_low", "lof_high", "mtime", "priority"]:
        setattr(newlnb, key, type(getattr(newlnb, key))(lnb[key]))

    conn=pydevdb.lnb_connection.lnb_connection()
    for key in ["diseqc_mini", "priority", "tune_string",
                "card_no", "diseqc_10", "enabled","connection_name",
                "diseqc_11","rotor_control"]:
                setattr(conn, key, type(getattr(conn, key))(lnb[key]))
    for key in ["card_mac_address",  "rf_input"]:
                setattr(conn, key, type(getattr(conn, key))(k[key]))
    newlnb.connections.push_back(conn)
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



statdb = pystatdb.statdb()
statdb.open("/mnt/neumo/db/statdb.mdb/", allow_degraded_mode=True)
statdbtxn = statdb.rtxn()


d=pydeser.degraded_export(statdbtxn)

stats=[x for x in d if x['type']=='signal_stat']
specs=[x for x in d if x['type']=='spectrum']


newspecs=[]
for spec_ in specs:
    spec = spec_['data']
    k = spec['k']
    newspec=pystatdb.spectrum.spectrum()
    for key in ["pol", "sat_pos", "start_time"]:
        setattr(newspec.k, key, type(getattr(newspec.k, key))(k[key]))
    for key in ["card_mac_address", "rf_input"]:
        setattr(newspec.k.rf_path, key, type(getattr(newspec.k.rf_path, key))(k['lnb_key'][key]))

    for key in ["dish_id", "lnb_id", "lnb_type"]:
        setattr(newspec.k.rf_path.lnb, key, type(getattr(newspec.k.rf_path.lnb, key))(k['lnb_key'][key]))
    newspec.k.rf_path.lnb.offset_pos =0



    for key in ["adapter_no", "end_freq", "is_complete",
                "start_freq",  "filename", "resolution",  "usals_pos" ]:
        setattr(newspec, key, type(getattr(newspec, key))(spec[key]))

    for o in spec["lof_offsets"]:
       newspec.lof_offsets.push_back(o)

    newspecs.append(newspec)


newstats=[]
for stat_ in stats:
    stat = stat_['data']
    k = stat['k']
    newstat=pystatdb.signal_stat.signal_stat()

    for key in ["locktime_ms", "symbol_rate"]:
        setattr(newstat, key, type(getattr(newstat, key))(stat[key]))

    for key in ['frequency', 'live',  'pol', 'time']:
        setattr(newstat.k, key, type(getattr(newstat.k, key))(k[key]))
    newstat.k.mux = type(newstat.k.mux)(**k['mux'])

    for key in ['card_mac_address','rf_input']:
        setattr(newstat.k.rf_path, key, type(getattr(newstat.k.rf_path, key))(k['lnb'][key]))

    for key in ['dish_id', 'lnb_id', 'lnb_type']:
        setattr(newstat.k.rf_path.lnb, key, type(getattr(newstat.k.rf_path.lnb, key))(k['lnb'][key]))

    for s in stat['stats']:
        ss = pystatdb.signal_stat_entry.signal_stat_entry(**s)
        newstat.stats.push_back(ss)
    newstats.append(newstat)






if True:
    devdb = pydevdb.devdb()
    devdb.open("/tmp/devdb.mdb/", allow_degraded_mode=False)
    txn=devdb.wtxn()
    for lnb in newlnbs:
        pydevdb.put_record(txn, lnb)
    txn.commit()


if True:
    statdb = pystatdb.statdb()
    statdb.open("/tmp/statdb.mdb/", allow_degraded_mode=False)
    txn=statdb.wtxn()
    for stat in newstats:
        pystatdb.put_record(txn, stat)
    for spec in newspecs:
        pystatdb.put_record(txn, spec)
    txn.commit()
