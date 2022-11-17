# NeumoDVB #

NeumoDVB is a DVB Settop box and dx-program for linux.
Its supports advanced muti-tuner cards based on the stid135 chip, such as TBS-6909x and TBS-6903x
and simpler cards based on stv091x like tbs5927, on tas2101 like tbs5990 and on si2183 like tbs6504
Some of the features include

 * spectral analysis of satellite bands
 * blindscan (TBS-6909x, TBS-6903x, tbs5927, and incomplete support for tbs5990)
 * scanning muxes
 * viewing and recording programs
 * epg scanning, including SkyUk, Freesat, Movistar, Viasat Nordic
 * watching encrypted streams using oscam
 * tuning to multi-stream and T2MI streams
 * controlling positioners
Currently this software is fully functional, but is of alpha quality. It has seen
very little testing.

## [Changes](docs/changes.md) ##

## [Installation](docs/INSTALL.md) ##

## Troubleshooting (docs/troubleshooting.md) ##

## [Reporting bugs](docs/bugs.md) ##


## Starting neumoDVB ##

Start the program from the Linux command line as follows if you wish to run it from the build directory:

```~/neumodvb/gui/neumodvb.py```

If instead you have installed it, then run it as `/usr/bin/neumodvb`. If all goes well a mostly empty, dark
window will appear and no errors should appear on the console. Some debug messages may appear in `/tmp/neumo.log`.

See the sections below to learn how to perform initial configuration of neumoDVB



## [Frontend configuration](docs/frontends.md) ##

## [LNB configuration](docs/lnbs.md) ##

## [Scanning muxes](docs/muxes.md) ##

## [Managing channels](docs/channel_management.md) ##

## [Viewing and recording TV](docs/viewing.md) ##

## [Spectrum analysis](docs/spectrum.md) ##

## [Positioner control](docs/positioner.md) ##

## [Filtering lists](docs/lists.md) ##


## [GUI Design in neumoDVB](docs/gui_design.md) ##
