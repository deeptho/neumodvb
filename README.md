# NeumoDVB #

NeumoDVB is a DVB Settop box and dx-program for linux.
Its supports advanced muti-tuner cards based on the stid135 chip, such as TBS-6909x and TBS-6903x
and simpler cards based on stv091x like tbs5927, on tas2101 like tbs5990 and on si2183 like tbs6504 and tbs5580
Some of the features include

 * spectral analysis of satellite bands
 * blindscan (TBS-6909x, TBS-6903x, tbs5927, m88rs6060, and incomplete support for tbs5990)
 * scanning muxes
 * scanning satellite bands by blind tuning
 * viewing and recording programs
 * epg scanning, including SkyUk, Freesat, Movistar, Viasat Nordic
 * watching encrypted streams using oscam
 * tuning to multi-stream and T2MI streams
 * controlling positioners
 * IQ constellation samples (on most cards)

To install and use neumoDVB, please read the instructions below. Many users don't bother and
then start wasting other people's time by asking questions answered in the documentation. Needless
to say, this demotivates developers.

neumoDVB cna be installed in two different ways

* Compile it yourself. This enables easy upgrading when bugs are fixed, but requires
  some basic knowledge on compiling. A good starting point is to read the instructions
  before trying random things and then expecting users of fora to fix the problems.

* On supported distributions (currently: fedora 39) and architectures (currently: x86_64



## [Changes](docs/changes.md) ##

## [Compilation/Installation from github](docs/INSTALL.md) ##

## [Installation from debian and rpm packages](docs/packages.md) ##

## [Troubleshooting](docs/troubleshooting.md) ##

## [Reporting bugs](docs/bugs.md) ##


## Getting Started with neumoDVB ##

Start the program from the Linux command line as follows if you wish to run it from the build directory:

```~/neumodvb/gui/neumodvb.py```

If instead you have installed it, then run it as `/usr/bin/neumodvb`. If all goes well a mostly empty, dark
window will appear and no errors should appear on the console. Some debug messages may appear in `/tmp/neumo.log`.

Now follow the first steps to configure neumoDVB

* Start by visiting the `frontend list' screen. This can be selected from the `Lists' menu or by typing
  the key combination `Shift-Ctrl-F`. You can find out more about this screen elsewhere in the documentation,
  and most likely nothing needs to be configured on this screen. However, check that your DVB card is
  listed. Depending on the type of card, one to eight adapters should appear for it. They should be
  shown with a white background (red lines indicate problems).

  On this screen you can also check the features supported by the card(s). For instance, if the blkindscan
  entry is unchecked, you have not installed the blindscan drivers. You should still be able to use
  some of the functionality of neumoDVB, but its main features like spectrum scan, constellation display
  and blindscan will not work.

* In case you have a satellite receiver card in the system, now visit the `LNB list` screen (press
  `Ctrl-Shift-L` or use the Lists menu). This screen will initially be empty. In order to use the
  satellite functions of neumoDVB, you need to define at least one LNB, by pressing `Ctrl-N` (or
  `New` from the `Edit` menu). This will turn on `edit mode` and also will insert a row of (empty/blank) data

  In the cell below `Card/RF in` select the card to which the LNB is connected, and the `RF input` (connector
  on the card) to which the LNB is connected.

  In case the LNB is connected via LNB switches, enter values in the `diseqc10` and `diseqc11` column. The
  values need to be between 0 and 3 for diseqc10 (committed switch) and between 0 and 15 for diseqc11
  (uncommitted switch). In case both type of switches are present, neumoDVB  assumes that the uncommitted
  switch is connected directly to the receiver, the committed switch is connected directly to the LNB. This
  can be changed by editing the `tune string` to become 'CUP`.

  In case you don't use diseqc10 or diseqc11, leave the corresponding number at is default value (-1).

  Other entries on this screen are readonly, or probably do not need to be configured for the simplest
  usage. See the documentation below for more information,.

The next step is to define some valid `muxes', i.e., to define tuning information.
This can be done in multiple ways.

* For a satellite cards, the most powerful and automated method is
  to start from a spectrum scan: select the LNB you have defined and select `Spectrum` from the `Control`
  menu. On this screen, press the 'Get Spectrum' button. After a while, a graph will appear (if your
  card supports spectrum scan). Wait until the `Get Spectrum` button's background turns white. Then
  press `Blindcan all`and wait until the process finishes. This will take at least 10 minutes, and maybe
  much longer, depending on the card

* For all cards, you can also enter tuning data manually. For cards supporting blindscan, this can be as
  simple as entering approximate values frequency for frequency and symbol rate (only DVBS and DVBC),
  polarisation (only DVBS). For other cards more info may need to be entered.

  In the `Lists` menu select `DVBC`, `CVBT` or `DVBS`. Then use `Edit - New` to create a new entry and
  enter at least frequency, symbol rate and polarisation. In case your card supports blindscan, set
  `System` to `Auto` to enable blindscan, otherwise select the correct delivery system and modulation.
  In some cases, you also may need to enter valid values for `Pls Code` and `Pls Mode`, `Stream` and
  `t2mi pid`.

  At this point, select `Control - Scan` to scan the mux. If all goes well, services will be found
  and will appear in the services list, typically after at least 30 seconds. It is possible that
  other muxes are discovered. If this is the case, then they will be scanned as well.

The last step is to check the `services` list. If all went well, services will appear there. You can
tune one of them by selecting `Control - Tune` and then view  the service by `Control - Live Screen`.
The `L` key hides the service list which is also shown on this screen. Pressing it a second time
will bring back the GUI. Pressing `Ctrl-F` will enter full screen mode

## Other Documentation ##

### [Changing options](docs/options.md) ###

### [Frontend configuration](docs/frontends.md) ###

### [LNB configuration](docs/lnbs.md) ###

### [Scanning muxes](docs/muxes.md) ####

### [Scanning satellite bands](docs/satellites.md) ####

### [Automating scanning](docs/commands.md) ###

### [Managing channels](docs/channel_management.md) ###

### [Status  list](docs/status_list.md) ###

### [Viewing and recording TV](docs/viewing.md) ###

### [Streaming services and muxes](docs/streaming.md) ###

### [Spectrum analysis](docs/spectrum.md) ###

### [Positioner control](docs/positioner.md) ###

### [Dish list](docs/dishes.md) ####

### [Signal history](docs/signal_history.md) ###

### [Filtering lists](docs/lists.md) ###

### [Using external programs with neumoDVB](docs/external.md) ###


### [GUI Design in neumoDVB](docs/gui_design.md) ###
