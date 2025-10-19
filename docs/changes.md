# Changes in neumoDVB #

## Changes in version neumodvb-2.1 ##

### Major new features

* Fast and slow playback, including osd message.
* Reverse play, including osd message.
* Smart jumping during playback: the amount of jumping backward now increases with each key press and
  is then reduced again as soon a the jump direction is changed using the left and right arrow keys.
* Allow reusing recently abandoned live buffer. This allows keeping timeshift data from a previous tune and rewinding that service.
  The main reason for implementing this is, is to prevent loss of all time shift data when accidentally tuning to another
  service: the old data remains available for a few miutes and will become useable as soon as the original service is retuned.

### Gui

* Allow jumping forward and backward in playback, even when servicelist is displayed.
* Stereo loudness normalization.
* Increased subtitle size.
* Display streams in 4/3 format as  16/9. Also detect weird HEVC aspect ratios and force them to 16/9.
* Service information window needlessly small in positioner dialog.
* Activate selected audio language at service start.
* Allow removing ch_order in service_list.
* Experimental fix to remove/reduce hick-ups at start, but some of these hickups may reappear due to
  the new reverse-play code.
* Reduced font size on live screen.
* Experimental change to try to remove artifacts seen by some users.
* Live screen: show blank channel order numbers when user has not set such numbers.
* Live screen: show message when stream fails to decrypt.

### Bug fixes

* Regression: menu from spectrum dialog removed due to commit 388034f16d552373be94ea9.
* Some data incorrectly copied when adding ts-in-ts muxes.
* Gridepg cache can fill up with new services discovered by scanning, resulting in an assertion.
* mmap at wrong offset, causing playback to malfunction.
* Remove debug code causing problems for some users.
* Commands sometimes disabled when gui is hidden in live mode.
* Unwanted showing of coloured border when gui is hidden.
* Some jump forward/backward bugs.
* Record not working when gui is hidden.
* SDT not processed when no nit is present
* Trying to save template mux (not allowed) when retuning too fast in positioner dialog.
* Bug: entering user defined muxes in muxlist causes template muxes to be inserted in database; change tune_src to USER in this case.
* Regression: neumodvb no longer working with DVBAPI, i.e., when proper drivers are not installed.
* scan_status_t::PENDING not set when tuned mux is not yet in database
* Update_stream_ids_from pat sometimes called erroneously.
* Invalid assertion when not using neumodvb drivers.
* Saved subtitle preference not applied at channel playback; Bug: saved preference incorrectly shown in subtitle dialog.
* Stackstring::rotate not working properly.
* Confusion on 28.2E between bloomberg and freesat eit
* Incorrect fixing of this->dbmux from sdt
* Don't log known skipped descriptors.
* Horizontal scroll not working in live epg
* Sometimes wrong config_id picked.
* Incorrect assertion during scan of 0.8W.
* fe_monitor sometimes does not exit due to race. Renamed fe_monitor_t::stop and ::start to pause and unpause.
* playback_mpm reads past num_bytes_safe_to_read, which could cause crash when mpm part is truncated.
* Oncorrect call to wait_for_update causes this call not to wait, resulting in a needless busy loop.
* Incorrect assertion when two non-lockable old muxes overlap.
* Incorrect assertion when tuning two overlapping muxes and both fail.
* Old error messages are prepended to new ones.
* Allow frequency update for muxes without si-data (e.g. gse)
* Regression: message to oscam truncated when pid=0x1fff.
* When filtering for frequency in service list, the frequency limits are narrowed by subsequent sorts, resulting in errors in the displayed services.
* Correctly propagate scan when scanning ts-in-ts muxes.

### Other
* Swap the East and West buttons on the  positioner dialog for installations on the southern hemisphere.
  Proper operation of Usals has not yet been tested in this case.
* Do not save muxes when lock times out.
* Force biss caid when decrypted data is received but there is no valid capmt data.

### Internals

* Simplified code for playback_mpm, introdcing `mpm_cursor_t` to seek by time and byte position.
* Reduce log verbosity.
* Improved setting/remembering of which mpv playback window willl receive GUI commands.
* Remove dependency on sip.
* Support for Ubuntu 24.4.
* Improved assertion.
* Change how textures are managed.
* Update main.yml.
* Refactor pause command.
* scam: add enigma namespace descriptor.
* Remove accidentally left over debug printf.
* Remove unneeded header.





## Changes in version neumodvb-2.0 ##

### Major new features

 * Support for muxes transmitted as a transport stream in transportstream, such as the Abertis muxes on 30.0W.


### OS support, compilation and installation

 * Support for Ubuntu 24.02.
 * Support for Fedora42.
 * cmake: automatically choose the best installed linker.
 * cmake: suppress cmake policy messages.
 * Attempt to fix cmake policy.
 * Update installation instructions.
 * Workaround for PyGridBlocksIterator' object is not iterable problem.
 * Export compile commands for clangd.
 * Remove unused menu items which make wxglade crash.
 * Remove unneeded declaration. Fixes #44.

### Live screen and service viewing
 * Add volumne slider to osd;  rename textbox-panels; add error text_box; Unfinished!
 * Show black screen when channel has no data.
 * Show EPG information in on-screen display.
 * Show onscreen message when service is not working or not active.
 * Live screen: react immediately to toggle_overlay rather than on next timer event.
 * Persist audio volumes in database and visually show  audio volume in the GUI.
 * Avoid confusing display of unknown epg content_code.
 * Improvement: tune channel/service when entering number in live mode.
 * Bug: video window not focused after hiding service list.
 * Bug: timezone not taken into account in OSD.
 * Change accelerator key for full-screen to backslash (to be similar to kodi).
 * Bug: CmdRecord not working in recordings list.
 * Make CmdStop work in recordings list.
 * When a specific mpv player is focused, CmdToggleRecord now records the program played in that player window rather than the one selected in the service list.
 * Bug: incorrect locking causing deadlock in viewing service.
 * Bug: playback mpm cannot find new data at start of new mpm part, resulting in video artifacts.
 * Handle oscam requests with the same filter_no but different demux_no.
 * More detailed scam logging.
 * Ignore oscam filter requests with a NULL pid.
 * scam: index filters by both filter_no and demux_no.
 * scam: handle cases where oscam registers same pid multiple times.
 * scam: use a single demux.
 * scam: allow more than one pmt.
 * Bug: PMT packets generated by playback_mpm contain duplicate packets causing playback errors, even though
   stream saved to disk is correct.
 * Bug: incorrect synchronization between live_mpm and playback_mpm, causing playback glitches when switching
   to a new mpm part.
 * Ensure that playback_mpms never read stream data past a pmt change.


### Recording

 * Change various stream related indices from int32 to int64 to cope with large recordings.
 * Various bugs in record dialog, which prevent editing some fields
 * Record dialog: Allow entering seconds in recording start time
 * Record dialog: Allow entering durations in multiple formats.
 * Increase size of start_time and duration boxes in record_dialog.
 * reclist: colour rows according to recording status.
 * Bug: crash when recording is stopped while tuning is still in progress.
 * Bug: Creating a new recording causes an assertion because record is not committed to database.
 * Bug: recording of live program causes unneeded tune and assertion.

### Tuning muxes

 * Alert user when tuning mux fails.
 * In positioner_dialog, show t2mi services, but only in case blindscan is off.
 * Allow tuning multiple muxes simultaneously.
 * frontend: avoid nullptr dereference when tuning was not started.
 * Bug: Incorrect media_mode for some service types.
 * Regression: signal_info no longer notified.

### Scanning

 * Bug: services not always shown in spectrum_dialog.
 * Bug: t2mi_pid not cleared when ckecking for new streams.
 * Bug: crash when oldfe does not exist.
 * Finalize scan: bug in "save" logic.
 * Force saving pmts in cases without valid nit/sdt.
 * Bug: If sat does not exist, selecting lnb network does not work.
 * Bug: earlier tuned mux and associated services accidentally removed when SI data in other mux contains conflicting information.
 * Loosen some assertions in section processing.
 * Bug: muxes of 28.2E appear on 26.0E. Be more careful when confirming sat_pos.
 * Fix for a mux on 7.0W which reports to be on 7.3E.
 * BUG: 11013V@7.0W contains extra bytes at the end of a descriptor. Make the code more robust to errors like that.
 * Reduce debug log verbosity.
 * BUG: when nit_actual is received but does not contain tuined mux, no mux is saved and SDT processing does not end. This fixes problems on 11013H@7.0W and 12604H@7.0W.
 * Fix for some muxes on 7.0w (12604H and 11013H) that claim falsely to be multistreams.
 * Bug: incorrect cleaning of muxes.
 * Bug: incorrect decision to merge services.
 * Bug: finalize_scan called multiple times.
 * Bug: new si streams are created with some data erroneously copied from other streams.
 * Bug: t2mi mux not created on 12606V due to incorrect handling of missing SDT and NIT.
 * Bug: t2mi mux created when mux is still a template.
 * Bug: add_mux erroneously aborts when called from sdt when nit_actual has been received.
 * Bug: when nit contains no dvbs muxes, mux_id is not created/updated in sdt processing.
 * Bug: filedescriptor closed before use when starting embedded stream.
 * Temporary kludge to detect multiple subtables in PSI. Needs to be replaced by something better which detects subtables based on timeouts.
 * Assertion wen spectrum_key is absent.
 * Fix on_nit_section_completion header.
 * Added payload_type in pes parser.
 * Bug: new services expired because they are not in SDT, while they are in PAT.
 * Avoid assertion when spectrum.annot_for_freq returns None.
 * Bug: SDT services not shown in spectrum or positioner dialog.
 * Bug: incorrect assertion when data is received from a nearby sat with slightly different sat_pos.


### Internals ande debugging

 * Remove newlines in debug messages.
 * Reduce debug verbosity.
 * Incorrect format string in debug message.
 * Updated libfmt to more recent version.
 * Remove pybind11 (use external package).
 * Update log4cxx.
 * Update pybind11.
 * Update minimal cmake version.
 * Assertions to catch filemapper bugs.
 * Assertions to catch erroneous use of fibers.
 * Log mux_id.
 * Remove unneeded code.
 * neumowxsvg: ignore some files.
 * Refactored get_api_type.
 * Incorrect debug message.
 * remove active_service_t::get_current_program_info.
 * Simplify make_client_mpm.
 * Simplify get_live_service.
 * Replace play_mux by subscriber.subscribe_mux; show error messages when it fails.
 * Make stream_filter_t use the full mux, rather than its key.
 * Move process_channel_data from mpm to active_service; moved dvbcsa from mpm to stream_reader.
 * Bug: stable_pat_ not properly set for embedded streams.
 * Bug: pmt parsing code reports unchanged pmts multiple times.
 * Turn of pmt rewriting in playback_mpm as it does not work.
 * Avoid num_generated_bytes_to_send>=0.
 * Remove bytepos_to_read argument from next_data_file.
 * Experimental change: fuzzy matching in epgdb::running_now.

## Changes in version neumodvb-1.9 ##

### Improvements

* Allow sharing of mux tuned in positioner_dialog.
* positioner_dialog: do not add requested isi to isi list when it does not exist in stream.
* Avoid showing fake stream_id -1 in positioner_dialog when user requests non-existing ISI.
* Improved handling of unrelated muxes with same mux_id.
* Distinguish between peak and mux scan in frontendlist.
* New si code to handle multiple ISI streams on the same tuned frontend.
* Remove driver_supports_t2mi argument.
* Use driver support for embedded t2mi streams when available.
* Allow illegal rbsp fields in video streams, which prevent proper time stamping of Ambience TV on 1.9E.
* Silence some log messages.
* Improved debug messages.
* Updated neumodmx.h.
* Silence some gdb warnings (.gdbinit).

### Bug fixes

* Incorrect naming of backup dbs.
* Do not share subscriptions between two template muxes.
* Incorrect assertions on transport streams without content.
* When tuning to good multi-stream, the current mux is not saved if the mux is a template.
* When tuning to a single stream which is really a multistream, si code never times out.
* Polarisation ignored when reserving mux.
* Unneeded tuning in some cases, causing a failure due to an unknown lnb.
* When user requests non-existing ISI, a mux is created in the database.
* Incorrect SI processing when requested ISI does not exist.
* Incorrect active_adapter_t::mux_for_key.
* Information from other mux shown in spectrum dialog when mux does not lock.
* Accidental reuse of mux_id on template muxes.
* Make get_positioner_move_stats return pol as well.
* Nit processing started before pat has been received, resulting incorrect flagging nit as invalid in positioner_dialog.
* Incorrect ts_id and network_id passed to driver and then to positioner_dialog.
* Number of services accidentally cleared in mux records in database.
* Infinite waiting for last_signal_info.
* Incorrect assertions.
* Request_tune can return without yet tuning when positioner is moving. In that case active_adapter concludes erroneously that tune failed.
* Handle races in task_queue_t::stop_running.
* Deadlock in receiver_t::stop.
* Matches_physical_fuzzy: make symmetric.
* Restrict calling active_adapter_t::on_stream_mux_change.
* Clean_overlapping_muxes: detect change from multistream to single stream and vice versa.
* Incorrectly set requested_stream_id.
* PAT_TUNED overwritten.
* Updated_new_dbfe not correctly set when reusing subscription.
* Show error message when starting blindscan without selected spectra, instead of silently failing.
* Improved chdb::matches_physical_fuzzy.
* Null pointer dereference on closing mpv window.
* Incorrect argument order in call leads to incorrect subscription.
* Suppress warning for MHP_IPv4RoutingDescriptorTag
* Assertions to ensure set_rf_input is called.
* Incorrect propagation of scanning when detecting new ISIs.
* scans_in_progress cleared immediately after being filled.
* Do not allow mux reuse if bbframes are not on.
* Fix Python assertion in frontend list.
* Improved removal of fibers when active_service is ending.
* Remove newlines in debug messages.
* Bug in deciding to request bbframes or not.
* Incorrect creation of service_name for pat-only service.
* Make lmdb_hint work again.
* No longer update stream_id from driver mux while processing si.
* T2MI mux causes update_stream_mux_tune_confirmation to be called.
* Allow sorting of lnb network and connection lists.
* Remove unused cable_no fields.
* Incorrectly adding sats to wrong database when user agrees to add them.
* Incorrect highlight colour in cable list


## Changes in version neumodvb-1.8 ##

### New features ###
* Support for unicable lnbs.
* Alow fuzzy filtering by frequency in service list.
* Added cable list.
* Improved layout of frontend list.
* Parse service name in pmt and use it when no sdt name is present.
* Added t2mi to mux grid combos.
* Display error when command is run on disabled lnb instead of silently failing.
* Improve dishlist layout
* Filtering screens based on frequency and symbol_rate now also shows muxes with small deviations.

### Bug fixes ###
* Confusion between chglist_filter_chg and chgmlist_filter_chg.
* Peak_matching sometimes leads to muxes which cannot be tuned, resulting in unscannable peaks.
* Add timeout mechanism to ensure blindscanner always ends.
* Confusion between unique_ptr and shared_ptr.
* Incorrect assertion due to confusion between 0.8W and 1.0W.
* Positioner_move_time computed when positioner did not move.
* Selecting satellite for lnb no longer working correctly.
* Draw_mux no longer working due to matplotlib change.
* Bug in deserialisation of vectors.
* Avoid assertion on corrupt stream due to poor reception.
* Deadlock when replying positive to add sat dialog.
* Adding satellite causes python stack dump.
* Race when stopping live channel.
* Wait longer when new adapters appear due to module reloading, preventing assertion.
* Changing rf_path has no effect in positioner dialog.
* Incorrect snr and such displayed on live screen when more than one service is displayed.
* Handle races between multiple demods attempting to send unicable commands.
* Using the same lnb is treated as a conflict when lnb is on positioner.
* Usals type not updated when selecting different lnb connection in positioner dialog.
* New usals position sometimes not saved when pressing the "save network" button.
* Avoid assertion when user configures lnb beyond a possible band.
* Handle missing satellites when networks already exist.
* Tuning to different mux leads to forgetting dish parameters in positioner_dialog.
* Filter service list by DVB type not working properly.
* Handle Sats with sat_band equal to UNKNOWN more gracefully.
* Positioner commands not working while mux is tuned.
* Incorrect assertion when pressing cancel during starting a scan.
* Incorrect updating of connection names.
* Incorrect subscription information for DVBC.
* Incorrect display of 'Ku' in DVB-C and DVB-T status list.
* Handle cards that have no rf mux.
* Regression: ref mux is not saved any more.
* Issue #4: python errors when filtering sat bands.
* Positioner control (moving dish to usals pos) no longer working.
* Embedded si_streams not cleared after tuning to t2mi and then to non-t2mi mux on same sat

### Internals and compilation ###
* Remove submodule date.
* Add supports.bbframes.
* Prevent fmt from being installed system-wide.
* Updated Ubuntu install instructions.
* Replace some assertions with error messages.
* Add dish name in debug output
* Add rf_path logging.


## Changes in version neumodvb-1.7 ##

* Support cards with multiple stid135 chips such as tbs6916.
* Improved handling of races when tuning multiple muxes on the same tuner in parallel.
  This can lead to some threads tuning before diseqc programming is finished, resulting
  in failed tunes. A typical effect is some muxes failing to scan.
* Bug: When clicking on mux, incorrect question to save network.
* Bug: incorrect format string in OSD.
* Remove unneeded or incorrect assertions.
* Experimental change: when positioner dialog is blind tuning, allow services to reuse adapter.
* Bug: DVBC and DVBT scan not starting.
* Make lnb connection list display more robust against errors in database.
* Create scripts for easier creation of .deb and .rpm packages.
* Bug: Incorrect priority handling when selecting LNBs and other resources.
* Bug: ac3 flag overwritten in pmt parsing (Anixe HD on 19.2E), resulting in no playback
* Bug: incorrect usage of future resulting in incorrect mux count during scanning.
* Bug: positioner_dialog considers some valid NITs invalid.
* Increased font size of the services list on the positioner dialog screen.
* Improved installation instructions for Ubuntu
* Frontend list now also shows config_id.
* Bug: scanning code ignores some valid information provided by driver.




## Changes in version neumodvb-1.6.1 ##

* Added the possibilty to make debian and rpm packages. This is still experimental and not fully automated;
* Updated installation instructions: more accurate dependencies, instructions to install from packages;
* Fixed "make install" as code was installed in wrong places due to changes in operating systems;
* Bug: python code was not installed in ubuntu23.10;
* Fix rpm generation: Update installation version and add tsduck and espeak as dependencies;
* Disable installation by users as they often mess it up;
* "syntax error" reported in in some python versions in util.py;
* Bug: ensure configdir exists when saving preferences, otherwise it will fail until the user
  manually creates the configdir;
* Avoid assertion when none of the user selected muxes can be scanned;
* Incorrect assertion when mux cannot be scanned;
* Bug:  stream_time_end overwritten with 0 when recording is stopped, reuslting in recording
  that fails to play back.
* Bug: needless assertion when service subscription fails.
* Bug: incorrect config_path returned when neumodvb is installed, resulting in crashes when
  run from installed package.


## Changes in version neumodvb-1.6 ##

### Streaming ###
* Add code for streaming;
* Add GUI for defining, starting and stopping stream.

### Tuning and scanning ###

* Bug: diseqc sometimes not sent when switching to different lnb;
* When scanning peaks, allow only the selected rf_path to be used;
* Bug: chdb::find_by_mux_fuzzy does not consider nearby sats;
* Remove unneeded service reservation;
* Avoid assertion when mux_scan_end message are sent after scan was cancelled by user;
* Prevent assertion when epg service has different network id than service;
* Assertion when scanning t2mi mux on 40E;
* Bug: current service subscribed second time instead of new service;
* Bug: when user is asked to created sat in spectrum_dialog, initialisation of the spectrum dialog seems to continue,
  but fails because some variables have not yet been set;
* Bug: incorrect usage of empty ref_mux when selecting sat and mux for lnb;
* Always add missing sat in spectrum dialog;
* Handle case where lnb is None in positioner and spectrum scan dialogs;
* Do not abort scanning when subscribing service;
* Bug: race when fe_monitor and tuner thread are being remove and new ones are starting.

### GUI ###
* Bug: default satlist filter incorrect.

### Documentation ###
* Installation instructions for ubuntu 23.10.
* Documented the options dialog;
* Fedora38 installation instructions;
* Documented streaming.
* Documented combining neumoDVB with external programs.

### Various fixes and improvements###

* Incorrectly formatted debug messages;
* Bug: Handle empty variable;
* Provide python binding for tvh_import script (external);
* Bug: deserialisation stops early after reading variant;
* Bug: short display name generation for enums produces unexpected results;
* wx assertion when typing invalid service number in service list;
* Log assertions to log file as well as to stdout;


## Changes in version neumodvb-1.5 ##

### Positioner ###

* DiSeqC12 has been thoroughly tested and is now working;
* When the dish is moving a progress dialog now pops up, and is removed automatically
  when the required motion time has passed. In the mean time, tuning is suspended. This
  is mostly important for spectrum acquisition, as starting the spectrum acquisition before
  the dish has stopped moving will lead to an incorrect spectrum. For tuning, neumoDVB already
  detected incorrect dish positions;
* Dish motion now always happens at maximum voltage;
* Bug: setting DiSeqC12 position in positioner_dialog does not work;
* Bug: DiSeqC12 is not correctly sent to positioner;
* Bug: Prevent saving `usals_pos' in lnb record in database  before tuning, to avoid saving bad information
  when tuning fails;
* Properly estimate positioner speed internally. However, this update is not used for anything,
  except in debug messages;
* Bug: assertion when closing main window when positioner_dialog still has subscription;
* Bug: sending positioner command fails because subscriber is erroneously unsubscribed after changing network;
* Bug: positioner_command not sent if there is no network for the currently requested `usals_pos';
* Bug: OnGotoUsals calls self.UpdateUsalsPosition twice;
* Bug: DiSeqC debug message does not show command bytes;
* Give positioner some time to power up before sending it commands. On at least one rotor, the initial
  command was ignored because the rotor was still initializing;
* Dishes are now listed on a new dish list. This allows setting the `powerup time' needed by the rotor
  for that dish, i.e., the time that neumoDVB needs to wait for it to initialize. The dish list also
  allows entering the rotor speed in degree per second. neumoDVB uses this information to compute
  how long to wait before the rotor has reached the desired satellite;
* Bug: spectrum acquisition did not wait for rotor to stop moving. This can lead to a distorted spectrum
  when the dish needs to move;
* Bug: in positioner dialog, typing a value next to one of the spin controls did not erase selected text
  as is common on other text entry fields;
* Improved detection of cases where the currently known `usals_pos' may be incorrect due to executing
  or aborting positioner commands;
* Renamed the `Save' button in positioner dialog to 'Save network', which more accurately reflects
  its function;
* Bug: Displayed `usals_pos' not updated after executing command;
* Bug: cannot send positioner commands with positioner behind DiSeqC switch, while no mux is tuned;
* Increased font size in service list to accommodate changes in recent fedora and ubuntu versions.
  As a result the font may now be too big on older versions;
* Bug: updates defined by user (e.g., `usals_pos') are not yet used by frontend code after positioner moves.

### Configuration ###
* Many options, such as filesystem paths where recordings and dtatabse are stored, softcam parameters,
  default reording and timeshift times, some tuning, and positioner parameters can now be set from the GUI.
  Most parameters are stored in the database, but the filesystem paths are stored in ~/.config/neumodvb/neumodvb.cfg.


### Automation ###

* New feature: create scan commands. A scan command defines a scan job that can be run periodically later.
  It is defined in terms of a number of satellite bands or muxes to scan or spectra to acquire, along with
  a list of available resources (dishes, cards) allowed during the scan and some tuning parameters.  The
  command is saved for later use and then runs automatically in the background, e.g., every day or every
  few hours. For this to work, neumoDVB needs to be running;
* Add the scan command list. This list allows to view all currently defined command and to edit them, e.g.,
  by adding or removing satellite bands or muxes to the scan or changing tuning parameters.

### Scanning, spectrum acquisition and tuning ###

* Show mux scan statistics during spectrum scan;
* Bug: scanning DVB-C and DVB-T muxes fails;
* Bug: `dvbs_muxlist' improperly shows both C and Ku muxes when filtered by a sat;
* Bug: Tuning to service on same mux does not unsubscribe previous service, leading to assertion failure;
* Notification of GUI when scanning muxes has finished has been made more reliable;
* When scanning muxes is not possible (e.g., no cards available) report this immediately instead of
  silently failing;
* Improved selection of multiple multiple sats/muxes, e.g., for scanning. It is now possible
  to add multiple non-contiguous ranges using `control-click';
* Add 49.0E Ku band to default list of satellites;
* Bug: after aborting a band_scan, the next spectrum acquisition fails because of some left over subscriptions;
* Hide scan status for bands which were never scanned in sat list;
* Reset mux scan status and band_scan scan_status in satellite scan after aborting scan;
* Bug: lnb network_list picks incorrect sat_band;
* Bug: incorrect multi-switch committed DiSeqC command was sent during spectrum acquisition;
  spectrum acquisition thus only worked when dish was already pointed at satellite;
* Bug: Incorrect active_adapter released during scan;
* Bug: subscription_id not correctly passed on during re-tune;
* Bug: usals position not always properly updated when it changes due to a user selection;
* Bug: correct `scan_status' overwritten by outdated one after `stream_id' changes in the stream;
* Incorrect debug error message about incorrect `tune_src' value;
* Do not report failed mux reservation in debug messages when called from scanner, as this floods logs;
* Remove tune_mode as field in `tune_options'. It is still used as a state variable in `dvb_frontend_t';
* Bug: Incorrect detection of exclusive use of lnb preventing parallel mux scan;
* Bug: Frequency readout sometimes overlaps with legend button in spectrum scan dialog;
* Improved selection of monitored_subscription_id during spectrum scan. The goal is to more cleverly
  switch between the various frontends, giving preference to frontends with discovered services,
  and prioritizing locked frontends over non-locked ones.


### Installation and compilation ###

* Remove Debian section from installation instructions;
* Add `__init__.py' to avoid conflicts with installed python packages;
* Suppress data structure packing warning;
* Updated required packages for Ubuntu 23.10.

### GUI related ###
* Add a new top level menu, the "DX" menu. Some commands have been moved to that menu;
* Bug: SNR not properly displayed on live screen;
* When satellite  does not exist, ask user to create it when starting positioner_dialog;
* Disable some commands that only make sense during live viewing, except on live screen;
* Improved menu system: disable items that cannot be used;
* In scan parameter dialog, correctly hide panels instead of only the check boxes on them;
* In bouquet edit mode, automatically switch service list after the mode has been activated,
  and automatically re-display service list when user finished  bouquet editing. This way of working
  also applies to adding/editing muxes and satellite bands to scan commands;
* In popup lists displaying satellite positions, avoid duplicates caused by having multiple
  bands (C, Ku...);
* Bug: Fake row was displayed when undoing edits on a record, because number of rows was computed incorrectly;
* Bug: incorrect Undo when no row was being edited;
* Allow editing existing autorecs;
* Improved display of subscriptions in frontend list;

### Recording and playback ###

* Show error message in GUI when file playback fails to start;
* Reset `subscription_id' and owner when recording finishes;
* Bug: assertion in subtitle GUI code.

### Internals ###

* Add sat_band to string representation of sat;
* Replace encode_ascending by non-template in most cases;
* Improved data_type template; still hackish for `ss::string_' detection;
* Allow vectors as sub-types in variants;
* Implement std::optional in database;
* Move re-tune_mode and tune_options definitions into database, Rename tune_options_t to
  subscription_options_t and derive it from devdb::tune_option_t;
* Allow setting `ss::vector' from list. Use band_scan_options in scan_bands_on_sats code instead of python lists;
* Export `fe_polarisation_vector_t';
* Refactor get_default_subscription_options;
* All subscriptions now use a shared pointer to subscriber_t as an input. This allows a subscription_id
  to be stored in the subscriber_t immediately after registering the subscription in the database and
  before tuning. Thus eliminates a race conditions that cause the HUI to loose notifications, e.g., about
  positioner motion;
* Bug: self.grids not properly populated;
* Bug: incorrect decision in `lnb_can_scan_sat_band';
* Replace `sat_pos' by sat in `spectrum_scan_t' and `spectrum_scan_options_t';
* Separate tune and re-tune in `tuner_thread_t' and in `active_adapter_t';
* Update libfmt;
* Avoid exception when pushing a task while thread has already finished executed and then waiting on
  the return value. In that case no valid future was returned. Instead, now it returns a future
  to an already finished task;
* Update minspincontrol code;
* Improve the way that various fields in `positioner_dialog_update_lnb';
* Replace code for re-reading LNB by code only reading lnb LOF-offsets, to make its purpose clearer;
* Introduced fem_state_t;
* Separated tune and retune code in frontend.cc;
* Implemented one_shot timer to implement request_wakeup. This is used to temporarily suspend
  tuning code, and continue tuning after positioner has reached its destination;
* Tuning and spectrum acquisition tasks are now run in fiber, which is suspended when there is
  a need to wait for the positioner to stop moving;
* Do not make all columns with key ending in '_time' read-only by default;
* Move ownership of dvb_frontend_t from fe_monitor_t to dvb_frontend_t;
* Replace `enum_to_str' with `to_str'; the latter is all inline code; also this removes a lot
  of code duplication;
* Move `scan_stats_t' into devdb;
* Remove `playback_map' and `mpv_map'; store active_playback and mpv references in subscriber instead;

## Changes in version neumodvb-1.4.1 ##

* Log incorrect usage of gtk_widget_set_name to debug sporadic problems on ubuntu
* Avoid using the same glContext from multiple thread. This fixes crash on startup on fedora39.

## Changes in version neumodvb-1.4 ##

### Main changes

* Allow scanning all existing muxes on multiple satellites, while selecting muxes based on polarisation and band.
* Allow scanning spectral bands on multiple satellites, while selecting bands based on polarisation and band
  and on frequency range.
* Have muxinfo display more info during scanning.
* Satellite list now shows information on last band scan in a multi-row format.
* Improved code for handling conflicts during subscriptions.
* Allow mux and service subscription to share same active mux.
* Various bug fixes related to incorrect selection of LNBs or not powering up LNBs or selecting bands when
  multples muxes are tuned in parallel. This also leads to more reliable scanning.
* Refactor scan code to make it less complicated and easier to maintain.
* Fixes for recovering recordings when neumoDVB is restarted while a recording is in progress.
* Add autorec code, which checks all new EPG data and creates recording records based on matching
  program name, story, start time, end time...
* sat_list is now organized per frequency band dvbc and dvbt are no longer included in that list. Having
  a different satellite entry per frequency band makes it easier to display only those satellites supporting
  a specific band.
* Use ch_order=65535 for new services so that they appear at end of service list and display 65535 as
  empty string. The end result is that for services without a ch_order are now displayed with a blank
  channel number and at the end of the list, as they are often not of interest to the user.
* Distinguish between cur_lnb_pos (what position does lnb point to) and cur_sat_pos (what as the
  last network used on lnb). The difference is important when multiple closely spaced satellites are
  received by the same lnb.
* Channel epg list has been improved: it allows filtering for a specifuic service. The info window has been
  removed. Instead, the  story is shown using multiple lines in a separate column.
* Much faster Sky UK and Freesat EPG processing due to various improvements to internal huffman
  decoders. All Sky UK epg can now be grabbed in less than 30 seconds.
* The EPG code, and some other code now also runs in parallel when scanning multiple muxes at once. The
  lmdb database still leads to some serialization.
* All debugging code now uses libfmt, leading to faster logging and more informative log messages with
  cleaner code.
* Internal database code has been improved to allow parallel calling from multiple threads without unneeded
  committing of transactions.
* Muxes now use the new `mux_id` as part of new primary for mux. This makes it easier to avoid bugs causing the
  wrong mux record to be updated or deleted, when multiple muxes overlap.
* In the service list the string column `mux_desc` has now been replaced with separate `frequency`
  and `polarisation` columns, allowing filtering and sorting by frequency or polarisation.
* The database now also stores schema_version in database records and in
  data structures, to be able to detect major schema upgrades.

### Spectrum scan and positioner dialog

* Send lnb error to tune_mux_panel rather than handle it via global subscriber.
* Improved error message when lnb action fails.
* When tune_mux_panel action fails, handle resulting error in spectrum_dialog or positioner_dialog.
* Reset get spectrum button if spectrum scan fails.
* Bug: spectra acquired within one minute not treated as the same in GUI, resulting in hiding of
  first spectrum when second is displayed.
* Move start_time from spectrum_options to internal fe state.
* Make get_spectrum also return peak and freq data to scan notification code.
* Bug: Indexing of spectra no longer correct.

### More reliable tuning and preventing conflicts between parallel tunes

* Incorrect comparison when no service was reserved.
* New fe_subscribe code.
* Report error message when lnb cannot be subscribed exclusively.
* Bug: mux list popup in positioner dialog shows Ka band muxes when LNB is of Ku type.
* Bug: frontend info not retrieved for adapters in use and file descriptor not closed.
* Bug: Incorrect live_service processing causes deletion of live buffers in use.
* Faster channel changing: instead of first calling unsubscribe from neumompv and then resubscribing,
  let tuner code do the unsubscribing. This can often reuse an existing active adapter and avoids closing
  and then reopening a frontend.
* Bug: active_adapter released when unsubscribing one of multiple subscriptions.
* Bug: cannot call start_running in constructor of active_adapter.
* Identify live_service_t by owner and subscription_id; remove epg field in live_service_t to simplify code.
* Incorrect handling of lock loss.
* Confusion between ret and errno.
* Bug: incorrect handling of resource conflicts.
* Bug: incorrect handling of unsubscription when multiple subscriptions use same fe.
* Bug: when subscription fails, active_adapter and subscription are not released.
* Bug: incorrect sending DiSeqC messages and changing voltage/pol during retune.
* Bug: active_adapter recreated even when adapter remains the same. New fe_subscribe code.
* Bug: assertion when tuning dvbt mux.
* Increase sleep times after changing voltage or rf input.
* Bug: when switching to a different lnb on a frontend, we need to allow for sufficient power up time to prevent
  tuning from failing.
* Do not power down tuner voltage before switching to a new lnb. Instead start with the old voltage (faster and better).
* Bug: DiSeqC commands not sent when switching to the same lnb via another card or rf_input (e.g., to scan
  another polarisation).
* Also send voltage commands afer rf_input has changed; otherwise tuning may fail.
* Bug: not checking if subscription uses same sat/pol/band when checking if frontend in use can be used leads
  to reusing subscription from another lnb.
* Bug: when reusing the same adapter, tuning uses old lnb.
* Bug: incorrect decision that card cannot be used.
* Bug: incorrect detection of need_DiSeqC.
* Bug: sat_pos not taken into account when checking if subscription can be reused.
* Bug: subscribe not handling properly the case where a subscription subscribes to a service (or to the mux
  itself) on the same mux as its old subscription.
* Unify subscribe_mux and matching_existing_subscription code for all dvb types.
* Bug: reserve_fe_in_use not storing mux_key in subscription when subscribing to mux (instead of service).
* Simplify subscriber notifications.
* Allow non exclusive satellite band reservations.
* drop may_control_lnb, may_move_dish, need_blind_tune, need_spectrum and loc a function arguments;
  use tune_options instead. Also remove them from subscription_ret_t. Remove tune_pars_t and replace
  with tune_optons_t.
* move several function parameters into tune_options_t; specifically include usals_location,
  dish_move_penalty and resource_reuse_bonus in tune_options.
* Switch most "subscribe" calls to using tune_options_t parameter.
* Bug: deadlock when reading last_signal_info.
* Incorrect debug message when overriding obviously incorrect si data with driver data.
* Simplify update_mux and its callbacks.
* constness of merge_muxes argument.

### Improved mux and satellite band scanning

* Add allowed_rf_paths, allowed_dishes, and allowed_card_mac_addresses to tune_options to more finely
  control what resources can be used in tuning.
* Last blind scan setting in positioner and spectrum dialog now remembered as future default.
* Bug: initial scan_report not shown.
* Improved decisions on whether to use mux data from database while scanning spectral peaks.
* Allow add_spectral_peaks to be also called with data from fe_monitor and handle the way that received
  spectra asre processed: send them also to scanner_t code.
* Bug: incorrect scan statistics.
* Bug: ensure that canceling subscription correctly ends scan.
* Bug: mux_common and scan_id lost when rescanning peak.
* Bug: incorrect rescanning on scan_result_t::NOTS.
* Gather scan_stats at point of change directly instead of returning them from function calls.
* Bug: incorrect detection of subscribing to a frequency peak.
* No longer include scan_stats in scan_report. Rename scan_report to scan mux_end_report_t. Ensure that
  notify_scan_mux_end is also called for muxes that fail to tune at subscription time. Refactor scan_loop.
* Separate code for scanning peaks and muxes.
* Make all notification functions thread safe, while still notifying python layers without delay whenever possible.
  Harmonize naming of notification functions.
* Safe deletion in ~scanner_t.
* Bug: when unsubscribing, data stored in fe_t and in database becomes inconsistent, leading to never ending scan.
* Bug: incorrect handling of incomplete BAT tables.
* Ignore DefaultAuthorityDescriptorTag to remove many log messages.
* Bug: incorrect frequency from si data overwrites correct data from driver.
* Bug: when reusing active_adapter old fds remain open, leading to incorrect early notification of scanner.
* Bug: last_signal_info and setting lock status in frontend.cc was performed in two steps, leading tuner_thread
  to see inconsistent information (is_dvb not yet set when lock is reported).
* Bug: incorrect reuse of existing subscription when scanning spectral peaks.
* Ensure generated mux_id always > 0.
* Bug: incorrect growing of vectors of plain old data. Leads to bouquet processing incorrectly expiring
  entries on 28.2E.
* Bug: when reusing active_adapter old fds remain open, leading to incorrect early notification of scanner.
* Ignore matype for dvbc and dvbt

### Recording

* Bug: recording not recovered from live recordings at restart.
* Bug: when crashing multiple times, second part of ongoing recording overwrites earlier recovered part.
* Add autorec and code for checking autorecs; Add autoreclist and add autorec dialog on various screens.
* Add make_unique_id for recordings.
* Bug: service.find_by_key not working.
* Bug: autorec_id not correctly computed when autorelist is empty
* Bug: autorec list not updated after adding autorec.
* Bug: reclist not updated after adding recording.
* Ignore database versions in recordings so that old recordings still play back. This relies on some structures
  remaining valid.
* Added owner field to rec_t. Start new tuner_thread for each active adapter.
* Move recording code from active_adapter to tuner_thread or recmgr_thread.
* Clean recording status of epg records, which may not be uptodate after a crash.

### GUI improvements and changes

* Do not show filter menu when right clicking on icons column.
* Improved display of service in frontend list.
* Ensure that satellite list is created when starting with empty database.
* CmdNew and CmdDelete should not be enabled on live panel.
* Allow selecting multiple non-contiguous rows in lists by ctrl-click.
* Add substring matches for string filtering and make it default.
* Display subscribed services in frontend list.
* Improved display of subscriptions in frontendlist.
* Display shorter matype str.
* Try to preserve position on screen of currently selected item.
* More logical sort order for dates/times.
* Receiver may only exit after all application windows have been closed.
* Font too big in chg selector on chgm screen.
* chglist: use only 1 set of field_matchers.
* Bug: assertion when selecting DVB type in frontend list.
* Bug: incorrect display of services on live screen if some service changes in the background.
* Bug: incorrect sat_pos and usals_pos set after editing lnb.
* Bug in channel group member list.
* Bug: speak no longer working.
* Bug: group select text font cut off on live panel.
* Bug: incorrect display of various fields in autoreclist.
* Bug: autorec_dialog code was incomplete: values not updated when saving.
* Bug: subtitle language selection not working.
* Bug: screen update ignores filter.
* Bug: incorrect handling of control parameters in dvb text strings

### EPG improvements

* New channel epg list: no more infow, allow selecting services,  allow showing all services, show story on
  multiple lines.
* Add service_name to epg records. Also add full service_key to epg records.
* Bug: due to libiconv problem, incorrect handling of larger than usual epg stories.
* Bug: sky epg scan never ends when summary data has no corresponding title.
* clear parser_status when receiving section with bad crc
* Use system tz db rather than downloaded one. Speeds startup of epg screen
* Distinguish between epg and nit scanning in statistics
* epg_scan completeness wrongly computed
* Avoid assertion on incorrect epg data with last_table_id smaller than first_table_id
* Fix channel_epg
* Implemented much faster opentv huffman decoder resulting in 2.5x faster opentv string decoder.
* Bug: gap between epg records sometimes shown as part of older epg record in grid epg
* Show also epg data from the recent past
* Remove epg records in live buffers and recordings (epg is still stored in rec_t record)
* Handle case where epg_screen is None
* Increase number of epg records per section callback to avoid reallocation.
* Incorrect assertion, leading to chrash when processing sky epg.
* Allow filtering in chepg_list when specific service is selected

### Compiling, debugging and installing

* Switch to libfmt fort logging, resulting in faster and more versatile code. Removed xformat and ssaccu.
* Reduce log verbosity.
* Add recmgr to logger output.
* Improved logging.
* Avoid accumulating error messages
* Make stackstring functions gdb friendly again.
* Provide fmt interface for stackstring.
* Provide fmt interfaces for pts_dts_t,m pcr_t and millisonds_t.
* Removed sprintf-like functions in stackstring Removed dtdebugx, dterrox, ...
* Remove all operator<< based debug and error calls. Remove dtdebug, dterror, dtinfo, ...
* Avoid cgdb crash: disable vector printing.
* Improved ndc in log messages (always show tuner number)
* Do not compile unused code
* Fix incorrect assertions.
* Bug: Invalid memory access.
* Suppress cmake warning
* Correctly indicate which symbols are exported by libneumoreceiver.so
* Default optimisation
* Replace date library (removing slowness of current_timezone) by std::chrono and libfmt
* Fedora38 installation instructions
* remove dead code.
* stackstring: conditionally disable asserts.
* Added ubuntu 23.04 installation instructions
* Compilation fixes for fedora38

### Database code improvements

* New database access code, allowing multithreaded sharing of wtxn (tested and working on epgdb, still
  single thread) cursors: add readonly field.
* Ignore database versions in recordings so that old recordings still play back. This relies on some
  structures remaining valid.
* New database schema for muxes and services: introduce mux_id as part of new primary for mux and remove extra_id.
  Add mux_id column in service list and mux list. Also, replace `mux_desc` with frequency and polarisation in services,
  allowing filtering of services based on frequency or polarisation. Add schema_version in database records and in
  data structures, to be able to detect major schema upgrades.
* Code for handling major database upgrades from python and specifically converting to the database
  format needed for neumodvb-1.3.
* Improved logging of auto upgrade.
* Allow receiver initialization to be interrupted for major database.
  upgrade and then for initialization to continue.
* Compute upgrade_dir in python and pass to c++ code.
* Introduce update_record_at_cursor.
* Allow cursor to be explicitly closed before their object is destroyed, this allowing transaction to be aborted.
* Add uuid in schema.
* Improve neumodbstats.py.
* Remove fake satellites with sat_pos_dvbc and sat_pos_dvbt satellite position.
* Improved checking of bad strings in deserialisation code.
* Make testmpm.py work again.
* Bug: deserialize_field_safe: correctly skip variable size fields in old record.
* Bug: cursors are not properly destroyed if no record is read with them.
* Bug: txnmgr accidentally removes open transaction.

### Multithreading and task scheduling

* A new thread is now created for each tuner. This allows parallel processing of EPG on multiple muxes.
* Identify multiple tuner threads by adapter_no.
* Add thread name for recmgr.
* Allow task_queue_t::cb() to work even when thread has exited.
* Disallow pushing tasks after exit() has been called.
* Tuner: run_taks executed too many times.
* BUG: various bugs in txnmgr.
* Bug: Incorrect releasing of wtxn in txnmgr when called from multiple threads.
* Bug: Incorrect locking.
* Bug: accessing released memory when waiting on a future to call thread end.
* Bug: iconv code in stackstring not threadsafe.
* Bug: tasks for unsubscribing were removed.
* Bug: update_lnb called from wrong thread
* Bug: pmt_parser destroys its own fiber at exit, causing bus error

### Various improvements and changes

* Add variant for service and band_scan in subscription_data to be able to store scan_id for band_scans.
  Could be done with service as well.
* Fix variant types in database.
* Add adapter_no to spectrum file name.
* Introduce tune_options.tune_pars to gather restrictions imposed by sharing resources with other subscriptions.
* Add to_str for enums in database generator. Distinghuish between various satellite bands in scan code.
* Avoid assertion in startup code.
* Raise assertion when using fefd which is not open.
* Bug: division by zero.
* Stackstring: avoid calling constructors when copying vectors containing trivial types. Leads to large.
  speedup in debug build. Speeds up operator[] as well. Bug: size of zero terminated string.
* Bug in growing memory of stackstring. Remove grow function. inline more stackstring code. Various optimizations.
* hide crc_is_correct and make inline.
* Replace map with array in code managing section flagging, resulting in speedup.
* Additional inlining; suppress warnings when compiled optimized; compile some code optimized by default.
* Handle version numbers with additional digits.
* Avoid deadlock.
* Needless stop() call.
* Bug: callcc code crashes when compiled optimized.
* Efficiency: needless copies of field_matchers.
* Enable contains to more types of stackstrings.

## Changes in version neumodvb-1.3 ##

* Improved identification of muxes during scan, even when details
  such as network id and ts id change durign scan. This prevents never ending
  scans in more cases.
* Updated documentation: mux list, status list.
* Display correct mux data in more cases and display bad nit info in more cases (in red).
* Show more mux information in service info on live screen.
* Bug: wrong band reported for wideband lnb.
* Also show total number of muxes during scan.
* Bug: incorrect nid and tid shown in positioner dialog.
* Perform pmt scanning also for t2mi streams.
* Bug: isi scan times out too soon.
* Bug: restricting service list to sat did not work properly.
* Introduce new assert code, which allows continuing past assertion in debugger
* Avoid introducing streams with stream_id=-1 when multi stream exists but does not lock.
* If mux cannot be locked, use NOLOCK as status instead of BAD. The latter means that the tuning parameters
  are invalid.
* Add scan_lock_result.
* Always preserve mux_key when consulting driver.
* Never change stream_id even if driver reports the wrong one, but allow overriding stream_id with value from
  driver_mux when mux is template.
* Properly handle case of stream ids changing during scan
* Move some data from si.scan_state to si.tune_state, so that it is not cleared by si.reset().
* When looking up template mux with stream_id=-1, ignore stream_id in lookup to also find multi stream.
* Simplify active_adapter_t::monitor() loop.
* Bug: scan subscribers erased during si.reset.
* Bug: passing local variables into tasks stored for other threads can lead to accessing invalid memory
* Make by_scan_status key order by pol and frequency as secondary sort order, changing order in which muxes
  are scanned.
* Avoid calling remove_fd with file descriptor <0.
* Bug: Incorrect error message about fe and lnb not found.
* Bug: Non Ku-Band subscriptions shown as None.
* Avoid assertion in CmdBouquetAddService.
* When selecting default network, take the one with the closest sat_pos
  rather than the closes usals_pos.
* Workaround for possible kernel bug which causes epoll to report that eventfd is readable, whereas it is not.
  This causes a subsequent notifier.reset() to hang forever in read.

* if peak_scan fails, then add the peak to the vector for future rescanning.
* Handling of failed peak tuning improved.
* Bug: data_start not correctly reset in some cases, using to incorrect no_data status.
* Require FEC lock when scanning dvb muxes.
* Bug: dereferencing std::optional without checking.
* Trust modulation parameters from driver in more cases.
* Bug: incorrect usage of make_key in screens sorting by predefined key.
* Bug: passing auto variables into lambda leading to incorrect tune options during scan.
* Deadlock due to active_si_stream_t destructor being called from receiver, causing access to functions
  which should only be used by tune thread


## Changes in version neumodvb-1.2.2 ##
* Really fixed row auto sizing and deleting in lnb list and other lists.


## Changes in version neumodvb-1.2.1 ##


### Main changes

* Report error when recording in background fails.
* Bug: recordings did not start.
* Bug: Avoid assertion when recordings list is empty.
* Assertion when showing service info in recordings screen.
* Fixed row auto sizing and deleting in lnb list and other lists.
* Fixed assertion while scanning DVB-C muxes.
* Bug: dvbsc and dvbt tune code sometimes find wrong mux due to bug in find_by_mux_fuzzy.
* Bug: incorrect selection of frontend during dvbsc and dvbt tune.
* Bug: assertion when adding more muxes to scan in progress.
* Frontend list: improved display of DVBC/DVBT subscriptions.
* Use the same tolerance everywhere when deciding if a satellite is sufficiently close to the
  current one  for possible reception; Allow error up to and including 1 degree.
* Bug: NIT_ACTUAL incorrectly treated as invalid in GUI in some cases.
* Slightly simplify positioner control: SLAVE control is replaced by NONE, signifying that
  no dish moving commands will be sent. Actual "slave" status is now determined from `on_positioner' field
  and `positioner' field is no longer automatically adjusted.
* Bug: custom lnb settings low_low/high freq_low/mid/high were ignored.
* Improved installation instructions: Merge pull request #29 from Saentist/patch-1.
* Bug: missing abs() when comparing sat_pos, leading to incorrect lnb network selection.
* Improved installation instructions Ubuntu 22.04.
* Avoid assertions when receiving SDT_OTHER table.
* When usals computation goes wrong, e.g., because user did not set proper usals_location or selects
  satellite below the horizon, set `lnb.cur_sat_pos` to lnb.usals_pos and avoid assertion.
* When user turns off `on_positioner`, for an LNB, ensure that `lnb.usals_pos` and
  `lnb.sat_pos` are set to values from the network list, such that the `cur_sat_pos`
  column is correct.
* Replace `rotor_control_t::FIXED_DISH` with `rotor_control_t::ROTOR_MASTER_MANUAL`,
  which will be used to indicate that user wants external-to-neumo rotor
  control. The only difference is that no dish moving commands will be sent.
* Bug: error deleting rows in lists.
* Bug: channel list (not service list) no longer working

### Other changes

* Crash when switching to recording playback due to null pointer dereference.
* Debug message messed up due to using msg as variable name.
* Bug: scan_status not preserved when updating muxes during scan.
* Fix erroneous assertions.
* Various python assertions because of non-imported symbols.
* Bug: incorrect deserialisation of boolean.
* Show SDT_OTHER in operator<<.
* Bug: incorrect usage of make_key in screens sorting by predefined key.
* Improved find_by_key by adding find_prefix, which can be different from key_prefix.
* Bug: `matching_sat` computation could access wrong database.

## Changes in version neumodvb-1.2 ##

### Most important changes ###

* Internal processing of SI data has been simplified, removing many exceptions while still correcting
  for incorrect data in muxes with bad SI data.
* Several bugs were fixed causing blindscan or mux scan to never end.
* During scanning, discovered services are now shown in positioner and spectrum dialog.
* Support for wxpython4.2.

### SI processing and blind scanning improvements ###
* Various bug fixes causing blindscan to never end (but some more remain).
* Correct handling of multistreams during scan.
* Bug: in by_mux_fuzzy_helper returns wrong mux, causing updates to be applied to the wrong mux.
* t2mi muxes sometimes incorrectly updated after scan.
* set t2mi_pid to zero before blind scanning, so that not only the t2mi mux is canned but also its parent mux.
* Add T2MI media type while scanning PMT.
* Ensure sdt data shown during spectrum scan corresponds to nit info.
* Bug: deadlock while saving services.
* Do not trust tuning information from muxes except from nit_actual and from driver.
* Bug: sometimes services missing in service list in positioner and spectrum dialog.
* Pick proper ts_id when NIT and SDT are absent.
* Avoid creating services with incorrect mux_key which can then not be tuned.
* Make frequency estimate robust against incorrect tone status returned from driver.
* Avoid creating muxes which differ only in extra_id on same frequency.
* Simplification of mux scanning code: Always give preference to sdt version if  ts_id and network_id,
  even if mux reports other (incorrect) data. If there is no SDT, prefer setting these values from
  NIT, and if there is no (valid) NIT, use the value of ts_id in PAT. Also ensure that all of this
  works irrespective of the other in which NIT and SDT are received and handle timeouts when they not exist.


### Handling of various muxes which broadcast incorrect SI data ###
* Incorrect detection of t2mi on 30.0W 11382H, which does not have t2mi data.
* 39.0E 10930V stream 1 reports 30E as satellite position in the NIT actual table.
  This causes NIT_ACTUAL to be ignored, resulting in no useful nit data. On the other hand,
  there is no SDT  table. Now,  pat data is used in this case.
* 14.0W. Incorrect modulation parameters belarus24hd.

### Bugs related to changing LNB settings ###

* Incorrect estimation of current satellite positions on offset LNBs.
* usals_pos not correctly set when entering sat_pos in network.
* Incorrect computation of cur_sat_pos.
* Offset angle for offset LNBS incorrectly estimated or not recomputed.
* Bug: lnb_cur_sat_pos messed up when closing lnb network dialog.
* Ensure that usals_pos and sat_pos are both initialized in network_list
  if either one is set. This also solves a bug when clicking "sort" on
  uninitialized usals_pos or sat_pos column.
* Ensure that spectrum acquisition always uses the most recent usals_pos
  if this has been changed from positioner_dialog.
* Update positioner and spectrum dialog title after changing sat; include card in title.
* Network and connection combo not updated after selecting lnb in positioner or spectrum dialog.
* Reference mux not saved when set.
* Usals type cannot be changed from positioner and change is not immediately visible in lnblist.
* Usals pos not correctly set in positioner dialog.

### Support for wxpython4.2 ###
* Detect wxpython version and remove workarounds needed un older versions of wxpython when
  wxpython4.2 is detected.
* Bug: incorrect  font size in grids vin wxpython4.2.
* Bug: excessive row spacing in live screen in wxpython4.2.
 *Bug: Excessive size  of bar gauges in wxpython4.2.
* Bug: incorrect size of comboxes in wxpython4.2.
* Bug; Togglebuttons have different size than regular buttons in newer OSes.


### Other GUI related changes ##
* Show list of services in postioner and spectrum dialogs.
* Improved selection of mux on tune mux panel if no reference mux is set:
  specifically select a tunable default frequency and polarisation.
* Bug: subtitles on arte confused by mpv with teletext. The solution is to not save teletext information
  in the stream as neumoDVB does not implement teletext anyway.
* Signal history: improved date formatting; Show signalhistory_plot in local time.
* Improved formatting of status_list.
* The speak function now also speaks "not locked" when appropriate.
* Improved sort default sort order for time columns.
* In DVBS mux list, indicate more clearly where network and ts ids were found, by introducing key_src.
* spectrum_plot: allow a range of no more than  30dB range in initial spectrum plot.
* Indicate initially sorted column in GUI using triangle. The old code sorted properly but did
  not indicate graphically which column was sorted.
* When filtering a column, avoid some needless clicks and mouse movements: in the popup window immediately.
  focus the new filter cell, without user having to click the cell. Also allow the filter dialog to close
  by just pressing ENTER.
* Bug: filter not updated after its value has been edited.
* Move lock indicator to more logical position, i.e., in front of transport stream specific lock indicators.
* Pick proper default polarisation for draw_mux (circular versus linear).
* Bug ShowOkCancel not imported resulting in python exception.
* Display unknown matype as blank.
* Bug: incorrect string value for Ka LNB subtype.
* Bug: In the status list screen, not only live muxes are shown but also previously tuned ones.


### Various bugs causing crashes or incorrect data ###

* For LNBs with One band, set freq_mid to freq_high in all cases.
* Bug: si_state returns erroneous value of timedout.
* Bug: si_state check erroneously returns duplicated instead of completed
  for single section, potentially resulting in lost SI data.
* Bug: when a table times out, it still must be processed because some code
  in active_si_stream relies on knowing ts_id.
* Bug: scan_status_t::RETRY status should also be cleared at startup.
* Avoid assertion when lock is lost after init_si has been called.
* Bug: incorrect looping over frequencies.
* Bug: crash due to accessing record of different type when looping over
  cursor range due to key_prefix not being set.
* Bug: database mux sometimes deleted when not overwritten.
* Bug: memory corruption due to not unregistering subscriber properly.
* Bug: Do not rely on voltage and tone retrieved from driver, because.
  for slave connections, these values are not even set in the driver.
* Prevent events being queued on window after it is destroyed.
* Bug: incorrect matching of rf_path causing wrong tuner to be used.
* Bug: PENDING status set on multistream while not scanning.
* Bug: incorrect usage of find_by_key like functions in conjunction with
  find_geq because default fields lead to incorrect lower bound.
* Introduce sat_pos_tolerance. Muxes on satellites less than 1 degree apart are now
  treated correctly when SI data is received.
* Workaround for false positive at start when using sanitized, causing a crash at startup
* Add Sanitize compilation option, disabling it by default


## Changes in version neumodvb-1.1 ##

### Most important changes ###

* Database format has changed once more, but only regarding LNB definitions. The main change is that there is now
  on entry per physical LNB  in the LNB list, whereas in the past there was a separate entry for each LNB input cable.
  This means that DiSeqC settings will have to be re-entered. The new format is explained in the documentation and
  was needed to allow some advanced features to work better.
* New layout of the LNB screen, with exactly one line per physical LNB. To "connect" an LNB to a card double-click on
  the cell in the `connections` column and add all tuners to which the LNB is connected. Most DiSeqC settings are associated
  with such a connection and need to be set on this connection, and no longer on the LNB itself. This allows neumoDVB
  to compute a single LOF correction value for each LNB  instead of separately for each connection.
  Connections and networks in lnb list also follow a multi-line layout. Individual connections or networks that can not
  be currently used for some reason are shown as strike-through text.
* Estimate the current satellite position for offset LNBs and display it in the LNB list instead of the USALS pos
  which is more difficult to interpret for offset LNBs.
* New `signal history` feature which shows historic SNR for muxes. Use it to inspect signal degradation over time.
* In spectrum and positioner dialog only show muxes for frequencies supported by the LNB. For example, do not show
  C-band muxes for a Ku-band LNB/
* Ensure immediate consistency of networks, connections and other lnb fields across lnb list and positioner dialog and
  spectrum dialog. For example, when adding a network to an LNB in the LNB list, the new network can be selected in the
  spectrum dialog without having to close and reopen it.
* More useful, default sat selection when positioner is currently not pointing to any sat.
* Improved layout of spectrum dialog to also show the date when a spectrum was captured in the spectrum list.
* Identify all LNBs in the system by a small, never changing number, allowing for easier brief identification of cards.
  E.g. C0 means card 0. C1 card 1.... neumoDVB takes care of this numbering, but the user can change it.
* Blindscan search range is now adapted to symbol rate leading to fewer cases where tuning fails.
* More robust estimation of lof_offsets to handle incorrect data in NIT
* Correctly identify roll-off values below 20%
* Introduce rf_coupler_id: setting this value to a number different than -1 on a connection signifies that that connection
  shares a cable with any other connection having the same rf_coupler_id. neumoDVB will then ensure that compatible voltage,
  polarisation, band, and satellites are selected on those connections.
* Do not allow dish motion when tuning services, only when using positioner. This is a choice that will be turned into an
  option later.
* In positioner and spectrum dialog, allow separate selection of lnb and tuner (lnb_connection)

### Bug fixes and improvements ###

* Avoid overwriting lnb values set in lnblist when saving lnb in positioner_dialog. This allows editing various pieces
  of LNB information in the LNB list, and the in the positioner dialog without the changes made on the two screens interfere.
* Bug: when editing service number in services screen, and then switching to live screen, the old channel number is shown
  instead of the new one
* Display ??? when lnb LOF loffset is not known
* Avoid some assertions when LNB's ref_mux is None and when lnb is None
* Bug: incorrect detection of adaptation field of ts packets in rare cases. These led to an assertion.
* Wait with saving pmts until IS ACTUAL has been received or timed out
* Improved guessing of network_id and ts_id when some SI tables are missing.
* Bug: mux_keys of some services incorrectly renamed when an invalid NIT is received, leading to un-tunable services.
* Detect dummy plf modcode
* Bug: Incorrect mux display in spectrum_scan at end of spectrum.
* Increase height of text annotations in spectrum scan
* Bug: do not call init_si on non transport streams
* Bug: signal_info and constellation scan long time not updated during blindscan all.
* Avoid assertion when rescan_peak fails
* Log scan result
* Clear old subscriptions left on some no longer existing adapter; Improved status list layout
* Bug: do not set DTV_VOLTAGE when voltage ioctl was used (redundant)
* Add user error messages when reservation fails on required lnb
* Fix a datarace in the way error messages are passed from tasks to calling threads
* Report error when SYS_AUTO is requested for non-neumo drivers
* When driver resturns stream_id==0xffffffff, do not set a pls code
* Handle sign in latitude and longitude
* Add tune_may_move_dish internal option.
* Display matype in more cases
* Bug: Fedora fc37 ComboBoxes do not work properly
* Take into account button dimensions when sizing ComboCtrl text
* Avoid assertion when starting positioner_dialog on disabled lnb
* Allow positioner dialog and spectrum list to start for enabled but not available lnbs.
* Bug: when adding record with duplicate key, row with question marks appears because row count is incorrect
* Bug: incorrect number of rows on screen (resulting in records with question marks) due to row_being_edited out of sync
  with screen's editing record
* Bug: incorrect handling of row_being_edited when user requests new record, when edit of new record in progress.
* Bug: RowSelectionMode not activated in service_list
* Always choose reasonable default for reference_mux. Implement select_reference_mux
* Add lnb_id to lnblist screen.
* Add on_positioner field to lnb, update it as needed, and show in lnblist
* Add lnb connection editor

## Changes in version neumodvb-1.0 ##

### Most important changes ###

* New database format: data related to local setup is now stored in anew database, called devdb.mdb
  Also, the definition of LNBs and frontends has changed to better support new features. As a result
  of the move, old LNB definitions will be lost and need to be reentered. The remaining database
  now contain data that is independent of the local hardware setup and can therefore be shared
  with other users.
* neumoDVB now makes better use of cards with and RF mux. These cards, such as
  TBS6909x and TBS6903x have multiple tuners (e.g., 4) and even more demodulators
  (e.g. 8). The RF mux allow connecting any demodulator to any tuner. This means that
  all demodulators can reach all connected satellite cables. Also, multiple muxes
  can share the same tuner and thus tune to multiple muxes in the same band and with the
  same polarisation simultaneously.
* neumoDVB can longer needs concepts like "slave tuners" because all tuners can use al RF inputs.
  So the concept of slave tuner has been removed.
* Parallel blind scan on supported cards, neumoDVB now uses all available demodulators
  to scan muxes. This makes blind scanning (almost) 8 times faster on supported cards
* Parallel SI scanning was already possible in neumoDVB, but now it can exploit more parallelism
  by setting the RF mux than before.
* Faster scanning: neumoDVB processes transponders with many (more than 200) multi-streams much faster
  by detecting streams which are not transport streams.
* Improved spectrum analysis: very wide band muxes are no longer detected as dozens of very small
  peaks, but as a single or a small number of peaks. This speeds up blind scanning as well. The estimated
  symbol rate of peaks used to be very inaccurate and is not more accurate.
* Change voltage from 0 to 18V in two steps to avoid current overloads when many DiSeqC switches switch
  simultaneously.

### Spectrum analysis and blind scan ###

* DVB-T frequency received from driver off by factor 1000.
* Parallel blind scan: on the spectrum dialog screen `Blind Scan all` now starts a parallel blind scan,
  using all adapters which can reach the selected LNB.
* The spectral peak analysis algorithm is now always performed by neumoDVB itself and the algorithm
  included in the drivers is ignored.
* New spectrum scan algorithm, missing fewer correct peaks and adding fewer incorrect peaks.
* SI Scanning code rewritten to avoid never ending scans and to make the overall process more efficient
  and tuning more responsive.  Specifically avoid trying to subscribe muxes after subscription on same
  sat band failed.
* Blind scan and regular SI scan code better integrated.
* For historic spectra, spectral peaks are always recomputed instead of loading them from disk.
  This allows taking advantage of newer (improved) analysis algorithms.
* Bug: signal_info poorly updated during spectrum scan.
* More efficient handling of newly discovered streams in multi-streams. If such streams are
  not transport streams, they are not scanned, but still entered in the database. When it is
  discovered that some streams have been removed, scanning thoese dead streams is prevented.
* Create new muxes for multi-streams as soon as they are detected before even tuning them.
* Improved multi-stream scanning: scan only dvb streams, but continue scanning for new matypes as long as
  new ones come in.
* Spectrum data is now indexed by key instead of by filename, eliminating confusion between spectra
  acquired within a very short time span.
* Bug: memory corruption in older spectral algorithms, leading to sporadic crashes.
* Issue #22: incorrect detrending with wideband LNB.
* The spectrum list now shows rf\_input instead of adapter\_no.
* Correctly show status of scan in progress in mux and service lists.
* Bug Fix scanning of DVB-C and DVB-T muxes caused by incorrect usage of matype
* Bug: error messages sometimes not shown when scanning fails
* Handle temporary failures during scanning. Such failures are caused by stid135 running out of LLR bandwidth.
* Improve the way signal info and scan notification are communicated to GUI.
* At startup, only clean mux pending/active status for dead pids, thus allowing multiple instances of neumoDVB
  running on the same computer and sharing resources.
* Ensure that merge_services is only called for dvb muxes; remove services.
  when it is detected that a dvb mux has been replaced with non DVB one.
* Disallow moving positioner during scan.
* When tuning fails set scan duration to tune time.
* Bug: frequencies are rounded to integer when shown in graph.
* Log lock time.
* When user presses abort tune while blind scan is in progress, stop blind scan as well.
* Continue blind scan properly when tuning fails, instead of hanging.

### frontend list

* Adapters and cards are now identified by a unique MAC address, which should avoid problems due to
  linux DVB adapter renumbering.
* Cards are now auto numbered. In the GUI C3 refers to card number 3.
  The main reason for this short card number is to refer to cards in the GUI using a small
  number instead of via a long MAC address.  The numbering remains
  stable when cards are removed and later reinstalled.
* Delivery systems can be disabled per adapter, e.g., to inform nuemoDVB that no DVB-T antenna
  is connected to a DVB-T/DVB-C card, but only a DVB-C-cable.
* Add subscription column, which shows the mux that is currently tuned.
* Various columns added to make it easier to distinghuish between cards of the same type.
* Distinguish between sweep and fft spectrum analysis capability.
* Various columns added to show features supported by adapters: blind scan, spectrum scan, multi-stream...
* Bug: incorrect detection of adapter presence and availability.

### LNB list ###
* LNBs are no longer coupled with frontends, but with card/rf\_in pairs. These can
  be selected from a popup menu, which onluy shows valid combinations.
* Change LNB key to card_mac_address, rf_input; add conversion script
* Bug: LNBlist screen corruption when no networks defined.
* Add can_subscribe_lnb_band_pol_sat and can_subscribe_dvbc_or_dvbt_mux.
* Bug: do not ignore LNBs on positioners which point to the correct sat when selecting and LNB for tuning.
* Bug: LNB incorrectly updated when frontends change or disappear.
* Add Ka-Band LNB definitions.
* Add wideband LNB definitions.
* Auto compute inversion based on LNB parameters.
* Skip unavailable LNBs during subscription.
* LNB naming includes card_no, and card_no, adapter name and adapter number are added as cached property.
* Add LOF low/high in LNB list


### Tuning and viewing ###

* Improved support for dvbapi
* Gradually change voltage from from 0 to 18V to avoid current overloads when many DiSeqC switches switch
  simultaneously.
* Wait 200ms after powering up DiSeqC circuitry.
* Handle invalid parameters during tuning (e.g., frequency/symbol_rate out of range), reporting them to
  GUI and properly releasing resources.
* Prevent forcing blind mode based on delsys; instead respect tune option.
* Split all tuning related actions into two phases: 1) reservation of resources; 2) the actual tuning
  This allows to better handle some failure cases.
* Subscriptions are now stored in the database, facilitating multiple instances of neumoDVB running parallel.
* Bug: DiSeqC commands sent without waiting for LNB power-up.
* Bug: tuning to DVB-C/DVB-T fails; invalid assertion.
* Bug: when tuning fails in positioner_dialog, subscription_id<0 is returned but mux is not unsubscribed (and
  cannot be unsubscribed).
* Bug: assertion when tuning mux with symbol_rate==0.
* Improve detection and handling of retune conditions; improved calling of in_scan_mux_end and
  on_notify_signal_info.
* Bug: confusion between DVB-C and DVB-S.
* Prefer to reuse current adapter when all else is equal.
* When two subscriptions use same tuner, do not power down tuner when ending only one of the subscriptions.
  This is supported as of neumo driver 1.5.
* Bug: voltage and tone state not cleared after frontend close, resulting in DiSeqC not being sent
  and tuning failing.
* Avoid crash on exit when fe_monitor not running.
* Assertion when softcam returns bad keys.
* Enable "tune add" command on service list screen, and not only on live screen.
* Bug: 2nd viewer window goes black when first is closed.
* Refactor get_isi_list; use matype_list when available.
* Get lock time and bitrate from driver; detect neumo api version.
* Bug: detected matype overwritten in some cases.

### SI processing ###

* Bug: incorrect skipping of stuffing table on 19.2E 10773H.
* Override SI-mux delsys from tuner (14.0W 11024H).
* If mux has no NIT and SDT still add services after SDT/NIT timeout. Happens on 14.0W 1647V.
* Bug: sat_pos wrong in pmt processing (14.0W).
* In case NIT data is obviously wrong, also fix polarisation, sat_pos and delivery system. Happens on 14.0W 11623V.
* Add special case for t2mi detection: 14.0W 11647V.
* In case NIT processing does not succeed, tuned_mux_key may be wrong and as a result SDT processing can
  conclude that we are on wrong sat leading to never ending scan (14.0W 11623V).
* If mux has no NIT and SDT still add services after SDT/NIT timeout. Happens on 14.0W 1647V
* Prevent never completing tables from not timing out.
* Add NOTS scan result to indicate that the mux is not a transport stream.
* Bug: sdt sometimes updates invalid mux.
* Correctly handle several cases on 30.0W where NIT and SDT disagree.
* Check for unlockable and non existing streams.
* Distinguish between non-locked muxed ans muxes with bad parameters that cause tuning failure.
* Retrieve signal_info when needed for si-processing from cache rather
  than synchronously retrieving it from driver (which stalls tuner thread).
* Bug: no mux_desc when adding a service found only in PMT.
* Process PMTs even in minimal scan.
* Prevent tables with persistent CRC errors from causing si scan to never complete.


### GUI related ###
* Save Usals location in database when user changes it (and reload from database) instead of storing
  usals location in read-only config file.
* Improved handling of illegal sort orders (no more assertion).
* Positioner dialog is now saved to database and no longer set in a config file.
* Show radio background for radio channels.
* Make page up/down work on numeric keypad.
* Display detected matype information.
* Positioner_dialog and spectrum_dialog: handle missing/incorrect data.
* Improved handling of failed edits.
* Improved handling of missing LNB data in GUI.
* Show DVB-C or DVB-T muxlist when user selects either of them on DVB-S muxlist.
* Add DVB-C and DVB-S to sat_list so that they can be selected on service screen; when
  selecting either on on DVB-S mux screen, the GUI switches to DVB=T or DVB-C mux screen accordingly.
* Bug: handle various cases of assertion when record is not found (due to other bugs).
* Add number of services to service, chgm screens and number of muxes to mux screen.
* Do not display -1 as stream_id in gui.
* Show tuned frequency also when there is no lock/.
* Show correct dB values in overlay when values reach maximum. (alternative for pr #23).
* Bug: Distinguish between SI table present and complete in GUI.
* Show more stream_ids.
* Show symbol_rate and frequency as soon as timing lock is achieved.
* Bug: incorrect reference row number after deleting record at reference cursor. The new code
  invalidates the cursor.
* Bug: incorrect row number updating of reference cursor while deleting records. This results in
  assertions being triggered and/or incorrect rows focused or displayed.
* Show error message when blind/spectrum scanning and drivers not installed.
* Report drivers not installed when user attempts to blind scan or spectrum scan.
* Bug: if current record does not exist, focus row 0 instead of -1

### Database and options ###

* Major database change: identify adapters by their mac address and no longer by adapter number.
  Limitations in the current database upgrade code result in the loss of all defined LNBs. Also,
  old spectra will not show the adapter on which they were captured.
* Split off lndb and fe database from chdb into devdb.
* Implement option merging when loading config files, thus adding options which are missing in user config file
* Clean expired services and channels which are more than one month old to avoid
  huge database.
* Clean subscriptions by dead processes in database.
* Clean channels which no longer have services.
* Add pyschemadb; proof of concept for reading non-upgraded db schema and data in pythhon test code.
* Add "neumodbstats.py" script which shows various statistics (number of records etc) of database records
* Avoid opening the same database twice. This prevents an assertion in mdb.c and leads to faster
  GUI.
* Proof of concept for reading database with outdated schema from python. This can be used to  convert old database
  formats in more flexible (but slower) ways.
* Report fatal lmdb errors on stderr.
* Fix neumoupgrade path issue.
* Bug: db_upgrade fails when database name has trailing slash.
* Increase default database mmap size, allowing databases to grow bigger if needed.
* Bug: erroneous assertion in cursor code.
* Bug: double abort in cursor code.
* Ensure all transactions are aborted.
* Bug: iterating over range fails when modifying records.
* Set proper key_prefix on find_... calls.
* Do not throw an exception when mdb_cursor_get returns EINVAL. This is needed when checking if the current
  cursor is still valid.
* Bug: continuing c.range() after cursor is made invalid.
* Add db_tcursor and db_tcursor_ move and copy assignment operators.
* Bug: source cursor not invalided in move assignment.
* Bug: MDB_GET_BOTH should be used when copying cursors with MDB_DUPSORT


### Various ###

* Ubuntu 22.4 compilation instructions.
* Ancient ubuntu: compilation fix.
* Bug: double abort after initializing sat_list.
* Delay removing active_adapters when interating over active_adapters.
* Bug: incorrect get_by_nit_tid_unique for dvbc and dvbt.
* Fix deadlocks because of multiple accesses to fe->ts by same thread.
* Bug: deadlock when adapter removed.
* Bug: resize_no_init for ss::string_ not adding trailing 0.
* Bug: lambda capture should store value instead of reference.


## Changes in version neumodvb-0.9 ##

SI-processing related

* Incorrect activation of PMT parsers
* Avoid showing incorrect SI data in tuned mux panel
* Faster detection of stable pat, resulting in faster SI scanning
* Better handle invalid SI information on 7.0E
* Handle SI-sections with different version numbers
* Also parse PMTs during regular SI scanning
* Avoid needless frequent epg scanning on services without epg

Spectrum and blind scanning

* When adapter is not available, show error message and gracefully end blindscan
* Properly handle multi-stream with the streams having the same network and ts id.
 These muxes would overwrite each other while scanning. Happens, e.g., on 14W.
* Avoid python back-traces when no peaks found in spectrum
* Fixed bug: when LNB changes due to clicking on a mux in the spectrum panel,
 the LNB select combobox does not show the newly selected LNB, which will be used for tuning
 but uses it anyway. This causes blind tunes to happen on unexpected LNBs
* Improvement: when the user loads a spectrum from file in spectrum scan, then selects a
 mux and blindscans, use the current LNB whenever possible, except if this one cannot
 tune the mux (wrong sat or wrong type of LNB). Only in that case, switch to the LNB originally used
 to capture the spectrum.
* Include not only hours and minutes but also seconds in names of files storing spectrum data. This
 solves the problem that spectrum acquisitions started within one minute would overwrite each other.
* The code now asks for a 50kHz resolution spectrum, leading to slightly slower scans in stid135.
 This detects more low symbol rate muxes.
* Fixed bug: DVBS2-QPSK streams incorrectly entered as DVBS1 streams in database
* Fixed bug: incorrect detection of roll-off parameters.
* More useful packet or bit error rate estimation. The new code measures errors before FEC/Viterbi/LDPC
 correction. This requires blindscan drivers with version > release.0.9.


Installation

* Prevent some files from installing

GUI

* Show disabled menu items as grayed out
* No longer use the single letters 'E' and 'C' as shortcuts, except in live mode
* Fixed bug that causes channels to scroll offscreen
* Fix bug that prevented proper navigation with arrow keys on EPG screen
* Fixed bug: onscreen display of SNR not working in radio mode
* Properly handle cases with unavailable SNR
* Fixed some layout bugs
* Add timing lock indicator (replaces pretty useless signal indicator)
* Immediately activate changes entered by user on "frontend list" screen. This is important
 when user enables/disables a frontend
* Add channel screenshot command
* Show more stream numbers in the positioner dialog and in the spectrum dialog on muxes with
 large number of streams

Compilation and internals

* Switch to clang20 by default except on "ancient Ubuntu" aka "Ubuntu LTS"
* Fix double close of cursors and incorrect freeing, leading to assertion failures
* Add command for building fedora rpms. Main problem to start really using this is a dependency
 on mpl-scatter-plot, which cannot be installed as an rpm
* Fix incorrect recompiling of pre-compiled headers with cmake > 3.19
* Removed dependency on setproctitle and made it less error-prone
* Add script for testing peak finding algorithm

DiSeqC

* Add more time between sending commands to cascaded swicthes, solving some erroneous switching


## Changes in version neumodvb-0.8 ##

Installation related:

* Improved installation instructions for fedora 36 and Ubuntu 20.04 LTS.


Spectrum acquisition and blindscan related:

* Added Export command, which can export service, mux, ... lists.
* Allow selecting multiple muxes for scanning, instead of selecting them one at a time and pressing `Ctrl-S` each time
* Allow blind scanning non detected peaks by selecting a region of spectrum graph.
* Improved positioning of frequency/symbol rate annotations on the spectrum graph. The labels are better
 readable and use less vertical space, preventing the need for vertical resizing in many cases.
* Added predefined pls code for 33.0E (BulgariaSat).
* Avoid python exception when spectrum scan results in zero detected peaks
* Avoid confusion between embedded and non-embedded streams on same frequency leading to missing or overwritten
 muxes in the database
* Bug when multiple subscriptions use the same mux, but one of them uses a t2mi substream;
* Prevent scan hanging for ever on some muxes
* Ensure that scan status is properly handled with embedded streams to prevent scans that never finish;
* Data race when starting embedded_stream_reader, causing tsp sub-process to be launched multiple times.
* Incorrect scan result reported on t2mi stream because of incorrect lock detection
* Autodiscover t2mi streams on 40.0E (workaround for detecting incompliant t2is)
* Handle empty NIT_ACTUAL in embedded stream on 40.0E 3992L T2MI. This
 causes the ts_id and original_network_id from the embedded t2mi SDT
 not to be taken into account.
* When tuning to t2mi mux from database record, preserve original_network_id
 and ts_id in the embedded si
* Allow resetting LNB LOF offset


EPG and recording related:

* Fixed layout bugs in grid epg.
* Improved handling of 'anonymous' recordings. These are recordings created by the user by
  entering start and end time instead of recording an epg record. In the new code, these
  anonymous recordings will be kept  as instructed by the user, but also secondary recordings
  will be created for each epg record matching the time window. This is convenient for services
  with only now/next epg, which may arrive after scheduling the anonymous recording. Anonymous
  recordings will be shown on the EPG screen, but will be overlayed with epg records, should these
  exist. Canceling a secondary epg recording also cancels the anonymous recording.


Playback and viewing:

* During live viewing and recordings playback, replace the original PMT with a minimal PMT
  containing only the video stream, and current audio and subtitle stream. This should prevent
  playback problems of services on 30.0W which share the same PMT. mpv tends to get confused
  about the audio stream selection in this case. This should also fix the missing sound on
  Sport Italia HD (5.0W).
* Incorrect indication of live/timeshift/rec status in live overlay
* Hide snr and strength when playing back a recording.
* Reduce buffering in tsp sub process to avoid delay when live viewing t2mi services

Various bugs fixed:

* anonymous recordings 60 times too long.
* Cannot activate edit mode in lnb network list

## Changes in version neumodvb-0.7 ##

Positioner related

* More consistent layout of positioner dialog. Disable some functions when unusable
* Fixes for DiSeqC12, e.g., new DiSeqC12 value not stored when user updates it
* Positioner bug fixes: send DiSeqC switch commands before sending any positioner command;
  report better error messages in GUI; properly handle continuous motion.
* Send DiSeqC switch commands before spectrum scan to avoid scanning the wrong lnb
* Prevent dish movement during mux scan
* Satellite is now only shown as "confirmed" (no question mark) if the position was actually
  found in the NIT table.
* Positioner dialog: show SNR and constellation even when tuner is not locked
* Avoid interference between positioner dialog and mux scan
* Disallow starting positioner dialog from some screens, when context does not allow guessing which
  lnb the user wants to use
* In positioner dialog allow negative SNRs. Yes, there are muxes which lock with a negative SNR!
* Documentation of positioner commands
* Allow rotors which can rotate by more than 65 degrees. The old code used to cause a goto south
  in this case.

Scanning related:

* Mux tables now show the source of the mux information. For instance if the frequency
 was found in the NIT table or rather from the tuner and if sat position was guessed or
 found in NIT
* Improved handling of bad data while scanning muxes:
  0 frequency (7.3W 11411H) or other nonsensical values;
  contradictory information in muxes from 0.8W and 1.0W;
  invalid satellite positions;
  on 42.0E, muxes are being reported as from 42.0W;
  some muxes report AUTO for modulation
  incorrect polarisation reported in C-band reuters mux on 22W;
  incorrect polarisation reported on 7.3W 11411H
  22.0E 4181V and other muxes which claim QAM_AUTO in NIT
* Fixed incorrectly reported roll-offs (may require driver update)
* Correctly handle some cases where mux or blindscan never finish
* Remove "scan-in-progress" data from database at startup to avoid an old scan from
  restarting when neumoDVB is restarted
* Improved handling of muxes reported on nearby satellites (e.g., 0.8W and 1.0W). Avoid creating
  duplicates in this case. The downside is that when scanning e.g., 0.8W the found mux may
  appear in the list for 1.0W and thus confuse the user
* Fixed incorrect display of scan status on mux list when mux scan is in progress
* Fixed bug where incorrect pls_code and pls_mode was retrieved from driver
* Correctly save DVBS2 matype in the database and do not show this value for DVBS
* Fix incorrect lock detection
* When scanning multistreams: retain tuning info when scanning next stream
* Spectrum blindscan now moves faster to the next candidate peak when mux does not contain
  a DVB stream

t2mi related:

* Fixed corruption in t2mi streams. This requires a patched tsduck as well.
* Improved handling of non-t2mi DVB-T service information on DVB-S muxes. Avoid entering
  such muxes as DVB-T
* correctly handle duplicate packets in transport streams; tsduck needs to be patched
  to also handle t2mi streams correctly

GUI related
* Handle some corruption in service and mux list
* Various GUI improvements.
* Changed key-binding for activating edit-mode.
* New "status list" screen shows which adapters are active and what they are doing
* Fix some crashes when ending program
* Fix some errors when service list is empty
* Allow user to select a stream_id even when dvb streams reports incorrectly that stream is
  not multistream
* Correctly update recordings screen when database changes
* Minimum RF level shown on live screen is now lower than before
* Fixed some issues with list filtering

Other

* Fixed some bugs in the code used to look up muxes. For instance, in rare cases a mux with
  the wrong polarization was returned
* Fixed bug: lnb offset correction was applied in wrong direction on C band
* Detection of whether dish needs to be moved was sometimes incorrect; database now contains
  the ultimate truth on this.
* Fixes to support Ubuntu 20.04
* Fixed race between closing and then reopening the same frontend, which caused sporadic assertions.
* correctly release lnb in more cases then before: when user closwes window; when spectrum blindscan
  finishes
* various other bugs
