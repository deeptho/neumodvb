import wx
import wx.lib.mixins.listctrl as listmix

import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/stackstring/')
import pychdb
import pyneumodb
import pyepgdb
from collections import namedtuple

import datetime
from dateutil import tz
from neumodvb import neumodbutils


import ipdb


db=pyneumodb.neumodb()
db.open("/mnt/scratch/neumo/chdb.lmdb/")


class chInfoTextCtrl(wx.TextCtrl):
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

class chListCtrl(wx.ListCtrl, listmix.ListCtrlAutoWidthMixin):
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


class muxListCtrl(wx.ListCtrl, listmix.ListCtrlAutoWidthMixin):

    def __init__(self, parent, *args, **kwds):
         #we use a very small size to ensure muxlist is as wide as viewer
        super().__init__(parent, wx.ID_ANY, size = wx.Size(10,10),
            style=wx.LC_REPORT|wx.LC_VIRTUAL|wx.LC_HRULES|wx.LC_VRULES |wx.EXPAND #|wx.LC_NO_HEADER
            )

        listmix.ListCtrlAutoWidthMixin.__init__(self)
        #listmix.ListRowHighlighter.__init__(self, color=wx.Colour('gray'))
        if False:
            self.il = wx.ImageList(16, 16)
            self.idx1 = self.il.Add(images.Smiles.GetBitmap())
            empty = self.makeBlank()
            self.idx2 = self.il.Add(empty)
            self.SetImageList(self.il, wx.IMAGE_LIST_SMALL)

        f = self.GetFont()
        self.dc = wx.ScreenDC()
        self.boldFont = wx.Font(24, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_BOLD, 0, "")
        self.def_font= self.GetFont()
        self.dc.SetFont(self.boldFont)
        parent.SetFont(self.boldFont)

        #self.dc.SetFont(f)
        self.data = self.__get_data__()
        self.__make_columns__()
        self.SetItemCount(len(self.data))
        self.attr1 = wx.ListItemAttr()
        self.attr1.SetBackgroundColour("light gray")
        #self.attr1.SetFont(self.boldFont)

        self.attr2 = wx.ListItemAttr()
        self.attr2.SetBackgroundColour("light blue")
        #self.EnsureVisible(190)
        #self.Select(191)
        #self.Focus(191)
        self.SetFocus()
        #wx.CallAfter(self.Focus, 191)
        self.Bind(wx.EVT_LIST_ITEM_SELECTED, self.OnItemSelected)
        self.Bind(wx.EVT_LIST_ITEM_ACTIVATED, self.OnItemActivated)
        self.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.OnItemDeselected)
        self.odd_color=wx.Colour('gray')
    is_sat = True
    #label: to show in header
    #dfn: display function
    CD = namedtuple('ColumnDescriptor', 'key label dfn')
    CD.__new__.__defaults__=(None, None, None)

    datetime_fn =  lambda x: datetime.datetime.fromtimestamp(x[1], tz=tz.tzlocal()).strftime("%Y-%m-%d %H:%M:%S")

    sat_columns = \
        [CD(key='k.sat_pos', label='Sat',
            dfn= lambda x: pychdb.sat_pos_str(x[1])),
         #CD(key='frequency', label='Mux',
         #   dfn= lambda x: pychdb.to_str(x[0], True)),
         CD(key='frequency', label='Frequency',  dfn= lambda x: x[1]/1000.),
         CD(key='pol', label='Pol', dfn=lambda x: lastdot(x).replace('POL','')),
         CD(key='delivery_system', label='System',
            dfn=lambda x: lastdot(x).replace('SYS',"")),
         CD(key='modulation', label='Modulation',
            dfn=lambda x: lastdot(x)),
         CD(key='symbol_rate', label='SymRate'),
         CD(key='pls_mode', label='Pls Mode', dfn=lastdot),
         CD(key='pls_code', label='Pls Code'),
         CD(key='stream_id', label='Stream'),
         CD(key='HP_code_rate', label='FEC', dfn=lambda x: lastdot(x).replace('FEC','')),
         CD(key='k.network_id', label='nid'),
         CD(key='k.ts_id', label='tsid'),
         CD(key='k.extra_id', label='subid'),
         CD(key='mtime', label='Last modified', dfn=datetime_fn),
         CD(key='scantime', label='Last scanned', dfn=datetime_fn) ]

    #TODO: separate screen for dvb t and dvb c
    other_columns =  \
        [CD(key='LP_code_rate', label='LP_code_rate'),
         CD(key='bandwidth', label='bandwidth'),
         CD(key='guard_interval', label='guard_interval'),
         CD(key='hierarchy', label='hierarchy'),
         CD(key='rolloff', label='rolloff'),
         CD(key='transmission_mode', label='transmission_mode')]

    def __make_columns__(self):
        """
        create all columns
        """
        self.columns = self.sat_columns
        w0,h = self.dc.GetTextExtent('   ')

        for idx, col in enumerate(self.columns):
            self.InsertColumn(idx, col.label, format=wx.LIST_FORMAT_LEFT)
            if len(self.data)>0:
                w,h = self.dc.GetTextExtent(self.OnGetItemText(0, idx))
                wh,h = self.dc.GetTextExtent(col.label)
                w = w0 + max(w,wh)
            else:
                w = wx.LIST_AUTOSIZE_USEHEADER
            self.SetColumnWidth(idx, width=w)
            #self.SetColumnFont(self.boldFont)
            #self.SetColumnToolTip(idx, "My tooltip")
        self.SetFont(self.def_font)
    def __get_data__(self):
        """
        retrieve the mux list
        """
        txn = db.rtxn()
        return pychdb.mux.list_all_by_sat_freq_pol(txn)





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
        if True:
            print('OnItemSelected: {} {}'.format(self.currentItem,
                   self.GetItemText(self.currentItem, 0)))
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
        pass     #print("OnItemDeselected: %s" % evt.Index)


    #-----------------------------------------------------------------
    # These methods are callbacks for implementing the "virtualness"
    # of the list...  Normally you would determine the text,
    # attributes and/or image based on values from some external data
    # source, but for this demo we'll just calculate them
    def OnGetItemText(self, item, colno):
        #print("OnGetItemText {} {}".format(item, col))
        col = self.columns[colno]
        mux = self.data[item]
        field = neumodbutils.subfield(mux, col.key)
        txt = str(field) if col.dfn is None else str(col.dfn((mux, field)) )
        return txt

    def OnGetItemImage(self, item):
        return -1
        if False and item % 3 == 0:
            return self.idx1
        else:
            return self.idx2

    def OnGetItemAttr(self, item):
        if item % 2 == 1:
            return self.attr1
        else:
            return None




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

    dotkeys = neumodbutils.get_dotkeys(pychdb.mux.mux)
    [ CD(key, key.split('.')[-1]) for key in dotkeys]
    from collections import OrderedDict
