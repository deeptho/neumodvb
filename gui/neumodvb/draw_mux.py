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

from matplotlib.widgets import RectangleSelector
import wx

class MuxSelector(RectangleSelector):
    def __init__(self, parent, pol):
        super().__init__(parent.axes, self.line_select_callback,
                         drawtype='box', useblit=False,
                         button=[1],  # don't use middle button
                         minspanx=5, minspany=5,
                         spancoords='pixels',
                         interactive=True)
        self.parent = parent
        self.t = None
        self.parent.figure.canvas.mpl_connect('key_press_event', self.toggle_selector)
        self.set_active(True)
        self.pol= pol
    def press(self, event):
        """Button press handler and validator."""
        return super().press(event)
        if not self.ignore(event):
            event = self._clean_event(event)
            self._eventpress = event
            self._prev_event = event
            key = event.key or ''
            key = key.replace('ctrl', 'control')
            # move state is locked in on a button press
            if key == self.state_modifier_keys['move']:
                self._state.add('move')
            self._press(event)
            return True
        return False
    def line_select_callback(self, eclick, erelease):
        'eclick and erelease are the press and release events'
        x1, y1 = eclick.xdata, eclick.ydata
        x2, y2 = erelease.xdata, erelease.ydata
        if self.t is not None:
            self.t.remove()
            self.t = None
        freq, symbol_rate = (x1+x2)/2, (x2-x1)*1000
        pol='H'
        txt = f"{freq:8.3f}{pol} {int(symbol_rate)}kS/s "
        posx = freq
        ylimits = self.parent.axes.get_ylim()
        _, sy = self.parent.annot_scale_factors
        posy = y2 + sy *(ylimits[1]-ylimits[0])
        self.t = self.parent.axes.text(posx, posy, txt, fontsize=10, ha='center')
    def set_label(self):
        if self.t is not None:
            self.t.remove()
            self.t = None
        x1, x2, y1, y2 = self.extents
        freq, symbol_rate = (x1+x2)/2, (x2-x1)*1000
        txt = f"{freq:8.3f}{self.pol} {int(symbol_rate)}kS/s "
        posx = freq
        ylimits = self.parent.axes.get_ylim()
        _, sy = self.parent.annot_scale_factors
        posy = y2 + sy *(ylimits[1]-ylimits[0])
        self.t = self.parent.axes.text(posx, posy, txt, fontsize=10, ha='center')
    def hide(self):
        self.set_active(False)
        self.set_visible(False)
        if self.t is not None:
            self.t.remove()
            self.t = None
        self.parent.canvas.draw()
    def show(self):
        self.set_active(True)
        self.set_visible(True)

    def toggle_selector(self, event):
        if event.key.lower() in ['v', 'h', 'l', 'r'] and self.active:
            self.pol = event.key.upper()
            self.set_label()
            self.parent.canvas.draw()
        if event.key in ['enter'] and self.active:
            x1, x2, y1, y2 = self.extents
            freq, symbol_rate = (x1+x2)/2, int((x2-x1)*1000)
            self.hide()
            wx.CallAfter(self.parent.add_drawn_mux, freq, self.pol, symbol_rate)
        if event.key in ['escape'] and self.active:
            self.hide()
