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
import wx

def ShowMessage(title, message=None, add_cancel=False, default_is_ok=False):
    from neumodvb.neumo_dialogs_gui import ErrorDialog_
    dlg = ErrorDialog_(None)
    message = title if message is None else message
    dlg.title.SetLabel(title)
    if not add_cancel:
        dlg.cancel.Hide()
    dlg.message.SetLabel(message)
    if default_is_ok:
        dlg.ok.SetFocus()
    ret = dlg.ShowModal() == wx.ID_OK
    dlg.Destroy()
    return ret

def ShowOkCancel(title, message=None, default_is_ok=False):
    return ShowMessage(title, message, add_cancel=True, default_is_ok=default_is_ok)
