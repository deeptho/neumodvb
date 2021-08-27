##Identifying and reporting bugs##

neumoDVB uses wxWidgets for its gui code. In turn this is layered on top of Gtk3. Unfortunately,
GTK3 often is the cause of bugs. While we try to work around them as much as possible, some rmin.
If you see error messages on the console starting with  `Gtk-CRITICAL`, this points to Gtk bugs
or incorrect use of Gtk by wxWidgets.
Often the bugs do not lead to user visible errors, but the error messages are still there and there
is no way to hide them. This is quite annoying, but it is not possible to suppress the messages.
Typical cause are erroneous computations of widget sizes.

The only good long term solution is to move away  from GTK3 as the backend, also because the developers are gradually removing useful or even essential features. GTK4 looks quite bad in this respect. There is some hope, e.g.,
the project <https://github.com/thesquash/stlwrt>, which aims to revive Gtk2 as a practical replacement
for GTK3, but even if this succeeds (no released code yet), stil someone will need to integrate this
in wxWidgets. neumoDVB will also need to be changed as it depends on some GTk3 features (css style sheets,
integration of mpv).
