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
                           ))
rotor_control = db_enum(name='rotor_control_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('FIXED_DISH', 0), #lnb on fixed dish
                           'ROTOR_MASTER_USALS', #lnb on rotor, with its cable connected to the rotor
                           'ROTOR_MASTER_DISEQC12', #lnb on rotor, with its cable connected to the rotor
                           'ROTOR_SLAVE', #lnb on rotor, with its cable not connected to the rotor
                           ))

positioner_cmd = db_enum(name='positioner_cmd_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('RESET', '0x00'),
                       ('HALT', '0x60'),
                       ('LIMITS_OFF', '0x63'),
                       ('LIMIT_EAST', '0x66'),
                       ('LIMIT_WEST', '0x67'),
                       ('DRIVE_EAST', '0x68'),
                       ('DRIVE_WEST', '0x69'),
                       ('STORE_NN', '0x6A'),
                       ('GOTO_NN', '0x6B'),
                       ('GOTO_XX', '0x6E'), #usals in degree
                       'GOTO_REF',

                   ))

lnb_type = db_enum(name='lnb_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', -1),
                           'C',
                           'KU',
                           'UNIV'
                           ))

lnb_pol_type = db_enum(name='lnb_pol_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', -1),
                           'HV',
                           'LR',
                           'VH', #inverted polarisation
                           'RL',
                           'H',
                           'V',
                           'L',
                           'R'
                           ))

scan_status = db_enum(name='scan_status_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('PENDING', 0),
                           ('IDLE', 1),
                           ('ACTIVE', 2)
                           ))

scan_result = db_enum(name='scan_result_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('NONE', 0), #never tried
                       ('FAILED', 1), #could not tune. No si received
                       ('ABORTED', 2), #timeout stage was not reached
                       ('PARTIAL', 3), #timeout stage was reached, but result was incomplete
                       ('OK', 4), #completion stage reached or timedout
                       ('NODATA', 5), #completion stage reached or timedout
                       ('DISABLED', 127),
                   ))

fe_band = db_enum(name='fe_band_t',
                  db= db,
                  storage = 'int8_t',
                  type_id = 100,
                  version = 1,
                  fields = (('UNKNOWN',-1),
	                          'LOW',
	                          'HIGH'
	                          ))


fe_polarisation = db_enum(name='fe_polarisation_t',
              db = db,
              storage = 'int8_t',
              type_id = 100,
              version = 1,
              fields = (('UNKNOWN',-1),
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
	          'ROLLOFF_20',
	          'ROLLOFF_25',
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
	          ('SYS_AUTO', 0),
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




fe_band_pol = db_struct(name='fe_band_pol',
                    fname = 'mux',
                    db = db,
                    type_id= lord('_F'), #TODO: duplicate
                    version = 1,
                    fields = ((1, 'fe_band_t', 'band', 'fe_band_t::UNKNOWN'),
                              (2, 'fe_polarisation_t', 'pol', 'fe_polarisation_t::UNKNOWN'),
                              ))

mux_key = db_struct(name='mux_key',
                    fname = 'mux',
                    db = db,
                    type_id= ord('M'), #TODO: duplicate
                    version = 1,
                    fields = ((1, 'int16_t', 'sat_pos', 'sat_pos_none'), #16000 DVBC 16001=DVBT
                              (2, 'uint16_t', 'network_id'),
                              (3, 'uint16_t', 'ts_id'),
                              (5, 'uint16_t', 't2mi_pid', 0), #we would brefer 0x1fff but this interferes
                                                              #with using default key as lower bound in find functions
                              (4, 'uint16_t', 'extra_id') #distinghuishes between transponders with same network_id/ts_id on same sat
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
                              (2, 'scan_status_t', 'scan_status'),
                              (3, 'scan_result_t', 'scan_result'),
                              (8, 'time_t', 'scan_duration'),
                              (4, 'uint16_t', 'num_services'),
                              (5, 'bool', 'epg_scan'),
                              (6, 'bool',  'is_template', 'false'),
                              (10, 'bool',  'freq_from_si', 'false'), #true if frequency was set from si
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
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (ord('s'), 'sat_freq_pol', ('k.sat_pos', 'frequency', 'pol')),
                (lord('s1'), 'network_id_ts_id', ('k.network_id', 'k.ts_id')),
                ),
                fields = ((1, 'mux_key_t', 'k'),
                          (2, 'chdb::fe_delsys_dvbs_t', 'delivery_system', 'chdb::fe_delsys_dvbs_t::SYS_DVBS2'),
                          (3, 'uint32_t', 'frequency'),
                          (9, 'chdb::fe_spectral_inversion_t', 'inversion'),
                          (4, 'chdb::fe_polarisation_t',  'pol', 'chdb::fe_polarisation_t::V'),
                          (5, 'uint32_t',  'symbol_rate', '27500000'),
	                        (7, 'chdb::fe_modulation_t', 'modulation', 'chdb::fe_modulation_t::PSK_8'),
	                        (8, 'chdb::fe_code_rate_t',  'fec', 'chdb::fe_code_rate_t::FEC_AUTO'), #aka fec_inner: dvb-s, dvb-c, dvb-t
	                        (10, 'chdb::fe_rolloff_t', 'rolloff'),
                          (11, 'chdb::fe_pilot_t', 'pilot'),
	                        (12, 'int16_t', 'stream_id',  -1),
	                        (13, 'chdb::fe_pls_mode_t', 'pls_mode', 'fe_pls_mode_t::ROOT'),
                          (16, 'uint8_t', 'matype', '0'),
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
                (ord('s')+256*1, 'freq', ('frequency',)),
                ),                     #not unique, could be split in unique and non-unique later
                fields = ((1, 'mux_key_t', 'k'),
                          (2, 'fe_delsys_dvbc_t', 'delivery_system', 'fe_delsys_dvbc_t::SYS_DVBC'),
                          (3, 'uint32_t', 'frequency'),
                          (10, 'chdb::fe_spectral_inversion_t', 'inversion'),
                          (4, 'uint32_t', 'symbol_rate', '27500000'),
	                        (5, 'chdb::fe_modulation_t', 'modulation', 'chdb::fe_modulation_t::QAM_AUTO'),
	                        (6, 'chdb::fe_code_rate_t',  'fec_inner', 'chdb::fe_code_rate_t::FEC_AUTO'),
	                        (7, 'chdb::fe_code_rate_t', 'fec_outer', 'chdb::fe_code_rate_t::FEC_AUTO'),
	                        (8, 'int16_t', 'stream_id',  -1),
                          (9, 'mux_common_t', 'c')
                ))


dvbt_mux = db_struct(name='dvbt_mux',
                fname = 'mux',
                db = db,
                type_id= ord('m')+256*2,
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                (ord('s')+256*2, 'freq', ('frequency',)),
                ),                     #not unique, could be split in unique and non-unique later
                fields = ((1, 'mux_key_t', 'k'),
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
	                        (11, 'int16_t', 'stream_id',  -1),
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
                          primary_key = ('key', ('position',)), #a keyis needed for temporary database
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
                                  (2, 'uint16_t', 'service_id')
                                  )
                        )


service = db_struct(name ='service',
                    fname = 'service',
                    db = db,
                    type_id = ord('s'),
                    version = 1,
                    primary_key = ('key', ('k',)), #unique
                    keys =  (
                        #The following would be a multi structure index and is not allowed
                        # ('sat_freq_pol_sid', ('k.mux.sat_pos', 'k.mux.frequency', 'k.mux.polarisation', 'service_id')),
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
                            (13, 'ss::string<16>', 'mux_desc'),
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



if False:
    dish_state = db_struct(name='dish_state',
                            fname = 'tuner',
                            db = db,
                            type_id= ord('i'),
                            version = 1,
                            primary_key = ('key', ('dish_id',)), #unique
                            fields = ((1, 'int16_t', 'dish_id'),
                                      (2, 'int16_t', 'position', 0), #current official position
                                      (3, 'time_t',  'move_time', 0), #last moved time
                            ))

"""
In case a moveable dish has a multifeed, we create a virtual dish
for each

"""
if False:
    position = db_struct(name='position',
                         fname = 'tuner',
                         db = db,
                         type_id= ord('p'),
                         version = 1,
                         fields = ((1, 'int16_t', 'position'), #official position as on king of sat
                                   (2, 'int16_t', 'diseqc12'), #usals or diseqc1.2
                                   #position_correction: offset added to official position, to
                                   #compensate for dish misalignment
                                   #@todo: There should also be a per mux correction
                                   (3, 'int16_t',  'position_correction', 0)
                         ))

    dish = db_struct(name='dish',
                            fname = 'tuner',
                            db = db,
                            type_id= ord('P'),
                            version = 1,
                            primary_key = ('key', ('dish_id',)), #unique
                            fields = ((1, 'int16_t', 'dish_id'),
                                      (2, 'boolean_t', 'on_rotor', 'false'), #dish is on a rotor or not?
                                      #offset_position: added to all satellite positions, either to
                                      #correct for misalignment, or in case of a mutifeed lnb,
                                      #where we define a virtual dish for each lnb
                                      (3, 'int16_t', 'position_offset', 0),

                                      #virtual dishis share the same link_group_id
                                      #link_group_id is the dish_id for the master dish
                                      (4, 'int16_t', 'link_group_id', -1),

                                      #if positions is empty, then dish can move to any position in this range
                                      (5, 'int16_t', 'min_position', -1800),
                                      (6, 'int16_t', 'max_position', 1800),

                                      #positions that this positioner can tune to;
                                      #we may need to exclude certain positions (no reception)
                                      (7, 'ss::vector<position_t,128>', 'positions')
                                      ))

"""
We call a adapter/frontend combination a tuner

Tuning rules:

-only one frontend can be used per adapter at any time

tuners can be restricted as follows:
-tuner y can only be used when x is active and then is resticted to same band/polarisation. E.g., loopthrough
This can be extressed as y.master_tuner=x


-tuner y can be used freely, unless x is active in which case it is restricted to same band/polarisation.
E.g. tbs6908 and tbs909x. There could be more than 2 such tuners. Such linked tuners could be put in a tuning
group (i.e., one common number expresses that they are linked) or we could directly punt the tuner_id
of the linked tuner (depending on the implementation, this restricts the solution to 2 linked tuners)

E.g. y.linked_tuner=x x.linked_tuner=x

Possible hypothetical cases:
-tuner y can be used freely or as a slave of x and then is resticted to the same mux as x. E.g., unicable


-we may want to restrict lnbs to certain muxes (e.g., in case of poor reception of a mux on some but
not all of the dishes). This could be easier if we created a virtual lnb for each satellite position
on a rotor?


Selected solution:
 -each tuner has as a link_group_id field. If this is set, any tuner sharing the same link_group_id
  is checked for tuning restrictions. For simplicty, link_group_id can be the tuner_id of one of the tuners.
  When the tuner links tuner x to y, x.link_group_id should be set to y.link_group_id, unless
  y.link_group_id=-1, in which case both  x.link_group_id  and  y.link_group_id  should be set to y.tuner_id

-each tuner x also has a master_tuner_id. If this is set (then it should probably be equal to link_group_id)
 then master_tuner_id will send the diseqc commands instead of x. x is restricted from sendin further diseqc
 but can still tune It may also be needed to


  TODO: if linked_tuner.linked_tuner_id is also set (>=0), it will send diseqc commands and will
  be


"""
fe_key = db_struct(name='fe_key',
                          fname = 'tuner',
                          db = db,
                          type_id= ord('U'),
                          version = 1,
                          fields = (
                              (1, 'int16_t', 'adapter_no'),
                              (2, 'int16_t', 'frontend_no')
                          ))

fe_supports = db_struct(name='fe_supports',
                        fname = 'tuner',
                        db = db,
                        type_id= ord('q'),
                        version = 1,
                        fields = ((1, 'bool', 'multistream', 'false'),
                                  (2, 'bool', 'blindscan', 'false'),
                                  (3, 'bool', 'spectrum', 'false'),
                                  (4, 'bool', 'iq', 'false')
                ))




fe = db_struct(name='fe',
               fname = 'tuner',
               db = db,
               type_id= ord('u'),
               version = 1,
               primary_key = ('key', ('k',)),
               keys =  (
                   (ord('f'), 'adapter_no', ('k.adapter_no',)),
               ),
               fields = (
                   (1, 'fe_key_t', 'k'),
                   (2, 'bool', 'present'),
                   (3, 'bool', 'can_be_used', 'true'),
                   (4, 'bool', 'enabled', 'true'),
                   (5, 'int16_t', 'priority', 0),

                   #link_group_id: -1 means not linked
                   (6, 'int16_t', 'link_group_id', -1),

                   #master_tuner_id: -1 means not linked
	                 (7, 'int8_t', 'tuner_group', -1),   #index of group of linked tuner which share some restrictions
                   (8, 'int16_t', 'master_adapter', -1),


                   (9, 'time_t', 'mtime'),
                   (10, 'uint32_t', 'frequency_min'),
                   (11, 'uint32_t', 'frequency_max'),
                   (12, 'uint32_t', 'symbol_rate_min'),
                   (13, 'uint32_t', 'symbol_rate_max'),
                   (14, 'fe_supports_t', 'supports'),
                   (15, 'ss::string<64>', 'card_name'),
                   (16, 'ss::string<64>', 'adapter_name'),
                   (17, 'ss::string<64>', 'card_address'),
                   (18, 'ss::string<64>', 'adapter_address'),
                   (19, 'ss::vector<chdb::fe_delsys_t>', 'delsys'),
               ))

"""
Principle: the same lnb can sometimes receive satellites from different positions. FOr example
an LNB tuned to 9.0E may be able to receive 10.0E as well. In this case the lnb will have two
network enries. The first one will be considered the main one, and the second one the secondary one,

For lnbs on a positioner, teh dish will move to the specifief sat_pos
TODO: we may add a second sat_pos field to implement secondary networks (like the 9.0E vs. 10.0E example)

"""

lnb_network = db_struct(name='lnb_network',
                fname = 'tuner',
                db = db,
                type_id= ord('n'),
                version = 1,
                primary_key = ('key', ('sat_pos',)), #this key is needed for temporary database (per lnb)
                fields = ((1, 'int16_t', 'sat_pos', 'sat_pos_none'),           #official satellite position
                          (2, 'int16_t',  'priority', 0),
                          (3, 'int16_t', 'usals_pos', 'sat_pos_none'), #only for master usals positioner: in 1/100 degree
                          (4, 'int16_t', 'diseqc12', -1), #only for positioner: disec12 value
                          #sat_pos tp compensete for dish misalignment
                          (6, 'bool',  'enabled', 'true'),
                          (5,  'mux_key_t', 'ref_mux'), #for all lnbs, reference tranponder for use in positioner dialog
                ))


"""
Principle:
there is one lnb record for each physical lnb connection. E.g., a quad lnb will  appear four times.
Each lnb is uniquely identified by (adaptor_no, dish_id, lnb_type, lnb_id).
lnb_id is needed because multiple lnnbs can be installed on the same dish and connected to the same adapter_no
for fixed dishes, lnb_id can be set to sat_pos, as it will be unique
for dishes on a positioner it can be set to the offset (0 for a central lnb, 30 for an lnb in an offset position)
There could be cases in wich e.g, a KU-C combo lnb is installed. In this case, the described sat_id choice
will still work

TODO: replace adapter_no by rf_id  whih identifies the physical connector on the card
"""

lnb_key = db_struct(name='lnb_key',
                          fname = 'tuner',
                          db = db,
                          type_id= ord('T'),
                          version = 1,
                          fields = (
                              (1, 'int8_t', 'adapter_no', -1), #should become: card address
                              #(4, 'int16_t', 'rf_input', '-1'), #replaces adapter_no

                              #dish_id=0 is also the "default dish"
                              (3, 'int8_t', 'dish_id', 0),
                              #lnb_pos: for fixed dish:  used to distinghuish lnbs (like a key)
                              #         for a rotor dish: 0, or a different value if an offset lnb is installed
                              (2, 'int16_t', 'lnb_id', '-1'), #unique identifier for lnb
                              #needed incase a C and Ku band are on the same dish
                              (5, 'lnb_type_t',  'lnb_type', 'lnb_type_t::UNIV'),
                          )
                        )


#lnb record
# part 1: what can it tune to? one satellite or all satellites on a positioner? All polarisations or only some?
#         C-band, ku-band high, ku band low ...
# part 2: linked tuners, i.e., restrictions related to slave/master tuners; this does NOT include restrictions
#         due to lnbs being on same positioner, as this is implied by using the positioner; it COULD however include
#         restrictions like: this lnb is always 3 degrees of the other one; the latter could be implemented
#         as part of the positioner
#part  3: how tuning is achieved; polarisation and band are not included because this is done automatically, possibly by a master tuner
lnb = db_struct(name='lnb',
                fname = 'tuner',
                db = db,
                type_id= ord('t'),
                version = 1,
                primary_key = ('key', ('k',)), #unique; may need to be revised
                keys =  (
                    #(ord('a'), 'adapter_sat', ('k.adapter_id', 'k.sat_pos')),
                ),
                fields = ((1, 'lnb_key_t', 'k'),  #contains adapter and absolute/relative dish pos

                          #for a positioner: last uals position to which usals roto was set
                          #This is the actual usals coordinate (may differ from exact sat_pos)
                          #For an offset lnb, this is not the actual usals_position, bu the
                          #usals position which would be set if the lnb was in the center
                          #So; pos sent to rotor = usals_pos - offset_pos
                          #not used for a fixed dish, but should be set equal to the sat in networks[0] for clarity,
                          #i.e., the main satellite
                          (20, 'int16_t', 'usals_pos', 'sat_pos_none'),

                          (2, 'lnb_pol_type_t',  'pol_type', 'lnb_pol_type_t::HV'), #bit flag indicating which polarisations can be used
                          (3, 'bool',  'enabled', 'true'), #bit flag indicating if lnb is allowed to be used
                          (4, 'int16_t',  'priority', -1), #
                          (5, 'int32_t', 'lof_low', -1), # local oscillator, -1 means default
                          (6, 'int32_t', 'lof_high', -1), # local oscillator, -1 means default
                          (7, 'int32_t', 'freq_low', -1), # lowest frequency which can be tuned
                          (18, 'int32_t', 'freq_mid', -1), # frequency to switch between low/high band
                          (19, 'int32_t', 'freq_high', -1), # highest frequncy wich can be tuned
                          (8, 'rotor_control_t', 'rotor_control', 'rotor_control_t::FIXED_DISH'), #
                          (21, 'int16_t', 'offset_pos', '0'), #only for master usals positioner: in 1/100 degre: offset w.r.t. t center of dish

                          (10, 'uint8_t' , 'diseqc_mini'),
                          (11, 'int8_t' , 'diseqc_10', '-1'),
                          (12, 'int8_t' , 'diseqc_11', '-1'),
                          # disec12 is not included here as this is part of the dish

                          (14,  'time_t', 'mtime'),
                          #Sometimes more than one network can be received on the same lnb
                          #for an lnb


                          # list of commands separted by ";"
                          #can contain
                          #  P send positioner commands
                          #  ? send positioner commands while keeping voltage high (todo; problem is we do not know
                          #  when we will reach destination)
                          (9, 'ss::string<16>' , 'tune_string', '"UCP"'),
                          (13, 'ss::vector<lnb_network_t,1>' , 'networks'),
                          (16,  'ss::string<16>', 'name'), #optional name
                          (17, 'ss::vector<int32_t,2>' , 'lof_offsets'), #ofset of the local oscillator (one per band)
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




#todo: move to different database
usals_location = db_struct(name ='usals_location',
                    fname = 'options',
                    db = db,
                    type_id = lord('ou'),
                    version = 1,
                    fields = (
                        (0, 'int16_t', 'usals_lattitude', '5090'), #in 1/100 degree
                        (1, 'int16_t', 'usals_longitude', '410'), #in 1/100 degree
                        (2, 'int16_t', 'usals_altitude', '0') #in m (?)
                              )
                    )
