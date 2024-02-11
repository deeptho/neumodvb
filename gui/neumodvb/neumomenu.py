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
from collections import namedtuple
from collections import OrderedDict
import wx

from neumodvb.util import dtdebug, dterror
#menu item
MI = namedtuple('MenuItemDesc', 'name label help kind start_disabled', defaults=[None, "", wx.ITEM_NORMAL, False])

#menu
MENU = namedtuple('MenuDesc', 'name, label entries')

SEP = MI('separator')

view_menu = (
    MI("FullScreen",
       _("&Full Screen\tCtrl-F"),
       _("full screen live viewing")),
    MI("ToggleGui",
       _("&Show/Hide Gui\tL"),
       _("Show/Hideo Gui ")),
    SEP,

    MI("LiveScreen",
       _("&Live Screen\tCtrl-L"),
       _("Show Live Screen")),
    SEP,
    MI("LiveChannels", _("&Channels\tC"),_("Show Channels/services")),
    MI("LiveEpg", _("Grid &EPG\tE"),_("Show grid epg")),
    MI("ChEpg",
       _("&Channel EPG\tShift-E"),
       _("Show channel epg")),
    MI("LiveRecordings",
       _("&Recordings\tR"),
       _("Show recordings")),
    SEP,
    MI("Exit",
       _("&Quit\tCtrl-Q"), "End program")
)


control_menu = (
    MI("Tune",  _("&Tune\tCtrl-Enter"), ""),
    MI("TuneAdd",  _("&Tune - Add\tShift-Ctrl-Enter"), ""),
    MI("Play",  _("&Play\tCtrl-Enter"), ""),
    MI("Play Add",  _("&Play- Add\tShift-Ctrl-Enter"), ""),
    SEP,
    MI("AddStream",  _("&Add Stream\tCtrl-K"), ""),
    SEP,
    MI("ToggleRecord", _("&Record\tCtrl-R"), ""),
    MI("AutoRec", _("&Create Auto record\tCtrl-G"), ""),
)

play_menu = (
    MI("Pause",  _("&Pause\tCtrl-Space"), ""),
    MI("Stop",  _("&Stop\tCtrl-X"), ""),
    MI("JumpBack",  _("&Back\tLeft"), ""),
    MI("JumpForward",  _("&Forward\tRight"), ""),
    SEP,
    MI("AudioLang",  _("&Audio language\tCtrl-Shift-3"), ""), #ctrl-#
    MI("SubtitleLang",  _("&Subtitle language\tCtrl-T"), ""),
    MI("ChannelScreenshot",  _("&ChannelScreenhot\tCtrl-J"), ""),
    MI("ToggleOverlay",  _("&Toggle overlay\tCtrl-O"), ""),
    SEP,
    MI("VolumeUp", _("&Volume Up\t="), ""),
    MI("VolumeDown", _("&Volume Down\t-"), "")
)

dx_menu = (
    MI("Inspect",  _("Inspect"), ""),
    #MI("PlayFile",  _("PlayFile"), ""),
    MI("SignalHistory",  _("&Signal History\tCtrl-H"), ""),
    SEP,
    MI("Scan",  _("&Scan\tCtrl-S"), ""),
    MI("CreateScanCommand",  _("&Create Scan Command\tAlt-S"), ""),
    SEP,
    MI("Spectrum",  _("&Spectrum\tCtrl-U"), _("Spectrum")),
    MI("Positioner",  _("&Positioner\tCtrl-P"), ""),
    SEP,
)

edit_menu = (
    MI("EditMode",
       _("&Edit Mode\tCtrl-E"),
       "",
       wx.ITEM_CHECK),
    MI("New",
       _("&New\tCtrl-N"),
       ""),
    MI("EditBouquetMode",
       _("Edit Bouquet&\tAlt-B"),
       "",
       wx.ITEM_CHECK),
    MI("EditCommandMode",
       _("Edit Command&\tAlt-Q"),
       "",
       wx.ITEM_CHECK),
    MI("BouquetAddService",
       _("Bouquet add service&\tCtrl-M"),
       "", start_disabled=True),
    MI("CommandAddSat",
       _("Command add sat&\tCtrl-M"),
       "", start_disabled=True),
    MI("CommandAddMux",
       _("Command add mux&\tCtrl-M"),
       "", start_disabled=True),
    MI("Delete",
       _("&Delete\tCtrl-D"),
       ""),
    MI("Undo",
       _("&Undo\tCtrl-Z"),
       ""),
    SEP,
    MI("Export",
       _("&Export List"),
       ""),
    SEP,
    MI("EditOptions",
       _("&Edit Options\tCtrl-J"),
       "")
)

lists_menu = (
    MI("StatusList",_("&Status\tShift-Ctrl-O"), _("status")),
    SEP,
    MI("ServiceList",_("&Services\tShift-Ctrl-S"), _("service list")),
    MI("ChgmList",_("&Channels/Bouqet\tShift-Ctrl-C"), ""),
    SEP,
    MI("DvbsMuxList",_("&DVB-S Muxes\tShift-Ctrl-M"), ""),
    MI("DvbcMuxList",_("&DVB-C Muxes\tShift-Ctrl-N"), ""),
    MI("DvbtMuxList",_("&DVB-T Muxes\tShift-Ctrl-T"), ""),
    SEP,
    MI("LnbList",_("&LNBs\tShift-Ctrl-L"), ""),
    MI("DishList",_("&Dishes\tShift-Ctrl-D"), ""),
    MI("SatList",_("&Satellites\tShift-Ctrl-A"), ""),
    MI("ChgList",_("&Bouquets\tShift-Ctrl-B"), ""),
    SEP,
    MI("FrontendList",_("&Frontends\tShift-Ctrl-F"), ""),
    MI("StreamList",_("&Streams\tShift-Ctrl-E"), ""),
    SEP,
    MI("RecList",_("&Recordings\tCtrl-Shift-R"), _("recordings list")),
    MI("AutoRecList",_("&AutoRecs\tCtrl-Shift-G "), _("autorecordings list")),
    MI("SpectrumList",_("&Spectra\tCtrl-Shift-U"), _("Spectra list")),
    SEP,
    MI("ScanCommandList",_("&Commands\tCtrl-Shift-Q"), _("Scan commands")),

)

main_menubar = (
    MENU('View',  _("&View"), view_menu),
    MENU('PLay', _("&Play"), play_menu),
    MENU('Control', _("&Control"), control_menu),
    MENU('DX', _("&DX"), dx_menu),
    MENU('Edit', _("&Edit"), edit_menu),
    MENU('Lists', _("&Lists"), lists_menu)
    )


class NeumoMenuBar(wx.MenuBar):
    """
    menu items will be enabled provided that current panel has the method needed by the menu item

    """
    def __init__(self, parent, *args, **kwds):
        super().__init__(*args, **kwds)
        self.parent = parent
        self.items = OrderedDict()
        self.menus = OrderedDict()
        self.make_menubar()
        parent.SetMenuBar(self)

    def get_panel_method_and_prio(self, method_name):
        w = wx.Window.FindFocus()
        while w is not None:
            if hasattr(w, method_name):
                return getattr(w, method_name), 3
            w = w.GetParent()
        ret = self.parent.get_panel_method(method_name)
        if ret is not None:
            return ret, 2
        return None, -1

    def get_panel_method(self, method_name):
        ret, _  = self.get_panel_method_and_prio(method_name)
        return ret

    def item_is_enabled(self, it, check_for_duplicates=False):
        """
        principle: lookup in parent if it supports specific method
        """
        if it.disabled: #menu item disabled because of some current state unrelated to specifically shown panel
            return False
        if it.desc.name in  ['Stop', 'Inspect']:
            return True
        m = self.get_panel_method(f'Cmd{it.desc.name}')
        if check_for_duplicates:
            accel = it.GetAccel()
            accel = None if accel is None else accel.ToRawString()
            enabled = m is not None and m != False and m == self.find_command(it.desc.name, accel)[0]
            return enabled
        else:
            return m is not None and m != False

    def OnShow(self, evt):
        menu =evt.GetMenu();
        dtdebug(f'Show menu {menu.name}')
        for it in menu.GetMenuItems():
            if hasattr(it, 'desc'):
                enabled = self.item_is_enabled(it, check_for_duplicates=True)
                it.Enable(enabled)

    def OnAction(self, evt):
        pass

    def find_command(self, key, accel):
        bestprio = -1
        found = None
        found_item = None
        dtdebug(f'FIND menu command key={key} accel={accel}')
        for name_, item in self.items.items():
            a = item[1].GetAccel()
            #we allow multiple commands with the same accelerator; they are mapped to the
            #same Cmd function but only one is not grayed out. @todo: a better method would change
            #the menu text in this case.
            accel_matches = a is not None and a.ToRawString() == accel
            cmd_matches = name_ == key
            if accel_matches or cmd_matches:
                method_name_ = f'Cmd{name_}'
                m, prio = self.get_panel_method_and_prio(method_name_)
                enabled = self.item_is_enabled(item[1])
                found = m if enabled and prio > bestprio else found
                found_item = item[1] if enabled and prio > bestprio else found_item
                bestprio = max(bestprio, prio)
        m =found
        prio = bestprio
        return m, found_item

    def run_command(self, method_name, accel, evt):
        key = method_name[3:]
        #accellerator does not toggle the menu
        dtdebug(f'accel command {method_name}')
        m, item  = self.find_command(key, accel)
        if m is not None:
            if item is not None  and item.IsCheck():
                item.Check(not item.IsChecked())
                ret = m(item.IsChecked())
                if not ret:
                    item.Check(not item.IsChecked())
                return ret
            dtdebug(f'run_command {method_name}')
            return m(evt)
        return

    def run_menu_command(self, method_name, accel, evt):
        key = method_name[3:]
        dtdebug(f'menu command {method_name}')
        m, item = self.find_command(key, accel)
        if m is not None:
            if item is not None  and item.IsCheck():
                ret = m(item.IsChecked())
                if not ret:
                    item.Check(not item.IsChecked())
                return ret
            dtdebug(f'run_menu_command {method_name}')
            return m(evt)
        return

    def make_accels(self):
        accels=[]
        for name, item in self.items.items():
            a = item[1].GetAccel()
            if a is not None:
                key_id = wx.NewIdRef()
                accels.append((a.GetFlags(),  a.GetKeyCode(), key_id))
                x=f'Cmd{item[0].name}'
                if item[0].name in ['JumpBack', 'JumpForward']:
                    continue
                #Note "x=x" below. This serves to copy x into a local variable; otherwise all
                #bindings will use the same value of x
                accel_key = a.ToRawString()
                self.parent.Bind(wx.EVT_MENU, lambda evt, x=x,
                                 accel=accel_key: self.run_command(x, accel, evt), id=key_id)
        accel_tbl = wx.AcceleratorTable(accels)
        return accel_tbl

    def make_menu(self, name, menudesc):
        menu = wx.Menu()
        menu.name = name
        for  mi in menudesc:
            if mi.name == 'separator':
                item = menu.AppendSeparator()
                item.desc = mi
            else:
                item = menu.Append(wx.ID_ANY, mi.label, mi.help, mi.kind)
                item.desc = mi
                item.disabled = item.desc.start_disabled
                a = item.GetAccel()
                self.items[mi.name] = (mi, item)
                x = f'Cmd{mi.name}'
                accel_key = None if a is None else a.ToRawString()
                #Note "x=x" below. This serves to copy x into a local variable; otherwise all
                #bindings will use the same value of x
                self.Bind(wx.EVT_MENU, lambda evt, x=x, accel=accel_key: self.run_menu_command(x, accel, evt), item)

        menu.Bind(wx.EVT_MENU_OPEN, self.OnShow)
        return menu

    def make_menubar(self):
        for menudesc  in main_menubar:
            menu = self.make_menu(menudesc.name,menudesc.entries)
            menu.desc = menudesc
            self.menus[menudesc.name] = (menudesc, menu)
            self.Append(menu, menudesc.label)

    def get_menu_item(self, name):
        return self.items[name][1]

    def get_menu(self, name):
        return self.menus[name][1]

    def edit_mode(self, onoff):
        dtdebug(f'editmode set to {onoff}')
        for name in [ 'Delete', 'Undo']:
            self.items[name][1].Enable(onoff)

"""
Principle of menu/accel handling:
The menubar registers accelerators and menu items.
When a shortcut key is pressed, we analyze its shortcut and then consider all
functions with that short cut (Some accelerators can be used multiple times).

We lookup if these functions exist on the ancestors of the currently focused widget,
on the live panel, on the grid of the current panel, or on the main frame of the application.

In case we find multiple candidates,  the function the highest priority is selected. Functions
found earlier in the search (usually the more specific ones) have the highest priority


"""
