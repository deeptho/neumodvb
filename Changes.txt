Changes in version neumodvb-0.9
-------------------
SI-processing related
-Incorrect activation of PMT parsers
-Avoid showing incorrect SI data in tuned mux panel
-Faster detection of stable pat, resulting in faster SI scanning
-Better handle invalid SI information on 7.0E
-Handle SI-sections with different version numbers
-Also parse PMTs during regular SI scanning
-Avoid needless frequent epg scanning on services without epg

Spectrum and blind scanning
-When adapter is not available, show error message and gracefully end blindscan
-Properly handle multi-stream with the streams having the same network and ts id.
 These muxes would overwrite each other while scanning. Happens, e.g., on 14W.
-Avoid python back-traces when no peaks found in spectrum
-Fixed bug: when LNB changes due to clicking on a mux in the spectrum panel,
 the LNB select combobox does not show the newly selected LNB, which will be used for tuning
 but uses it anyway. This causes blind tunes to happen on unexpected LNBs
-Improvement: when the user loads a spectrum from file in spectrum scan, then selects a
 mux and blindscans, use the current LNB whenever possible, except if this one cannot
 tune the mux (wrong sat or wrong type of LNB). Only in that case, switch to the LNB originally used
 to capture the spectrum.
-Include not only hours and minutes but also seconds in names of files storing spectrum data. This
 solves the problem that spectrum acquisitions started within one minute would overwrite each other.
-The code now asks for a 50kHz resolution spectrum, leading to slightly slower scans in stid135.
 This detects more low symbol rate muxes.
-Fixed bug: DVBS2-QPSK streams incorrectly entered as DVBS1 streams in database
-Fixed bug: incorrect detection of roll-off parameters.
-More useful packet or bit error rate estimation. The new code measures errors before FEC/Viterbi/LDPC
 correction. This requires blindscan drivers with version > release.0.9.


Installation
-Prevent some files from installing

GUI
-Show disabled menu items as grayed out
-No longer use the single letters 'E' and 'C' as shortcuts, except in live mode
-Fixed bug that causes channels to scroll offscreen
-Fix bug that prevented proper navigation with arrow keys on EPG screen
-Fixed bug: onscreen display of SNR not working in radio mode
-Properly handle cases with unavailable SNR
-Fixed some layout bugs
-Add timing lock indicator (replaces pretty useless signal indicator)
-Immediately activate changes entered by user on "frontend list" screen. This is important
 when user enables/disables a frontend
-Add channel screenshot command
-Show more stream numbers in the positioner dialog and in the spectrum dialog on muxes with
 large number of streams

Compilation and internals
-Switch to clang20 by default except on "ancient Ubuntu" aka "Ubuntu LTS"
-Fix double close of cursors and incorrect freeing, leading to assertion failures
-Add command for building fedora rpms. Main problem to start really using this is a dependency
 on mpl-scatter-plot, which cannot be installed as an rpm
-Fix incorrect recompiling of pre-compiled headers with cmake > 3.19
-Removed dependency on setproctitle and made it less error-prone
-Add script for testing peak finding algorithm

Diseqc
-Add more time between sending commands to cascaded swicthes, solving some erroneous switching


Changes in version neumodvb-0.8
-------------------

Installation related:

-Improved installation instructions for fedora 36 and Ubuntu 20.04 LTS.


Spectrum acquisition and blindscan related:

-Added Export command, which can export service, mux, ... lists.

-Allow selecting multiple muxes for scanning, instead of selecting them one at a time and pressing `Ctrl-S` each time

-Allow blind scanning non detected peaks by selecting a region of spectrum graph.

-Improved positioning of frequency/symbol rate annotations on the spectrum graph. The labels are better
 readable and use less vertical space, preventing the need for vertical resizing in many cases.

-Added predefined pls code for 33.0E (BulgariaSat).

-Avoid python exception when spectrum scan results in zero detected peaks

-Avoid confusion between embedded and non-embedded streams on same frequency leading to missing or overwritten
 muxes in the database

-Bug when multiple subscriptions use the same mux, but one of them uses a t2mi substream;

-Prevent scan hanging for ever on some muxes

-Ensure that scan status is properly handled with embedded streams to prevent scans that never finish;

-Data race when starting embedded_stream_reader, causing tsp sub-process to be launched multiple times.

-Incorrect scan result reported on t2mi stream because of incorrect lock detection

-Autodiscover t2mi streams on 40.0E (workaround for detecting incompliant t2is)

-Handle empty NIT_ACTUAL in embedded stream on 40.0E 3992L T2MI. This
 causes the ts_id and original_network_id from the embedded t2mi SDT
 not to be taken into account.

-When tuning to t2mi mux from database record, preserve original_network_id
 and ts_id in the embedded si

-Allow resetting LNB LOF offset


EPG and recording related:

-Fixed layout bugs in grid epg.

-Improved handling of 'anonymous' recordings. These are recordings created by the user by entering start
 and end time instead of recording an epg record. In the new code, these anonymous recordings will be kept
 as instructed by the user, but also secondary recordings will be created for each epg record matching the
 time window. This is convenient for services with only now/next epg, which may arrive after scheduling the
 anonymous recording. Anonymous recordings will be shown on the EPG screen, but will be overlayed with
 epg records, should these exist. Canceling a secondary epg recording also cancels the anonymous recording.


Playback and viewing:

-During live viewing and recordings playback, replace the original PMT with a minimal PMT containing only
 the video stream, and current audio and subtitle stream. This should prevent playback problems of services
 on 30.0W which share the same PMT. mpv tends to get confused about the audio stream selection in this case.
 This should also fix the missing sound on Sport Italia HD (5.0W).

-Incorrect indication of live/timeshift/rec status in live overlay

-Hide snr and strength when playing back a recording.

-Reduce buffering in tsp sub process to avoid delay when live viewing t2mi services


Various bugs fixed:

-anonymous recordings 60 times too long.

-Cannot activate edit mode in lnb network list





Changes in version neumodvb-0.7
-------------------
Positioner related

-More consistent layout of positioner dialog. Disable some functions when unusable

-Fixes for diseqc12, e.g., new diseqc12 value not stored when user updates it

-Positioner bug fixes: send diseqc switch commands before sending any positioner command;
 report better error messages in GUI; properly handle continuous motion.

-Send diseqc switch commands before spectrum scan to avoid scanning the wrong lnb

-Prevent dish movement during mux scan

-Satellite is now only shown as "confirmed" (no question mark) if the position was actually
 found in the NIT table.

-Positioner dialog: show SNR and constellation even when tuner is not locked

-Avoid interference between positioner dialog and mux scan

-Disallow starting positioner dialog from some screens, when context does not allow guessing which
 lnb the user wants to use

-In positioner dialog allow negative SNRs. Yes, there are muxes which lock with a negative SNR!

-Documentation of positioner commands

-Allow rotors which can rotate by more than 65 degrees. The old code used to cause a goto south
 in this case.

-------------------
Scanning related:

-Mux tables now show the source of the mux information. For instance if the frequency
 was found in the NIT table or rather from the tuner and if sat position was guessed or
 found in NIT

-Improved handling of bad data while scanning muxes:
   0 frequency (7.3W 11411H) or other nonsensical values;
   contradictory information in muxes from 0.8W and 1.0W;
   invalid satellite positions;
   on 42.0E, muxes are being reported as from 42.0W;
   some muxes report AUTO for modulation
   incorrect polarisation reported in C-band reuters mux on 22W;
   incorrect polarisation reported on 7.3W 11411H
   22.0E 4181V and other muxes which claim QAM_AUTO in NIT

-Fixed incorrectly reported roll-offs (may require driver update)

-Correctly handle some cases where mux or blindscan never finish

-Remove "scan-in-progress" data from database at startup to avoid an old scan from
 restarting when neumoDVB is restarted

-Improved handling of muxes reported on nearby satellites (e.g., 0.8W and 1.0W). Avoid creating
 duplicates in this case. The downside is that when scanning e.g., 0.8W the found mux may
 appear in the list for 1.0W and thus confuse the user

-Fixed incorrect display of scan status on mux list when mux scan is in progress

-Fixed bug where incorrect pls_code and pls_mode was retrieved from driver

-Correctly save DVBS2 matype in the database and do not show this value for DVBS

-Fix incorrect lock detection

-When scanning multistreams: retain tuning info when scanning next stream

-Spectrum blindscan now moves faster to the next candidate peak when mux does not contain
 a DVB stream


-------------------
t2mi related:


-Fixed corruption in t2mi streams. This requires a patched tsduck as well.

-Improved handling of non-t2mi DVB-T service information on DVB-S muxes. Avoid entering
 such muxes as DVB-T

-correctly handle duplicate packets in transport streams; tsduck needs to be patched
 to also handle t2mi streams correctly

------------
GUI related

-Handle some corruption in service and mux list

-Various GUI improvements.

-Changed key-binding for activating edit-mode.

-New "status list" screen shows which adapters are active and what they are doing

-Fix some crashes when ending program

-Fix some errors when service list is empty

-Allow user to select a stream_id even when dvb streams reports incorrectly that stream is not multistream

-Correctly update recordings screen when database changes

-Minimum RF level shown on live screen is now lower than before

-Fixed some issues with list filtering


------------
Other

-Fixed some bugs in the code used to look up muxes. For instance, in rare cases a mux with
the wrong polarization was returned

-Fixed bug: lnb offset correction was applied in wrong direction on C band

-Detection of whether dish needs to be moved was sometimes incorrect; database now contains
 the ultimate truth on this.

-Fixes to support Ubuntu 20.04

-Fixed race between closing and then reopening the same frontend, which caused sporadic assertions.

-correctly release lnb in more cases then before: when user closwes window; when spectrum blindscan
 finishes

-various other bugs


=========================
