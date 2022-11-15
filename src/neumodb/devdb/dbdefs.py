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


gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='c', output_dir=None)

db = db_db(gen_options)

def lord(x):
    return  int.from_bytes(x.encode(), sys.byteorder)

db_include(fname='stats', db=db, include='neumodb/chdb/chdb_db.h')

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
			                     'WDBUK'
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
                              (4, 'int64_t', 'card_mac_address', -1), #Unique for each card
                              (1, 'int8_t', 'rf_input', -1),

                              (3, 'int8_t', 'dish_id', 0), #dish_id=0 is also the "default dish"
                              #because of switches, the same cable could be attached to multiple dishes

                              (2, 'int16_t', 'lnb_id', '-1'), #unique identifier for lnb
                              #Because of swicthes the same cable may lead to multiple lnbs
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
                          (20, 'int16_t', 'usals_pos', 'sat_pos_none'),

                          (2, 'lnb_pol_type_t',  'pol_type', 'lnb_pol_type_t::HV'), #bit flag indicating which polarisations can be used
                          (3, 'bool',  'enabled', 'true'), #bit flag indicating if lnb is allowed to be used
                          (4, 'int16_t',  'priority', -1), #
                          (5, 'int32_t', 'lof_low', -1), # local oscillator, -1 means default
                          (6, 'int32_t', 'lof_high', -1), # local oscillator, -1 means default
                          (7, 'int32_t', 'freq_low', -1), # lowest frequency which can be tuned
                          (18, 'int32_t', 'freq_mid', -1), # frequency to switch between low/high band
                          (19, 'int32_t', 'freq_high', -1), # highest frequency wich can be tuned
                          (8, 'rotor_control_t', 'rotor_control', 'rotor_control_t::FIXED_DISH'), #
                          (21, 'int16_t', 'offset_pos', '0'), #only for master usals positioner: in 1/100 degre: offset w.r.t. t center of dish

                          (10, 'uint8_t' , 'diseqc_mini'),
                          (11, 'int8_t' , 'diseqc_10', '-1'),
                          (12, 'int8_t' , 'diseqc_11', '-1'),
                          # disec12 is not included here as this is part of the dish

                          (14,  'time_t', 'mtime'),
                          #Sometimes more than one network can be received on the same lnb
                          #for an lnb

                          (24, 'bool', 'can_be_used', 'true'), #updated as adapters are discovered
                          (25, 'int8_t', 'card_no',  '-1'), #updated as adapters are discovered

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



rf_input_key = db_struct(name='rf_input_key',
                          fname = 'fedev',
                          db = db,
                          type_id= lord('RI'),
                          version = 1,
                          fields = (
                              (4, 'int64_t', 'card_mac_address', -1), #Unique for each card
                              (1, 'int8_t', 'rf_input', -1)
                          )
                        )


rf_input = db_struct(name='rf_input',
                   fname = 'fedev',
                   db = db,
                   type_id= lord('ri'),
                   version = 1,
                   primary_key = ('key', ('k',)), #unique; may need to be revised
                   keys =  (
                ),
                     fields = ((1, 'rf_input_key_t', 'k'),  #unique id for one of the connectors on one of the cards
                               (3, 'int16_t', 'switch_id', -1), #if>=0 means inputs connected to same cable
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
                                     (3, 'int16_t', 'rf_in', -1),
                                     #(8, 'int16_t', 'rf_group_id', -1),
                                     (4, 'lnb_key_t', 'lnb_key'),
                                     (5, 'chdb::fe_polarisation_t', 'pol', 'chdb::fe_polarisation_t::NONE'),
                                     (6, 'fe_band_t', 'band', 'fe_band_t::NONE'),
                                     (7, 'int16_t', 'usals_pos', 'sat_pos_none')
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
                   (25, 'int16_t', 'rf_in'),
                   (21, 'int16_t', 'adapter_no'),
                   (24, 'bool', 'supports_neumo'),
                   (2, 'bool', 'present'),
                   (3, 'bool', 'can_be_used', 'true'),
                   (4, 'uint8_t', 'enable_dvbs', 'true'),
                   (30, 'uint8_t', 'enable_dvbt', 'true'),
                   (31, 'uint8_t', 'enable_dvbc', 'true'),
                   (5, 'int16_t', 'priority', 0),

                   #link_group_id: -1 means not linked
                   #(6, 'int16_t', 'link_group_id', -1),

                   #master_tuner_id: -1 means not linked
	                 #(7, 'int8_t', 'tuner_group', -1),   #index of group of linked tuner which share some restrictions
                   #(23, 'int64_t', 'master_adapter_mac_address', -1),

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
                        (0, 'int16_t', 'usals_lattitude', '5100'), #in 1/100 degree
                        (1, 'int16_t', 'usals_longitude', '4100'), #in 1/100 degree
                        (2, 'int16_t', 'usals_altitude', '0') #in m (?)
                              )
                    )
