import os
from inspect import getsourcefile
import sys

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'chdb'

from generators import set_env, db_db, db_struct, db_enum


gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='c', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)


"""
transponders are identified by the key:
uint16_t: satpos
uint16_t: nit_id
uint16_t: ts_id
uint16_t: extra_id; //use for cases where multiple transponders share the same ts_id
                          //When adding a new mux, we always check the list and pick the next highest number

"""

list_filter_type = db_enum(name='list_filter_type_t',
                           db = db,
                           storage = 'int8_t',
                           type_id = 100,
                           version = 1,
                           fields=(('UNKNOWN', -1),
                                   'ALL_SERVICES',
                                   'SAT_SERVICES',
                                   'ALL_CHANNELS',
                                   'BOUQUET_CHANNELS'
                           ))


group_type = db_enum(name='group_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', -1),
                           'FAVORITE',
                           'BOUQUET',
                           'NETWORK',
                           'SAT',
                           #'ENTRY',
                           'ANY'
                           ))

media_mode = db_enum(name='media_mode_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', 0),
                           ('TV', 1),
                           ('RADIO', 2),
                           ('DATA', 3),
                           ('T2MI', 4),
                           ))
scan_status = db_enum(name='scan_status_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('PENDING', 0), #mux will be scanned soon
                       ('IDLE', 1),  #mux has been scanned before
                       ('ACTIVE', 2), #mux is being scanned
                       ('NONE', 3), #mux has never been scanned
                       ('RETRY', 4) #mux was scanned but experienced temporary failure. rescanning is needed
                           ))

#Where does tuning paramter data come from?
tune_src = db_enum(name='tune_src_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('TEMPLATE', 0), #temporary values entered by user
                       ('NIT_TUNED', 1),      #mux has been tuned, confirming tuning parameters
                                        #freq is value from nit, but some values overridden from driver

                       ('NIT_ACTUAL', 2),     #mux has not been tuned
                                        #freq is value from NIT_ACTUAL table


                       ('NIT_OTHER', 3),     #mux has not been tuned
                                        #freq is value from NIT_OTHER table


                       ('DRIVER', 4),  #mux has been tuned
                                       #tuning parameters from driver

                       ('USER', 5),    #user has locked the data from being overwritten
                       ('AUTO', 6), #temporary state: user has turned off "USER", but source of data is unknown
                       ('UNKNOWN', -1), #not initialised
                   ))

key_src = db_enum(name='key_src_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('NONE', -1),  #IDs randomly chosen, no SI data available afer tuning
                       ('SDT_TUNED', 1),   #mux has been tuned, ids from SDT
                       ('NIT_TUNED', 2),   #mux has been tuned, only NIT read from stream,
                       ('PAT_TUNED', 3),   #mux has been tuned, but no valid SDT/PAT; ts_id is reliable network_id not
                                                        #no valid NIT_ACTUAL and SDT_ACTUAL
                                                        #nid is invalid, ts_id comes from PAT
                       ('NIT_ACTUAL', 4),   #mux has NOT been tuned, found in NIT_ACTUAL
                       ('NIT_OTHER', 5),   #mux has NOT been tuned, found in NIT_OTHER
                       ('SDT_OTHER', 6),   #mux has NOT been tuned, found in NIT_OTHER
                       ('USER', 7), #user has locked the data from being overwritten
                       ('AUTO', 8) #temporary state: user has turned off "USER", but source of data is unknown
                           ))

scan_result = db_enum(name='scan_result_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('NONE', 0), #never tried
                       ('NOLOCK', 1), #could not lock. No fec lock
                       ('ABORTED', 2), #timeout stage was not reached
                       ('PARTIAL', 3), #timeout stage was reached, but result was incomplete
                       ('OK', 4), #completion stage reached or timedout
                       ('NODATA', 5), #completion stage reached or timedout
                       ('NOTS', 6), #not a transport stream, but was locked
                       ('BAD', 7), #illegal tuning parameter or tune failure
                       ('TEMPFAIL', 8), #scanning failed because of lack of resources
                       ('DISABLED', 127),
                   ))

lock_result = db_enum(name='lock_result_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('NONE', 0), #never tried
                       ('NOLOCK', 1), #could not lock. No fec lock
                       ('TMG', 2), #
                       ('CAR', 3), #
                       ('FEC', 4), #fec lock
                       ('SYNC', 5), #sync bytes received (dvbs2)
                   ))

fe_polarisation = db_enum(name='fe_polarisation_t',
              db = db,
              storage = 'int8_t',
              type_id = 100,
              version = 1,
              fields = (('NONE',-1),
	                      'H',
	                      'V',
	                      'L',
	                      'R'
	            ))


fe_spectral_inversion = db_enum(name='fe_spectral_inversion_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'INVERSION_OFF',
	          'INVERSION_ON',
	          'INVERSION_AUTO'
            )))


fe_code_rate = db_enum(name='fe_code_rate_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          ('FEC_NONE',0),
	          'FEC_1_2',
	          'FEC_2_3',
	          'FEC_3_4',
	          'FEC_4_5',
	          'FEC_5_6',
	          'FEC_6_7',
	          'FEC_7_8',
	          'FEC_8_9',
	          'FEC_AUTO',
	          'FEC_3_5',
	          'FEC_9_10',
	          'FEC_2_5',
	          'FEC_5_11',
	          'FEC_1_4',
	          'FEC_1_3',
	          'FEC_11_15',
	          'FEC_11_20',
	          'FEC_11_45',
	          'FEC_13_18',
	          'FEC_13_45',
	          'FEC_14_45',
	          'FEC_23_36',
	          'FEC_25_36',
	          'FEC_26_45',
	          'FEC_28_45',
	          'FEC_29_45',
	          'FEC_31_45',
	          'FEC_32_45',
	          'FEC_4_15',
	          'FEC_5_9',
	          'FEC_7_15',
	          'FEC_77_90',
	          'FEC_7_9',
	          'FEC_8_15',
	          'FEC_9_20'
        )))


fe_modulation = db_enum(name='fe_modulation_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'QPSK',
	          'QAM_16',
	          'QAM_32',
	          'QAM_64',
	          'QAM_128',
	          'QAM_256',
	          'QAM_AUTO',
	          'VSB_8',
	          'VSB_16',
	          'PSK_8',
	          'APSK_16',
	          'APSK_32',
	          'DQPSK',
	          'QAM_4_NR',
            ('DUMMY_PLF', 64),
        )))

fe_transmit_mode = db_enum(name='fe_transmit_mode_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'TRANSMISSION_MODE_2K',
	          'TRANSMISSION_MODE_8K',
	          'TRANSMISSION_MODE_AUTO',
	          'TRANSMISSION_MODE_4K',
	          'TRANSMISSION_MODE_1K',
	          'TRANSMISSION_MODE_16K',
	          'TRANSMISSION_MODE_32K',
	          'TRANSMISSION_MODE_C1',
	          'TRANSMISSION_MODE_C3780',
        )))

fe_bandwidth = db_enum(name='fe_bandwidth_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'BANDWIDTH_8_MHZ',
	          'BANDWIDTH_7_MHZ',
	          'BANDWIDTH_6_MHZ',
	          'BANDWIDTH_AUTO',
	          'BANDWIDTH_5_MHZ',
	          'BANDWIDTH_10_MHZ',
	          'BANDWIDTH_1_712_MHZ',
        )))

fe_guard_interval = db_enum(name='fe_guard_interval_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'GUARD_INTERVAL_1_32',
	          'GUARD_INTERVAL_1_16',
	          'GUARD_INTERVAL_1_8',
	          'GUARD_INTERVAL_1_4',
	          'GUARD_INTERVAL_AUTO',
	          'GUARD_INTERVAL_1_128',
	          'GUARD_INTERVAL_19_128',
	          'GUARD_INTERVAL_19_256',
	          'GUARD_INTERVAL_PN420',
	          'GUARD_INTERVAL_PN595',
	          'GUARD_INTERVAL_PN945',
        )))


fe_hierarchy = db_enum(name='fe_hierarchy_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'HIERARCHY_NONE',
	          'HIERARCHY_1',
	          'HIERARCHY_2',
	          'HIERARCHY_4',
	          'HIERARCHY_AUTO',
)))

fe_interleaving = db_enum(name='fe_interleaving_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'INTERLEAVING_NONE',
	          'INTERLEAVING_AUTO',
	          'INTERLEAVING_240',
            'INTERLEAVING_720',
        )))

fe_pilot = db_enum(name='fe_pilot_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'ON',
	          'OFF',
	          'AUTO',
        )))

fe_rolloff = db_enum(name='fe_rolloff_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          'ROLLOFF_35',
            'ROLLOFF_25',
	          'ROLLOFF_20',
	          'ROLLOFF_AUTO',
            'ROLLOFF_15',
            'ROLLOFF_10',
            'ROLLOFF_5',
        )))


delsys_type = db_enum(name='delsys_type_t',
                      db = db,
                      storage = 'int8_t',
                      type_id = 100,
                      version = 1,
                      fields = ((
                          ('NONE'),
	                        ('DVB_S'),
                          ('DVB_T'),
                          ('DVB_C')
                          #('ATSC_T'), Several other, as yet unsupported due to lack of hardware and signals
                          )))

fe_delsys = db_enum(name='fe_delsys_t',
                  db = db,
                  storage = 'int8_t',
                  type_id = 100,
                  version = 1,
                  fields = ((
	                    ('SYS_UNDEFINED'),
	                    ('SYS_DVBC_ANNEX_A'),
	                    ('SYS_DVBC_ANNEX_B'),
	                    ('SYS_DVBT'),
	                    ('SYS_DSS'),
	                    ('SYS_DVBS'),
	                    ('SYS_DVBS2'),
	                    ('SYS_DVBH'),
	                    ('SYS_ISDBT'),
	                    ('SYS_ISDBS'),
	                    ('SYS_ISDBC'),
	                    ('SYS_ATSC'),
	                    ('SYS_ATSCMH'),
	                    ('SYS_DTMB'),
	                    ('SYS_CMMB'),
	                    ('SYS_DAB'),
	                    ('SYS_DVBT2'),
	                    ('SYS_TURBO'),
	                    ('SYS_DVBC_ANNEX_C'),
	                    ('SYS_DVBC2'),
                      ('SYS_DVBS2X'),
	                    ('SYS_DCII'),
                      ('SYS_AUTO')
                      )))


fe_delsys_dvbs = db_enum(name='fe_delsys_dvbs_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          ('SYS_AUTO', 22),
	          ('SYS_DVBS', 5),
	          ('SYS_DVBS2', 6),
	          ('SYS_ISDBS', 9),
	          ('SYS_TURBO', 17),
        )))

#dvbc and atsc-c and ISDB-C
fe_delsys_dvbc = db_enum(name='fe_delsys_dvbc_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          ('SYS_UNDEFINED', 0),
	          ('SYS_DVBC', 1), #'SYS_DVBC_ANNEX_A',
	          ('SYS_ATSC_C', 2), #'SYS_DVBC_ANNEX_B',
            ('SYS_ISDBC', 10),
	          ('SYS_DVBC_J', 18)  #'SYS_DVBC_ANNEX_C'
        )))


fe_delsys_dvbt = db_enum(name='fe_delsys_dvbt_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
	          ('SYS_UNDEFINED', 0),
	          ('SYS_DVBT', 3),
	          ('SYS_DVBT2', 16),
	          ('SYS_TURBO', 17),
        )))


fe_caps =  db_enum(name='fe_caps_t',
                   db =db,
                   storage = 'uint32_t',
                   type_id = 100,
                   version = 1,
                   fields = ((
	                              ('FE_CAN_FEC_AUTO', 0x200),
	                              ('FE_CAN_QPSK', 0x400),
	                              ('FE_CAN_TRANSMISSION_MODE_AUTO', 0x20000),
	                              ('FE_CAN_BANDWIDTH_AUTO', 0x40000),
	                              ('FE_CAN_GUARD_INTERVAL_AUTO', 0x80000),
	                              ('FE_CAN_HIERARCHY_AUTO', 0x100000),
	                              ('FE_CAN_MULTISTREAM', 0x4000000),
	                              ('FE_CAN_TURBO_FEC', 0x8000000),
                                ('FE_CAN_2G_MODULATION', 0x10000000)
                                )))



""""

DVB-C ATSC-C ISDB-C
--------
Delivery system:
Frequency (Hz):
Symbol rate (Sym/s):
Constellation:
FEC:
PLP ID:



ISDB-T
------------
Delivery system:
Frequency (Hz):
Bandwidth:
Guard interval:
Layer A: FEC:
Layer A: Constellation:
Layer A: Segment count:
Layer A: Time interleaving:
Layer B: FEC:
Layer B: Constellation:
Layer B: Segment count:
Layer B: Time interleaving:
Layer C: FEC:
Layer C: Constellation:
Layer C: Segment count:
Layer C: Time interleaving:
"""

fe_pls_mode = db_enum(name='fe_pls_mode_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
            ('ROOT', 0),
            'GOLD',
            'COMBO',
        )))


spectral_peak = db_struct(name='spectral_peak',
                    fname = 'mux',
                    db = db,
                    type_id= lord('sp'),
                    version = 1,
                    fields = ((1, 'uint32_t', 'frequency', '0'),
                              (2, 'uint32_t', 'symbol_rate', '0'),
                              (3, 'fe_polarisation_t', 'pol', '')
                              ))


#key unique on satelliye
#furthermore all muxes with the same mux_id must have the same tuning parameters
#in case of conflict the entry with stream_id=-1, t2mi_pid = -1 are authorative
#value -1 means: no stream_id or not an embedded stream
#value mux_id=0 is not valid, means "uninitialised"
mux_key = db_struct(name='mux_key',
                    fname = 'mux',
                    db = db,
                    type_id= ord('P'), #TODO: duplicate
                    version = 1,
                    fields = ((1, 'int16_t', 'sat_pos', 'sat_pos_none'), #16000 DVBC 16001=DVBT
                              (2, 'int16_t', 'stream_id', '-1'),
                              (5, 'int16_t', 't2mi_pid', -1), #we would brefer 0x1fff but this interferes
                                                              #with using default key as lower bound in find functions
                              (4, 'uint16_t', 'mux_id')
                              ))


#special epg types
epg_type = db_enum(name='epg_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', -1),
                           'DVB',
                           'FREESAT',
                           'FSTHOME',
                           'SKYUK',
                           'SKYIT',
                           'MOVISTAR',
                           'VIASAT',
                           ))

mux_common = db_struct(name='mux_common',
                    fname = 'mux',
                    db = db,
                    type_id= ord('M')+256*1, #TODO: duplicate
                    version = 1,
                    ignore_for_equality_fields = ('mtime',),
                    fields = ((1, 'time_t', 'scan_time'),
                              (3, 'scan_result_t', 'scan_result'),
                              (18, 'lock_result_t', 'scan_lock_result'),
                              (8, 'time_t', 'scan_duration'),
                              (5, 'bool', 'epg_scan'),
                              (2, 'scan_status_t', 'scan_status', 'scan_status_t::NONE'),
                              (12, 'uint32_t', 'scan_id', '0'),
                              (4, 'uint16_t', 'num_services'),
                              (16, 'uint16_t', 'network_id'), #usually redundant
                              (17, 'uint16_t', 'ts_id'), #usually redundant
                              (14, 'uint16_t', 'nit_network_id'), #usually redundant
                              (15, 'uint16_t', 'nit_ts_id'), #usually redundant

                              (11, 'tune_src_t', 'tune_src', 'tune_src_t::AUTO'),
                              (13, 'key_src_t', 'key_src', 'key_src_t::NONE'),

                              (7, 'time_t', 'mtime'),
                              (9, 'ss::vector<epg_type_t,2>', 'epg_types'),
                              ))

"""
DVB-S, ISDB-S
---------
Frequency (kHz):
Symbol rate (Sym/s):
Polarization:
Modulation:
FEC:
Rolloff:
Pilot:
ISI (Stream ID):
PLS mode:
PLS code:
"""
dvbs_mux = db_struct(name='dvbs_mux',
                fname = 'mux',
                db = db,
                type_id= ord('m'),
                version = 2,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (lord('sf'), 'sat_pol_freq', ('k.sat_pos', 'pol', 'frequency')),
                (lord('sn'), 'network_id_ts_id_sat_pos', ('c.network_id', 'c.ts_id', 'k.sat_pos')),
                (lord('ss'), 'scan_status', ('c.scan_status', 'pol', 'frequency')),
                ),
                fields = ((17, 'mux_key_t', 'k'),
                          (2, 'chdb::fe_delsys_dvbs_t', 'delivery_system', 'chdb::fe_delsys_dvbs_t::SYS_DVBS2'),
                          (3, 'uint32_t', 'frequency'),
                          (9, 'chdb::fe_spectral_inversion_t', 'inversion'),
                          (4, 'chdb::fe_polarisation_t',  'pol', 'chdb::fe_polarisation_t::V'),
                          (5, 'uint32_t',  'symbol_rate', '27500000'),
	                        (7, 'chdb::fe_modulation_t', 'modulation', 'chdb::fe_modulation_t::PSK_8'),
	                        (8, 'chdb::fe_code_rate_t',  'fec', 'chdb::fe_code_rate_t::FEC_AUTO'), #aka fec_inner: dvb-s, dvb-c, dvb-t
	                        (10, 'chdb::fe_rolloff_t', 'rolloff'),
                          (11, 'chdb::fe_pilot_t', 'pilot'),
	                        (13, 'chdb::fe_pls_mode_t', 'pls_mode', 'fe_pls_mode_t::ROOT'),
                          (16, 'int16_t', 'matype', '-1'),
	                        (14, 'uint32_t', 'pls_code',  1),
                          (15, 'mux_common_t', 'c')
                ))


dvbc_mux = db_struct(name='dvbc_mux',
                fname = 'mux',
                db = db,
                type_id= ord('m')+256*1,
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (lord('cf'), 'freq', ('frequency',)),
                (lord('cn'), 'network_id_ts_id_sat_pos', ('c.network_id', 'c.ts_id')),
                (lord('cs'), 'scan_status', ('c.scan_status', 'frequency')),
                ),                     #not unique, could be split in unique and non-unique later
                fields = ((11, 'mux_key_t', 'k'),
                          (2, 'fe_delsys_dvbc_t', 'delivery_system', 'fe_delsys_dvbc_t::SYS_DVBC'),
                          (3, 'uint32_t', 'frequency'),
                          (10, 'chdb::fe_spectral_inversion_t', 'inversion'),
                          (4, 'uint32_t', 'symbol_rate', '27500000'),
	                        (5, 'chdb::fe_modulation_t', 'modulation', 'chdb::fe_modulation_t::QAM_AUTO'),
	                        (6, 'chdb::fe_code_rate_t',  'fec_inner', 'chdb::fe_code_rate_t::FEC_AUTO'),
	                        (7, 'chdb::fe_code_rate_t', 'fec_outer', 'chdb::fe_code_rate_t::FEC_AUTO'),
                          (9, 'mux_common_t', 'c')
                ))


dvbt_mux = db_struct(name='dvbt_mux',
                fname = 'mux',
                db = db,
                type_id= ord('m')+256*2,
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (lord('tf'), 'freq', ('frequency',)),
                (lord('tn'), 'network_id_ts_id_sat_pos', ('c.network_id', 'c.ts_id')),
                (lord('ts'), 'scan_status', ('c.scan_status', 'frequency')),
                ),                     #not unique, could be split in unique and non-unique later
                fields = ((14, 'mux_key_t', 'k'),
                          (2, 'chdb::fe_delsys_dvbt_t', 'delivery_system', 'chdb::fe_delsys_dvbt_t::SYS_DVBT2'),
                          (3, 'uint32_t', 'frequency'),
                          (13, 'chdb::fe_spectral_inversion_t', 'inversion'),
	                        (4, 'chdb::fe_bandwidth_t', 'bandwidth'),
                          (5, 'chdb::fe_modulation_t', 'modulation', 'chdb::fe_modulation_t::QAM_AUTO'),
                          (6, 'chdb::fe_transmit_mode_t', 'transmission_mode'),
                          (7, 'chdb::fe_guard_interval_t', 'guard_interval'),
	                        (8, 'chdb::fe_hierarchy_t', 'hierarchy'),
	                        (9, 'chdb::fe_code_rate_t',  'HP_code_rate', 'chdb::fe_code_rate_t::FEC_AUTO'),
	                        (10, 'chdb::fe_code_rate_t', 'LP_code_rate', 'chdb::fe_code_rate_t::FEC_AUTO'),
                          (12, 'mux_common_t', 'c')
                ))


sat = db_struct(name='sat',
                    fname = 'sat',
                    db = db,
                    type_id = ord('a'),
                    version = 1,
                    primary_key = ('key', ('sat_pos',)), #unique
                    fields = ((1, 'int16_t', 'sat_pos', 'sat_pos_none'),
                              (2, 'ss::string<32>', 'name'),
                              (3, 'mux_key_t', 'reference_tp'),
                              ))

language_code = db_struct(name='language_code',
                          fname = 'service',
                          db = db,
                          type_id= lord('la'),
                          version = 1,
                          #position (not relevant for language prefs, but useful to indicate order in pmt)
                          primary_key = ('key', ('position',)), #a key is needed for the temporary database
                          fields = ((1, 'int8_t', 'position', -1),   #position in pmt, or for prefs: version in case of duplicate languages;
                                    (2, 'int8_t', 'lang1'),
                                    (3, 'int8_t', 'lang2'),
                                    (4, 'int8_t', 'lang3')))


service_key = db_struct(name='service_key',
                        fname = 'service',
                        db = db,
                        type_id= ord('S'),
                        version = 1,
                        fields = ((1, 'mux_key_t', 'mux'),
                                  (3, 'uint16_t' , 'network_id'),
                                  (4, 'uint16_t' , 'ts_id'),
                                  (2, 'uint16_t', 'service_id')
                                  )
                        )


service = db_struct(name ='service',
                    fname = 'service',
                    db = db,
                    type_id = ord('s'),
                    version = 1,
                    primary_key = ('key', ('k.mux', 'k.service_id')), #unique
                    keys =  (
                        (ord('s'), 'network_id_ts_id_service_id_sat_pos', ('k.network_id', 'k.ts_id', 'k.service_id', 'k.mux.sat_pos')),
                        (ord('c'), 'ch_order', ('ch_order',)),
                        (ord('n'), 'name', ('tolower:name',)),
                        #(ord('t'), 'media_mode_name', ('media_mode', 'tolower:name',)),
                        #(ord('u'), 'media_mode_ch_order', ('media_mode', 'ch_order',)),
                    ),                     #not unique, could be split in unique and non-unique later
                    #a filter is a field which has to match; only used for lists, filters can be combined
                    #if a prefix is available, using the prefix is faster
                    #possible use case: order by ch_order (index media_ch_order can be removed)
                    #and retain only TV channels
                    filter_fields = (
                        ('sat_pos', 'k.mux.sat_pos'),
                        ('media_mode','media_mode')
                    ),
	                  fields=((1, 'service_key_t', 'k'),
                            (15, 'uint32_t', 'frequency'),
                            (16, 'chdb::fe_polarisation_t',  'pol', 'chdb::fe_polarisation_t::NONE'),
                            (2, 'time_t', 'mtime'), #last seen or last updated?
	                          (3, 'uint16_t', 'ch_order', '0'),
                            (4, 'uint16_t', 'service_type', '0'),
                            (5, 'uint16_t', 'pmt_pid', 'null_pid'),
                            (6, 'media_mode_t', 'media_mode', 'chdb::media_mode_t::UNKNOWN'),
                            (9, 'boolean_t', 'encrypted', "false"),
                            (10,'boolean_t', 'expired', "false"),
                            (14,'uint16_t', 'video_pid', "0xffff"),
                            #variable length data
                            (7, 'ss::string<16>', 'name'),
                            (8, 'ss::string<16>', 'provider'),
                            #each uint32_t consts of 3 bytes langcode ended by a one byte
                            #in case a lang code occurs more than once, 0 will mean the
                            #first one, 1 the second one and so one. If the first one
                            #is not available in the stream, the second one will be used.
                            (11, 'ss::vector<language_code_t,4>', 'audio_pref'),
                            #each uint32_t consts of 3 bytes langcode ended by a one byte
                            #in case a lang code occurs more than once, 0 will mean the
                            #first one, 1 the second one and so one. If the first one
                            #is not available in the stream, the second one will be used.
                            (12, 'ss::vector<language_code_t,4>', 'subtitle_pref')
	                  ))

chg_key = db_struct(name ='chg_key',
                    fname = 'service',
                    db = db,
                    type_id = ord('G'),
                    version = 1,
	                  fields=((1, 'group_type_t', 'group_type'),
                            (2, 'int32_t', 'bouquet_id', 'bouquet_id_template'), #int32_t to support non-dvb bouquets
                            (3, 'int16_t', 'sat_pos', 'sat_pos_none')
	                          ))

chg = db_struct(name ='chg',
                fname = 'service',
                db = db,
                type_id = ord('g'),
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                    #The following would be a multi structure index and is not allowed
                    # ('sat_freq_pol_sid', ('k.mux.sat_pos', 'k.mux.frequency', 'k.mux.polarisation', 'service_id')),
                    (ord('m'), 'name', ('tolower:name',)),
                ),                     #not unique, could be split in unique and non-unique later
	              fields=((1, 'chg_key_t', 'k'),
                        (5, 'uint16_t', 'num_channels'),
                        (3, 'ss::string<16>', 'name'),
                        (4, 'time_t', 'mtime')
	                  ))


chgm_key = db_struct(name ='chgm_key',
                        fname = 'service',
                        db = db,
                        type_id = ord('L'),
                        version = 1,
	                      fields=((1, 'chg_key_t', 'chg'),
                                (3, 'int32_t', 'channel_id', 'channel_id_template')
	                          ))

chgm = db_struct(name ='chgm',
                    fname = 'service',
                    db = db,
                    type_id = ord('l'),
                    version = 1,
                    primary_key = ('key', ('k',)), #unique
                    keys =  (
                        #The following would be a multi structure index and is not allowed
                        # ('sat_freq_pol_sid', ('k.mux.sat_pos', 'k.mux.frequency', 'k.mux.polarisation', 'service_id')),
                        (lord('l1'), 'chgm_order', ('chgm_order',)),
                        (lord('l2'), 'chg_service', ('k.chg', 'service')),
                        (ord('L'), 'name', ('tolower:name',)),
                    ),                     #not unique, could be split in unique and non-unique later
	                  fields=((1, 'chgm_key_t', 'k'),
                            (2, 'uint16_t', 'user_order'),  #user preference
                            (6, 'uint16_t', 'chgm_order'),#set from stream
                            (11, 'service_key_t', 'service'),
                            #The following fields are cached t avoid service lookups
                            (8, 'uint8_t', 'service_type', '0'),
                            (9, 'bool', 'expired', 'false'),
                            (10, 'bool', 'encrypted', 'false'),
                            (3, 'media_mode_t', 'media_mode', 'chdb::media_mode_t::UNKNOWN'),
                            (4, 'ss::string<16>', 'name'),
                            (5, 'time_t', 'mtime'),
	                          ))




#Singleton listing channels tuned to (and perhaps later some other data)
browse_history = db_struct(name ='browse_history',
    fname = 'tune_state',
    db = db,
    type_id = ord('h'),
    version = 1,
    primary_key = ('key', ('user_id',)),
    fields = ((1, 'int32_t', 'user_id'), #unique per subscription id?
              (2, 'list_filter_type_t', 'list_filter_type', 'list_filter_type_t::ALL_SERVICES'),
              (3, 'uint32_t', 'service_sort_order', '((uint32_t) service_t::subfield_t::ch_order)<<24'),
              (4, 'uint32_t', 'chgm_sort_order', '((uint32_t) chgm_t::subfield_t::chgm_order)<<24'),
              (5, 'sat_t', 'filter_sat'),
              (6, 'chg_t', 'filter_chg'),
              (7, 'uint32_t', 'servicelist_sort_order', '((uint32_t) service_t::subfield_t::ch_order)<<24'),
              (8, 'uint32_t', 'chgmlist_sort_order', '((uint32_t) chgm_t::subfield_t::chgm_order)<<24'),
              (9, 'uint32_t', 'dvbs_muxlist_sort_order', '((uint32_t) chgm_t::subfield_t::chgm_order)<<24'),
              (10, 'uint32_t', 'dvbc_muxlist_sort_order', '((uint32_t) chgm_t::subfield_t::chgm_order)<<24'),
              (11, 'uint32_t', 'dvbt_muxlist_sort_order', '((uint32_t) chgm_t::subfield_t::chgm_order)<<24'),
              (12, 'sat_t', 'servicelist_filter_sat'),
              (13, 'chg_t', 'chgmlist_filter_chg'),
              (14, 'sat_t', 'dvbs_muxlist_filter_sat'),
              (15, 'ss::vector<service_t,8>', 'services'),
              (16, 'ss::vector<chgm_t,8>', 'chgms'),
              (17, 'ss::vector<service_t,2>', 'servicelist_services'),
              (18, 'ss::vector<chgm_t, 2>', 'cgmlist_chgms'),
              (19, 'ss::vector<dvbs_mux_t, 2>', 'dvbs_muxlist_muxes'),
              (20, 'ss::vector<dvbc_mux_t, 2>', 'dvbc_muxlist_muxes'),
              (21, 'ss::vector<dvbt_mux_t, 2>', 'dvbt_muxlist_muxes'),
              ))
