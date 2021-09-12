'''
    MiniSpinButton.py

    A custom class that looks like the small gtk2 SpinButton, not the monstrous version under gtk3.
    An attempt has been made to make it work with python2 and python3 - See the variable PY2
    Because the idea behind this SpinCtrl is to allow it to be really small, size it to cope
    with your Min/Max values or use the SetFontSize function.

    Events: EVT_MINISPIN        A mouse click up or down
                                A mouse scroll up or down
                                An arrow-up or arrow-down

            EVT_MINISPINUP      A click up
                                A mouse scroll up
                                An arrow-up key

            EVT_MINISPINDOWN    A click down
                                A mouse scroll down
                                An arrow-down key
    Event Notes:
        All events return GetValue() i.e.

            def OnSpin(self,event):
                current_spin_value = event.GetValue()

        If monitoring SpinUp/SpinDown and SpinCtrl, SpinUp or SpinDown is the first event, followed by
        the SpinCtrl

    Functions:
        GetValue()              Returns numeric value in the control

        GetMin()                Returns minimum value

        GetMax()                Returns maximum value

        GetRange()              Returns tuple (min, max) values

        GetIncrement()          Get the value used to increment the control per spin

        GetFontSize()           Get the Font size used for the control

        SetValue(int)           Set numeric value in the control

        SetMin(int)             Set minimum value - Automatically resets Range

        SetMax(int)             Set maximum value - Automatically resets Range

        SetRange(min,max)       Set minimum and maximum values

        SetIncrement(int)       Set the value to increment the control per spin

        SetFontSize()           Set the Font size used for the control

        SetBackgroundColour(colour)

        Enable(Bool)            Enable/Disable the control
                                On Disable the control value is frozen

        IsEnabled()             Is the control Enabled - Returns True/False

    Default Values:
        min     -       0
        max     -       100
        initial -       0
        range   -       (min, max)
        increment       1
        font size       SYS_SYSTEM_FONT Size

Author:     J Healey
Created:    31/08/2018
Copyright:  J Healey - 2018
License:    GPL 2 or any later version
Email:      rolfofsaxony@gmail.com

Usage example:

import wx
import minispinbutton as MSB
class Frame(wx.Frame):

    def __init__(self, parent):
        wx.Frame.__init__ (self, parent, -1, "Mini Spin Button")

        panel = wx.Panel(self, -1, size=(400,200))
#        self.ctl = MSB.MiniSpinButton(panel, -1, size=(15,20), pos=(10,10), min=-5, max=100, initial=0)
#        self.txt = wx.TextCtrl(panel, -1, size=(50,20), pos=(50,10))
        self.ctl = MSB.MiniSpinButton(panel, -1, size=(15,20), min=-5, max=100, initial=0)
        self.txt = wx.TextCtrl(panel, -1, size=(50,20))
        self.ctl.SetBackgroundColour('gold')
        self.ctl.Bind(MSB.EVT_MINISPIN, self.OnSpin)
        #self.ctl.Bind(MSB.EVT_MINISPINUP, self.OnSpinUp)
        #self.ctl.Bind(MSB.EVT_MINISPINDOWN, self.OnSpinDown)
        sizer = wx.BoxSizer(wx.HORIZONTAL)
        sizer.Add(self.txt,1,0,0)
        sizer.Add(self.ctl,0,0,0)
        self.SetSizer(sizer)
        self.Show()

    def OnSpin(self, event):
        obj = event.GetEventObject()
        self.txt.SetValue(str(obj.GetValue()))
        self.ctl.SetFocus()
        print ("Spin", obj.GetValue())

    def OnSpinUp(self, event):
        print ("Spin Up", event.GetValue())

    def OnSpinDown(self, event):
        print ("Spin Down")

app = wx.App()
frame = Frame(None)
app.MainLoop()
'''

import wx
from wx.lib.embeddedimage import PyEmbeddedImage
import sys
if sys.version_info.major == 2:
    PY2 = True
else:
    PY2 = False
spinupdown = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAPiAAAD4gHuD5mHAAAAB3RJTUUH4ggSCB03FPpcVwAAAbZJREFUeNrtmrtKA0EY'
    b'Rs+gnZekSBQEQd9DMIUYL6go6qvYW4qILyUaExVBsLG1slIQxUvWJtNEjGMyszMr34GPrXaZ'
    b's7P7Mzv7gxBCCCGEEEJ8Y+8/SOwAGbBVZIndjoTNPGCKJGCAUpeETakoMgYo/yBhUy3KjGS/'
    b'5LwIElcOIm3gMmWJo84gM0eZwxQlNh0FurOR0su92KeEzWzsSuZSoVwTtSxPepKwGYsl0vIs'
    b'chJDoulZwqaRp8R+IAmbtTwk1gNL2KyGlFjJScKm7LuSGWACeMlZ5A2o+JSZ7lw0ixRv3ESU'
    b'yIBr18emFxPAVAJLoXvgQdsfQgghhBBCCCGEEEKIsLjsdM8B75HHeAs8Dnohl26GkPkAZnzd'
    b'lVYkiSc8/yOZAl4jiNSNMd6f0wrwnKPEUsiXrp6TxGkeVWQ5sMRZP4Ma6uOcu86xFuhGTedd'
    b'3xueZ6JNxOYan2V5hIgdQlVPEispLHHGgc8BJLZTWq8t9CnRTHEBukbCLU1/mxpjDv4gMpz6'
    b'p8GFQ5kdpSCt5b1EahSoP36MiN1xoWWOi/wpbTu0W/9hX2ALIYQQQgghhBDp8wVxiX7Ava8A'
    b'egAAAABJRU5ErkJggg==')
spindown = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAPiAAAD4gHuD5mHAAAAB3RJTUUH4ggSCBk1npj4fwAAAiJJREFUeNrt2M+LEmEc'
    b'BvDvK1tYEJg/Rr0EeulsRsHqevHgdPLmWbqFYtBJQpK5CN6yf8DSgwPhqUNERy8aaOcFL0ER'
    b'1cWMCEV5uqwgtKvt6rgjPB+Y28w77zPvO+983xEhIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiKi'
    b'8wBgq76s649a04ASkaiI/D7j2qtWZxCRa0qp7roTD1Y9BaUU+v3+w2g0+ui0czRNE5fLZVmK'
    b'QCAg9Xr9iYh0T/qz2fQqlUpvT57OTg/TNF9ufZqbpvl5lyEajcZHS1768Xjs03X9yy5C6Lp+'
    b'DOC6ZYsOgGg8Hrc0xOHhIQDcsnw5nkwmD9xu99yKEJqm/QFwz/Llf9H4YDB4ZkWQXq+X2/lH'
    b'qtPpvHA4HFsJEAqF0O12yzsPMZvNBMBBLpd7t40g2Wz2NQDHpVUTAG5UKpXjiwZwOp0wDOMD'
    b'gI0rBMcm74tS6lcmk7mbTCZ/XKSNRCLxyev1JpRS00ut7RY3BxCLRCLnGo10Oj2bTqcR21XL'
    b'tVrtKBwO/1cIj8eD4XB4x3Yh6vW6iIi0Wq2nwWBwZQi/349ms/nYbluFf1Sr1VdnhfD5fBiN'
    b'Rs9tvxkrFApKRMQwjPfLAZRSEBGUy+U3S/sc++8qATjz+fzP5TDtdvsbgCu2n1IL8/l8EcYX'
    b'i8W+iwhSqdRXADf3JsQpI3NULBYB4P6iItjbnxcAbu/dSKwIw99SRERERERb8xeP3+hzsiuu'
    b'uQAAAABJRU5ErkJggg==')
spinup = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAPiAAAD4gHuD5mHAAAAB3RJTUUH4ggSCAYkOXLWEwAAAipJREFUeNrt2M+LEmEc'
    b'BvDvK9uyBQumqHgp9NLZhALdPQk6nfwT8q7YVUISj57KQK+WHhTDU4eI8KIXhTQ6Bgodimjx'
    b'YEKEMvJ0acCW3DZz/AHPBx68zAzz8H3fmUERIiIiIiJaAwC//e57iVt7W2ahxGkqlQKAuyIi'
    b'uq7vT4n5fG6UcASDwTMRQSQS+QLg+t5MZmESR4lE4puIwEij0fgK4MpelEkmk0pEJJvNvlks'
    b'oZSCiCCTybz8VUTt/FRyudzzxRKLcTgcGI/HT3b25kulkoiIVKvVh263G8uKiAhcLhcqlcqD'
    b'nV1i+Xz+1Ov1XljCiN1ux2AwuL2Lmzvo8/kuVcJINBrVZ7OZb2dKDAaD41AodPYvJYyEw+GP'
    b'hULhaB3LzLLqiUopAXBcr9ffNptNxyrXaLVaN0ejUQvAoVJbeJDpui4ADuLx+OtVJnE+sVjs'
    b'BQDLVjZ/u91+arFYsI4iHo8HnU4ns/F90e/3H62jwPl0u934xkpMp9N7NpttbkYRp9P5A8Ad'
    b'098xAPwnJycwo4SRQCAAADdMm8RkMnFomvbZzBJGNE37AOCaKZOp1WqfNlHCSLlcfrf2aaTT'
    b'6VebLGGkVqs9u+xUDi4qoZSSXq9X9Pv92p+OcTqdYrVaTduTxWLx/nA4fK+Uemzcz9IX9F8m'
    b'okTELyLfl5x7aPbDUkSuKqU6/72sduXjdK//iSEiIiIiIiIiIiIiIiIiIiIiIiIiIiIiItqe'
    b'nwRG71f6w806AAAAAElFTkSuQmCC')
spindisabled = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAAJUAAAEqCAYAAAAcSRJbAAAABHNCSVQICAgIfAhkiAAAAAlw'
    b'SFlzAAAD4gAAA+IB7g+ZhwAAABl0RVh0U29mdHdhcmUAd3d3Lmlua3NjYXBlLm9yZ5vuPBoA'
    b'AADDSURBVHic7cExAQAAAMKg9U9tDQ+gAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPgytxAAARuD4uMAAAAASUVORK5C'
    b'YII=')

getspinupdownImage = spinupdown.GetImage
getspindownImage = spindown.GetImage
getspinupImage = spinup.GetImage
getspindisabledImage = spindisabled.GetImage

msbEVT_MINISPIN = wx.NewEventType()
EVT_MINISPIN = wx.PyEventBinder(msbEVT_MINISPIN, 1)
msbEVT_MINISPINUP = wx.NewEventType()
EVT_MINISPINUP = wx.PyEventBinder(msbEVT_MINISPINUP, 1)
msbEVT_MINISPINDOWN = wx.NewEventType()
EVT_MINISPINDOWN = wx.PyEventBinder(msbEVT_MINISPINDOWN, 1)

class SpinEvent(wx.PyCommandEvent):
    """ Event sent from the :class:`MiniSpinButton` when a spin value changes. """

    def __init__(self, eventType, eventId=1, value=0):
        """
        Default class constructor.

        :param `eventType`: the event type;
        :param `eventId`: the event identifier.
        """

        wx.PyCommandEvent.__init__(self, eventType, eventId)
        self._eventType = eventType
        self.value = value

    def GetValue(self):
        """
        Retrieve the value of the control at the time
        this event was generated."""
        return self.value

class MiniSpinButton(wx.Control):

    def __init__(self, parent, id=wx.ID_ANY, pos=wx.DefaultPosition, size=wx.DefaultSize, min=0, max=100, style= "", initial=0, name="MiniSpinButton"):
        """
        Default class constructor.

        @param parent:  Parent window. Must not be None.
        @param id:      identifier. A value of -1 indicates a default value.
        @param pos:     MiniSpinButton position. If the position (-1, -1) is specified
                        then a default position is chosen.
        @param size:    If the default size (-1, -1) is specified then a default size is chosen.
        @param min:     Minimum allowed value.
        @param max:     Maximum allowed value.
        @param initial: Initial value.
        @param name:    Widget name.
        """

        wx.Control.__init__(self, parent, id, pos=pos, size=size, name=name)
        self._min = min
        self._max = max
        self._initial = initial
        self._pos = pos
        self._size = size
        self._name = name
        self._id = id
        self._frozen_value = 0
        self._bgcolour = wx.WHITE
        self._increment = 1

        if self._initial > self._max:
            self._initial = self._max
        if self._initial < self._min:
            self._initial = self._min

        self._Value = initial
        self.SetBackgroundColour(self._bgcolour)

        # Initialize images
        self.InitialiseBitmaps()
        self._tophalf = self._img.GetHeight() / 2

        #MiniSpinButton

        if PY2:
            self.spinner = wx.StaticBitmap(self, -1, bitmap=wx.BitmapFromImage(self._img))
        else:
            self.spinner = wx.StaticBitmap(self, -1, bitmap=wx.Bitmap(self._img))

        #End

        # Bind the events
        self.Bind(wx.EVT_MOUSEWHEEL, self.OnScroll)
        self.Bind(wx.EVT_CHAR, self.OnChar)
        self.spinner.Bind(wx.EVT_LEFT_DOWN, self.OnSpin)

        sizer = wx.BoxSizer(wx.HORIZONTAL)
        sizer.Add(self.spinner, 0, 0, 0)
        self.SetImage(self._initial)
        self.SetSizerAndFit(sizer)
        self.Show()

    def InitialiseBitmaps(self):
        self._imgup = self.SetImageSize(getspinupImage())
        self._imgdown = self.SetImageSize(getspindownImage())
        self._imgupdown = self.SetImageSize(getspinupdownImage())
        self._imgdisabled = self.SetImageSize(getspindisabledImage())
        if self._initial <= self._min:
            self._img = self._imgup
        elif self._initial >= self._max:
            self._img = self._imgdown
        else:
            self._img = self._imgupdown

    def SetImageSize(self,img):
        #Size the image as Full height and the width is half the height
        h = self.GetSize()[1]
        img = img.Scale(int(h/2),int(h),quality=wx.IMAGE_QUALITY_HIGH)
        return img

    def SetValue(self,value):
        self._Value = value
        self.SetImage(value)
        self.Update()

    def GetValue(self):
        return self._Value

    def SetMin(self,value):
        self._min = value

    def GetMin(self):
        return self._min

    def SetMax(self,value):
        self._max = value

    def GetMax(self):
        return self._max

    def SetRange(self,min,max):
        self._min = min
        self._max = max

    def GetRange(self):
        return self._min, self._max

    def IsEnabled(self):
        return wx.Control.IsEnabled(self)

    def Enable(self, value):
        if value and self.IsEnabled(): # If value = current state do nothing
            return
        if not value and not self.IsEnabled():
            return
        wx.Control.Enable(self, value)
        self.Update()
        if value:
            #Enable via callafter in case someone has been scrolling away on the disabled control
            wx.CallAfter(self.OnReset)
        else:
            #Disable - Freeze the controls Value and change bitmap
            self._frozen_value = self.GetValue()
            self._img = self._imgdisabled
            if PY2:
                self.spinner.SetBitmap(wx.BitmapFromImage(self._img))
            else:
                self.spinner.SetBitmap(wx.Bitmap(self._img))

    def OnReset(self):
        #Reset the control to the state it was in when it was Disabled
        self.SetValue(self._frozen_value)
        self.SetImage(self._frozen_value)

    def SetIncrement(self,value):
        self._increment = value

    def GetIncrement(self):
        return self._increment

    def SetFontSize(self,value):
        self._font.SetPointSize(value)
        self.ctl.SetFont(self._font)

    def GetFontSize(self):
        return self._font.GetPointSize()

    #Spin image clicked (Top half = Up | Bottom half = Down)
    def OnSpin(self, event):
        pos = event.GetY()
        if pos < self._tophalf:
            self.OnScroll(None, self._increment)
        else:
            self.OnScroll(None, -self._increment)

    #Keyboard input, test for Arrow Up / Arrow down
    def OnChar(self, event):
        obj = event.GetEventObject()
        key = event.GetUnicodeKey()
        if key == wx.WXK_NONE:
            key = event.GetKeyCode()

        #Test for position keys
        if key == wx.WXK_UP:
            self.OnScroll(None, self._increment)
        if key == wx.WXK_DOWN:
            self.OnScroll(None, -self._increment)

    #Mouse scroll: check rotation for direction
    #Check for non event override value in spin
    def OnScroll(self, event=None, spin=None):
        value = self.GetValue()
        if event:
            if event.GetWheelRotation() > 0:
                rotation = self._increment
            else:
                rotation = -self._increment
        else:
            rotation = spin
        #All values are added, because negative rotation or spin are already negative values
        if rotation > 0:
            if value + rotation <= self._max:
                value += rotation
                self.SetValue(value)
                event = SpinEvent(msbEVT_MINISPINUP, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
                event = SpinEvent(msbEVT_MINISPIN, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)

        elif rotation < 0:
            if value + rotation >= self._min:
                value += rotation
                self.SetValue(value)
                event = SpinEvent(msbEVT_MINISPINDOWN, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
                event = SpinEvent(msbEVT_MINISPIN, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
        else:
            #No rotation
            pass

        self.SetImage(value)

    def SetImage(self, value):
        #Set appropriate image
        if value <= self._min:
            self._img = self._imgup
        elif value >= self._max:
            self._img = self._imgdown
        else:
            self._img = self._imgupdown
        if PY2:
            self.spinner.SetBitmap(wx.BitmapFromImage(self._img))
        else:
            self.spinner.SetBitmap(wx.Bitmap(self._img))
        self.Layout()
