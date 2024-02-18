# neumoDVB #

## Interfacing with external programs ##

When neumoDVB is tuned to services or muxes or is streaming them, DVB data can be passed to external
programs in multiple ways.

The easiest is to create and start a stream, e.g., to port 9999 on some computer, either the
one on which neumoDVB is running (use 127.0.0.1 as the destination) or some other computer (use its
host name or IP adress).


While neumoDVB has tuned to a mux, data can also be read out of the linux adapter in the following way

* First identify the frontend in use for a specific mux on the `frontendslist` screen. The `adapter` column
will have entries like `A1 TBS 6909X`, indicating that the adapter number: 1 in this example.
* On the computer running neumoDVB, the command `dvbsnoop -adapter 1 -s ts -tsraw -b` will send a complete transport
stream to standard output, which allows it to be saved or processed:
  * **Saving**:  dvbsnoop -adapter 1 -s ts -tsraw -b  > /tmp/test.ts
  * **Analyzing** with dvbsnoop: e.g., `dvbsnoop -adapter 1 -s ts -tssubdecode -ph 0 0x0`
  * **Analyzing** with tsduck: e.g., `dvbsnoop -adapter 1 -s ts -tsraw -b | tsp -P until --seconds 15 -P analyze -O drop`

While neumoDVB is streaming a mux or service to port 9999 on some computer, data from the stream can
be processed on that computer as follows:

* **Saving**:  `tsp -I ip 9999   > /tmp/test.ts`
* **Piping** to some other programs standard input:  `tsp -I ip 9999 | dvbsnoop -if - -s ts  -tssubdecode -ph 0 0x0`


### Viewing services ###
Such a stream is an UDP stream. It can be viewed with various programs
For example:

* vlc udp://@:9999
* mpv udp://@:9999

In case a complete transport stream is being streamed, you will need to use the menu in `vlc` to select
a service for viewing. `mpv` instead will pick one itself.

### Analyzing streams ###
`tsduck`  and `dvbsnoop` are good programs to analyse streams.

`tsduck` can take transport streams as input via UDP, and then manipulate them in various ways, such
as extracting T2MI substreams (but neumoDVB can do that as well), extracting specific services, inserting
EPG, or analyzing the stream in various ways. `dvbsnoop` cannot directly read UDP streams, but yo

Some analysis examples:

* Analyzing using **tsduck**: `tsp -I ip 9999 -P until --seconds 15 -P analyze -O drop` Analyzes the stream during 15 seconds, displaying PIDs, bit rates, services ...
* Analyzing the PAT table using **dvbsnoop**: `tsp -I ip 9999 | dvbsnoop -if - -s ts  -tssubdecode -ph 0 0x0


### Analyzing and listening to DAB ###
The following examples assume that you first create and start a stream for `11013V-11 at 5.0W` to port 9999, which contains
a DAB substream. Then you can

 * **Inspect** the DAB channels:  tsp -I ip 9999 | ts2na -p 1000 -s 0 | na2ni | ni2out --list
 * **Listen** to embedded DAB using **dablin-gtk**: tsp -I ip 9999 | ts2na -p 1000 -s 0 | na2ni | dablin_gtk


The following examples assume that you first create and start a stream for `11426H at 28.2E` to port 9999, which contains
a DAB substream. Then you can

 * **Inspect** the DAB channels:  tsp -I ip 9999 | ts2na -p 1063 -s 12 | na2ni | ni2out --list
 * **Listen** to embedded DAB using **dablin-gtk**: tsp -I ip 9999 | -p 1063 -s 12 | na2ni | dablin_gtk
