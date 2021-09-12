#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb


bbc1 = pychdb.service()

def bbc1mux():
    m = pychdb.mux()
    m.k.sat_pos = 282
    m.k.network_id = 2
    m.k.ts_id = 2050
    m.k.extra_id = 0
    m.frequency =10847000
    m.polarisation = pychdb.fe_polarisation_t.V
    m.symbol_rate = 23000000
    m.modulation = pychdb.fe_modulation_t.PSK_8
    m.HP_code_rate = pychdb.fe_code_rate_t.FEC_AUTO
    m.LP_code_rate = pychdb.fe_code_rate_t.FEC_AUTO
    m.pls_code = 1
    m.pls_mode = pychdb.fe_pls_mode_t.ROOT
    m.stream_id = -1
    m.delivery_system =  pychdb.fe_delivery_system_t.SYS_DVBS2
    return m

m=bbc1mux()


def bbc1service(m):
    s = pychdb.service()
    s.k.mux.sat_pos = m.k.sat_pos
    s.k.mux.network_id = m.k.network_id
    s.k.mux.ts_id = m.k.ts_id
    s.k.mux.extra_id = m.k.extra_id
    s.k.service_id = 6941
    s.ch_order = 101
    s.name  = 'BBC ONE HD'
    return s

s=bbc1service(m)

import pyrecdb
r=pyrecdb.rec()
