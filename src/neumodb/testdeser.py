#!/usr/bin/python3
import sys
import os
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/neumodb/statdb')
sys.path.insert(0, '../../build/src/neumodb/schema')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pydeser
import pychdb
import pyschemadb
chdb = pychdb.chdb()
chdb.open("/mnt/neumo/db/chdb.mdb/", allow_degraded_mode=True)
txn=chdb.rtxn()

if False:
    sort_order = pychdb.service.subfield_from_name('k.service_id')<<24
    screen=pychdb.lnb.screen(txn, sort_order=sort_order)

    for idx in range(screen.list_size):
        ll = screen.record_at_row(idx)
        print(ll)


d=pydeser.test_export(txn)
lnbs=[x for x in d if x['type']=='lnb']

if False:
    schema = pydeser.schema_map(txn)
    print(f"database: type={schema.db_type} version={schema.version}")
    print(f"Record types")
    for rec  in schema.schema:
        print(f"    {rec.name}: id={rec.type_id} vers={rec.record_version}")
        for field in rec.fields:
            print(f"        {field.name}: {field.type} # id={field.field_id} "
                  f" size={field.serialized_size}")


if False: #pure python implemantion which return map (not compiled)
    schema = pydeser.schema_map(txn)
    print(f"database: type={schema['db_type']} version={schema['version']}")
    print(f"Record types")
    for key, rec in schema['records'].items():
        print(f"    {rec['name']}: id={rec['type_id']} vers={rec['record_version']}")
        for field in rec['fields']:
            print(f"        {field['name']}: {field['type']} # id={field['field_id']} "
                  f" size={field['serialized_size']}")
