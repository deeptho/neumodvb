#!/usr/bin/python3
# Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
import inspect
import functools


def is_enum(field):
     return hasattr(field, '_member_names_') and hasattr(field,'__module__')

def enum_to_str(val):
     return str(val).split('.')[-1]

def enum_labels(field):
     ret = []
     for k in getattr(field, '_member_names_'):
          ret.append(k.replace('_', ' '))
     return ret

def enum_values_and_labels(field):
     from collections import OrderedDict
     ret = OrderedDict()
     for k in getattr(field, '_member_names_'):
          ret[getattr(field,k)] = k.replace('_', ' ')
     return ret

def enum_value_for_label(enum_t, label):
     ret = getattr(enum_t, label, None)
     if ret is not None:
          return ret
     l = label.replace(' ', '_')
     ret = getattr(enum_t, l, None)
     if ret is not None:
          return ret
     else:
          return None

def get_subfield(obj, dotkey):
     keys = dotkey.split('.')
     try:
          ret = functools.reduce(getattr, keys, obj)
     except ValueError:
          ret = ""
     return ret

def enum_set_subfield(obj, dotkey, val):
     if type(dotkey) is not list:
          keys = dotkey.split('.')
     else:
          keys = dotkey
     if len(keys)>1:
          for key in keys[:-1]:
               obj = getattr(obj, key)
     try:
          setattr(obj, keys[-1], val)
     except:
          from neumodvb.util import dtdebug
          dtdebug(f'Cannot set field {dotkey} to value {val}')

def is_class(field):
     return hasattr(field,'__module__')  and not hasattr(field, '_member_names_')

def get_dotkeys(cls, prefix=[]):
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
          if is_class(field):
               dotkeys += get_dotkeys(type(field), prefix + [k])
          else:
               dotkeys.append('.'.join(prefix + [k]))
     return dotkeys
