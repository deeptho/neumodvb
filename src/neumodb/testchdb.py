#!/usr/bin/python3
import sys
import os
import shutil
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/neumodb/neumodb')
sys.path.insert(0, '../../build/src/neumodb/chdb')
sys.path.insert(0, '../../build/src/neumodb/epgdb')
sys.path.insert(0, '../../build/src/stackstring/')
#import pyneumodb
import pychdb
import pyepgdb

def pl(lst):
    for x in lst:
        print(f"n={x.ch_order}, mm={x.media_mode} sat={x.k.mux.sat_pos} "
              f"mux={x.k.mux.network_id}-{x.k.mux.ts_id}-{x.k.mux.extra_id} sid={x.k.service_id}")

def make_service(ch_order, service_id=None, ts_id=12, sat_pos=282, name=None):
    service_id = ch_order if service_id is None else service_id
    s=pychdb.service.service()
    s.k.mux.sat_pos=sat_pos
    s.k.mux.network_id=1
    s.k.mux.ts_id=ts_id
    s.k.service_id = service_id
    s.ch_order=ch_order
    s.name = f'service {service_id}' if name is None else name
    return s

def add_service(txn, *args, **kwargs):
    s= make_service(*args, **kwargs)
    pychdb.put_record(txn, s)
    return s

ref=None
toremove=None

def make_services1():
    txn = chdb.wtxn()
    global ref
    global toremove
    ref=add_service(txn, 105, name='bbc5')
    add_service(txn, 102, name='bbc2')
    add_service(txn, 101, name='bbc1')

    toremove=add_service(txn, 103, name='bbc3')
    add_service(txn, 104, name='bbc4')
    txn.commit()

def test2_make_services2():
    """
    add 4 services, including 2 with same ch_order
    in a single transaction
    """

    txn = chdb.wtxn()
    ref=add_service(txn, 105, name='bbc5')
    add_service(txn, 102, name='bbc2')
    add_service(txn, 101, name='bbc1')

    toremove=add_service(txn, 103, name='bbc3')
    add_service(txn, 101, service_id=104, name='bbc4')
    txn.commit()
    return ref, toremove

def add_services1():
    txn = chdb.wtxn()
    add_service(txn, 101, name='bbc1 updated')
    add_service(txn, 99, name='bbc0')
    txn.commit()

def remove_services1(toremove):
    txn = chdb.wtxn()
    pychdb.delete_record(txn, toremove)
    txn.commit()

def ps(screen):
    for i in range(screen.list_size):
        rec = screen.record_at_row(i)
        #print (rec.k.chg.bouquet_id)
        if rec.k.service.mux.sat_pos == 282:
            print(rec, rec.mtime)
    print("="*10)

def get_screen():
    txn = chdb.rtxn()
    subfield = pychdb.service.subfield_from_name('ch_order')<<24
    screen= pychdb.service.screen(txn, subfield)
    txn.abort()
    return screen


def update(screen):
    txn = chdb.rtxn()
    screen.update(txn)
    txn.abort()
    print('+'*10)


chdb_name = "/tmp/chdb.mdb/"


def test1():
    global chdb
    shutil.rmtree(chdb_name, ignore_errors=True)
    chdb = pychdb.chdb()
    chdb.open(chdb_name)
    make_services1()
    screen = get_screen()
    screen.set_reference(ref)
    ps(screen)
    add_services1()
    ps(screen)
    update(screen)
    ps(screen)


def test2():
    """
    """

    #add 4 services, including 2 with same ch_order in a single transactions
    ref, toremove = test2_make_services2()
    screen = get_screen()
    #set reference bbc5 (non dupllicate)
    screen.set_reference(ref)
    ps(screen)
    if True:
        remove_services1(toremove)
        ps(screen)
    print("update2")
    update(screen)
    print("update2 done")
    ps(screen)
    return screen

if False:
    screen=test2()

    print ('-'*10)
    print('reverse')
    for row in reversed(range(4)):
        rec=screen.record_at_row(row)
        print(f"ROW={row} rec={rec}")

    print ('-'*10)
    print('forward')
    for row in range(4):
        rec=screen.record_at_row(row)
        print(f"ROW={row} rec={rec}")
    print ('-'*10)

def is_same(s1, s2):
    return s1.k.service_id == s2.k.service_id and s1.k.mux.ts_id == s1.k.mux.ts_id \
        and s1.k.mux.sat_pos == s2.k.mux.sat_pos

def test_sceen(screen, services):
    """
    test if screen agrees with services
    """
    for r1,r2 in zip(screen.records, services):
        assert(is_same(r1, r2))
    print ("PASS: screen")

def test_list(screen, services, reverse):
    """
    test if screen agrees with services
    """
    def reader():
        for row in reversed(range(len(services))) if reverse else range(len(services)):
            rec=screen.record_at_row(row)
            yield rec
    for r1,r2 in zip(reader(), reversed(services) if reverse else services):
        assert(is_same(r1, r2))
    print (f"PASS: list reverse={reverse}")

def testa1():
    """
    multiple services in a single transaction, with non-duplicate ch_orders
    list forward and reverse
    """
    global chdb
    shutil.rmtree(chdb_name, ignore_errors=True)
    chdb = pychdb.chdb()
    chdb.open(chdb_name)
    txn = chdb.wtxn()
    services =[]
    ref=add_service(txn, 105, name='bbc5')
    services.append(ref)
    services.append(add_service(txn, 102, name='bbc2'))
    services.append(add_service(txn, 101, name='bbc1'))
    toremove=add_service(txn, 103, name='bbc3')
    services.append(toremove)
    services.append(add_service(txn, 104, name='bbc4'))
    services.append(add_service(txn, 106, name='bbc6'))
    services.append(add_service(txn, 107, name='bbc7'))
    txn.commit()
    screen = get_screen()
    correct = sorted(services, key = lambda s: s.ch_order)
    test_sceen(screen, correct)
    #test without reference
    test_list(screen, correct, True)
    test_list(screen, correct, False)
    #try al possible references
    for s in services:
        screen.set_reference(s)
        test_list(screen, correct, True)
        test_list(screen, correct, False)


def testa2(idxref, idxtoremove):
    """
    multiple services in a single transaction, followed by removal, with non-duplicate ch_orders
    list forward and reverse
    """
    global chdb
    shutil.rmtree(chdb_name, ignore_errors=True)
    chdb = pychdb.chdb()
    chdb.open(chdb_name)
    txn = chdb.wtxn()
    services =[]
    services.append(add_service(txn, 105, name='bbc5'))
    services.append(add_service(txn, 102, name='bbc2'))
    services.append(add_service(txn, 101, name='bbc1'))
    services.append(add_service(txn, 103, name='bbc3'))
    services.append(add_service(txn, 104, name='bbc4'))

    txn.commit()
    screen = get_screen()

    txn = chdb.wtxn()
    pychdb.delete_record(txn, services[idxtoremove])
    txn.commit()

    txn = chdb.rtxn()
    screen.update(txn)
    txn.abort()
    ref= services[idxref]
    del services[idxtoremove]
    correct = sorted(services, key = lambda s: s.ch_order)

    screen.set_reference(ref)
    test_list(screen, correct, True)
    test_list(screen, correct, False)

if False:
    testa1()
if False:
    for i1 in range(5):
        for i2 in range(5):
            if i2 != i1:
                testa2(i1, i2)


def lnb_screen():
    txn = chdb.rtxn()
    subfield = pychdb.lnb.subfield_from_name('k.lnb_pos')<<24
    screen= pychdb.lnb.screen(txn, subfield)
    txn.abort()
    return screen


def chg_screen():
    txn = chdb.rtxn()
    subfield = pychdb.chg.subfield_from_name('k.bouquet_id')<<24
    screen= pychdb.chg.screen(txn, subfield)
    txn.abort()
    return screen

def chgm_screen():
    txn = chdb.rtxn()
    subfield = pychdb.chgm.subfield_from_name('chgm_order')<<24
    screen= pychdb.chgm.screen(txn, subfield)
    txn.abort()
    return screen


chdb = pychdb.chdb()
chdb.open('/mnt/neumo/db/chdb.mdb')
s=get_screen()
if False:
    for i in range(s.list_size):
        rec= s.record_at_row(i)
        print(f'[{i}]= {rec}')

if False:
    chgs = chg_screen()
    ps(chgs)
if False:
    chgm = chgm_screen()
    ps(chgm)

if False:
    print ('-'*10)
    print('forward')
    for row in range(4):
        rec=screen.record_at_row(row)
        print(f"ROW={row} rec={rec}")


    print ('-'*10)
    print('reverse')
    for row in reversed(range(6)):
        rec=screen.record_at_row(row)
        print(f"ROW={row} rec={rec}")
if True:
    screen=get_screen()
    txn = chdb.wtxn()
    for row in range(screen.list_size):
        rec=screen.record_at_row(row)
        if rec.ch_order == 0:
            rec.ch_order=65535
            pychdb.put_record(txn, rec)
    txn.commit()
