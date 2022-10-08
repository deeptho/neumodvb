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
import subprocess

#the following import also sets up import path
import neumodvb
from neumodvb.util import setup
from neumodvb.config import options
from neumodvb.util import load_gtk3_stylesheet, dtdebug, dterror, maindir

dbpath=options.db_dir

setup()



def upgrade(dbtype, dbname):
    global dbpath
    db = os.path.join(dbpath, dbname)
    cmd=f"neumoupgrade -t{dbtype} {db}.mdb"
    print(cmd)
    subprocess.call(cmd, shell=True, env=os.environ)


upgrade("chdb", "chdb")
upgrade("recdb", "recdb")
upgrade("epgdb", "epgdb")
upgrade("statdb", "statdb")
