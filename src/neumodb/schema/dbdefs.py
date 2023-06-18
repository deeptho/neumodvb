import os
from inspect import getsourcefile

def get_scriptdir():
    scriptdir=os.path.dirname(__file__)
    if scriptdir is None:
        scriptdir = os.path.dirname(os.path.abspath(getsourcefile(lambda:0)))
    return scriptdir


dbname = 'schema'

from generators import set_env, db_db, db_struct, db_enum

gen_options = set_env(this_dir= get_scriptdir(), dbname=dbname, db_type_id='@', output_dir=None)

db = db_db(gen_options)


"""


"""


neumo_schema_index_field =db_struct(name='neumo_schema_index_field',
                    fname = 'schema',
                    db = db,
                    type_id= 0x00ffff05,
                    version = 1,
                    fields = ((5, 'ss::string<16>', 'name'),
                              ))

neumo_schema_index_record =db_struct(name='neumo_schema_index_record',
                    fname = 'schema',
                    db = db,
                    type_id= 0x00ffff06,
                    version = 1,
                    fields = ((1, 'int32_t', 'type_id'), #of parent record (not needed)
                              (2, 'int32_t', 'index_id'),
                              (3, 'ss::string<32>', 'name'),
                              (4, 'ss::vector<neumo_schema_index_field_t,16>', 'fields'),
                              ))

neumo_schema_record_field =db_struct(name='neumo_schema_record_field',
                    fname = 'schema',
                    db = db,
                    type_id= 0x00ffff01,
                    version = 1,
                    fields = ((1, 'int32_t', 'field_id'),
                              (2, 'int32_t', 'type_id'),
                              (3, 'int32_t', 'serialized_size'),
                              (4, 'ss::string<16>', 'type'),
                              (5, 'ss::string<16>', 'name'),
                              ))

neumo_schema_record =db_struct(name='neumo_schema_record',
                    fname = 'schema',
                    db = db,
                    type_id= 0x00ffff02,
                    version = 1,
                    fields = ((1, 'int32_t', 'type_id'),
                              (2, 'uint32_t', 'record_version'),
                              (3, 'ss::string<32>', 'name'),
                              (4, 'ss::vector<neumo_schema_record_field_t,16>', 'fields'),
                              #The following is not yet ok: if overall schema is changed, then database upgrade code
                              #will read the wronng "schema" schema. This "schema" schema would need to be
                              #stored in the database so that it can upgrade itself
                              #(5, 'ss::vector<neumo_schema_index_record_t,4>', 'indexes'),
                              ))

neumo_schema_key = db_struct(name='neumo_schema_key',
                    fname = 'schema',
                    db = db,
                    type_id= 0x00ffff03,
                    version = 1,
                    fields = ((1, 'int64_t', 'magic', '0x0b28b65981858458LL'), ))



neumo_schema = db_struct(name='neumo_schema',
                fname = 'schema',
                db = db,
                type_id= 0x00ffff04,
                version = 1,
                primary_key = ('key', ('k',)), #unique
                keys =  (
                ),
                fields = ((1, 'neumo_schema_key_t', 'k'),
                          (2, 'uint32_t', 'version', '0'),
                          (3, 'ss::string<32>', 'db_type', '"generic"'),
                          (5, 'ss::vector<uint8_t,16>', 'uuid', '0'), #set at database creation and never changed
                          (4, 'ss::vector<neumo_schema_record_t, 64>', 'schema')))
