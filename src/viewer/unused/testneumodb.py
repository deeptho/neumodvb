#!/usr/bin/python3
# Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
import pyneumodb
import pyepgdb
import inspect
import functools

db=pyneumodb.neumodb()

def is_pybind11_enum(field):
     return hasattr(field, '__entries') and hasattr(field,'__module__')

def pybind11_subfield(obj, dotkey):
    keys = dotkey.split('.')
    return functools.reduce(getattr, keys, obj)

def is_pybind11_class(field):
     return hasattr(field,'__module__')  and not hasattr(field, '__entries')

def pybind11_get_dotkeys(cls, prefix=[]):
    """
    For an object hierarchy, get a list of keys and subkeys separated by dots.
    Can be called with a class or an object
    prefix: should not be used
    """
    dummy = cls() if inspect.isclass(cls) else cls
    keys= iter( k for k in dir(dummy) if not k.startswith('_'))
    dotkeys = []
    for k in keys:
        field = getattr(dummy, k)
        if is_pybind11_class(field):
            dotkeys += pybind11_get_dotkeys(type(field), prefix + [k])
        else:
            dotkeys.append('.'.join(prefix + [k]))
    return dotkeys



if True:
    db.open("/mnt/scratch/neumo/chdb.lmdb/")
    txn=db.rtxn()
    q=pychdb.service.list_all_by_key(txn)
    m = q[12]

    #q=pychdb.service.list_all_by_name(txn)
    #TODO: if pychdb.list_all_by_key(txn) is not saved to variable: double free detected

    del txn
if False:
    db.open("/mnt/scratch/neumo/epgdb.lmdb/")
    txn=db.rtxn()
    q1=pyepgdb.epg_record.list_all_by_key(txn)
    del txn
