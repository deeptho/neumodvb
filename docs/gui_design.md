# neumoDVB #
## GUI design in neumoDVB ##
neumodDVB comes with two types of screens: the list screens are meant for service scanning, channel editing
and so forth. They are meant for use as other programs and roughly follow the same UI themes.
On the other hand, some other screens are meant for viewing in "set-top box mode".

As editing/configuring  and viewing are very different tasks, they require different GUI choices:

* Viewing screens have a dark background and large font sizes, providing maximum readability, and no bright
  backgrounds, which is best for late night viewing

* Editing/configuration/... screens are meant to be used closed up on a PC screen and therefore best follow
  your preferred GUI theme.

* As you may not be able to use different devices for both tasks, minimal font size and readability
  is important in both cases

* As you may want to control neumoDVB using a remote control rather than a keyboard and mouse, functionality
  essential for viewing mode is accessible using shortcuts without using the mouse. The short cuts can be
  easily mapped onto remote control buttons (but this functionality is not provided by neumoDVB)

*neumoDVB is in no way perfect and does not even fully comply with the design guidelines below.* In some
cases this because of lack of time, in other cases because of limitations of the toolkit (e.g., scrolling
with the  mouse wheel in the list screens is quite sluggish).

The following text is meant to provide some insight in the underlying choices for neumoDVB's GUI.
It describes mostly how it should be, and not so much how it actually is in its current form.
Comments Filter:

If you contribute changes, please try to adhere to the general design philosophy when possible.

The following design guidelines are essential in the view of the developers. They completely
go against "modern", which tend to violate essential guidelines for GUI design, as they were
proposed many years ago, when software was still improving instead of deteriorating:

* GUI items should be discoverable easily. Menu bars are the solution, but modern GUIs tend to prefer hidden
menus or even no menus at all. At best they provide some icons instead. Icons as in modern GUIs can be visually
nice, but also  very mysterious: what do they actually do?
If a window can be scrolled, then the presence of a scroll bar should reveal this.
Instead in modern GUIs, the scroll-bar is hidden and when it appears it is minuscule and thus difficult to operate.
We don't want that.


* Actionable GUI elements should be clearly distinguished form text and decorations and should follow clear
conventions. For instance, "greyed out" used to mean that some command is not available in the current context.
Modern GUIs tend to prefer the opposite: no distinction between decorations and actionable items, grey text
as the norm...

* Buttons should follow a clear convention: If the text of a button says "on", does that mean that
  the button is currently on, or that you have to press the button to turn it on? In neumoDVB on states
  are indicated by a darker colored background (like a pressed in button).  Not entirely satisfying,
  but currently the best compromise.

* Readability is important: that means big enough font sizes, well-contrasting colours. Modern GUIs don't
  seem to care much and prefer low contrast interfaces in shades of gray, minuscule fonts... Many people
  suffer from color blindness. Therefore - whenever difference in color matters - we try to pick colours
  that are distinguishable even for color blind people. For example, we use blue and red to indicate
  "complete" and "incomplete" SI-scanning states in the positioner dialog rather than the more logical green
  and red, because green and red cannot be distinguished well by many color blind. In case where the loss
  of color is less critical, red and green may still be used.

Unfortunately, this gradual degradation in modern GUI design is reflected in toolkits
(e.g., lots of dropped essential functionality in GTK3 compared to GTK2, eve more to come in GTK4) and working
around it is not easy. Some poor design choices in various toolkits also mean that it is very difficult or
even impossible to ensure that font sizes and colours are consistent on different screens.  For instance,
even discovering the current screen resolution is a nightmare, with some software deliberately reporting wrong
values, other software having crucial functions not implemented and therefore also reporting wrong values ..

This means that even the demos provided in wxWidgets have a mixture of minuscule (sometimes unreadable)
and correctly sized fonts. The underlying cause is the poor handling (not by wxWidgets but by underlying
software layers) of "high-DPI" displays. For instance X-windows reports such displays to have a resolution
of 96dpi, thus contradicting the results obtained through other means.

neumoDVB tries to work around these problems where possible, but still has some problems in this area.

If you experience GUI problems with neumoDVB (e.g., improper font sizes), there are several steps you
can take:
* Change some parts of your operating system's GUI configuration. For instance, add an Xorg config file
  with the proper display resolution. In my case, I have added `/etc/X11/xorg.conf.d/40-dpi.conf`
  with the following content:

  ```
\# Will set your DPI to 141x141 if the screen is 1080p
Section "Monitor"
    Identifier   "<default monitor>"
    DisplaySize  310 170    # In millimeters
EndSection
  ```
 * Instead of gnome, try a GUI system such as Mate, which adopts a similar design philosophy as
   neumoDVB.

 * Try experimenting with various themes and tweaking tools

 * Try experimenting with the settings in `neumodvb.css`. CSS support is incomplete in GTK3,
   and wxWidgets sometimes makes changes on top. So success is not guaranteed

 * (Unfortunately) some color and font settings are hard coded in the python files. Fortunately,
   making changes is easy. If you find settings that work better, report them in a ticket.

 * Report the problem as a ticket on GitHub, but then please explain clearly what is the problem
   and all the (non-working) solutions you have tried.
