# neumoDVB #
## Troubleshooting ##


In case of problems, the following information may help to understand the problems.

* NeumoDVB's GUI is written python using the wxWidgets library. In case of problems
  with the GUI, the usual problem will be either a crash (wxWidgets) or a python stack trace. Debug and
  error messages in the log file (by default: `/tmp/neumo.log`) may also shed light on the problem.

* The GUI calls into  a number of shared libraries, which implement the DVB receiver, the database code...
  These libraries come in two flavors:  Those named `libxxx.so` do the actual work, whereas those named
  `pyXXX.so`are interface libraries allowing to call the `libxxx.so` libraries from python.

  If NeumoDVB has been installed (`make install`), `libxxx.so` libraries are searched for in `/usr/lib64`,
  whereas `pyXXX.so` libraries are searched for in `/usr/lib64/python3.x/site-packages`. Otherwise, the libraries
  are searched in a the build directory. Specifically the buld location is computed in `gui/util.py`.

  Typical problems include: libraries do not exist or are in the wrong place, confusion between
  multiple incompatible versions of these libraries (e.g., left over from an outdated install).

* NeumoDVB needs several configuration files to actually start. These configuration files are searched for in
  various places:

  * `~/.config/neumodvb`; this directory can be created by the user to override default settings.
  * `/etc/neumodvb`; `make install` places default configuration files there.
  * `neumodvb/config`;  if neumoDVB is run from the build tree, config files are loaded from the
  source code tree (if it still exists and can be found).

  Note that each config file is always loaded from exactly one of the above locations. If you copy
  e.g., `/etc/neumo.xml` to `~/.config/neumodvb`, then only `~/.config/neumodvb` will be read and `/etc/neumo.xml`
  will be ignored.

* NeumoDVB needs  write access to several folders for storing data. These locations are specified in
  the configuration files, and default are sub directories in `~/neumo/`.
  Obviously, problems will occur if any of the locations is not writable or has insufficient disk space.

  * The directory storing the channel, EPG, recordings and stat databases, by default : `~/neumo/db/`.
  These directories needs to be on a fast  file system, e.g., an SSD. If the file system is too slow,
  problems may occur with high-volume EPG streams: channel switching may become slow and EPG data may be
  incomplete. However, neumoDVB should not crash (that would be a bug).

  * The directory storing recordings, by default: `~/neumo/recordings`. Obviously, this needs to
  be on a filesystem with plenty of room as a single recording can be several Gigabytes in size.

  * The locations where live buffers will be stored, by default: `~/neumo/live`. Live buffers store the audio and
  video of the currently viewed service(s) to allow pausing and time shift. The *cannot* be turned off.
  This too needs to  be on a file system with plenty of room.

 It may not be a good idea to keep the live and recording folder in your home directory. Instead you may want
 to store them in a separate filesystem. This way, you can backup them separately from your regular files.

* NeumoDVB logs a log of debug and error messages to its log file. The log file location can be configured
  by editing `neumo.xml`, which contains the path of the actual log file, and also allows turning on or off
  various classes of of debug messages (google for Log4CXX to understand this file). By default, log files
  are stored in `/tmp/`. In case of problems, these files can grow quite big. So if you experience strange
  problems, check that the /tmp file system has sufficient space.
