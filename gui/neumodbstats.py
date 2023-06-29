#!/usr/bin/python3
import sys
import os
import argparse

#the following import also sets up import path
import neumodvb

import pychdb
import pyrecdb
import pydevdb
import pystatdb
import pyepgdb

epilog= """
"""

parser = argparse.ArgumentParser(description='Neumodb file generator',
                                     epilog=epilog, formatter_class=argparse.RawDescriptionHelpFormatter)

choices = ('all', 'chdb', 'recdb', 'devdb', 'epgdb', 'statdb', 'idx')
parser.add_argument("--db", action='store',
                    default='chdb',
                    choices = choices,
                    help="name of database")

parser.add_argument("--path", action='store',
                    default='/mnt/neumo/db/',
                    help="location database")


args = parser.parse_args()


def stats(path, db):
    if db == 'chdb':
        db = pychdb.chdb(readonly=True)
    elif db == 'recdb':
        db = pyrecdb.recdb(readonly=True)
    elif args.db == 'devdb':
        db = pydevdb.devdb(readonly=True)
    elif db == 'statdb':
        db = pystatdb.statdb(readonly=True)
    elif db == 'epgdb':
        db = pyepgdb.epgdb(readonly=True)
    else:
        return
    print('============')
    print(f'statistics for {path}\n')
    db.open(path)
    db.stats()

def idxstats(path):
    db = pyrecdb.recdb(readonly=True)
    db.open(path)
    print('============')
    print(f'statistics for rec:{path}\n')
    db.stats()
    epgdb = pyepgdb.epgdb(db)
    epgdb.open_secondary("epg")
    print(f'statistics for epg:{path}\n')
    epgdb.stats()
    idxdb = pyrecdb.recdb(db)
    idxdb.open_secondary("idx")
    print(f'statistics for idx:{path}\n')
    idxdb.stats()

print("\n")
if args.db == 'all':
    for choice in  ('chdb', 'recdb', 'devdb', 'epgdb', 'statdb'):
        path = os.path.join(args.path, f'{choice}.mdb')
        if not os.path.exists(path):
            path= args.path
        stats(path, choice)
elif args.db == 'idx':
    path = os.path.join(args.path, f'index.mdb')
    if not os.path.exists(path):
        path= args.path
        idxstats(path)
else:
    path = os.path.join(args.path, f'{args.db}.mdb')
    if not os.path.exists(path):
        path= args.path
    stats(path, args.db)
