#!/usr/bin/python3
import sys
import os
import time
import csv

import neumodvb #forces setting up paths
from neumodvb.config import options, get_configfile
import pychdb
from pathlib import Path


def load_sats(txn):
    sats= get_configfile('sats.txt')
    if sats is None:
        dterror("Could not open sats.txt; cannot initialize database")
        return
    from neumodvb.neumodbutils import enum_value_for_label

    with open(sats, "r") as csv_file:
        reader = csv.reader(csv_file, delimiter='\t', quotechar='"', quoting=csv.QUOTE_MINIMAL)
        idx = 0
        for row in reader:
            if idx == 0 :
                assert row[0] == 'sat_pos'
                assert row[1] == 'band'
                assert row[2] == 'name'
                idx +=1
                continue
            sat_pos, band, name = row
            sat = pychdb.sat.sat()
            sat.sat_pos = int(sat_pos)
            sat.sat_band = enum_value_for_label(pychdb.sat_band_t, band)
            sat.name = name
            pychdb.put_record(txn, sat)

def init_db():
    db = pychdb.chdb()
    db.open(options.chdb)
    txn = db.wtxn()
    load_sats(txn)
    txn.commit()

def fix_db():
    db = pychdb.chdb()
    db.open(options.chdb)
    txn = db.wtxn()
    txn.commit()
