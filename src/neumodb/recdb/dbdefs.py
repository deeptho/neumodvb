import os
import sys

from inspect import getsourcefile

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'recdb'

from generators import set_env, db_db, db_struct, db_enum, db_include

gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='r', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)

db_include(fname='chdb', db=db, include='neumodb/chdb/chdb_db.h')
db_include(fname='epgdb', db=db, include='neumodb/epgdb/epgdb_db.h')

list_filter_type = db_enum(name='list_filter_type_t',
                           db = db,
                           storage = 'int8_t',
                           type_id = 100,
                           version = 1,
                           fields=(('UNKNOWN', -1),
                                   'ALL_RECORDINGS',
                                   'SCHEDULED_RECORDINGS',
                                   'IN_PROGRESS_RECORDINGS',
                                   'COMPLETED_RECORDINGS',
                                   'FAILED_RECORDINGS'
                           ))


rec_type = db_enum(name='rec_type_t',
                     db = db,
                     storage = 'int8_t',
                     type_id = 100,
                     version = 1 ,
                     fields = (('NONE', 0),
		                           'RECORDING', #
		                           'IN_RESERVATION',
                     ))


marker_key = db_struct(name='marker_key',
                    fname = 'rec',
                    db = db,
                    type_id= ord('M'),
                    version = 1,
                    fields = ((1, 'milliseconds_t', 'time'), #in milliseconds since start
                              ))

marker = db_struct(name='marker',
                fname = 'rec',
                db = db,
                type_id= ord('m'),
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (ord('s'), 'packetno', ('packetno_start',)),
                ),                     #unique
                fields = ((1, 'marker_key_t', 'k'),
                          #first_packet and last_packet are packet indices
                          #they increment linearly starting from the time the channel was tuned
                          #In case of a recording the first packetno may therefore differ from 0
                          (2, 'uint32_t', 'packetno_start', 0xffffffff), #packet index of start of pat or pmt
                          (3, 'uint32_t', 'packetno_end'),  #packet index of end of i-frame
                ))

#range of data, which is part of a specific recording
#Each fragment covers a time interval which is represented by double time time values
# 1. play time: for all rec_fragments of the same recording, the play time intervals are  contiguous
#    (play_time_start of a fragment equals play_time_end of preceeding fragment)
# 2. stream_time: this is the original time at which the fragment was recorded. record_time intervals
#    are not contigious, e.g., due to commercial cut.
#At recording time (i.e., when not edited), both coincide
#All of these times are in milliseconds since an arbitrary start point
#TODO:  do we want play_time_start of the first rec_fragment to be 0 or not?


rec_fragment = db_struct(name='rec_fragment',
                         fname = 'rec',
                         db = db,
                         type_id= ord('a'),
                         version = 1,
                         fields = (
                             (1, 'milliseconds_t', 'play_time_start'), # in milliseconds
                             (2, 'milliseconds_t', 'play_time_end'), # in milliseconds
                             (3, 'milliseconds_t', 'stream_time_start'), # in milliseconds
                             (4, 'milliseconds_t', 'stream_time_end'), # in milliseconds
                         ))


rec = db_struct(name='rec',
                     fname = 'rec',
                     db = db,
                     type_id= ord('r'),
                     version = 1,
                     primary_key = ('key', ('epg.k',)), #unique
                     keys =  ((ord('S'), 'status_start_time', ('epg.rec_status', 'epg.k.start_time')),
                              (ord('T'), 'start_time', ('real_time_start',)),
                     ),                     #not unique
                     fields = (
                         (1, 'rec_type_t', 'rec_type'),
                         (14, 'int32_t', 'owner', -1), #pid of the process executing the recording, or -1
                                                       #when the recording is not active
                         (3, 'int32_t', 'subscription_id'), #subscription_id of recording in progress
                         #in milliseconds, relative to start tuning service
                         (4, 'milliseconds_t', 'stream_time_start'),
                         (5, 'milliseconds_t', 'stream_time_end'), #if missing => runs to end
                         #official end time unix epoch
                         (6, 'time_t', 'real_time_start'), #in unix epoch
                         #when the recording started (could be after or before end_time, e.g. because
                         #of power failure of post record time)
                         (7, 'time_t', 'real_time_end'), #in unix epoch
                         #@todo with the code below, pre/post_record_time are determined when the
                         #record is created. It could be useful to allow global values
                         #which could be changed afterwards by a single global preference
                         (8, 'time_t', 'pre_record_time'), # seconds to start recording early
                         (9, 'time_t', 'post_record_time'), # seconds to start recording late
                         (10, 'ss::string<256>', 'filename'), #relative path where recording will be stored
                         (11, 'chdb::service_t', 'service'),
                         (12, 'epgdb::epg_record_t', 'epg'),
                         (13, 'ss::vector<rec_fragment_t,0>', 'fragments')
                     ))


#map stream times to file_ids
file_key = db_struct(name='file_key',
                    fname = 'rec',
                    db = db,
                    type_id= ord('F'),
                    version = 1,
                         #play_time is linear time from start of playback, taking into account any removed
                         #parts of the recording
                         #stream_time is linear time from start of tuning to this channel
                    fields = ((1, 'milliseconds_t', 'stream_time_start'),
                              ))
#A file storing part of a live buffer or part of a recording
file = db_struct(name='file',
                fname = 'rec',
                db = db,
                type_id= ord('f'),
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (ord('f'), 'fileno', ('fileno',)),
                ),                     #unique
                fields = ((1, 'file_key_t', 'k'),
                          (2, 'int32_t', 'fileno'), #needed to avoid consulting db during timeshift to find how many records have been written
                          (3, 'milliseconds_t', 'stream_time_end', 'std::numeric_limits<milliseconds_t>::max()'), #in milliseconds
                          (4, 'time_t', 'real_time_start'), #unix epoch
                          (5, 'time_t', 'real_time_end'), #in unix epoch
                          (6, 'int64_t', 'stream_packetno_start'),  #redundant
                          (7, 'int64_t', 'stream_packetno_end', 'std::numeric_limits<int64_t>::max()'),   #redundant
                          (8, 'ss::string<128>', 'filename')
                ))


#The following is used to clean live buffers from the filesystem
#remaining after a crash and also to provide lists of live channels in the gui
#
live_service = db_struct(name = 'live_service',
                        fname='rec',
                        db = db,
                        type_id= ord('L'),
                        version = 1,
                         primary_key = ('key', ('owner', 'subscription_id')), #unique
                         keys =  (
                             #(ord('l'), 'update_time', ('update_time',)), #unique
                         ),
                        fields = (
                            (7, 'int32_t', 'owner', -1),
                            (8, 'int32_t', 'subscription_id', -1),
                            (1, 'time_t', 'creation_time'),
                            (2, 'int8_t', 'adapter_no'),
                            (3, 'time_t', 'last_use_time', '-1'), #-1 signifies still being used
                            (4, 'chdb::service_t', 'service'), #last used service
                            (5, 'ss::string<128>', 'dirname'),
                            #(6, 'epgdb::epg_record_t', 'epg') #currently active epg
                        ))



autorec = db_struct(name='autorec',
                    fname = 'rec',
                    db = db,
                    type_id= ord('e'),
                    version = 1,
                    primary_key = ('key', ('id',)), #unique
                    keys =  (
                        (lord('es'), 'service', ('service',)),
                    ),                     #not unique

                    fields = ((1, 'int32_t', 'id', '-1'), # -1 means "not set"
                              (2, 'chdb::service_key_t', 'service'), #sat_pos_none indicated: not set
                              (3, 'int32_t', 'starts_after', '0'), #in seconds from midnight
                              (4, 'int32_t', 'starts_before', '3600*24'), #in seconds from midnight
                              (5, 'int32_t', 'min_duration', '0'), #in seconds
                              (6, 'int32_t', 'max_duration', '2*3600'), #in seconds
                              (7, 'ss::vector<uint16_t,4>', 'content_codes', '0'), #any
                              (8, 'ss::string<16>', 'event_name_contains'),
                              (9, 'ss::string<16>', 'story_contains'),
                              (10, 'ss::string<16>', 'service_name'), #only for informational purposes
                              ))

stream_descriptor = db_struct(name='stream_descriptor',
                     fname = 'rec',
                     db = db,
                     type_id= lord('sd'),
                     version = 1,
                     primary_key = ('key', ('packetno_start',)), #unique
                     keys =  (
                     ),                     #not unique
                     fields = ((1, 'int64_t', 'packetno_start', '-1'), #index of last packet of the pmt in the stream
                                                                       #so pmt will apply from packet after this one on
                               (3, 'time_t', 'real_time_start'), #unix epoch
                               (4, 'milliseconds_t', 'stream_time_start'), #unix epoch
                               (8, 'uint16_t', 'pmt_pid', '0x1FFF'),
                               (5, 'ss::vector<chdb::language_code_t,4>', 'audio_langs'),
                               (6, 'ss::vector<chdb::language_code_t,4>', 'subtitle_langs'),
                               (7, 'ss::vector<uint8_t,64>', 'pmt_section')
                     ))

#Singleton listing recordings viewed
browse_history = db_struct(name ='browse_history',
                    fname = 'rec',
                    db = db,
                    type_id = ord('h'),
                    version = 1,
                    primary_key = ('key', ('user_id',)),
                    fields = ((1, 'int32_t', 'user_id'), #unique per subscription id?
                              (2, 'list_filter_type_t', 'list_filter_type', 'list_filter_type_t::ALL_RECORDINGS'),
                              (3, 'uint32_t', 'rec_sort_order', '((uint32_t) rec_t::subfield_t::stream_time_start)<<24'),
                              (4, 'ss::vector<rec_t,8>', 'recordings')
                    ))



"""
multi-file mpeg: and mpeg sttream captured from the same service, which is split over
multiple files, each containing part of the stream. The main idea is the following:

Rationale:
While viewing a channel, neumodvb stores the last N hours of video into the file system
to implement timeshift. At some point, old data must be discarded. This could be done with
a ringbuffer (thus using only one file), by punching holes at the start of a growing file
(thus also using only one file), or by splitting the ive buffer into multiple files and
the deleting or overwriting the oldest file.

A user may also schedule recordings. Ideally a scheduled recording should have all its data
in one file, so that this file can be played directly with external viewers. However there
are complications:
1. when a program is already in progress, and has data stored in the live buffer, the user may
only then decide to record the program. In this case data would have to be copied to the recording
file. The resulting IO could cause performance problems. Also, some locking would be needed to
prevent not yet copied data  from being overwritten in the live buffer.

2. recordings on the same channel can overlap in time (e.g., record extra data before start and
after end of program because program start time can very). During overlaps, we risk having to write
data 3 times to disk (live buffer, recording 1 and recording 2), again producing large io.

neumodb instead writes video data to disk only once, but spreads the data over multiple files. Live buffers and
recordings are databases containing pointers to fragments of data in those files. As data
is shared between recordings and live buffers, some care must be taken to delete data in the
files. For instance, a live buffer should delete old files or punch holes in its files only
when this data is not used by other live buffers or recordings.

Concretely, a recording or live buffer is therefore a directory containing multiple
datafiles and one index (database) file

file 1
 optional zero blocks (sparse file; holes are created in recordings to save disk space by removing unneeded bytes)
 range 1.1
 optional zero blocks
 range 1.2
 ...
file 2
 optional zero blocks (sparse file; holes are created to save disk space)
 range 2.1
 optional zero blocks
 range 2.2
 ...

...

file n

index
 table files (with recid a unique id to distinghuise between multiple logical programs indexed in the same
              index file; recid is a small integer)
   range1.1 descriptor: recid, file1, start time 1.1, end time 1.1, start pos and end pos of 1.1 in file 1
   range1.2 descriptor: recid, file1, start time 1.1, end time 1.1, start pos and end pos of 1.1 in file 1
   range2.1 descriptor: ...
   ....

 markers to datablocks containing one pat, one pmt and one i-frame
  (to associate byte positions with time stamps)
   marker 1:  time and bytepos of start/end of datablock, id of file in which block is located (block
     can be spread over multiple files)
   marker 2:
 ...

 epg and program data related to each recording
   record 1: recid, epg, channel ...

Allowing multiple logical programs (recordings) in one database files enables
-storing all metadata for live buffers and ongoing recordings in one database (simpler code)
-in finalized recording: have multiple versions of programs. e.g., with or without commercial cut,
 and with multiple versions, to implement undo


A recording will share data with the live buffer(s) if the recording takes places while live viewing
is in progress. It will also share data with overlapping recordings. This is implemented
with hard links.

recording 1:
 recid -> symlink to 1, sigfnifying that this recording has id 1
 file1 -> hard link to realfile1
 file2 -> hardlink to realfile2
 ...
 index1 -> hardlink to liveindex

recording 2:
 recid -> symlink to 2, sigfnifying that this recording has id 2
 file2 -> hard link to realfile2
 file3 -> hardlink to realfile3 (*: this file is still growing)
 index2 -> hardlink to liveindex

live buffer
 recid ->  symlink to 0, sigfnifying that this recording has id 0
 file1 -> hard link to realfile1
 file2 -> hard link to realfile2
 file3 -> hard link to realfile3 (*: this file is still growing)
 liveindex

During live watching, the code can perform periodic cleanup
-remove old data ranges not used by any recording (can be checked easily because all recordings
  with data ranges are visible in the database)
-remove old timestamps under the same conditions
-note: in case the number of hard links to a data file equals 1, livebuffer may assume that the
 data file is no longer in use by a recording and may delete any data and index items accordingly

After a recording is finished, the recording will be finalised as follows
-liveindex is copied over index1
-however: liveindex is not modified. If recording1 is later completely deleted,
 liveindex will not be aware of this, unless indirectly by noticing that the data file
 hard link no longer exists
-liveindex is now cleaned by removing all non relevant data; non relevant data is data in the index
 file with recid different from the live recid as indicated in the directory symlink

At any later time: housekeeping
-check all data files; if a datafile has hard link count  (indicating it is only in use by this
 recording) then also punch holes in the files for all non needed data ranges
 For instance, file1 may be 30 minutes long, but only the last 15 minutes belong to the current program
 => punch a hole of 15 minutes to remove 50% of disk usage

Export:
-merge all files into a single file, and optionally convert it to another format.



Scheduled Recording - tasks for tuner threads
0. If epg database has been reset (e.g., is empty), all "scheduled" rec_t records are updated to status new

all epg records epgdb.x received from stream are compared to all recdb.y rec_t records in recdb which have
a status different from "finished" or "failed"

when a matching record recdb.y is found  in recdb:
1.a if recdb.y is a new record (meaning that the epg code has not noticed it yet), the epg
    code looks up the epg recdb.z and updates start_time, end_time, event_name ... if needed
1.b if recdb.y is not a new record (meaning that the epg code has noticed it in the past),
    the above update is only performed if the epgdb.x has changed compared to its last value
    (this saves looking up recdb.z)
2. If in 1, recdb.y has changed, such that the recording starts earlier than planned (e.g. within a few
   seconds from now, or start time has moved to the past), the recording code is notified


Scheduled Recording - tasks for recording thread

Every 60 seconds, thread checks if database has changed (e.g., due to epg autorecs, or updating
of rec_t by epg code) by checking current transaction number. Thread maintains a "next start_time"
variable and starts/stops recordings as needed.

Alternatively, the thread can just recheck the recdb database looping over the list of scheduled recordings.
This requires a needless lookup of the first candidate rec_t record.

Recording threads will be woken up by epg code when needed, eliminating the possible 60 second worst case
wait time. Any recording programmed by an external program will be noticed after at most one minute.

When a user schedules a recording, the recording thread can also be woken up. This means an immediate
record will occur if the program is already in progress when the recording is scheduled.


Finished recording
A recording is finished by the recording thread.


"""
