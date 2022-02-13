#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
import pyepgdb

def save(lst, filename):
    with open(filename, "w") as f:
        for x in lst:
            f.write(f'{str(x)}\n')


chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/")
txn=chdb.rtxn()
sat_pos = -80
frequency = 11861000
polarisation = pychdb.fe_polarisation_t.H
t2mi_pid = 0
stream_id = -1
c = pychdb.dvbs_mux.find_by_sat_freq_pol_fuzzy(txn, sat_pos, frequency, polarisation, t2mi_pid, stream_id)
