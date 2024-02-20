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

import configparser
import os
import sys
import pathlib
from pathlib import Path
import re
from datetime import timedelta
from configobj import ConfigObj, TemplateInterpolation

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


def get_themes_dir():
    maindir_ = maindir()
    src_configdir =  pathlib.Path(maindir_, '../../config')
    dirs = ['~/.config/neumodvb', '/etc/neumodvb',  f'{src_configdir}']
    filenames = [ os.path.realpath(os.path.expanduser(f'{d}')) for d in dirs]
    for f in filenames:
        p =Path(f, 'share/themes')
        if p.is_dir():
            return str(Path(os.path.expanduser(f)))
    return None

def get_configfile(file):
    maindir_ = maindir()
    src_configdir =  pathlib.Path(maindir_, '../../config')
    dirs = [ f'{src_configdir}', '/etc/neumodvb', '~/.config/neumodvb']
    filenames = [ os.path.realpath(os.path.expanduser(f'{d}/{file}')) for d in dirs]
    for f in filenames:
        if Path(f).exists():
            return str(Path(os.path.expanduser(f)))
    return None


def get_config():
    maindir_ = maindir()
    src_configdir =  pathlib.Path(maindir_, '../../config')
    dirs = [f'{src_configdir}', '/etc/neumodvb', '~/.config/neumodvb']
    filenames = [ os.path.realpath(os.path.expanduser(f'{d}/neumodvb.cfg')) for d in dirs]
    config = None
    for f in filenames:
        if Path(f).exists():
            print(f'loading options from {f}')
            if config is None:
                config = ConfigObj(f, interpolation=False)
            else:
                config.merge(ConfigObj(f, interpolation=False))
    config.main.interpolation = 'Template'
    return config

def save_config(configobj):
    maindir_ = maindir()
    configdir = '~/.config/neumodvb'
    os.makedirs(os.path.expanduser(configdir), exist_ok=True)
    filename = os.path.realpath(os.path.expanduser(f'{configdir}/neumodvb.cfg'))

    #try:
    print(f'saving options in {filename}')
    with open(filename, 'wb') as f:
        configobj.write(f)
    #except:
    #    print("Failed to save preferences")


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
    """
    read paths and default options from various config files

    Note that options will be  overwritten by values stored in the database.
    This happens when  the receiver c++ object is created: it is passed the options object
    that we read and construct here, only used to provide initial values for the database

    Note that some options cannot be overriden by database values. For example, filesystem paths
    are needed to locate the database and cannot be stored in the database

    """
    def __init__(self):
        relative_files = ('logconfig', 'gui', 'css', 'mpvconfig')
        o = options_t()
        c = get_config()
        cfg = get_configfile(c['LOGGING']['logconfig'])
        o.upgrade_dir =  str(pathlib.Path(maindir(), 'upgrade'))
        set_logconfig(cfg)
        engine = TemplateInterpolation(c)
        for sec in  ['PATHS', 'LOGGING', 'CONFIG']:
            for k,v in c[sec].items():
                if k in relative_files:
                    v = get_configfile(v)
                if hasattr(o, k) and v is not None:
                    old = getattr(o,k)
                    v = os.path.expanduser(v)
                    dtdebug(f'changing receiver option {k} from default {old} to {v}')
                    setattr(o, k, type(old)(v))
                    setattr(self, k, type(old)(v))
                else:
                    setattr(self, k, v)

        self.receiver = o


options = get_processed_options()
