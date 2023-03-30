import os
from inspect import getsourcefile
import sys

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'devdb'

from generators import set_env, db_db, db_struct, db_enum, db_include


gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='b', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)

db_include(fname='chdb', db=db, include='neumodb/chdb/chdb_db.h')

rotor_control = db_enum(name='rotor_control_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('ROTOR_MASTER_MANUAL', 0),  #rotor controlled by user
                           'ROTOR_MASTER_USALS', #lnb on rotor, with its cable connected to the rotor
                           'ROTOR_MASTER_DISEQC12', #lnb on rotor, with its cable connected to the rotor
                           'ROTOR_NONE', #lnb on rotor, with its cable not connected to the rotor, or lnb not on rotor
                           ))

positioner_cmd = db_enum(name='positioner_cmd_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('RESET', '0x00'),
                       ('HALT', '0x60'), #e0 30 60
                       ('LIMITS_OFF', '0x63'), #e0 30 63
                       ('LIMIT_EAST', '0x66'), #e1 30 66
                       ('LIMIT_WEST', '0x67'), #e1 30 67
                       ('DRIVE_EAST', '0x68'), #e1 31 68 40
                       ('DRIVE_WEST', '0x69'), #e1 31 69 40
                       ('STORE_NN', '0x6A'), #e0 30 6a xx
                       ('GOTO_NN', '0x6B'), # e0 30 6b nn
                       ('GOTO_XX', '0x6E'), #par=usals in degree
                       ('RECALCULATE_POSITIONS','0x6F') , # e0 30 6f 00 { 00 , 00}
                       ('GOTO_REF'),  # e0 30 6b 00 same as GOTO_NN with par 0
                       ('LIMITS_ON'),  # e0 30 6a 00 samen as STORE_NN with par 0
                       ('NUDGE_WEST'),  #/e1 31 69 xx same as drive west but with different par
                       ('NUDGE_EAST')  #/e1 31 68 xx same as drive east but with different par

                   ))

lnb_type = db_enum(name='lnb_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(('UNKNOWN', -1),
                           'C',
                           'KU',
                           'UNIV',
                           'WDB',
			                     'WDBUK',
                           'KaA',
                           'KaB',
                           'KaC',
                           'KaD',
                           'KaE'
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

fe_band = db_enum(name='fe_band_t',
                  db= db,
                  storage = 'int8_t',
                  type_id = 100,
                  version = 1,
                  fields = (('NONE',-1),
	                          'LOW',
	                          'HIGH'
	                          ))



fe_band_pol = db_struct(name='fe_band_pol',
                    fname = 'fedev',
                    db = db,
                    type_id= lord('_F'), #TODO: duplicate
                    version = 1,
                    fields = ((1, 'fe_band_t', 'band', 'fe_band_t::NONE'),
                              (2, 'chdb::fe_polarisation_t', 'pol', 'chdb::fe_polarisation_t::NONE'),
                              ))


"""
Principle: the same lnb can sometimes receive satellites from different positions. FOr example
an LNB tuned to 9.0E may be able to receive 10.0E as well. In this case the lnb will have two
network enries. The first one will be considered the main one, and the second one the secondary one,

For lnbs on a positioner, teh dish will move to the specifief sat_pos
TODO: we may add a second sat_pos field to implement secondary networks (like the 9.0E vs. 10.0E example)

"""

lnb_network = db_struct(name='lnb_network',
                fname = 'fedev',
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
                          (5,  'chdb::mux_key_t', 'ref_mux'), #for all lnbs, reference tranponder for use in positioner dialog
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
                          fname = 'fedev',
                          db = db,
                          type_id= ord('T'),
                          version = 1,
                          fields = (
                              (3, 'int8_t', 'dish_id', 0), #dish_id=0 is also the "default dish"
                              #because of switches, the same cable could be attached to multiple dishes
                              (2, 'int16_t', 'lnb_id', '-1'), #unique identifier for lnb. Still needed?

                              #Usually the  orbital position (fixed dish) or the offset position on dish
                              #uniquely identifies the lnb, but not always: multiple dishes can point to
                              #the same orbital position and combined lnbs or lnbs on a revolver could
                              #have the same orbital position. That is why we have lnb_type and dish_id as extra keys

                              #lnb_pos: for fixed dish:  used to distinghuish lnbs (like a key)
                              #         for a rotor dish: 0, or a different value if an offset lnb is installed
                              #needed incase a C and Ku band are on the same dish
                              (5, 'lnb_type_t',  'lnb_type', 'lnb_type_t::UNIV'),
                          )
                        )


rf_path = db_struct(name='rf_path',
                        fname = 'fedev',
                    db = db,
                    type_id= lord('TC'),
                    version = 1,
                    fields = ((1, 'lnb_key_t', 'lnb'),
                              (2, 'int64_t', 'card_mac_address', -1), #Unique for each card
                              (3, 'int8_t', 'rf_input', -1),
                              )
                    )

lnb_connection = db_struct(name='lnb_connection',
                fname = 'fedev',
                db = db,
                type_id= lord('tc'),
                version = 1,
                 primary_key = ('key', ('card_mac_address','rf_input')), #this key is needed for temporary database (per lnb)
                fields = ((1, 'int64_t', 'card_mac_address', -1), #Unique for each card
                          (2, 'int8_t', 'rf_input', -1),

                          (3, 'bool',  'enabled', 'true'), #bit flag indicating if lnb is allowed to be used
                          (13, 'bool',  'can_be_used', 'true'), #bit flag indicating if this connection can be used
                          (4, 'int16_t',  'priority', -1), #
                          (5, 'rotor_control_t', 'rotor_control', 'rotor_control_t::ROTOR_NONE'), #
                          (6, 'uint8_t' , 'diseqc_mini'),
                          (7, 'int8_t' , 'diseqc_10', '-1'),
                          (8, 'int8_t' , 'diseqc_11', '-1'),
                          # disec12 is not included here as this is part of the dish

                          #Sometimes more than one network can be received on the same lnb
                          #for an lnb

                          (10, 'int8_t', 'card_no',  '-1'), #updated as adapters are discovered
                          (14, 'int8_t', 'rf_coupler_id',  '-1'), #defines coupling with other connnection

                          # list of commands separted by ";"
                          #can contain
                          #  P send positioner commands
                          #  ? send positioner commands while keeping voltage high (todo; problem is we do not know
                          #  when we will reach destination)
                          (11, 'ss::string<16>' , 'tune_string', '"UCP"'),
                          (12,  'ss::string<16>', 'connection_name'),
                ))



#lnb record
# part 1: what can it tune to? one satellite or all satellites on a positioner? All polarisations or only some?
#         C-band, ku-band high, ku band low ...
# part 2: linked tuners, i.e., restrictions related to slave/master tuners; this does NOT include restrictions
#         due to lnbs being on same positioner, as this is implied by using the positioner; it COULD however include
#         restrictions like: this lnb is always 3 degrees of the other one; the latter could be implemented
#         as part of the positioner
#part  3: how tuning is achieved; polarisation and band are not included because this is done automatically, possibly by a master tuner
lnb = db_struct(name='lnb',
                fname = 'fedev',
                db = db,
                type_id= ord('t'),
                version = 1,
                primary_key = ('key', ('k',)), #unique; may need to be revised
                keys =  (
                    #(ord('a'), 'adapter_mac_address', ('k.adapter_mac_address', 'k.sat_pos')),
                ),
                fields = ((1, 'lnb_key_t', 'k'),  #contains adapter and absolute/relative dish pos
                          #for a positioner: last uals position to which usals roto was set
                          #This is the actual usals coordinate (may differ from exact sat_pos)
                          #For an offset lnb, this is not the actual usals_position, bu the
                          #usals position which would be set if the lnb was in the center
                          #So; pos sent to rotor = usals_pos - offset_pos
                          #not used for a fixed dish, but should be set equal to the sat in networks[0] for clarity,
                          #i.e., the main satellite
                          (2, 'int16_t', 'usals_pos', 'sat_pos_none'), #satellite position of center lnb
                          (18, 'int16_t', 'cur_sat_pos', 'sat_pos_none'), #satellite position of this lnb (different if in offset pos)

                          (16, 'bool',  'on_positioner', 'false'), #bit flag indicating if lnb is on rotor
                          (3, 'lnb_pol_type_t',  'pol_type', 'lnb_pol_type_t::HV'), #bit flag indicating which polarisations can be used
                          (4, 'bool',  'enabled', 'true'), #bit flag indicating if lnb is allowed to be used
                          (17, 'int16_t', 'offset_angle', '0'), #in 1/100 degree: offset w.r.t. t center of dish, used for adjusting usals_pos for display purposes
                          (5, 'int16_t',  'priority', -1), #
                          (6, 'int32_t', 'lof_low', -1), # local oscillator, -1 means default
                          (7, 'int32_t', 'lof_high', -1), # local oscillator, -1 means default
                          (8, 'int32_t', 'freq_low', -1), # lowest frequency which can be tuned
                          (9, 'int32_t', 'freq_mid', -1), # frequency to switch between low/high band
                          (10, 'int32_t', 'freq_high', -1), # highest frequency wich can be tuned
                          (11,  'time_t', 'mtime'),
                          (12, 'bool', 'can_be_used', 'true'), #updated as adapters are discovered
                          (13, 'ss::vector<lnb_network_t,1>' , 'networks'),
                          (14, 'ss::vector<lnb_connection_t,1>' , 'connections'),
                          (15, 'ss::vector<int32_t,2>' , 'lof_offsets'), #ofset of the local oscillator (one per band)
                ))



fe_key = db_struct(name='fe_key',
                          fname = 'fedev',
                          db = db,
                          type_id= ord('U'),
                          version = 1,
                          fields = (
                              (3, 'int64_t', 'adapter_mac_address'),
                              (5, 'uint8_t', 'frontend_no'),
                          ))

fe_supports = db_struct(name='fe_supports',
                        fname = 'fedev',
                        db = db,
                        type_id= ord('q'),
                        version = 1,
                        fields = ((1, 'bool', 'multistream', 'false'),
                                  (2, 'bool', 'blindscan', 'false'),
                                  (3, 'bool', 'spectrum_sweep', 'false'),
                                  (5, 'bool', 'spectrum_fft', 'false'),
                                  (4, 'bool', 'iq', 'false')
                ))


fe_subscription = db_struct(name='fe_subscription',
                           fname = 'fedev',
                           db = db,
                           type_id= lord('qr'),
                           version = 1,
                           fields = ((1, 'int32_t', 'owner', -1),
                                    # (2, 'int32_t', 'subscription_id', -1),
                                     #(3, 'int16_t', 'rf_in', -1),
                                     #(8, 'int16_t', 'rf_group_id', -1),
                                     (3, 'rf_path_t', 'rf_path'),
                                     (4, 'chdb::fe_polarisation_t', 'pol', 'chdb::fe_polarisation_t::NONE'),
                                     (5, 'fe_band_t', 'band', 'fe_band_t::NONE'),
                                     (6, 'int16_t', 'usals_pos', 'sat_pos_none'),
                                     (7, 'int16_t', 'use_count', '0'),
                                     (8, 'int32_t', 'frequency', '0'),
                                     (9, 'int32_t', 'stream_id', '-1'),
                                     (10, 'int32_t', 'rf_coupler_id', '-1'),
                ))


fe = db_struct(name='fe',
               fname = 'fedev',
               db = db,
               type_id= ord('u'),
               version = 1,
               primary_key = ('key', ('k',)),
               keys =  (
                   (ord('f'), 'adapter_no', ('adapter_no',)),
                   (ord('g'), 'card_mac_address', ('card_mac_address',))
               ),
               fields = (
                   (29, 'int16_t', 'card_no', '-1'), #unique and stable generated number
                   (1, 'fe_key_t', 'k'),
                   #(25, 'int16_t', 'rf_in'),
                   (21, 'int16_t', 'adapter_no'),
                   (24, 'bool', 'supports_neumo'),
                   (2, 'bool', 'present'),
                   (3, 'bool', 'can_be_used', 'true'),
                   (4, 'uint8_t', 'enable_dvbs', 'true'),
                   (30, 'uint8_t', 'enable_dvbt', 'true'),
                   (31, 'uint8_t', 'enable_dvbc', 'true'),
                   (5, 'int16_t', 'priority', 0),
                   (28, 'fe_subscription_t', 'sub'),
                   (9, 'time_t', 'mtime'),
                   (10, 'uint32_t', 'frequency_min'),
                   (11, 'uint32_t', 'frequency_max'),
                   (12, 'uint32_t', 'symbol_rate_min'),
                   (13, 'uint32_t', 'symbol_rate_max'),
                   (20, 'int64_t', 'card_mac_address'),
                   (14, 'fe_supports_t', 'supports'),
                   (15, 'ss::string<64>', 'card_name'),
                   (26, 'ss::string<64>', 'card_short_name'),
                   (16, 'ss::string<64>', 'adapter_name'),
                   (17, 'ss::string<64>', 'card_address'),
                   (19, 'ss::vector<chdb::fe_delsys_t>', 'delsys'),
                   (27, 'ss::vector<int8_t>', 'rf_inputs'),
               ))


#todo: move to different database
usals_location = db_struct(name ='usals_location',
                    fname = 'options',
                    db = db,
                    type_id = lord('ou'),
                    version = 1,
                    fields = (
                        (0, 'int16_t', 'usals_latitude', '0'), #in 1/100 degree
                        (1, 'int16_t', 'usals_longitude', '0'), #in 1/100 degree
                        (2, 'int16_t', 'usals_altitude', '0') #in m (?)
                              )
                    )

user_options = db_struct(name ='user_options',
                    fname = 'options',
                    db = db,
                    type_id = lord('oo'),
                    version = 1,
                    primary_key = ('key', ('user_id',)),
                    fields = (
                        (0, 'int32_t', 'user_id', '0'),
                        (1, 'time_t', 'mtime'),
                        (2, 'usals_location_t', 'usals_location'),
                        (3, 'int32_t', 'pre_record_time', '1*60'), #1 minute
                        (4, 'int32_t', 'max_pre_record_time', '1*3600'), #1 hour
                        (5, 'int32_t', 'post_record_time', '5*60'), #5 minutes
                        (6, 'int32_t', 'default_record_time', '2*3600'), #2 hours
                        (7, 'int32_t', 'timeshift_duration', '2*3600'), #2 hours
                        (8, 'int32_t', 'livebuffer_retention_time', '10*60'),  #10 minutes
                        (9, 'int32_t', 'livebuffer_mpm_part_duration', '10*60'),  #10 minutes
                        (10, 'int32_t', 'dish_move_penalty', 100),
                        (11, 'int32_t', 'resource_reuse_bonus', 1000),
                        (12, 'bool', 'tune_may_move_dish', 'false')
                    ))



tuned_frequency_offsets_key = \
    db_struct(name='tuned_frequency_offsets_key',
              fname = 'fedev',
              db = db,
              type_id= lord('FO'),
              version = 1,
              fields = (
                  (1, 'lnb_key_t', 'lnb_key'),
                  (2, 'fe_band_t', 'band'),
              ))


tuned_frequency_offset = \
    db_struct(name='tuned_frequency_offset',
              fname = 'fedev',
              db = db,
              type_id= lord('fo'),
              version = 1,
              fields = (
                  (1, 'int16_t', 'sat_pos', 'sat_pos_none'),           #official satellite position
                  (2, 'uint32_t', 'nit_frequency', '0'), #frequency as defined in NIT
                  (3, 'chdb::fe_polarisation_t', 'pol'),
                  (4, 'int32_t', 'frequency_offset', '0')
              ))

tuned_frequency_offsets = \
    db_struct(name='tuned_frequency_offsets',
              fname = 'fedev',
              db = db,
              type_id= lord('fs'),
              version = 1,
              primary_key = ('key', ('k', )),
              fields = (
                  (1, 'tuned_frequency_offsets_key_t', 'k'),
                  (2, 'ss::vector<tuned_frequency_offset_t, 11>', 'frequency_offsets'),
                  (3, 'time_t', 'mtime') #last seen or last updated?
              ))
