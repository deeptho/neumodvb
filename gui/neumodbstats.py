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

choices = ('all', 'chdb', 'recdb', 'devdb', 'epgdb', 'statdb')
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
        db = pychdb.chdb()
    elif db == 'recdb':
        db = pyrecdb.recdb()
    elif args.db == 'devdb':
        db = pydevdb.devdb()
    elif db == 'statdb':
        db = pystatdb.statdb()
    elif db == 'epgdb':
        db = pyepgdb.epgdb()
    else:
        return
    print('============')
    print(f'statistics for {path}\n')
    db.open(path)
    db.stats()

print("\n")
if args.db == 'all':
    for choice in choices[1:]:
        path = os.path.join(args.path, f'{choice}.mdb')
        stats(path, choice)
else:
    path = os.path.join(args.path, f'{args.db}.mdb')
    stats(path, args.db)
