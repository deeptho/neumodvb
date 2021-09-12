# Multithreading aspects
------------------------
The system runs the following threads which "own" the respective data structures
Only the owning threads can access data in the structures they own. This is implemented
by declaring all those structure as friends of each other



tuner_thread (singleton instance): recmgr, tuner, simgr, frontend
scam_thread  (singleton instance): scam, active_scam (multiple)
service_thread (multiple instances): active_service
frontend_thread (multiple instances): fe_monitor_t
receiver_tread  (singleton instance): dvb_frontend_t, adapter_reservation_t, dvb_adapter_t, dvbdev_monitor_t,  adaptermgr_t


Foreign threads can therefore only access public data/methods in these data stuctures
Those access must be restricted as follows:

-PRBABLY NT constant data in a data structure accessed via a shared_ptr (ok because this does not change after datastructure is created), as long as access -> memory reordering !
 is through shared pointers, such that object still exists

-methods which lock a mutex in the objects, as this ensures a consistent memory view

A foreign thread can also request the owner thread to perform a task, by entering this task on
the owning thread's task que. This calls methods in a callback class derived from one of the
data structures. Methods defined on the callback class can access all protected data and methods
of the data structures and can therefore do more. This is safe because it is executed by the owning
thread.


=========================
All data structures are kept alive by shared pointers in receiver_t: all_tuners, reserved_muxes, reserved_services
They are created by the main thread and are auto destroyted when all threads release them



----------------------

# Starting a channel by a user
=========================
0. user calls receiver_t::subscribe
1. main thread examines receiver.reserved_services and receiver.reserved_muxes
to find an appropriate tuner and lnb to stream the service

2. main tread executes dvb_receiver_t::subscribe_
main thread reserves the tuner, mux and service if they are already in use, but compatible
with the current request
-if the service is already active, no action is taken other than reservation (to keep the service running)
-if the mux is already active, no action is taken for the mux other than reservation (to keep the mux running),
otherwise the tuner thread is requested to execute tuner_thread.tune
-a new active_service_t is created, a service_thread is started on it
-the tuner thread is requested to execute tuner_thread.add_service

3. tuner_thread tunes one of the tuners using  frontend_t::tune_it; at the end of tuning, the PAT table is registered
3. tuner_thread executes  tuner_t::add_service. This stores a pointer to the currently active service
4. whenever a pat update is received, the tuner thread checks if the corresponding pmt has been registered
   and registers it
5. when a pmt_update is received, tuner.update_service_pmt is called; this requests the relevant service thread
to execute service_thread.activate

6. service_thread.activate registers the video, ... pids for its own stream


#Stopping a channel by a user
=========================
0. user calls receiver_t::unsubscribe
1. main thread unreserves the service and the mux in its own data structure
2. main thread requests a tuner_thread_t.remove_service, which unregisters the pmt pid and stops further is processing on it
3. main_thread requests a service.service_thread.deactivate, which will cause the service thread to exit
4. main thread requests a tuner_thread.deactivate IF tuner is no longer reserved

Note that 2+4 and 3 can run in parallel, but the main thread has to wait until all operations are finished
before retuning




#Spontaneously stopping a channel when a recording ends
==============================
Any threaf can simply call receiver_t.unsubscribe provided that the thread has not locked receiver.mutex
and if it does not wait for the futures. The easiest solution is to launch a background task which makes
the relevant call


#scam handling
================

	useful threads: https://tvheadend.org/issues/3073

There's already a ring buffer in the descrambler code in tvh. But it's small (about 550K).
Look for 'Fill a temporary buffer until the keys are known to make' string in src/descrambler/descrambler.c .
It should be probably configurable.

On other side, very large buffer can desynchronize the ecm/key responses, because there's no mechanism to
link ECM to the key response at the moment - so everything is implemented using comparing timestamps at the
moment. It's issue of DVBAPI, the newcamd protocol has sequence numbers, so the linking can be detected with
this protocol.

Regarding your linkage concerns, we know that the key is even/odd and what key we need, so as long as the buffer
is smaller than a cw cycle, this should not be an issue. Thanks for the hint where to start in the code, might
be interesting to play with it.


just some backround info why the CA system is important. In the case of NDS, the new CW arrives really short bevor
it is needed, so a buffer won't harm for almost a whole cw cycle (7 sec). On the other hand most other CA systems
send the new CW shortly after the old is invalid, which, when delaying eg for 1 second, would still be valid, but
overwritten by the new one. The patch did work fine with NDS, but it failed on CW and Nagra (all locals). I
remember getting glitches on CW for ORF when it was possible to emulate this card and the answer came faster than
expected. That's why there is a possibility to delay the CW answer in OSCAM.


tvheadend descrambler.c:1198
stream key[%d] is not valid
key validity based on time stamps

"quick ecm" means that key change happens very short time after ecm


The buffering scheme is trivial - the current code always decodes all data when the key is available and buffers
data up to specified amounf of TS packets in the general configuration when the key is not available.

I believe that client (player) should do proper buffering when the ECM is late. In this case, there's "time gap"
in the stream delivery until the key is not received and then all descrambled data from the temporary buffer are
flushed out (the temporary buffer becomes empty).


We have multiple strategies for handling scam

1. all data (video, audio, cat, ecm) is handled by the same service thread

Advantages:
-we know precisely the position of an ecm in the stream and can find with 100% certainty
which key change it preceeds

Disadvantages
-complex interaction between decryption and parsing:
ecms need to be parsed even if we cannot decrypt data yet;
video can only be parsed when we can decrypt. So we may need to parse data twice: first
parse/process ecms (will also need to examine/skip all video data at this pojnt). Then decrypt.
Then parse video (accessing data a second time)

-all ecm data will be stored in the recorded stream (could also have advantages), removing it is
tricky because byte positions may change (once video is parsed, data is entered into the database.
From that point on, any mixed in ecm data cannot be removed; so removal of such data must be done
while writing the database)

-in this case ecm data must be passed to the scam thread


2. separate threads for ecm data and channel data

Advantages:
-sections shared by multiple services on same mux are handled naturally (no duplicates)
-parsing ecm naturally separated from parsing video

Disadvantages:
-relative location of ecm vs video packets is lost. We must rely on timestamps to determine the
APPROXIMATE location of the ecms in the stream and similar time stamps for video. However, the order
in which both threads read and timestamp data is somewhat fuzzy


-stream starts
-datarange received
-wait first key, skipping all data until key is found; suppose first key is even
-install even and odd keys (odd key is old+invalid; even key is newest+valid) and start decrypting
-when a transition occurs form even->odd:
  -mark even key as being outdated;
  -check if odd key us more recent than even key; if so use it
  -if no recent enough off key is available, record current time; call it twait
  -wait for next odd key for a maximum period of time
  -if next received key is odd and occurs before timeout, check request time t1 of this key
  if twait>=t1, consider the key valid; install it and continue decrypting

  -otherwise it is outdated

When waiting for a key with parameter (twait)
 -wait for a new key to be found
 -if t_request(key) > twait => need to skip data until next transition to correct parity,
                               which also needs to have t>= twait
                            => this means we have to note the timestamp of the tranisitons (requires
                               minimal parsing)
                            => alternatively: just discard everything up to now, or count the number
                               of transitions and compare to msgid?
                            => other alternative: after reading data, check all outstanding key requests,
                               find all the ones in the past without a "current byte pos" and set "current
                               byte pos" to the current byte pos.
                               When a new key arrives, we need to skip data until "current byte pos" of the
                               key, and then skip more data until we reach the correct parity

                               This is problematic, because "current_byte_pos" is specific for a service,
                               but the filtered data being sent is associated with a pid

 -if t_request(key) < twait => install it and continue decryption




-if key found (discard all data until t>=t1 => would require timestamping received data)
 so instead discard all data until now and install key






3. some compromise: service thread reads ecms, but only to find their position. In this case we still
need to syncrhonize these ecms with those in the scam thread



#recordings handling
===================

user toggles a recording in user interface. First the corresponding service and epg records and the best
matching recording  are located. If a matching recording is found, the recording is deleted and/or stopped.
Otherwise a new recording is inserted in the global database and/or started.


Creating a new recording:
dvb_receiver_t::toggle_recording:
  calls recordings.new_recording to insert a new rec_t and epg_record_t and service_t record into the
  global database. At this point tuner_thread can start noticing the recording

  calls tuner_thread.push_task(tuner_thread.new_schedrec) to inform tuner thread that this record is new
  This has the side effect of waking up tuner_thread (main reason for having this call) and for
  informing it how to efficiently update next_recording_event_time.

=> processing in tuner_thread:
  update  next_recording_event_time which determines how long to sleep
  run_tasks() will start the recording if needed


Cancelling a recording
dvb_receiver_t::toggle_recording:
  calls recordings.delete_recording  removes rec and the correspondign epg_record from the
  global database. tuner_thread will no longer be able to start the recording, but more action
  is needed in case the recording has already started

  calls tuner_thread.push_task(tuner_thread.delete_schedrec) to inform tuner thread that this record
  has been deleted. This has the side effect of waking up tuner_thread (so that effect of deleting
  recording is almsot immediate)

=> processing in tuner_thread:
  calls  run_tasks() will stop the recording if it is already in progress


An epg record is updated (start_time,  end_time or event_name changes

si.on_epg_update is called from tuner_thread, but only for new and changed records. We also process
new messages, so that we can also handle autorecs and re-initialised epg database
    tuner.tuner_thread.on_epg_update:
      recmgr.on_epg_update



#live buffer management
===========================

a live buffer is tied to a specific adapter and service (or perhaps even to a specific lnb and service)

a service remains active if any of the following is true
-live buffer is currently on screen
-live buffer has a recording in progress
-live buffer has a reservation in progress

when the live buffer is not on screen and the last reservation or recording ends
(typically a few minutes after the end of an epg event), the service stops streaming, i.e.,
becomes inactive

At this point the live buffer can be preserved on disk for a while (15 minutes) to allow
the user to still see the program, by retuning the service. We now have a potential problem: if
the user retunes, the service may start on a different adaptor.

One solution could be to make the livebuffer only depend on the service (not the adapter). This has
the (minor) downside that it becomes impossible to start the same service on multiple adapters (which
could be useful for some debugging purposes (e.g., compare output of 2 dvb cards)

Another solution would be to never allow reusing a live buffer on which streaming was stopped.
In this case, either the live buffer is discarded, or the user should be provided some mechanism
to return to the buffer, which is different from retuning.


A reservation is like a recording, except that it is automatically deleted when streaming ends,
or a fixed time later. A reservation can also be "indefinite"


When zapping, a user has the option to to "preserve" a live service. This creates an indefinite
reservation, which keeps the service streaming. When latter the user zaps to the service again,
the live buffer is still available. Alternatively, the reservaton ccan auto expire, e.g., 30 minutes after
last viewing the live buffer. The main advantage is that such a service does not keep streaming forever
if the user has forgotten about it. The main disadvantage is that streaming may stop an inapproriate time:
e.g., user may "preserve" live channel just before a program of interest starts (e.g., commercials still
on), then zap away, and expect to be able to return to the live service, only to discover that only the
commercial was preserved. A rule could be that a live buffer auto-expires 30 minutes after it has last
been viewed.

reservation
-has an epg record (so that it can adjust to changing start times).
-is started just like a recording, and then is put on screen
-when user zaps, the presence of a reservation preserves the live buffer, but gives
 it an expiry time, which is equal to min(now+30, end_of_current_program +30 minutes)

====================
mpm format

an mpm file part starts at file.stream_packetno_start and ends at  file.stream_packetno_end
a marker indicates the start and end of an access unit containing an iframe, pmt and pat.
marker.packetno_end can be in a different file part than marker.packetno_start



=======================
update live screen


active_service->mpm.db contains all data about the current live buffer (including old epg epg)
active_service also provides times for first/last byte in the live buffer

=======================
epg update for live buffer and recordings

when service starts streaming, available epg is read for a period of at least two hours
by 	active_service_t::update_epg(now) call in active_service_t constructor
This info is stored in the local database of the live buffer or recording

Every 30 minutes period.run() in active_service_t::housekeeping updates this epg
data. This is needed because only for new epg records  active_service_t::on_epg_update
is called and we need to make sure that we also look at old, unmodified epg data.

Each time an epg record is updated or a new one arrives, the epg code calls
active_service_t::on_epg_update and this in turn will insert the record
in the local (mpm) database if it is not for too far in the future (in the latter case,
periodic.run will handle it)

As an alternative to period.run(), we can also use  meta_marker_t::need_epg_update
to check if we need an update for the current live stream and then call
active_service_t::update_epg_.  meta_marker_t::need_epg_update has a safety measure to prevent
excessive calling in case there is no epg available. It ensures that an update can be called only
evert 2 minutes t most.


====================
thread 1 (wxwidgets( calls receiver_t::toggle_recording
 -> calls thread 30 tuner_thread_t::cb_t::toggle_recording

thread 30 (tuner):
 -> calls  recmgr.toggle_recording
 -> calls receiver_t::stop_recording
 -> calls thread 12: receiver_thread_t::cb_t::stop_recording

thread 12 (receiver):
 -> calls receiver_t::stop_recording
 -> calls thread receiver_thread_t::cb_t:unsubscribe
    which waits for thread 30 to execute a tuner_thread task:
      = cb(tuner.tuner_thread).remove_service(tuner, service);



# mux changes handling
======================

After tuning to a mux, several types of changes can be found to this mux

1. fe_monitor_t can discover a corrected value for the mux's frequency based on blindscan
information from the driver. Other parameters, such as symbol_rate can also change, but
frequency is more important. For instance if we want to tune to a new mux, we must check if
it is the same as a mux in use. This test can be based on ts_id and network_id, but the true
test is if the frequency and polarisation match approximately, as ts_id and network_id could change
in some cases. This is especially the case when the mux initially is intered by the user as a template
(only frequency and polarisation are set; ts_id is a dummy value)

fe_monitor_t and dvb_frontend_t and dvb_adapter_t provide functions to store this
information in chdb::signal_info_t, but tehy never change the values in the chdb::any_mux_t records
they contain internally (which are mostly used for display purposes)

2. the si code can detect that the ts_id and/or network_id for the mux have changed.
Also information in the stream can find the correct frequency. All this information
is updated in si_t::current_tp. Note that frequency may differe from what is found by fe_monitor_t,
e.g. because of frequency offset in the lnb

So now we face the problem of inconsistent information:
-receiver_thread_t maintains mux_t records and  uses them to determine what mux an adapter is currently
tuned to, to determine if this tuner can be used for tuning to a second service

-tuner_thread_t uses its own current_tp to retune if needed (and for printing debug messages),
but it really should use the most uptodate data
It also updates si.current_tp based on stream data

-fe_monitor_threads does not use reserved_mux, but some of its functions like is_same_mux
are called from other threads to attempt to determine if the frontend is tuned to a specific
mux. Currently this test is based on ts_id, network_id which is not what we want
