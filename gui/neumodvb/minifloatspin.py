'''
    MiniFloatSpin.py

    A custom class that looks like the small gtk2 FloatSpin, not the monstrous version under gtk3.
    Based on TextCtrl with number format checking
    An attempt has been made to make it work with python2 and python3 - See the variable PY2
    Because the idea behind this SpinCtrl is to allow it to be really small, size it to cope
    with your Min/Max values or use the SetFontSize function.
    Accepts manual input from number keys and the numeric keypad.

    Styles: wx.TE_RIGHT (Default)
            wx.TE_LEFT
            wx.TE_CENTRE

            wx.BORDER_NONE is always applied

    Events: EVT_MINISPIN        A value change occurred in the spinctrl
                                A value was keyed in
                                A mouse click up or down
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

        If monitoring SpinUp/SpinDown and Spin, SpinUp or SpinDown is the first event, followed by
        the Spin

        Entering values manually gives a Spin event for each Valid key press
        i.e.
            typing in 1.2 will give an event for 1 and another for 2
            With 1.5 in the control, using delete or backspace to remove the 5 or the 1
            will only give an event after pressing Return
            With 1.1 in the control using arrow keys to position the cursor between the
            ones and then pressing 2 (1.21) will fire an event on pressing 2 but the returned
            value will be the number in the control 1.21

        Pasting a value in sends a single Spin event

        Entering values manually WHEN Limited is set to True
            If the value becomes less than the Minimum value, Minimum value is applied
            If the value becomes greater than the Maximum value, Maximum value is applied

    Functions:
        GetValue()              Returns numeric value in the control

        GetMin()                Returns minimum value

        GetMax()                Returns maximum value

        GetRange()              Returns tuple (min, max) values

        GetIncrement()          Get the value used to increment the control per spin

        GetFontSize()           Get the Font size used for the control

        GetDigits()             Return the number of digits after the decimal point

        SetValue(int)           Set numeric value in the control

        SetMin(int)             Set minimum value - Automatically resets Range

        SetMax(int)             Set maximum value - Automatically resets Range

        SetRange(min,max)       Set minimum and maximum values

        SetIncrement(int)       Set the value to increment the control per spin

        SetFontSize()           Set the Font size used for the control

        SetDigits()             Set the number of digits after the decimal point

        SetLimited(Bool)        Set whether the control should limit the value to fall
                                within the current range
                                If False values not within current range are displayed in red

        IsLimited()             Is the control currently limiting the value to fall
                                within the current range - Returns True/False

        SetBackgroundColour(colour)

        SetForegroundColour(colour)

        Enable(Bool)            Enable/Disable the control
                                On Disable the control value is frozen

        IsEnabled()             Is the control Enabled - Returns True/False


    MiniFloatSpin is decimal point aware, in as much, as it is aware that in some locales
    the separator between the significant digits and the mantissa, is not always a decimal point.
    The current locales "decimal point" is used for display and input purposes and converted
    to a decimal point for calculation purposes.

    When incrementing/decrementing values the resultant float is always rounded to the number
    of set digits after the decimal point, to avoid numbers like 1.3000000003, interfering
    with tests on min and max values.

    Only Floating point decimal format is supported (%f).

    Default Values:
        min     -       0.0
        max     -       100.0
        initial -       0.1
        digits  -       2
        style   -       Align Right | Border None
        limited -       True
        range   -       (min, max)
        increment       0.1
        font size       SYS_SYSTEM_FONT Size

Author:     J Healey
Created:    31/08/2018
Copyright:  J Healey - 2018-2023
Version:    1.1
License:    GPL 3 or any later version
Email:      <rolfofsaxony@gmx.com>

Changelog:
1.1     Ability to replace highlighted numbers in the control
        suggested by edwardsmith999
        Given the above change, adjust the insertion point, so that when replacing,
        the next input character is inserted in the right place

1.0     Original release

Usage example:

import wx
import minifloatspin as MFS
class Frame(wx.Frame):

    def __init__(self, parent):
        wx.Frame.__init__ (self, parent, -1, "Mini Float Spin")

        panel = wx.Panel(self, -1, size=(400,100))
        self.ctl = MFS.MiniFloatSpin(panel, -1, pos=(10,20), size=(35,20), min=0.0, max=9.99, initial=1.0,
                                     style= wx.TE_RIGHT, name="minifloatspin")
        self.ctl.Bind(MFS.EVT_MINISPIN, self.OnSpin)
        self.ctl.Bind(MFS.EVT_MINISPINUP, self.OnSpinUp)
        self.ctl.Bind(MFS.EVT_MINISPINDOWN, self.OnSpinDown)
        self.Show()

    def OnSpin(self, event):
        obj = event.GetEventObject()
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
import locale
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
spinupdownw = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAOEAAADhAEDBbnhAAACH0lEQVR42u3bT0tVQRgG8Oe9JETcuoYpEbgI+gCK0CrI'
    b'hQQ30TDSr9IyaCkSfRnRj6D5D1euAlu0SvFPFIR4nxb3CAp179wzM2fO1PNbn8V9Zs6855yZ'
    b'9wIiIiIiIiI3kXz7L4RYZNfrnEMs8abnJC2nAEayxT9rxQrTCB0CQAvA6V8uOQXwIJcZ6edT'
    b'DiH2HIJ0SO7WOcSH4kfSMcxKHUMssJxXdapQL+jncYhKZgEq1EmAMRkGcG5mTFF+xwKFuCrL'
    b'zVTPkdXAd+p6irWxxTg2qwzxnnHNVRFintWYjVa1SL6MsC56uQ/gzLWSmWOZHQXwBcDtCoNc'
    b'AHgE4NgljEuQcQCfAQyleOiamYUqv2upQhQDue99a5EcK6Y3ta9m9k07ICIiIiIiIiIiIhKX'
    b'yyb2M3R3xlP+xgMzO/MNsgdgImGQSwBPzOyw10V9d+PNbBLATqIQ3wE8RPdsBl5BCvMAfiUI'
    b'sthoNI6CHPQUt5cBGClG5k5FIdpm5nxk7TQjxYgcA6iqpW9jkBClkGxHPtHdqOzGJfkuVopo'
    b'z5EeYTYBPA05PugeSZdqrjHPmdkGMBUoSBPAz7IdQr7Nme1AIWbN7IdPm5PvWjGS90heeiyL'
    b'N7V4WSvCzJQMsVW7t0+Sc7VtaRpUp9NZHiDIrVp/F5DccWiVbWbRJ98nyHQuIYzk3WTdcRWE'
    b'+Zjtd/S1Du3t7DcFsv43j4iIiIiIyP/kNycu8wiaKoSnAAAAAElFTkSuQmCC')
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
spindownw = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAOEAAADhAEDBbnhAAACFElEQVR42u3YP4jaUBwH8G+OU9qCSIdbuzjcrFd0qNe9'
    b'q0scdPC2cmChg2CHVHBwrdU97WoXoUMsxcU/cBzozTcYKNzZmEawlCIU4+tSQeh53h+jEb6f'
    b'KZDw8r55Ly+/F4CIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIhuQwjhqr4s64+0pA0JwAGA3wvO'
    b'eZ3OAOAhgJNlF+5e9xQkSRKhUOio0+m8vOoa0zQxGo0cS2EYBlKp1Gtd10/+9ed+0yufz2ti'
    b'A2RZ/rDyaS7L8sU6QySTyTNHXnqfz7enadrlOkJomnYO4JGTi85Bs9l0NES73RYAnji+HHu9'
    b'3hfD4dB2IsRgMBgDCDu+/M8aDwaDihNBwuHw8do/UtFo9L1tr2ZgdF0XkUjk7dpDTCYTANgt'
    b'l8tfVhFEVdVPAHY2WU34stns+V0DjMdjoSjK6SoqhJ17vi+/KpXK03q9/uMubTQajW+WZT0H'
    b'8Gejtd3czZ91u91bjUa1Wp14PJ6g66rldDp92Ov1bhTCsiwRCARCrguhqioAIB6Pv+n3+9eG'
    b'MAxDJBKJV27bKvwnk8l8XBTCNE3h9/vfuX4zViwWJQBQFOXrfIDpdCqEECKXy32+4T7INbvK'
    b'B6VS6ed8mFgsNgDgcf2UmrFte3a412q1TCGEqNVq3wE83poQV4zMYaFQEAAicxXB1v682N+6'
    b'kVgUZqtDEBERERG5zl+4IpChEQH6CQAAAABJRU5ErkJggg==')
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
spinupw = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAADIAAABkCAYAAADE6GNbAAAABmJLR0QA/wD/AP+gvaeTAAAA'
    b'CXBIWXMAAAOEAAADhAEDBbnhAAACDElEQVR42u3YMYjaUBwG8C9HlWtBpKBrOzh01gMd6rk4'
    b'dXUwDjo41oKFDg4drODg2Fqla9q5i9AhyjlVhQ7Vzh0MHVqrQcEOpYu5fxcPLPSOO0k0wvdb'
    b'Engvj/fxfy95BCAiIiIiIhuIyD/Xgw4B4MHBhtmY9GmtVhMAMQBYrVaHE8KyrIvbYL/fN0VE'
    b'2u32TwB3D6YyG5M8bjQav2RDKpWaAfAcRJh6va4AQLlcPtsMcX5+LiIilUrlw7qr4vqqlEql'
    b'd3IJ0zTF7/e/cu3kNU0DAGQymeeTyUSuMp1OJZvNPnXtEisWi6fj8ViuYz6fSygUirhxcz8c'
    b'jUZyE61Wa+XxeMKuCREKhXzdbteULXQ6nW+FQuHYjmV2tO2DiqIAgC+dTn9OJpPBbcZIJBL3'
    b'A4HARwDe9Xi7tf5C32o2mx2xgaZp7wEc7WXzx+Px15Zl2ZFDDMOQWCz2Yuf7IhwOl8UB0Wj0'
    b'yc5CeL3eR4vFwnIiyGw2+wMguotvzEmv1xMnDQYDAXDPsUr4fL6grus/ZAd0Xf8K4I4jlVFV'
    b'9bvsUC6X+2J7NarVqi57oKrq2+tWRbkqhKIoiEQib4bD4eP/9TFNE8vl0rENOZ1Okc/nnxmG'
    b'8fJiPjcOstF+AuD3JW1ep1+WAG4D+GTHgdAV57qD/hNDRERERERERERERERERERERERERERE'
    b'RES0P38BI16CNrA+BBwAAAAASUVORK5CYII=')
spindisabled = PyEmbeddedImage(
    b'iVBORw0KGgoAAAANSUhEUgAAAJUAAAEqCAYAAAAcSRJbAAAABHNCSVQICAgIfAhkiAAAAAlw'
    b'SFlzAAAD4gAAA+IB7g+ZhwAAABl0RVh0U29mdHdhcmUAd3d3Lmlua3NjYXBlLm9yZ5vuPBoA'
    b'AADDSURBVHic7cExAQAAAMKg9U9tDQ+gAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    b'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPgytxAAARuD4uMAAAAASUVORK5C'
    b'YII=')

getspinupdownImage = spinupdown.GetImage
getspinupdownwImage = spinupdownw.GetImage
getspindownImage = spindown.GetImage
getspindownwImage = spindownw.GetImage
getspinupImage = spinup.GetImage
getspinupwImage = spinupw.GetImage
getspindisabledImage = spindisabled.GetImage

mfsEVT_MINISPINCTRL = wx.NewEventType()
EVT_MINISPIN = wx.PyEventBinder(mfsEVT_MINISPINCTRL, 1)
mfsEVT_MINISPINUP = wx.NewEventType()
EVT_MINISPINUP = wx.PyEventBinder(mfsEVT_MINISPINUP, 1)
mfsEVT_MINISPINDOWN = wx.NewEventType()
EVT_MINISPINDOWN = wx.PyEventBinder(mfsEVT_MINISPINDOWN, 1)


class SpinEvent(wx.PyCommandEvent):
    """ Event sent from the :class:`MiniFloatSpin` when a spin value changes. """

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


class MiniFloatSpin(wx.Control):

    def __init__(self, parent, id=wx.ID_ANY, pos=wx.DefaultPosition, size=wx.DefaultSize, min=0.0, max=100.0, initial=0.1, style=wx.TE_RIGHT, name="MiniSpinCtrl", example=None):
        """
        Default class constructor.

        @param parent:  Parent window. Must not be None.
        @param id:      identifier. A value of -1 indicates a default value.
        @param pos:     MiniSpinCtrl position. If the position (-1, -1) is specified
                        then a default position is chosen.
        @param size:    If the default size (-1, -1) is specified then a default size is chosen.
        @param min:     Minimum allowed value.
        @param max:     Maximum allowed value.
        @param initial: Initial value.
        @param style:   Alignment (Left,Middle,Right).
        @param name:    Widget name.
        @param example: Size will be set to fit string example
        """

        wx.Control.__init__(self, parent, id, pos=pos, size=size, name=name, style=style)
        self.parent = parent
        self._min = float(min)
        self._max = float(max)
        self._digits = 2
        self._initial = float(initial)
        self._pos = pos
        self._size = size
        self._name = name
        self._limited = True
        self._id = id
        self._frozen_value = "0.0"
        self._fgcolour = wx.BLACK
        self._bgcolour = wx.WHITE
        self._outrange_colour = wx.RED
        self._increment = 0.1
        self._font = wx.SystemSettings.GetFont(wx.SYS_SYSTEM_FONT)

        if self._limited:
            if self._initial > self._max:
                self._initial = self._max
            if self._initial < self._min:
                self._initial = self._min

        self.SetForegroundColour(self._fgcolour)
        self.SetBackgroundColour(self._bgcolour)
        self.SetWindowStyle(wx.BORDER_NONE)
        self._style = style

        txtstyle = wx.TE_NOHIDESEL | wx.TE_PROCESS_ENTER

        if style & wx.TE_LEFT or style == wx.TE_LEFT:
            txtstyle = txtstyle | wx.TE_LEFT
        elif style & wx.TE_CENTRE:
            txtstyle = txtstyle | wx.TE_CENTER
        else:
            txtstyle = txtstyle | wx.TE_RIGHT
        if style & wx.TE_READONLY:
            txtstyle = txtstyle | wx.TE_READONLY
        if style & wx.BORDER_NONE:
            txtstyle = txtstyle | wx.BORDER_NONE

        # Test decimal point is comma # locale.setlocale(locale.LC_ALL, 'es_ES.utf8')
        self._decimal = locale.localeconv()["decimal_point"]
        self._formatstring = '%.'+str(self._digits)+'f'
        disp_initial = self._formatstring % self._initial

        # Initialize images
        self.InitialiseBitmaps()

        # MinifloatSpin

        self.ctl = wx.TextCtrl(self, id, value=disp_initial, \
                               pos=self._pos, size=self._size, style=txtstyle, name=self._name)
        if PY2:
            self.spinner = wx.StaticBitmap(self, -1, bitmap=wx.BitmapFromImage(self._img))
        else:
            self.spinner = wx.StaticBitmap(self, -1, bitmap=wx.Bitmap(self._img))

        # End

        # Bind the events
        self.Bind(wx.EVT_MOUSEWHEEL, self.OnScroll)
        self.spinner.Bind(wx.EVT_LEFT_DOWN, self.OnSpin)
        self.ctl.Bind(wx.EVT_CHAR_HOOK, self.OnChar)
        self.Bind(wx.EVT_PAINT, self.OnPaint)

        sizer = wx.BoxSizer(wx.HORIZONTAL)
        sizer.Add(self.ctl, 1, wx.EXPAND, 0)
        sizer.Add(self.spinner, 0, wx.ALIGN_CENTER_VERTICAL)
        self.SetImage(self._initial)
        if example is not None:
            self.SetSizeByExample(example)
        self.SetSizerAndFit(sizer)
        self.Show()

    def SetSizeByExample(self, example):
        sz = self.ctl.GetTextExtent(example+' ')
        self.ctl.SetInitialSize(self.ctl.GetSizeFromTextSize(sz))

    def OnPaint(self, event):
        if self.IsInRange():
            self.ctl.SetForegroundColour(self.GetForegroundColour())
        else:
            self.ctl.SetForegroundColour(self._outrange_colour)
        self.ctl.SetBackgroundColour(self.GetBackgroundColour())
        self.spinner.SetBackgroundColour(self.GetBackgroundColour())

    def InitialiseBitmaps(self):
        self._imgup = self.SetImageSize(getspinupImage())
        self._imgupw = self.SetImageSize(getspinupwImage())
        self._imgdown = self.SetImageSize(getspindownImage())
        self._imgdownw = self.SetImageSize(getspindownwImage())
        self._imgupdown = self.SetImageSize(getspinupdownImage())
        self._imgupdownw = self.SetImageSize(getspinupdownwImage())
        self._imgdisabled = self.SetImageSize(getspindisabledImage())
        clr = self.parent.GetBackgroundColour()
        clr = self.ImageColour(clr)
        if clr == "white":
            if self._initial <= self._min:
                self._img = self._imgupw
            elif self._initial >= self._max:
                self._img = self._imgdownw
            else:
                self._img = self._imgupdownw
        else:
            if self._initial <= self._min:
                self._img = self._imgup
            elif self._initial >= self._max:
                self._img = self._imgdown
            else:
                self._img = self._imgupdown

    def ImageColour(self, colour):
        try:
            red = wx.Colour.Red(colour)
            green = wx.Colour.Green(colour)
            blue = wx.Colour.Blue(colour)
            brightness = ((red*red*0.241)+(green*green*0.691)+(blue*blue*0.068))**(0.5)
        except Exception:
            brightness = 127
        if brightness <= 127:
            txt_colour = "white"
        else:
            txt_colour = "black"
        return txt_colour

    def SetImageSize(self, img):
        # Size the image as Full height and the width is half the height
        h = self.GetSize()[1]
        img = img.Scale(int(h/2), int(h), quality=wx.IMAGE_QUALITY_HIGH)
        return img

    def SetValue(self, value):
        value = float(value)
        upd_value = self._formatstring % value
        upd_value = upd_value.replace(".", self._decimal)
        self.ctl.SetValue(upd_value)
        self.SetImage(value)
        self.Update()

    def GetValue(self):
        value = self.ctl.GetValue()
        value = value.replace(self._decimal, ".")
        try:
            value = float(value)
        except ValueError:
            value = 0.0
        return value

    def SetMin(self, value):
        self._min = float(value)

    def GetMin(self):
        return self._min

    def SetMax(self, value):
        self._max = float(value)

    def GetMax(self):
        return self._max

    def GetDigits(self):
        return self._digits

    def SetDigits(self, value):
        self._digits = value
        self._formatstring = '%.'+str(self._digits)+'f'
        value = self.ctl.GetValue()
        if value == "":
            value = "0.0"
        value = float(value)
        upd_value = self._formatstring % value
        upd_value = upd_value.replace(".", self._decimal)
        self.ctl.SetValue(str(upd_value))

    def SetLimited(self, value):
        self._limited = value

    def SetRange(self, min, max):
        self._min = float(min)
        self._max = float(max)

    def GetRange(self):
        return self._min, self._max

    def IsLimited(self):
        return self._limited

    def IsEnabled(self):
        return wx.Control.IsEnabled(self)

    def Enable(self, value):
        if value and self.IsEnabled():  # If value = current state do nothing
            return
        if not value and not self.IsEnabled():
            return
        wx.Control.Enable(self, value)
        self.Update()
        if value:
            # Enable via callafter in case someone has been scrolling away on the disabled control
            wx.CallAfter(self.OnReset)
        else:
            # Disable - Freeze the controls Value and change bitmap
            self._frozen_value = self.ctl.GetValue()
            self._img = self._imgdisabled
            if PY2:
                self.spinner.SetBitmap(wx.BitmapFromImage(self._img))
            else:
                self.spinner.SetBitmap(wx.Bitmap(self._img))

    def OnReset(self):
        # Reset the control to the state it was in when it was Disabled
        self.ctl.SetValue(self._frozen_value)
        self.SetImage(float(self._frozen_value))

    def IsInRange(self):
        if self._min <= self.GetValue() <= self._max:
            return True
        else:
            return False

    def SetIncrement(self, value):
        self._increment = value

    def GetIncrement(self):
        return self._increment

    def SetFontSize(self, value):
        self._font.SetPointSize(value)
        self.ctl.SetFont(self._font)

    def GetFontSize(self):
        return self._font.GetPointSize()

    # Spin image clicked (Top half = Up | Bottom half = Down)
    def OnSpin(self, event):
        H = self.ctl.GetSize()[1] / 2
        pos = event.GetY()
        if pos < H:
            self.OnScroll(None, self._increment)
        else:
            self.OnScroll(None, -self._increment)

    # Keyboard input, test for Arrow Up / Arrow down
    def OnChar(self, event):
        obj = event.GetEventObject()
        pos = obj.GetInsertionPoint()
        left_selected, right_selected = obj.GetSelection()
        curr_text = obj.GetValue()
        curr_text = curr_text.replace(self._decimal, ".")
        key = event.GetUnicodeKey()
        if key == wx.WXK_NONE:
            key = event.GetKeyCode()

        # Test for and Swap out numeric pad keys for standard 0-9 values
        if key == wx.WXK_NUMPAD0:
            key = 48
        elif key == wx.WXK_NUMPAD1:
            key = 49
        elif key == wx.WXK_NUMPAD2:
            key = 50
        elif key == wx.WXK_NUMPAD3:
            key = 51
        elif key == wx.WXK_NUMPAD4:
            key = 52
        elif key == wx.WXK_NUMPAD5:
            key = 53
        elif key == wx.WXK_NUMPAD6:
            key = 54
        elif key == wx.WXK_NUMPAD7:
            key = 55
        elif key == wx.WXK_NUMPAD8:
            key = 56
        elif key == wx.WXK_NUMPAD9:
            key = 57
        else:
            pass

        # Test for position keys
        if key == wx.WXK_UP:
            self.OnScroll(None, self._increment)
        elif key == wx.WXK_DOWN:
            self.OnScroll(None, -self._increment)
        elif key == wx.WXK_LEFT:
            event.Skip()
        elif key == wx.WXK_RIGHT:
            event.Skip()
        elif key == wx.WXK_BACK:
            event.Skip()
        elif key == wx.WXK_DELETE:
            event.Skip()
        elif key == wx.WXK_RETURN:
            new_text = curr_text
            if new_text == "" or new_text == "." or new_text == "-":
                new_text = self._formatstring % self._min
            # Test for beyond range and limited display
            if float(new_text) > self._max and self._limited:
                new_text = self._formatstring % self._max

            if float(new_text) < self._min and self._limited:
                new_text = self._formatstring % self._min

            new_text = self._formatstring % float(new_text)
            disp_text = new_text.replace(".", self._decimal)

            self.ctl.SetValue(disp_text)
            if len(new_text) > 0:  # Avoid Return on empty text
                event = SpinEvent(mfsEVT_MINISPINCTRL, self.GetId(), float(new_text))
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
                self.SetImage(float(new_text))
            event.Skip()

        # Test for Minus character at first position
        elif chr(key) == "-" and pos == 0:
            new_text = self._formatstring % self._min
            event.Skip()

        # Test for decimal divider character
        elif chr(key) == self._decimal and curr_text.count(self._decimal) == 0:
            event.Skip()

        # Test for Valid numeric input
        elif chr(key).isdigit():
            # Test for nothing in curr_text
            # if len(curr_text) < 1:
            #     check_text = "0.0"
            # else:
            #     check_text = curr_text

            # create replacement text comprising old text and the new character
            # allowing for old text partially or wholely selected for replacement
            # and/or for the insertion point being changed by the keys
            # Delete, Backspace and Left, Right arrow keys
            selected = right_selected - left_selected
            # Nothing is selected insert key
            if selected == 0:
                new_text = curr_text[:pos] + chr(key) + curr_text[pos:]

            # Otherwise replace selected with key
            else:
                new_text = curr_text[:left_selected]+chr(key)+curr_text[right_selected:]

            # Test for beyond range and limited display
            if float(new_text) > self._max and self._limited:
                new_text = self._formatstring % self._max
            if float(new_text) < self._min and self._limited:
                new_text = self._formatstring % self._min

            disp_text = new_text.replace(".", self._decimal)

            self.ctl.SetValue(disp_text)
            if selected == 0:
                self.ctl.SetInsertionPoint(pos+1)
            else:
                self.ctl.SetInsertionPoint(left_selected+1)
            event = SpinEvent(mfsEVT_MINISPINCTRL, self.GetId(), float(new_text))
            event.SetEventObject(self)
            self.GetEventHandler().ProcessEvent(event)
            self.SetImage(float(new_text))
            event.Skip()
        else:
            pass

    # Mouse scroll: check rotation for direction
    # Check for non event override value in spin
    def OnScroll(self, event=None, spin=0):
        value = self.ctl.GetValue()
        value = value.replace(self._decimal, ".")
        if value == "":
            value = "0.0"
        value = float(value)
        _increment = self._increment
        if wx.GetKeyState(wx.WXK_CONTROL):
            _increment *= 10
            spin *= 10
        if event:
            if event.GetWheelRotation() > 0:
                rotation = _increment
            else:
                rotation = -_increment
        else:
            rotation = spin

        adj=True
        # All values are added, because negative rotation or spin are already negative values
        if rotation > 0:
            if round(value + rotation, self._digits) <= self._max and self._limited is True:
                value += rotation
            elif self._limited is False:
                value += rotation
            else:
                adj=False
            value = round(value, self._digits)

            # fire SpinUp event if the adjustment value is not zero
            if adj:
                upd_value = self._formatstring % value
                upd_value = upd_value.replace(".", self._decimal)
                self.ctl.SetValue(str(upd_value))
                event = SpinEvent(mfsEVT_MINISPINUP, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
                event = SpinEvent(mfsEVT_MINISPINCTRL, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)

        elif rotation < 0:
            if round(value + rotation, self._digits) >= self._min and self._limited is True:
                value += rotation
            elif self._limited is False:
                value += rotation
            else:
                adj=False
            value = round(value, self._digits)

            # fire SpinDown event if the adjustment value is not zero
            if adj:
                upd_value = self._formatstring % value
                upd_value = upd_value.replace(".", self._decimal)
                self.ctl.SetValue(str(upd_value))
                event = SpinEvent(mfsEVT_MINISPINDOWN, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
                event = SpinEvent(mfsEVT_MINISPINCTRL, self.GetId(), value)
                event.SetEventObject(self)
                self.GetEventHandler().ProcessEvent(event)
        else:
            # No rotation
            pass

        self.SetImage(value)

    def SetImage(self, value):
        # Set appropriate image
        clr = self.parent.GetBackgroundColour()
        clr = self.ImageColour(clr)
        if clr == "white":
            if value <= self._min:
                self._img = self._imgupw
            elif value >= self._max:
                self._img = self._imgdownw
            else:
                self._img = self._imgupdownw
        else:
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
