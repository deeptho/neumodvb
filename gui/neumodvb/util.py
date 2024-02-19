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
import sys
import os
import pathlib
from inspect import currentframe, getframeinfo
from itertools import islice

def is_installed():
    return 'lib64' in Path(__file__).parts or 'lib' in Path(__file__).parts

def maindir():
    dir=pathlib.Path(os.path.realpath(__file__)).parent
    return str(dir.resolve())

configdir = None
setup_done = False
wxpythonversion = None
wxpythonversion42 = None

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
        sys.path.insert(0, str(srcdir / 'neumodb/devdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/chdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/statdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/epgdb'))
        sys.path.insert(0, str(srcdir / 'neumodb/recdb'))
        sys.path.insert(0, str(srcdir / 'stackstring/'))
    else:
        import sysconfig
        sys.path.insert(0, str(pathlib.Path(maindir_)))
        sys.path.insert(0, f"{pathlib.Path(sysconfig.get_path('stdlib'), 'neumodvb')}")
    os.environ['PATH'] +=  os.pathsep + str(builddir / 'neumodb') # for neumoupgrade
    #to suppress some more annoying warnings
    os.environ['XDG_CURRENT_DESKTOP'] = 'none'
    os.environ['WXSUPPRESS_SIZER_FLAGS_CHECK'] = '1'
    os.environ['NO_AT_BRIDGE'] = '1'
    from neumodvb.config import get_themes_dir
    themes_dir = get_themes_dir()
    if themes_dir is not None:
        os.environ['GTK_THEME'] = 'Neumo'
        os.environ['GTK_DATA_PREFIX'] = themes_dir
    from packaging import version
    import wx
    global wxpythonversion, wxpythonversion42
    wxpythonversion = version.parse(wx.version().split(' ')[0])
    wxpythonversion42 = version.parse('4.2.0')
    setup_done = True


def lastdot(x):
    if type(x) in (list, tuple):
        x=x[-2]
    return str(x).split('.')[-1].replace('_',' ')

def get_ppi():
    import gi
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    return Gdk.Screen.get_default().get_resolution()

def get_screen_size():
    #    s=mng.window.screen().availableSize()
    import gi
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    display = Gdk.Display.get_default()
    g = display.get_monitor(0).get_geometry()
    return g.width, g.height

def get_text_extent(text, font=None, extra='', compensate=False):
    """
    compensate: hack which can probably be removed
    """
    import gi
    import wx
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    global dc__
    dc = wx.ScreenDC()
    if font is not None:
        dc.SetFont(font)
    w0,h0 = dc.GetTextExtent(text+extra)
    if compensate:
        res = Gdk.Screen.get_default().get_resolution()
        w = int(w0*res/96+0.5)
        h = int(h0*res/96+0.5)
    else:
        w = w0
        h = h0
    return (w, h)

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


def get_object(evt):
    s = evt.GetExtraLong()
    import pyreceiver
    return pyreceiver.get_object(s)

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

def parse_latitude(val):
    import re
    m=re.match(r'^([-]{0,1}[0-9]+[.]{0,1}[0-9]*)\s*([NSns]{0,1})', val)
    if m is not None:
        pos, north_south = m.groups()
        pos = int (float(pos)*100)
        north_south = north_south.lower()
        if 'n' in north_south:
            pass # -4.0N becomes 4.0S
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
            pass # -4.0E becomes 4.0W
        elif 'w' in east_west:
            pos = -abs(pos) #-7.0W becomes 7.0W
        return pos
    else:
        return 0

def is_circ(lnb):
    import pydevdb
    return lnb.pol_type in (pydevdb.lnb_pol_type_t.LR, pydevdb.lnb_pol_type_t.RL,
                            pydevdb.lnb_pol_type_t.L, pydevdb.lnb_pol_type_t.R)


def find_parent_prop(self, attr, parent=None):
    if parent is not None  and hasattr(self, attr):
        return getattr(self, attr)
    if hasattr(self, 'Parent'):
        return find_parent_prop(self.Parent, attr, parent=self if parent is None else parent)
    elif hasattr(self, 'parent'):
        return find_parent_prop(self.parent, attr, parent=self if parent is None else parent)

def batched(iterable, n):
    "Batch data into tuples of length n. The last batch may be shorter."
    # batched('ABCDEFG', 3) --> ABC DEF G
    if n < 1:
        raise ValueError('n must be at least one')
    it = iter(iterable)
    while (batch := tuple(islice(it, n))):
        yield batch

def get_last_scan_text_dict(st):
    return dict(muxes=f" Muxes: ok={st.locked_muxes} failed={st.failed_muxes} " \
                f"pending={st.pending_muxes} active={st.active_muxes}",
                peaks=f"Peaks: failed={st.failed_peaks} pending={st.pending_peaks}",
                bands=f"Bands: pending={st.pending_bands} active={st.active_bands}" )
