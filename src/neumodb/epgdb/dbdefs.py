import os
import sys

from inspect import getsourcefile

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'epgdb'

from generators import set_env, db_db, db_struct, db_enum, db_include

gen_options=set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='e', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)

db_include(fname='chdb', db=db, include='neumodb/chdb/chdb_db.h')

"""
transponders are identified by the key:
uint16_t: satpos
uint16_t: nit_id
uint16_t: ts_id
uint16_t: distinghuisher; //use for cases where multiple transponders share the same ts_id
                          //When adding a new mux, we always check the list and pick the next highest number


"""
rec_status = db_enum(name='rec_status_t',
                     db = db,
                     storage = 'int8_t',
                     type_id = 101,
                     version = 1 ,
                     fields = (('NONE', 0), #uninitialised
		                           'SCHEDULED', #epg code is monitoring this record
		                           'IN_PROGRESS', #currently recording
                               'FINISHING', #recording is being copied from a livebuffer
		                           'FINISHED', #recording has finished
		                           'FAILED')) #recording has failed

#needs to be compatible with values in dbdepfs.py<chdb>
epg_type = db_enum(name='epg_type_t',
              db = db,
              storage = 'int8_t',
              type_id = 100,
              version = 1,
              fields = (('epg_invalid', -1),
		                    ('epg_dvb', 0),
		                    ('epg_freesat',1),
                        ('epg_fst_home',2),
		                    ('epg_sky', 3),
                        ('epg_skyit', 4),
                        ('epg_movistar', 5),
                        ('epg_viasat', 6)
                        ))





epg_source = db_struct(name='epg_source',
                       fname = 'epg',
                       db = db,
                       type_id= ord('S'),
                       version = 1,
                       fields = ((1, 'epg_type_t', 'epg_type'),
                                 (2, 'uint8_t', 'table_id'),
	                               (3, 'uint8_t', 'version_number'),
	                               (5, 'int16_t', 'sat_pos'),
	                               (6, 'uint16_t', 'network_id'),
	                               (7, 'uint16_t', 'ts_id')))


epg_key = db_struct(name='epg_key',
                    fname = 'epg',
                    db = db,
                    type_id= ord('E'),
                    version = 1,
                    fields = ((1, 'chdb::service_key_t', 'service'),
	                            (2, 'time_t', 'start_time'),
                              (3, 'uint32_t', 'event_id'), #ids from 0 to 0xffff come from dvb streams
                              (4, 'bool', 'anonymous', 'false')
                    ))


epg_record = db_struct(name='epg_record',
                       fname = 'epg',
                       db = db,
                       type_id= ord('e'),
                       version = 1,
                       primary_key = ('key', ('k',)), #unique
                       keys =  (
                       ),                     #not unique, could be split in unique and non-unique later
                       fields = ((1, 'epg_key_t', 'k'),
                                 (3, 'uint16_t',  'parental_rating'),
                                 (4, 'epg_source_t', 'source'),
                                 (5, 'time_t', 'end_time'),
                                 (6, 'time_t', 'mtime'),
                                 (7, 'rec_status_t', 'rec_status', 'rec_status_t::NONE'), #indicates that program will be recorded
                                 (10, 'uint16_t', 'series_link', '0xffff'), #indicates that program will be recorded
                                 (8, 'ss::string<64>', 'event_name'),
                                 (9, 'ss::string<256>', 'story'),
                                 (12, 'ss::string<256>', 'service_name'),
                                 (11, 'ss::vector<uint16_t,4>',  'content_codes')),
                       filter_fields = (
                           ('epg_service', 'k.service'),
                       )
                       )


"""
Where should sched_rec_t records be stored? Suppse we do NOT store them in epgdb but in recdb.
Race situtations might occur:
 1-a new sched_rec_t is created by user or by epg code from an auto_rec
 2-this record is stored in recdb
 3-the epg code starts monitoring this new record
 4-because this is a new record, the epg code must first check for a similar existing record,
  which may have updated start/end or event_name  (such changes are very unlikely at this point! So this
  step might be skipped)
 5-whenever new epg records are found, epg code determines if sched_rec.epg should be updated
 6-if an "urgent" update occurs (new start_time is in the past), rec code must be notified (unlikely)

Possible race: between 1 and 3 an epg record update could have been received.
                this is why we have step 4, and also why step 4 is executed by the epg
                code and not by another thread



"""
