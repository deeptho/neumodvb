# Neumo dvb (C) 2019-2020 deeptho@gmail.com
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
import wx.lib.mixins.listctrl as listmix

import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pyneumodb
import pychdb
import pyneumodb
import pyepgdb
from collections import namedtuple

import datetime
from dateutil import tz
from neumodvb import neumodbutils


import ipdb


#db=pychdb.chdb()
#db.open("/mnt/scratch/neumo/chdb.lmdb/")


class ChInfoTextCtrl(wx.TextCtrl):
    def __init__(self, *args, **kwds):
        super().__init__( *args, **kwds)
        self.ChangeValue("test ")
        self.SetDefaultStyle(wx.TextAttr(wx.RED))
        self.AppendText("Red text\n")
        f = self.GetFont()
        self.SetDefaultStyle(wx.TextAttr(wx.NullColour, wx.LIGHT_GREY, font=f.Bold()))
        #self.SetDefaultFont(f.Bold())
        self.AppendText("Red on grey text\n")
        #self.SetDefaultFont(f)
        self.SetDefaultStyle(wx.TextAttr(wx.BLUE, font=f))
        self.AppendText("Blue on grey text\n")

class ChListCtrl(wx.ListCtrl, listmix.ListCtrlAutoWidthMixin):
    def __init__(self, parent, *args, **kwds):
         #we use a very small size to ensure chlist is as wide as viewer
        super().__init__(parent, wx.ID_ANY, size = wx.Size(10,10),
            style=wx.LC_REPORT|wx.LC_VIRTUAL|wx.LC_HRULES|wx.LC_VRULES |wx.EXPAND |wx.LC_NO_HEADER
            )

        listmix.ListCtrlAutoWidthMixin.__init__(self)
        self.il = wx.ImageList(16, 16)
        if False:
            self.idx1 = self.il.Add(images.Smiles.GetBitmap())
        empty = self.makeBlank()
        self.idx2 = self.il.Add(empty)
        self.SetImageList(self.il, wx.IMAGE_LIST_SMALL)


        self.InsertColumn(0, "n", format=wx.LIST_FORMAT_RIGHT)
        self.InsertColumn(1, "C")
        f = self.GetFont()
        dc = wx.ScreenDC()
        dc.SetFont(f)
        w,h = dc.GetTextExtent("100000")
        print("W={}".format(w))
        self.SetColumnWidth(0, w)
        #self.SetColumnWidth(1, width=wx.LIST_AUTOSIZE_USEHEADER)
        #self.SetColumnWidth(1, width=10)
        self.setResizeColumn(2) #strange, should be 1

        self.SetItemCount(200)
        self.attr1 = wx.ListItemAttr()
        self.attr1.SetBackgroundColour("yellow")

        self.attr2 = wx.ListItemAttr()
        self.attr2.SetBackgroundColour("light blue")
        #self.EnsureVisible(190)
        self.Select(191)
        self.Focus(191)
        self.SetFocus()
        #wx.CallAfter(self.Focus, 191)
        self.Bind(wx.EVT_LIST_ITEM_SELECTED, self.OnItemSelected)
        self.Bind(wx.EVT_LIST_ITEM_ACTIVATED, self.OnItemActivated)
        self.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.OnItemDeselected)

    def OnFocus(self):
        print("Focused ")
    def makeBlank(self):
        empty = wx.Bitmap(16,16,32)
        dc = wx.MemoryDC(empty)
        dc.SetBackground(wx.Brush((0,0,0,0)))
        dc.Clear()
        del dc
        empty.SetMaskColour((0,0,0))
        return empty

    def OnItemSelected(self, event):
        self.currentItem = event.Index
        print('OnItemSelected: "%s", "%s", "%s"\n' %
              (self.currentItem,
               self.GetItemText(self.currentItem),
               self.getColumnText(self.currentItem, 1)))
        #if self.currentItem==self.GetItemCount() -2:
        #    self.SetItemCount(self.GetItemCount()+10)

    def OnItemActivated(self, event):
        self.currentItem = event.Index
        print("OnItemActivated: %s\nTopItem: %s\n" %
                           (self.GetItemText(self.currentItem), self.GetTopItem()))

    def getColumnText(self, index, col):
        item = self.GetItem(index, col)
        return item.GetText()

    def OnItemDeselected(self, evt):
        print("OnItemDeselected: %s" % evt.Index)


    #-----------------------------------------------------------------
    # These methods are callbacks for implementing the "virtualness"
    # of the list...  Normally you would determine the text,
    # attributes and/or image based on values from some external data
    # source, but for this demo we'll just calculate them
    def OnGetItemText(self, item, col):
        #print("OnGetItemText {} {}".format(item, col))
        if col == 0:
            return str(item)
        else:
            return "Item %d, <b>column</b> %d" % (item, col)

    def OnGetItemImage(self, item):
        return -1
        if False and item % 3 == 0:
            return self.idx1
        else:
            return self.idx2

    def OnGetItemAttr(self, item):
        if item % 3 == 1:
            return self.attr1
        elif item % 3 == 2:
            return self.attr2
        else:
            return None



def lastdot(x):
    return str(x[1]).split('.')[-1].replace('_',"")




if __name__ == "__main__":
    import signal
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    try:
        if sys.ps1:
            interpreter = True
    except AttributeError:
        interpreter = False
        if sys.flags.interactive:
            interpreter = True

    dotkeys = neumodbutils.get_dotkeys(pychdb.service.service)
    [ CD(key, key.split('.')[-1]) for key in dotkeys]
    from collections import OrderedDict
