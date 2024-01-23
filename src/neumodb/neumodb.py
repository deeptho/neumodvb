#!/usr/bin/python3
# Neumo dvb (C) 2019-2024 deeptho@gmail.com
# Copyright notice:
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
import os
import sys
import argparse
from collections import OrderedDict
from pprint import pformat
from jinja2 import Template, Environment, FileSystemLoader, PackageLoader, select_autoescape
import regex as re
from inspect import getsourcefile
from generators import *
from importlib import import_module

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


if True:
    scriptdir=get_scriptdir()


epilog="""
"""

def make_arg_parser():
    parser = argparse.ArgumentParser(description='Neumodb file generator',
                                     epilog=epilog, formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument("--db", action='store',
                        default='chdb',
                        help="name of database")
    parser.add_argument("--output-dir", action='store',
                        default=os.path.join(scriptdir, 'templates'),
                        help="location of template files")
    return parser

parser = make_arg_parser()
args = parser.parse_args()
options = vars(args)



def ensure_dir(f):
    d = os.path.dirname(f)
    if not os.path.exists(d):
        os.makedirs(d)
#ensure_dir("generated")

#TODO: the ordering of the calls below is important (db.save_db()) has side effects). This should be fixed
def generate(db, output_dir='/tmp'):
    db.check_structs()
    db.prepare()
    db.save_db()
    db.save_enums()
    db.save_structs()


dbdef=import_module('{}.{}'.format(options['db'], 'dbdefs'))
if False:
    dbdef.db.check_structs()
    dbdef.db.prepare()
    s= dbdef.db.all_structs['mux_t']
    #s= dbdef.db.all_structs['chgm_t']
    #s= dbdef.db.all_structs['mux_t']
    field_names = [f['name'] for f in s.fields]
    key_fields = [[f['name'] for f in key['fields']] for key in s.keys]
    key_index_names = [key['index_name'] for key in s.keys]
    variants =s.keys[0]['variants']
    micro_fields = [f['name'] for f in variants[-1]['fields'] ]
    os.path.commonprefix(micro_fields[:-1])
    [os.path.commonprefix(micro_fields[:i+1]) for i in range(len(micro_fields))]
    #print([ (key['index_name'], [v['name'] for v in key['variants']]) for key in s.keys])
    print (
        [ [(vv['prefix_name'],[f['name'] for f in vv['fields']])
           for vv in key['key_prefixes']] for key in s.keys]
        )
elif False:
    dbdef.db.check_structs()
    dbdef.db.prepare()
    t = dbdef.db.all_structs['rec_t']
    k=t.keys[0]

else:
    generate(dbdef.db)

#for testing
#from testchdb.dbdefs import db


if __name__ == '__main__':
    try:
        if sys.ps1:
            interpreter = True
    except AttributeError:
        interpreter = False
        if sys.flags.interactive:
            interpreter = True
    if not interpreter:
        import signal
        signal.signal(signal.SIGINT, signal.SIG_DFL)
    parser = make_arg_parser()
    args = parser.parse_args()
    options = vars(args)
