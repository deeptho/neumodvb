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
                   fields=(('UNKNOWN', -1, 'unk'),
                           'C',
                           'Ku',
                           ('UNIV',None,'unv'),
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


"""
Principle: the same lnb can sometimes receive satellites from different positions. FOr example
an LNB tuned to 9.0E may be able to receive 10.0E as well. In this case the lnb will have two
network enries. The first one will be considered the main one, and the second one the secondary one,

For lnbs on a positioner, the dish will move to the specifief sat_pos
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
                          #sat_pos tp compensate for dish misalignment
                          (6, 'bool',  'enabled', 'true'),
                          (5,  'chdb::mux_key_t', 'ref_mux'), #for all lnbs, reference tranponder for use in positioner dialog
                ))


"""
Principle:
there is one lnb record for each physical lnb connection. E.g., a quad lnb will  appear four times.
Each lnb is uniquely identified by (adaptor_no, dish_id, lnb_type, lnb_id).
lnb_id is needed because multiple lnbs can be installed on the same dish and connected to the same adapter_no
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
                              (3, 'int8_t', 'dish_id', -1), #dish_id=-1 is also the "default dish"
                              #because of switches, the same cable could be attached to multiple dishes
                              (2, 'int16_t', 'lnb_id', '-1'), #unique identifier for lnb.

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

dish = db_struct(name='dish',
                 fname = 'fedev',
                 db = db,
                 type_id= lord('di'),
                 version = 1,
                 ignore_for_equality_fields = ('mtime',),
                 primary_key = ('key', ('dish_id', )),
                 fields = ((1, 'int8_t', 'dish_id', -1), #Unique for each dish
                           (3, 'int16_t', 'cur_usals_pos', 'sat_pos_none'), #satellite position last moved to
                           (11, 'int16_t', 'target_usals_pos', 'sat_pos_none'), #satellite position begin moved to
                           (6, 'int32_t', 'powerup_time', '1500'), #How long after powerup to wait for positioner
                                                                   #to be ready for motion commands
                           (7, 'ss::vector<int16_t,2>', 'speeds', '{100, 200}'), #Speed rotor moves at 13 and 18 V
                                                                                 #in (sat_pos) centidegrees per second
                           (8, 'time_t', 'mtime',),
                           (9, 'bool', 'enabled', 'true')
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
                          # disecqc12 is not included here as this is part of the dish

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
                          #for a positioner: last uals position to which usals rotor was set
                          #This is the actual usals coordinate (may differ from exact sat_pos)
                          #For an offset lnb, this is not the actual usals_position, bu the
                          #usals position which would be set if the lnb was in the center
                          #So; pos sent to rotor = usals_pos - offset_pos
                          #not used for a fixed dish, but should be set equal to the sat in networks[0] for clarity,
                          #i.e., the main satellite
                          (2, 'int16_t', 'usals_pos', 'sat_pos_none'), #satellite position of center lnb
                          #(20, 'int16_t', 'usals_pos_reliable', 'true'), #false if positioner position is unknown
                          (18, 'int16_t', 'lnb_usals_pos', 'sat_pos_none'), #satellite position currentlly pointed to
                                                                          #by this lnb (different from usals_pos
                                                                          #for an offset lnb
                          (19, 'int16_t', 'cur_sat_pos', 'sat_pos_none'), #official satellite position last used on this
                                                                          #lnb. This may be different from cur_lnb_pos
                                                                          #in case of multiple close sats or because
                                                                          #of dish tweaking

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


subscription_data = db_struct(name ='subscription_data',
                           fname = 'fedev',
                           db = db,
                           type_id= lord('qs'),
                           version = 1,
                                 fields =((1, 'int32_t', 'subscription_id'),
                                          (2, 'bool', 'has_mux'),
                                          (3, 'bool', 'has_service'),
                                          (5, 'std::variant<std::monostate,chdb::service_t, chdb::band_scan_t>', 'v')
                ))

#Overall contract: if fields like sat_pos, dish_id, mux_key... are set to valid values
#then these values can never be changed by existing subscriptions
#So e.g.,
# -an illegal mux_key (with sat_pos==sat_pos_none) implies that the reservation is for a
#  full sat band (used by spectrum scan) and cannot tune to amux
# if sat_pos is specifified, then this frontend promises to always remain tuned to this sat
# if usals_pos =- sat_pos_none, then dish can be moved at any time (and sat_pos==sat_pos_none as well)

fe_subscription = db_struct(name='fe_subscription',
                           fname = 'fedev',
                           db = db,
                           type_id= lord('qr'),
                           version = 1,
                           fields = ((1, 'int32_t', 'owner', -1),
                                     (14, 'int32_t', 'config_id', -1),
                                     (3, 'rf_path_t', 'rf_path'),
                                     (2, 'int16_t', 'sat_pos', 'sat_pos_none'),   #if value is sat_pos_none
                                                                                  #then subscription
                                                                                  #is allowed to switch
                                                                                  #to different sat
                                     (4, 'chdb::fe_polarisation_t', 'pol', 'chdb::fe_polarisation_t::NONE'),
                                     (5, 'chdb::sat_sub_band_t', 'band', 'chdb::sat_sub_band_t::NONE'),
                                     (6, 'int16_t', 'usals_pos', 'sat_pos_none'), #if value is sat_pos_none
                                                                                  #then subscription
                                                                                  #is allowed to switch
                                                                                  #to different sat by
                                                                                  #selecting another lnb using diseqc
                                                                                  #or by moving the dish using diseqc
                                                                                       #is subscribed exclusive
                                     (7, 'int16_t', 'dish_usals_pos', 'sat_pos_none'), #if value is_pos_none
                                                                                       #then subscription is allowed
                                                                                       #to move dish
                                     (9, 'int8_t', 'dish_id', '-1'), #if value is -1
                                                                     #then subscription is allowed
                                                                     #to move dish
                                     (8, 'int32_t', 'frequency', '0'),
                                     (10, 'int32_t', 'rf_coupler_id', '-1'),
                                     (12, 'chdb::mux_key_t' , 'mux_key'),
                                     (13, 'ss::vector<subscription_data_t>' , 'subs'),
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
                        (14, 'bool', 'tune_use_blind_tune', 'false'),
                        (12, 'bool', 'tune_may_move_dish', 'false'),
                        (10, 'int32_t', 'dish_move_penalty', 100),
                        (11, 'int32_t', 'resource_reuse_bonus', 1000),
                        (16, 'bool', 'scan_use_blind_tune' , 'true'),
                        (13, 'bool', 'scan_may_move_dish', 'false'),
                        (18, 'int32_t', 'scan_max_duration', '180'),
                        (17, 'bool', 'band_scan_save_spectrum', 'false'),
                        (15, 'bool', 'positioner_dialog_use_blind_tune', 'true'),
                        (6, 'int32_t', 'default_record_time', '2*3600'), #2 hours
                        (3, 'int32_t', 'pre_record_time', '1*60'), #1 minute
                        (4, 'int32_t', 'max_pre_record_time', '1*3600'), #1 hour
                        (5, 'int32_t', 'post_record_time', '5*60'), #5 minutes
                        (7, 'int32_t', 'timeshift_duration', '2*3600'), #2 hours
                        (8, 'int32_t', 'livebuffer_retention_time', '10*60'),  #10 minutes
                        (9, 'int32_t', 'livebuffer_mpm_part_duration', '10*60'),  #10 minutes
                        (19, 'ss::string<16>', 'softcam_server', '"192.168.2.254"'),
                        (20, 'int16_t', 'softcam_port', '9000'),
                        (21, 'bool', 'softcam_enabled', 'true')
                    ))


tuned_frequency_offsets_key = \
    db_struct(name='tuned_frequency_offsets_key',
              fname = 'fedev',
              db = db,
              type_id= lord('FO'),
              version = 1,
              fields = (
                  (1, 'lnb_key_t', 'lnb_key'),
                  (2, 'chdb::sat_sub_band_t', 'band'),
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


scan_target = db_enum(name='scan_target_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
            ('NONE', 0),
            'SCAN_MINIMAL',
            'SCAN_FULL',
            'SCAN_FULL_AND_EPG',
            'DONE'
            )))

tune_mode = db_enum(name='tune_mode_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
            ('IDLE', 0),
            'NORMAL',
            'SPECTRUM',
            'BLIND',
            'POSITIONER_CONTROL',
            'UNCHANGED'
            )))

retune_mode = db_enum(name='retune_mode_t',
        db = db,
        storage = 'int8_t',
        type_id = 100,
        version = 1,
        fields = ((
            ('AUTO', 0), #for normal tuning: retune if lock failed or if wrong sat detected
            'NEVER',
            'IF_NOT_LOCKED',
	          'UNCHANGED'
            )))

subscription_type = db_enum(name='subscription_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=(
                       ('NONE', '-1'),
                       'TUNE', # regular viewing: resourced are reserved non-exclusively. This means other lnbs
                               # on the same dish can be used by other subscriptions
	                     'MUX_SCAN',  # scanning muxes in the background: resources are reserved non-exclusively,
                                    # also reserved non-exclusively
	                     'BAND_SCAN',  # scanning spectral band in the background: resources are reserved
                                              # non-exclusively
                       'SPECTRUM_ACQ',  # Spectrum acquisition from spectrum_dialog reserved exclusively
	                     'LNB_CONTROL' #in this case, a second subscriber cannot subscribe to the mux
                                     #at first tune, position data is used from the lnb. Retunes cannot
										                 #change the positioner and diseqc settings afterwards. Instead, the user
										                 #must explicitly force them by a new tune call (diseqc swicthes), or by
                                     #sending a positoner commands (usals, diseqc1.2)
										                 #
                                     #Also, lnb and dish are reserved exclusively, which means no other lnbs on
                                     #the dish can be used on the same dish
	                 ))

run_type = db_enum(name='run_type_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=('NEVER',
                           'ONCE',
                           'HOURLY',
                           'DAILY',
                           'WEEKLY',
                           'MONTHLY'
                   ))

run_status = db_enum(name='run_status_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=('NONE',
                           'RUNNING',
                           'PENDING',
                           'FINISHED',
                   ))

run_result = db_enum(name='run_result_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=('NONE',
                           'FAILED',
                           'ABORTED', #command was stopped because it ran too long
                           'SKIPPED', #command should have been run, but was not and will not be run in future
                           'OK',
                   ))


stream_state = db_enum(name='stream_state_t',
                   db = db,
                   storage = 'int8_t',
                   type_id = 100,
                   version = 1,
                   fields=('OFF',
                           'ON'    #running
                   ))


tune_options = db_struct(name ='tune_options',
    fname = 'options',
    db = db,
    type_id = lord('TP'),
    version = 1,
    ignore_for_equality_fields = ('mtime',),
    fields = ((16,  'subscription_type_t', 'subscription_type'),
              (1, 'scan_target_t', 'scan_target'),
              (2, 'int32_t', 'scan_max_duration', '180'),
              (3, 'std::optional<ss::vector_<int8_t>>', 'allowed_dish_ids'),
              (4, 'std::optional<ss::vector_<int64_t>>', 'allowed_card_mac_addresses'),
              (5, 'std::optional<ss::vector_<rf_path_t>>', 'allowed_rf_paths'),
              (7, 'bool', 'use_blind_tune', 'false'),
	            (8, 'bool', 'may_move_dish', 'false'),    #subscription is allowed to move the dish when tuning if no
                                                        #other subscriptions conflict; afterwards dish may not be moved
	            (11, 'bool', 'propagate_scan', 'true'),
	            (12, 'bool', 'need_spectrum', 'false'),
	            (13, 'retune_mode_t', 'retune_mode', 'retune_mode_t::AUTO'),
              (14, 'int32_t', 'resource_reuse_bonus', '0'),
	            (15, 'int32_t', 'dish_move_penalty', '0')
              ))

band_scan_options = db_struct(
    name ='band_scan_options',
    fname = 'options',
    db = db,
    type_id = lord('Sb'),
    version = 1,
    ignore_for_equality_fields = ('mtime',),
    fields = (
              (1, 'int32_t', 'start_freq', '-1'),
              (2, 'int32_t', 'end_freq', '-1'),
              (3, 'ss::vector<chdb::fe_polarisation_t,4>', 'pols')
              ))

scan_stats = db_struct(
    name ='scan_stats',
    fname = 'options',
    db = db,
    type_id = lord('Ss'),
    version = 1,
    ignore_for_equality_fields = ('mtime',),
    fields = ((1, 'int16_t', 'pending_peaks', '0'),
	            (2, 'int16_t', 'pending_muxes', '0'),
	            (3, 'int16_t', 'pending_bands', '0'),
	            (4, 'int16_t', 'active_muxes', '0'),
	            (5, 'int16_t', 'active_bands', '0'),
	            (6, 'int16_t', 'finished_peaks', '0'), #total number of peaks we scanned without using database parameters
	            (7, 'int16_t', 'finished_muxes', '0'), #total number of muxes we scanned
	            (8, 'int16_t', 'finished_bands', '0'), #total number of bands we scanned
	            (9, 'int16_t', 'failed_peaks', '0'), #peaks which we could not lock with frequency/symbolrate from spectrum
	            (10, 'int16_t', 'failed_muxes', '0'), #muxes which could not be locked
	            (11, 'int16_t', 'locked_peaks', '0'), #peaks which we could lock with frequency/symbolrate from spectrum
	            (12, 'int16_t', 'locked_muxes', '0'), #muxes which locked
	            (13, 'int16_t', 'si_muxes', '0'), #muxes with si data
              (14, 'bool', 'finished', 'false')
              ))

scan_command = db_struct(
    name ='scan_command',
    fname = 'options',
    db = db,
    type_id = lord('SC'),
    version = 1,
    primary_key= ('key', ('id',)),
    keys = ((lord('rs'), 'run_status_next_time', ('run_status', 'next_time')),
            ),
    ignore_for_equality_fields = ('mtime',),
    fields = ((1, 'int16_t', 'id', '-1'), # -1 means "not set"
              (2, 'time_t', 'start_time', '-1'), #when to run first
              (3, 'run_type_t', 'run_type', 'run_type_t::NEVER'), #what time of day, day of week or month
              #to run
              (4, 'int16_t', 'interval', '1'), #larger interval
              (5, 'int16_t', 'max_duration', '3600'), #max duration in seconds
              (6, 'bool', 'catchup', 'true'), #if true, then run the last planned scan if it was not run
              (17, 'time_t', 'next_time', '-1'), #when to run next
              (7, 'time_t', 'mtime'),
              (16, 'run_status_t', 'run_status'), #set when command is actually started
              (15, 'time_t', 'run_start_time', '-1'), #last time command was actually started
              (19, 'time_t', 'run_end_time', '-1'), #last time command was actually started
              (18, 'run_result_t', 'run_result'), #set when command is actually started
              (9, 'tune_options_t', 'tune_options'),
              (11, 'band_scan_options_t', 'band_scan_options'),
              (10, 'ss::vector<chdb::sat_t,1>', 'sats'),
              (12, 'ss::vector<chdb::dvbs_mux_t,1>', 'dvbs_muxes'),
              (13, 'ss::vector<chdb::dvbc_mux_t,1>', 'dvbc_muxes'),
              (14, 'ss::vector<chdb::dvbt_mux_t,1>', 'dvbt_muxes'),
              (20, 'int32_t', 'owner', -1), #pid of the process executing the recording, or -1
                                            #when the command is not executing
              (21, 'int32_t', 'subscription_id'), #subscription_id of recording in progress
              (22, 'scan_stats_t', 'scan_stats'), #subscription_id of recording in progress

              ))

stream = db_struct(name='stream',
               fname = 'fedev',
               db = db,
               type_id= lord('st'),
               version = 1,
               primary_key = ('key', ('stream_id',)),
               fields = (
                   (1, 'int32_t', 'stream_id', '-1'), #unique identifier
                   (2, 'stream_state_t', 'stream_state', 'stream_state_t::ON'),
                   (3, 'std::variant<chdb::dvbs_mux_t,chdb::dvbc_mux_t, chdb::dvbt_mux_t, chdb::service_t>',
                    'content'),
                   (4, 'ss::string<32>', 'dest_host', '"127.0.0.1"'),
                   (5, 'int32_t', 'dest_port', "9999"),
                   (6, 'int32_t', 'subscription_id', '-1'), #subscription_id when active
                   (9, 'int32_t', 'streamer_pid', -1), #pid of the process executing the stream or -1
                   (11, 'int32_t', 'owner', -1), #pid of the process owning stream or -1
                   (7, 'int32_t', 'user_id', '0'),
                   (8, 'time_t', 'mtime'),
                   (12, 'bool', 'autostart', 'false'), #start when neumoDVB is started
                   (10, 'bool', 'preserve', 'true') #remove when stopped
               ))
