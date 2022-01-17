import os
from inspect import getsourcefile
import sys

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'statdb'

from generators import set_env, db_db, db_struct, db_enum, db_include


gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='s', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)

db_include(fname='stats', db=db, include='neumodb/chdb/chdb_db.h')

signal_stat_entry = db_struct(name='signal_stat_entry',
                              fname = 'stats',
                              db = db,
                              type_id= lord('Ie'),
                              version = 1,
                              fields = ((1, 'float32_t',  'signal_strength'),
                                        (2, 'float32_t',  'snr'),
                                        (3, 'float32_t',  'ber')
                                        ))

#todo: this record could also be archived, but in that case
#it would be useful to have statistics per lnb, whereas for
#displaying signal stats, it is better to have a single record per adapter
#this requires more flexible grouping of records in multiple database
#and different keys per db
signal_stat = db_struct(name='signal_stat',
                fname = 'stats',
                db = db,
                type_id= ord('I'),
                version = 1,
                primary_key = ('key', ('lnb_key','mux_key', 'time')), #unique
                fields = ((1, 'chdb::mux_key_t', 'mux_key'),
                          (2, 'chdb::lnb_key_t', 'lnb_key'),
                          (3, 'int32_t', 'frequency'),
                          (4, 'chdb::fe_polarisation_t', 'pol'),
                          (5, 'time_t', 'time'), #when was first measurement taken?
                          (6, 'bool', 'live', 'true'), #measurement live or not
                          (9, 'ss::vector<signal_stat_entry_t, 24>',  'stats')
                )) #substream id dvb-s2 only



spectrum_key = db_struct(name='spectrum_key',
                         fname = 'stats',
                         db = db,
                         type_id= lord('sk'),
                         version = 1,
                         fields = ((1, 'chdb::lnb_key_t', 'lnb_key'),
                                   (2, 'int16_t', 'sat_pos'),
                                   (3, 'chdb::fe_polarisation_t', 'pol'),
                                   (4, 'time_t', 'start_time')
                                   ))


spectrum = db_struct(name='spectrum',
                     fname = 'stats',
                     db = db,
                     type_id= lord('s'),
                     version = 1,
                     primary_key = ('key', ('k',)), #unique
                     fields = ((1, 'spectrum_key_t', 'k'),
                               (2, 'uint32_t', 'start_freq'),
                               (3, 'uint32_t', 'end_freq'),
                               (4, 'uint32_t', 'resolution'),
                               (7, 'int16_t', 'usals_pos', 'sat_pos_none'), #exact usals pos
                               (6, 'bool', 'is_complete', 'true'),
                               (5, 'ss::string<256>', 'filename'), #relative path where spectrum stored
                               (8, 'ss::vector<int32_t, 2>', 'lof_offsets') #offset of the local oscillator (one per band)
                               ))
