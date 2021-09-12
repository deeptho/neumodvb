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

import configparser
import os
import sys
import pathlib
from pathlib import Path
import re
from datetime import timedelta
from configobj import ConfigObj

import neumodvb
from neumodvb.util import is_installed, maindir, dtdebug, dterror
from pyreceiver import options_t, set_logconfig

regex = re.compile(r'^((?P<days>[\.\d]+?)d)?((?P<hours>[\.\d]+?)h)?((?P<minutes>[\.\d]+?)m)?((?P<seconds>[\.\d]+?)s)?$')

def parse_time(time_str):
    """
    Parse a time string e.g. (2h13m) into a timedelta object.

    Modified from virhilo's answer at https://stackoverflow.com/a/4628148/851699

    :param time_str: A string identifying a duration.  (eg. 2h13m)
    :return datetime.timedelta: A datetime.timedelta object
    """
    parts = regex.match(time_str)
    assert parts is not None, "Could not parse any time information from '{}'.  Examples of valid strings: '8h', '2d8h5m20s', '2m4s'".format(time_str)
    time_params = {name: float(param) for name, param in parts.groupdict().items() if param}
    return timedelta(**time_params)


def get_configfile(file):
    src_dir='/home/philips/neumodvb'
    dirs = ['~/.config/neumodvb', '/etc/neumodvb',  f'{src_dir}/config', f'{src_dir}/config']
    filenames = [ os.path.expanduser(f'{d}/{file}') for d in dirs]
    for f in filenames:
        if Path(f).exists():
            return str(Path(os.path.expanduser(f)))
    return None


def get_config():
    src_dir='/home/philips/neumodvb'
    dirs = ['~/.config/neumodvb', '/etc/neumodvb',  f'{src_dir}/config', f'{src_dir}/config']
    filenames = [ os.path.expanduser(f'{d}/neumodvb.cfg') for d in dirs]
    for f in filenames:
        if Path(f).exists():
            print(f'loading options from {f}')
            config = ConfigObj(f, interpolation='template')
            return config
    return None

def getsubattr(o, k):
    parts = k.split('.')
    try:
        for part in parts:
            o = getattr(o, part)
    except AttributeError:
        return None
    return o

def setsubattr(o, k, val):
    parts = k.split('.')
    for part in parts[:-1]:
        o = getattr(o, part)
    setattr(o, parts[-1], val)

class SubObj(object):
    def __init__(self):
        pass

def setsubattr_obj(o, k, val):
    parts = k.split('.')
    for part in parts[:-1]:
        d = getattr(o, part, None)
        if d is None:
            d = SubObj()
            setattr(o, part, d)
        o = d
    setattr(o, parts[-1],  val)


class get_processed_options(object):
    def __init__(self):
        relative_files = ('logconfig', 'gui', 'css', 'mpvconfig')
        o =options_t()
        c= get_config()
        cfg = get_configfile(c['LOGGING']['logconfig'])
        set_logconfig(cfg)
        for sec in  ['PATHS', 'SCAM', 'LOGGING', 'CONFIG']:
            for k,v in c[sec].items():
                if k in relative_files:
                    v = get_configfile(v)
                if hasattr(o, k):
                    old = getattr(o,k)
                    v = os.path.expanduser(v)
                    dtdebug(f'changing receiver option {k} from default {old} to {v}')
                    setattr(o, k, type(old)(v))
                    setattr(self, k, type(old)(v))
                else:
                    setattr(self, k, v)

        import pychdb # for usals_location
        for sec in  ['DISH', 'RECORD', 'TIMESHIFT']:
            for k,v in c[sec].items():
                try:
                    old = getsubattr(o,k)
                    dtdebug(f'changing receiver option {k} from default {old} to {v}')
                    if type(old) == timedelta:
                        v = parse_time(v)
                        setsubattr(o, k, v)
                        setsubattr(self, k, v)
                    else:
                        setsubattr(o, k, type(old)(v))
                        setsubattr_obj(self, k, type(old)(v))
                except AttributeError:
                    pass_dict

        self.receiver = o


options = get_processed_options()
