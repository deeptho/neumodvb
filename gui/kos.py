#!/usr/bin/python3
import sys
import os
import time
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pyneumodb
import pychdb


import urllib.request
import configparser
Mux=pychdb.mux.mux


url='https://en.kingofsat.net/dl.php?pos=26E&fkhz=0'


pol_dict=dict(H=pychdb.fe_polarisation_t.H,
              V=pychdb.fe_polarisation_t.V,
              L=pychdb.fe_polarisation_t.L,
              R=pychdb.fe_polarisation_t.R
)

delsys_dict = { 'DVB-S' : pychdb.fe_delivery_system_t.DVBS,
                'S2' : pychdb.fe_delivery_system_t.DVBS2 }

mod_dict = { 'QPSK' : pychdb.fe_modulation_t.QPSK,
             '8PSK' : pychdb.fe_modulation_t.PSK_8,
             '8PSK ACM/VCM' : pychdb.fe_modulation_t.PSK_8,
             '16APSK' : pychdb.fe_modulation_t.APSK_16}

#TO CHECK: generate id for mux?
def parse_tp(freq, pol, symrate, fec, standard, modulation, include_c_band=False):
    m =Mux()
    m.frequency = int(freq)
    if not include_c_band and m.frequency < 10000000:
        return None
    print(f"{freq} {pol} {symrate} {fec} {standard} {modulation}")
    m.symbol_rate =int(symrate)*1000
    m.polarisation = pol_dict[pol]
    m.delivery_system = delsys_dict[standard]
    m.modulation = mod_dict[modulation]
    return m

def get_init(pos, db):
    now =int (time.time())
    posstr = f"{pos/10.:0.1f}E" if pos>=0 else f"{-pos/10.:0.1f}W"
    url = f'https://en.kingofsat.net/dl.php?pos={posstr}&fkhz=1'
    response = urllib.request.urlopen(url)
    data = response.read()      # a `bytes` object
    text = data.decode('utf-8') # a `str`; this step can't be used if data is binary
    config = configparser.ConfigParser()
    config.read_string(text)
    tst = int(config['SATTYPE']['1'])
    name = config['SATTYPE']['2']
    it = iter(config['DVB'].items())
    next(it) # skip line which has transponder count

    txn = db.wtxn()
    for k,v in it:
        args =  v.split(',')
        mux = parse_tp(*args)
        if mux is None:
            continue
        mux.k.sat_pos = pos
        mux.mtime = now
        pychdb.mux.make_unmake_unique_if_new(txn, mux)
        pychdb.put_record(txn, mux)
    #txn.commit()

if True:
    db = pychdb.chdb()
    db.open("/mnt/neumo/db/chdb.mdb/")
    get_init(-50, db)
