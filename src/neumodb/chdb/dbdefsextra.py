import os
from inspect import getsourcefile

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir

    
dbname = 'testchdb'

from generators import set_env, db_db, db_struct, db_enum
    
set_env(this_dir= get_scriptdir(), dbname=dbname, output_dir=None)

db = db_db()

#tuner capability record
# part 1: what can it tune to? one satellite or all satellites on a positioner? All polarisations or only some?
#         C-band, ku-band high, ku band low ...
# part 2: linked tuners, i.e., restrictions related to slave/master tuners; this does NOT include restrictions
#         due to lnbs being on same positioner, as this is implied by using the positioner; it COULD however include
#         restrictions like: this lnb is always 3 degrees of the other one; the latter could be implemented
#         as part of the positioner
#part  3: how tuning is achieved; polarisation and band are not included because this is done automatically, possibly by a master tuner
tuner_cap = db_struct(name='tuner_cap',
                fname = 'tuner',
                db = db,
                is_table = True,
                type_id= ord('c'),
                version = 1,
                primary_key = ('mux', ('k',)), #unique
                keys =  (
                ), 
                fields = ((1, 'int8_t', 'tuner_id'), #tuner used for this capability
                          (1, 'uint16_t', 'sat_pos'), #100*orbital position, or 128000+i to refer to positioner i
                          (2, 'uint32_t', 'frequency'),
                          (3, 'int8_t',  'polarisations', ''), #bit plag indicating which polarisations can be used
                          (4, 'uint8_t',  'bands'), #bit flag for each possible band
                          (5, 'int8_t', 'delivery_systems'), #Not needed? could be autodiscovered. bit flag for each [posisble system
	                        (7, 'int8_t', 'modulations'), #Not needed? could be autodiscovered. bit flag for each [posisble modulation
                          #part 2; NOTe tuner_group could be removedl in that case a negative
                          #master tuner indicates a group, which all tuners being able to act as master
	                        (8, 'int8_t', 'tuner_group', -1), #index of group of linked tuner which share some restrictions
                                                            #-1 if not in group
	                        (9, 'int8_5', 'master_tuner', -1),  #index of the tuner which needs to send diseqc commands,
                                                              #(including for positioner) select polarisation
                                                              #-1 if this tuner can act as master tuner
	                        (10, 'bool', 'link_polarisation'), #true if all tuners in the group share polarisation
	                        (11, 'bool', 'link_band'), #true if all tuners in the group share frequency band

                          #part 3
                          (12, 'ss:string' , 'tune_string'),
                          # list of commands separted by ";"
                          #can contain
                          #  ABCD diseqc 1.0
                          #  1...64 diseqc 1.1
                          #  " "    long pause
                          #  ","    short pause
                          #  P send positioner commands
                          #  PH send positioner commands while keeping voltage high (select H polarisation)
                          #  T  ask driver to tune frequency and such
                ))

                    

position_t = db_struct(name='position',
                        fname = 'tuner',
                        db = db,
                        is_table = False,
                        type_id= ord('p'),
                        version = 1,
                        fields = ((1, 'int16_t', 'position'), #official position as on king of sat
                                  (2, 'bool', 'use_usals'), #usals or diseqc1.2
                                  (2, 'int16_t',  'usals_position') #actual usals position to send, or diseqc1.2 index
                                  
                                  )
                        )
                        
positioner = db_struct(name='positioner',
                        fname = 'tuner',
                        db = db,
                        is_table = False,
                        type_id= ord('P'),
                        version = 1,
                        fields = ((1, 'int16_t', 'id'),
                                  (2, 'ss::vector<position_t,128>', 'positions') #positions that this positioner can tune
                                                                               #to
                                  
                                  )
                        )
                        
def generate(output_dir='/tmp'):
    db.save_enums()
    db.save_structs()
    db.save_db()
