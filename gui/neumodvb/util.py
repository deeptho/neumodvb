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
import pathlib
from inspect import currentframe, getframeinfo

def is_installed():
    return 'lib64' in Path(__file__).parts or 'lib' in Path(__file__).parts

def maindir():
    dir=pathlib.Path(os.path.realpath(__file__)).parent
    return str(dir.resolve())

configdir = None
setup_done = False

def setup():
    """
    update the module search path if we are running from the source directory
    """
    global setup_done
    if setup_done:
        return
    setup_done = True
    maindir_ = maindir()
    builddir = pathlib.Path(maindir_, '../../' , 'build/src')
    configdir =  pathlib.Path(maindir_, 'config')
    if builddir.is_dir():
        srcdir = builddir.resolve()
        sys.path.insert(0, str(srcdir / 'viewer/'))
        sys.path.insert(0, str(srcdir / 'receiver'))
        sys.path.insert(0, str(srcdir / 'stackstring'))
        sys.path.insert(0, str(srcdir / 'neumodb'))
        sys.path.insert(0, str(srcdir / 'neumodb/chdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/statdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/epgdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/recdb'))
        sys.path.insert(0, str(srcdir / 'stackstring/'))
    else:
        sys.path.insert(0, str(pathlib.Path(maindir_)))
    os.environ['PATH'] +=  os.pathsep + str(builddir / 'neumodb') # for neumoupgrade
    #to suppress some more annoying warnings
    os.environ['XDG_CURRENT_DESKTOP'] = 'none'
    os.environ['NO_AT_BRIDGE'] = '1'
    from neumodvb.config import get_themes_dir
    themes_dir = get_themes_dir()
    if themes_dir is not None:
        os.environ['GTK_THEME'] = 'Neumo'
        os.environ['GTK_DATA_PREFIX'] = themes_dir

    setup_done = True


def lastdot(x):
    if type(x) in (list, tuple):
        x=x[-1]
    return str(x).split('.')[-1].replace('_',' ')

def get_ppi():
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gdk
    return Gdk.Screen.get_default().get_resolution()

def get_text_extent(text, font=None, extra='**'):
    import gi
    import wx
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    global dc__
    dc = wx.ScreenDC()
    if font is not None:
        dc.SetFont(font)
    w0,h0 = dc.GetTextExtent(text+extra)
    res = Gdk.Screen.get_default().get_resolution()
    w0 = int(w0*res/96+0.5)
    h0 = int(h0*res/96+0.5)
    return (w0,h0)

def load_gtk3_stylesheet(fname):
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gtk, Gdk, GObject

    style_provider = Gtk.CssProvider()
    style_provider.load_from_path(fname)

    Gtk.StyleContext.add_provider_for_screen(
        Gdk.Screen.get_default(),
        style_provider,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
    )

def dtdebug(message):
    import pyreceiver
    frameinfo = getframeinfo(currentframe().f_back)
    filename = frameinfo.filename.split('/')[-1]
    pyreceiver.log(False, filename, frameinfo.function, frameinfo.lineno, message);

def dterror(message):
    import pyreceiver
    frameinfo = getframeinfo(currentframe().f_back)
    filename = frameinfo.filename.split('/')[-1]
    pyreceiver.log(True, filename, frameinfo.function, frameinfo.lineno, message);

def parse_lattitude(val):
    import re
    m=re.match(r'^([-]{0,1}[0-9]+[.]{0,1}[0-9]*)\s*([NSns]{0,1})', val)
    if m is not None:
        pos, north_south = m.groups()
        pos = int (float(pos)*100)
        north_south = north_south.lower()
        if 'n' in north_south:
            pos = abs(pos)
        elif 's' in north_south:
            pos = -abs(pos)
        return pos
    else:
        return 0

def parse_longitude(val):
    import re
    m=re.match(r'^([-]{0,1}[0-9]+[.]{0,1}[0-9]*)\s*([EWew]{0,1})', val)
    if m is not None:
        pos, east_west = m.groups()
        pos = int (float(pos)*100)
        east_west = east_west.lower()
        if 'e' in east_west:
            pos = abs(pos)
        elif 'w' in east_west:
            pos = -abs(pos)
        return pos
    else:
        return 0

global old_GetTextExtent
old_GetTextExtent =None

def patch_wx():
    import gi
    import wx
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    global old_GetTextExtent
    if old_GetTextExtent is None:
        old_GetTextExtent =wx.ScreenDC.GetTextExtent
    def GetTextExtent(dc, text):
        w0,h0 = old_GetTextExtent(dc, text)
        res = Gdk.Screen.get_default().get_resolution()
        w0 = int((w0+0.5)*res/96)
        h0 = int((h0+0.5)*res/96)
        #print(f"CALLED {text} {w0} {h0}")
        return (w0,h0)
    wx.ScreenDC.GetTextExtent = GetTextExtent
