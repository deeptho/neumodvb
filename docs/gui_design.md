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

* neumoDVB is not perfect and does not fully comply with the design guidelines below. In some
  cases this because of lack of time, in other cases because of limitations of the toolkit (e.g.,
  scrolling with the  mouse wheel in the list screens is quite sluggish).

The following text is meant to provide some insight in the underlying choices for neumoDVB's GUI.
It describes mostly how it should be, and not so much how it actually is in its current form.


If you contribute changes, please try to adhere to the general design philosophy when possible.

The following design guidelines are essential in the view of the developers. They completely
go against "modern" practices, which tend to violate essential guidelines for GUI design, which were
based on careful thought about usability and feature discoverability.
Those were the days when software was still improving instead of deteriorating:

* GUI items should be discoverable easily. Menu bars are the solution for this, but modern GUIs tend to prefer hidden
  menus or even no menus at all and rely on accidental discovery of features. At best they provide some mysterious
  icons, which correspond to features.  Icons as in modern GUIs can be visually nice, but often do not convey
  the meaning of a feature well: what do they actually do?  If a window can be scrolled, then the
  presence of  a scroll bar should reveal this. Instead in modern GUIs, the scroll-bar is hidden and when it
  appears it is too thin and thus difficult to operate. We don't want that.

* Actionable GUI elements should be clearly distinguished from text and decorations and should follow clear
  conventions. For instance, "grayed out" used to mean that some command is not available in the current
  context. Modern GUIs tend to prefer the opposite: no distinction between decorations and actionable items,
  grey text as the norm, tiny fonts, huge empty spaces instead of useful information, no scroll bars to indicate that
  more information is available...

* Buttons should follow a clear convention: If the text of a button says "on", does that mean that
  the button is currently on, or that you have to press the button to turn it on? In neumoDVB on states
  are indicated by a darker colored background (like a pressed in button).  Not entirely satisfying,
  but currently the best compromise.

* Readability is important: that means big enough font sizes, well-contrasting colours. Modern GUIs don't
  seem to care much and prefer low contrast interfaces in shades of gray, minuscule fonts... Many people
  suffer from color blindness. Therefore - whenever difference in color matters - we try to pick colours
  that are well distinguishable even for color blind people (Hence the preference for blue and red instead of
  green and red).

Unfortunately, this gradual degradation in modern GUI design is reflected in toolkits (e.g., essential
functionality removed in GTK3 compared to GTK2, more will be dropped in GTK4) and working around it is
not easy. Some poor design choices in various toolkits also mean that it is very difficult or
even impossible to ensure that font sizes and colours are consistent on different screens.  For instance,
even discovering the current screen resolution is a nightmare, with some software deliberately reporting
wrong values for compatibility with software bugs, other software having crucial functions not implemented
and therefore also reporting wrong values. Fortunately, the `wxWidgets` toolkit used in neumoDVB gets it
right in many cases, and tries to do the right thing.

neumoDVB tries to work around the remaining problems where possible, but still has some problems in this area.

If you experience GUI problems with neumoDVB (e.g., improper font sizes), there are several steps you
can take:

* Change some parts of your operating system's GUI configuration. For instance, add an Xorg config file
  with the proper display resolution. In my case, I have added `/etc/X11/xorg.conf.d/40-dpi.conf`
  with the following content:
```
# Will set your DPI to 141x141 if the screen is 1080p
Section "Monitor"
    Identifier   "<default monitor>"
    DisplaySize  310 170    # In millimeters
EndSection
```
 * Instead of gnome, try a GUI system such as Mate, which adopts a similar design philosophy as
   neumoDVB.

 * Try experimenting with various themes and tweaking tools. Note that the default themes are so bad and buggy
   that neumoDVB has to rely on its own tweaked theme in `config/share/themes/Neumo/`.

 * Try experimenting with the settings in `neumodvb.css`. CSS support is incomplete in GTK3,
   and wxWidgets sometimes makes changes on top. So success is not guaranteed.

 * (Unfortunately) some color and font settings are hard coded in the python files. Fortunately,
   making changes in Python is easy. If you find settings that work better, report them in a ticket.

 * Report the problem as a ticket on GitHub, but then please explain clearly what is the problem
   and all the (non-working) solutions you have tried.
