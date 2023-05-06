#!/usr/bin/python3
import sys
import os
#sys.path.insert(0, '../../gui')
import neumodvb
from neumodvb.config import options
from neumodvb.util import dtdebug, dterror
import pydeser
import pychdb
import pydevdb
import pystatdb
import pyrecdb
import pyepgdb

def clean_dvbs_muxes(wtxn):
    ref = pychdb.dvbs_mux.dvbs_mux()
    screen=pychdb.dvbs_mux.screen(wtxn, sort_order=pychdb.dvbs_mux.dvbs_mux_order.key,
                                  key_prefix_data =ref,
                                  key_prefix_type=pychdb.dvbs_mux.dvbs_mux_prefix.none)
    for i in range(screen.list_size):
        rec=screen.record_at_row(i)
        pychdb.delete_record(wtxn, rec)

def clean_dvbc_muxes(wtxn):
    ref = pychdb.dvbc_mux.dvbc_mux()
    screen=pychdb.dvbc_mux.screen(wtxn, sort_order=pychdb.dvbc_mux.dvbc_mux_order.key,
                                  key_prefix_data =ref,
                                  key_prefix_type=pychdb.dvbc_mux.dvbc_mux_prefix.none)
    for i in range(screen.list_size):
        rec=screen.record_at_row(i)
        pychdb.delete_record(wtxn, rec)

def clean_dvbt_muxes(wtxn):
    ref = pychdb.dvbt_mux.dvbt_mux()
    screen=pychdb.dvbt_mux.screen(wtxn, sort_order=pychdb.dvbt_mux.dvbt_mux_order.key,
                                  key_prefix_data =ref,
                                  key_prefix_type=pychdb.dvbt_mux.dvbt_mux_prefix.none)
    for i in range(screen.list_size):
        rec=screen.record_at_row(i)
        pychdb.delete_record(wtxn, rec)

def clean_services(wtxn):
    ref = pychdb.service.service()
    screen=pychdb.service.screen(wtxn, sort_order=pychdb.service.service_order.key,
                                  key_prefix_data =ref,
                                  key_prefix_type=pychdb.service.service_prefix.none)
    for i in range(screen.list_size):
        rec=screen.record_at_row(i)
        pychdb.delete_record(wtxn, rec)

class upgrader_t(object):
    from_version=2
    to_version=3
    def print(self, *args):
        print(*args)
        dtdebug(*args)

    def __init__(self):
        #maps old mux_key to new mux
        self.mux_map ={}
        #maps new key to old key
        self.inverse_mux_map ={}
        self.stdout = None

    def upgrade(self,from_version, to_version):
        global options
        chdb = pychdb.chdb()
        chdb.open(options.chdb, allow_degraded_mode=True)
        if chdb.db_version != from_version  or  pychdb.neumo_schema_version != to_version:
            self.print(f'This script can only upgrade from version={from_version} to version={to_version}')
            self.print(f'Current chdb version: {chdb.db_version}; needed={pychdb.neumo_schema_version}')
            return False
        chdbtxn = chdb.rtxn()
        d=pydeser.degraded_export(chdbtxn)
        del chdbtxn
        del chdb
        self.dvbs_muxes = [x for  x in d if x['type']=='dvbs_mux']
        self.dvbc_muxes = [x for  x in d if x['type']=='dvbc_mux']
        self.dvbt_muxes = [x for  x in d if x['type']=='dvbt_mux']
        self.services= [ x for x in d if x['type']=='service']
        self.chgms= [ x for x in d if x['type']=='chgm']

        self.upgrade_chdb()
        self.upgrade_recdb()
        self.upgrade_devdb()
        self.upgrade_statdb()
        self.upgrade_epgdb()
        return True

    def upgrade_chdb(self):
        global options

        self.chdb = pychdb.chdb(autoconvert_major_version=True)
        self.chdb.open(options.chdb)

        wtxn = self.chdb.wtxn()

        self.convert_muxes(wtxn)
        self.convert_services(wtxn)
        self.convert_chgms(wtxn)
        wtxn.commit()

    def upgrade_recdb(self):
        global options
        recdb = pyrecdb.recdb(autoconvert_major_version=True)
        recdb.open(options.recdb)

        recdb_wtxn = recdb.wtxn()
        chdb_rtxn = self.chdb.rtxn()
        self.fix_recs(recdb_wtxn, chdb_rtxn)
        chdb_rtxn.abort()
        recdb_wtxn.commit()

    def upgrade_devdb(self):
        global options
        devdb = pydevdb.devdb(autoconvert_major_version=True)
        devdb.open(options.devdb, allow_degraded_mode=True)
        devdbrtxn = devdb.rtxn()
        d=pydeser.degraded_export(devdbrtxn)
        del devdb

        self.lnbs = [x for  x in d if x['type']=='lnb']

        devdb = pydevdb.devdb(autoconvert_major_version=True)
        devdb.open(options.devdb)
        wtxn = devdb.wtxn()
        self.convert_lnbs(wtxn)
        wtxn.commit()

    def upgrade_statdb(self):
        global options
        statdb = pystatdb.statdb(autoconvert_major_version=True)
        #force autoconversion
        statdb.open(options.statdb)

    def upgrade_epgdb(self):
        global options
        epgdb = pyepgdb.epgdb(autoconvert_major_version=True)
        #force autoconversion
        epgdb.open(options.epgdb)


    def copy_dvbs_mux(self, wtxn, oldmux):
        newmux = pychdb.dvbs_mux.dvbs_mux()
        newmux.k.mux_id = 0
        newmux.k.sat_pos = oldmux['k']['sat_pos']
        if newmux.k.sat_pos == pychdb.sat.sat_pos_none:
            return
        newmux.k.stream_id = oldmux['stream_id']
        newmux.k.t2mi_pid = oldmux['k']['t2mi_pid']
        if newmux.k.t2mi_pid == 0:
            newmux.k.t2mi_pid = -1
        newmux.c.network_id = oldmux['k']['network_id']
        newmux.c.ts_id = oldmux['k']['ts_id']
        if len(oldmux['c']['epg_types']) >0:
            for t in oldmux['c']['epg_types']:
                newmux.c.epg_types.push_back(pychdb.epg_type_t(t))
        for key in  ['delivery_system', 'fec', 'frequency', 'inversion', 'matype', 'modulation', 'pilot',
                     'pls_code', 'pls_mode', 'pol', 'rolloff', 'symbol_rate']:
            setattr(newmux, key, type(getattr(newmux, key))(oldmux[key]))
        for key in ['key_src', 'nit_network_id', 'scan_duration', 'scan_status',
                    'epg_scan', 'mtime', 'nit_ts_id', 'scan_id', 'scan_time',
                    'num_services', 'scan_result']:
            #todo: 'epg_types',
            setattr(newmux.c, key, type(getattr(newmux.c, key))(oldmux['c'][key]))
        newmux.c.tune_src = pychdb.tune_src_t.TEMPLATE # to force make_unique_if_template to work
        pychdb.dvbs_mux.make_unique_if_template(wtxn, newmux)
        newmux.c.tune_src = type(newmux.c.tune_src)(oldmux['c']['tune_src'])
        k = tuple(v for k,v in oldmux['k'].items())
        self.mux_map[k] = newmux
        key = (newmux.k.sat_pos, newmux.k.stream_id, newmux.k.t2mi_pid, newmux.k.mux_id)
        self.inverse_mux_map[key] = k
        pychdb.put_record(wtxn, newmux)

    def copy_dvbc_mux(self, wtxn, oldmux):
        newmux = pychdb.dvbc_mux.dvbc_mux()
        newmux.k.mux_id = 0
        newmux.k.sat_pos = pychdb.sat.sat_pos_dvbc
        newmux.k.stream_id = oldmux['stream_id']
        newmux.k.t2mi_pid = oldmux['k']['t2mi_pid']
        if newmux.k.t2mi_pid == 0:
            newmux.k.t2mi_pid = -1
        newmux.c.network_id = oldmux['k']['network_id']
        newmux.c.ts_id = oldmux['k']['ts_id']
        if len(oldmux['c']['epg_types']) >0:
            for t in oldmux['c']['epg_types']:
                newmux.c.epg_types.push_back(pychdb.epg_type_t(t))
        for key in  ['delivery_system', 'fec_inner', 'fec_outer', 'frequency', 'inversion', 'modulation',
                     'symbol_rate']:
            setattr(newmux, key, type(getattr(newmux, key))(oldmux[key]))
        for key in ['key_src', 'nit_network_id', 'scan_duration', 'scan_status',
                    'epg_scan', 'mtime', 'nit_ts_id', 'scan_id', 'scan_time',
                    'num_services', 'scan_result']:
            #todo: 'epg_types',
            setattr(newmux.c, key, type(getattr(newmux.c, key))(oldmux['c'][key]))
        newmux.c.tune_src = pychdb.tune_src_t.TEMPLATE # to force make_unique_if_template to work
        pychdb.dvbc_mux.make_unique_if_template(wtxn, newmux)
        newmux.c.tune_src = type(newmux.c.tune_src)(oldmux['c']['tune_src'])
        k = tuple(v for k,v in oldmux['k'].items())
        self.mux_map[k] = newmux
        key = (newmux.k.sat_pos, newmux.k.stream_id, newmux.k.t2mi_pid, newmux.k.mux_id)
        self.inverse_mux_map[key] = k
        pychdb.put_record(wtxn, newmux)

    def copy_dvbt_mux(self, wtxn, oldmux):
        newmux = pychdb.dvbt_mux.dvbt_mux()
        newmux.k.mux_id = 0
        newmux.k.sat_pos = pychdb.sat.sat_pos_dvbt
        newmux.k.stream_id = oldmux['stream_id']
        newmux.k.t2mi_pid = oldmux['k']['t2mi_pid']
        if newmux.k.t2mi_pid == 0:
            newmux.k.t2mi_pid = -1
        newmux.c.network_id = oldmux['k']['network_id']
        newmux.c.ts_id = oldmux['k']['ts_id']
        if len(oldmux['c']['epg_types']) >0:
            for t in oldmux['c']['epg_types']:
                newmux.c.epg_types.push_back(pychdb.epg_type_t(t))
        for key in  ['delivery_system', 'bandwidth', 'guard_interval', 'hierarchy', 'frequency', 'inversion', 'modulation',
                     'transmission_mode']:
            setattr(newmux, key, type(getattr(newmux, key))(oldmux[key]))
        for key in ['key_src', 'nit_network_id', 'scan_duration', 'scan_status',
                    'epg_scan', 'mtime', 'nit_ts_id', 'scan_id', 'scan_time',
                    'num_services', 'scan_result']:
            #todo: 'epg_types',
            setattr(newmux.c, key, type(getattr(newmux.c, key))(oldmux['c'][key]))
        newmux.c.tune_src = pychdb.tune_src_t.TEMPLATE # to force make_unique_if_template to work
        pychdb.dvbt_mux.make_unique_if_template(wtxn, newmux)
        newmux.c.tune_src = type(newmux.c.tune_src)(oldmux['c']['tune_src'])
        k = tuple(v for k,v in oldmux['k'].items())
        self.mux_map[k] = newmux
        key = (newmux.k.sat_pos, newmux.k.stream_id, newmux.k.t2mi_pid, newmux.k.mux_id)
        self.inverse_mux_map[key] = k
        pychdb.put_record(wtxn, newmux)

    def fix_dvbs_ids(self, wtxn):
        screen=pychdb.dvbs_mux.screen(wtxn, sort_order=pychdb.dvbs_mux.dvbs_mux_order.sat_pol_freq,
                                      key_prefix_data =None,
                                      key_prefix_type=pychdb.dvbs_mux.dvbs_mux_prefix.none)
        prevmux = None
        for i in range(screen.list_size):
            mux=screen.record_at_row(i)
            if prevmux is not None and prevmux.pol == mux.pol and prevmux.k.sat_pos == mux.k.sat_pos:
                tol = min(mux.symbol_rate, prevmux.symbol_rate)/2000
                if abs(prevmux.frequency - mux.frequency) < tol:
                    if mux.k.mux_id != prevmux.k.mux_id:
                        pychdb.delete_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        oldkey = self.inverse_mux_map[key]
                        mux.k.mux_id = prevmux.k.mux_id
                        pychdb.put_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        self.inverse_mux_map[key] = oldkey
                        self.mux_map[oldkey] = mux
            prevmux = mux

    def fix_dvbc_ids(self, wtxn):
        screen=pychdb.dvbc_mux.screen(wtxn, sort_order=pychdb.dvbc_mux.dvbc_mux_order.freq,
                                      key_prefix_data =None,
                                      key_prefix_type=pychdb.dvbc_mux.dvbc_mux_prefix.none)
        prevmux = None
        for i in range(screen.list_size):
            mux=screen.record_at_row(i)
            if prevmux is not None:
                tol = min(mux.symbol_rate, prevmux.symbol_rate)/2000
                if abs(prevmux.frequency - mux.frequency) < tol:
                    if mux.k.mux_id != prevmux.k.mux_id:
                        pychdb.delete_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        oldkey = self.inverse_mux_map[key]
                        mux.k.mux_id = prevmux.k.mux_id
                        pychdb.put_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        self.inverse_mux_map[key] = oldkey
                        self.mux_map[oldkey] = mux
            prevmux = mux

    def fix_dvbt_ids(self, wtxn):
        screen=pychdb.dvbt_mux.screen(wtxn, sort_order=pychdb.dvbt_mux.dvbt_mux_order.freq,
                                      key_prefix_data =None,
                                      key_prefix_type=pychdb.dvbt_mux.dvbt_mux_prefix.none)
        prevmux = None
        for i in range(screen.list_size):
            mux=screen.record_at_row(i)
            if prevmux is not None:
                tol = 7000
                if abs(prevmux.frequency - mux.frequency) < tol:
                    if mux.k.mux_id != prevmux.k.mux_id:
                        pychdb.delete_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        oldkey = self.inverse_mux_map[key]
                        mux.k.mux_id = prevmux.k.mux_id
                        pychdb.put_record(wtxn, mux)
                        key = (mux.k.sat_pos, mux.k.stream_id, mux.k.t2mi_pid, mux.k.mux_id)
                        self.inverse_mux_map[key] = oldkey
                        self.mux_map[oldkey] = mux
            prevmux = mux

    def convert_muxes(self, wtxn):
        clean_dvbs_muxes(wtxn)
        failed=0
        count=0
        #first make new muxes based on old database
        for mux in self.dvbs_muxes:
            try:
                self.copy_dvbs_mux(wtxn, mux['data'])
                count+=1
            except:
                failed+=1
        self.fix_dvbs_ids(wtxn)

        clean_dvbc_muxes(wtxn)
        for mux in self.dvbc_muxes:
            self.copy_dvbc_mux(wtxn, mux['data'])
            count +=1
        self.fix_dvbc_ids(wtxn)

        clean_dvbt_muxes(wtxn)
        for mux in self.dvbt_muxes:
            self.copy_dvbt_mux(wtxn, mux['data'])
            count +=1
        self.fix_dvbt_ids(wtxn)
        #ensure that muxes in same frequencies have identical mux_id
        self.print(f'muxes converted={count} failed={failed}')

    def copy_service(self, wtxn, oldservice):
        k = tuple(v for k,v in oldservice['k']['mux'].items())
        if k[0] == pychdb.sat.sat_pos_none:
            return
        newservice = pychdb.service.service()
        mux = self.mux_map.get(k, None)
        if mux is None:
            return False
        newservice.k.mux = mux.k
        newservice.k.service_id = oldservice['k']['service_id']
        newservice.k.network_id = oldservice['k']['mux']['network_id']
        newservice.k.ts_id = oldservice['k']['mux']['ts_id']
        newservice.frequency = mux.frequency
        newservice.pol = getattr(mux, 'pol', pychdb.fe_polarisation_t.NONE)
        for key in [ 'ch_order', 'encrypted', 'expired',
                     'media_mode', 'mtime', 'name', 'pmt_pid',
                     'provider', 'service_type',  'video_pid']:
            setattr(newservice, key, type(getattr(newservice, key))(oldservice[key]))

        for key in ['subtitle_pref', 'audio_pref']:
            v=getattr(newservice, key)
            for t in oldservice[key]:
                    v.push_back(pychdb.language_code.language_code(**t))
        pychdb.put_record(wtxn, newservice)
        return True

    def convert_services(self, wtxn):
        clean_services(wtxn)
        #first make new muxes based on old database
        failed = 0
        count = 0

        for service in self.services:
            try:
                if self.copy_service(wtxn, service['data']):
                    count += 1
                else:
                    failed += 1
            except:
                failed +=1
        self.print(f'services converted={count} failed={failed}')


    def copy_chgm(self, wtxn, oldchgm):
        chgm_key = pychdb.chgm_key.chgm_key()
        chgm_key.channel_id = oldchgm['k']['channel_id']
        for key in [ 'group_type', 'bouquet_id', 'sat_pos']:
            setattr(chgm_key.chg, key, type(getattr(chgm_key.chg, key))(oldchgm['k']['chg'][key]))
        chgm = pychdb.chgm.find_by_key(wtxn, chgm_key)
        if chgm is None:
            dterror('not found')
        else:
            k = tuple(v for k,v in oldchgm['service']['mux'].items())
            chgm.service.mux = self.mux_map[k].k
            chgm.service.network_id = oldchgm['service']['mux']['network_id']
            chgm.service.ts_id = oldchgm['service']['mux']['ts_id']
            pychdb.put_record(wtxn, chgm)

    def convert_chgms(self, wtxn):
        #first make new muxes based on old database
        failed = 0
        count = 0

        for chgm in self.chgms:
            try:
                self.copy_chgm(wtxn, chgm['data'])
                count += 1
            except:
                failed +=1
        self.print(f'chgms converted={count} failed={failed}')

    def copy_lnb(self, wtxn, oldlnb):
        lnb_key = pydevdb.lnb_key.lnb_key()
        for key in ['dish_id', 'lnb_id',  'lnb_type']:
            setattr(lnb_key, key, type(getattr(lnb_key, key))(oldlnb['k'][key]))
        lnb=pydevdb.lnb.find_by_key(wtxn, lnb_key)

        newnetworks=[]
        for idx, network in  enumerate(oldlnb['networks']):
            r= network['ref_mux']
            t2mi_pid = r['t2mi_pid']
            if t2mi_pid == 0:
                t2mi_pid = -1
            key = (r['sat_pos'], r['network_id'], r['ts_id'], t2mi_pid, r['extra_id'])
            ref_mux = self.mux_map.get(key, None)
            n = pydevdb.lnb_network.lnb_network()
            for key in ['enabled', 'priority', 'sat_pos', 'usals_pos']:
                setattr(n, key, type(getattr(n, key))(network[key]))
            if ref_mux is not None:
                n.ref_mux = ref_mux.k
            newnetworks.append(n)
        lnb.networks.resize(0)
        for n in newnetworks:
            lnb.networks.push_back(n)
        pydevdb.put_record(wtxn, lnb)

    def convert_lnbs(self, wtxn):
        failed = 0
        count = 0

        for lnb in self.lnbs:
            try:
                self.copy_lnb(wtxn, lnb['data'])
                count += 1
            except:
                failed +=1
        self.print(f'lnbs converted={count} failed={failed}')

    def fix_recs(self, recdb_wtxn, chdb_rtxn):
        screen=pyrecdb.rec.screen(recdb_wtxn, sort_order=pyrecdb.rec.rec_order.key,
                                  key_prefix_data = None,
                                  key_prefix_type=pyrecdb.rec.rec_prefix.none)

        prevmux = None
        for i in range(screen.list_size):
            rec = screen.record_at_row(i)
            e = rec.epg.k.service
            service = pychdb.service.find_by_network_id_ts_id_service_id_sat_pos(chdb_rtxn, e.network_id, e.ts_id, e.service_id, e.sat_pos)
            if service is None:
                continue
            rec.service = service
            pyrecdb.put_record(recdb_wtxn, rec)


# not converted (not much needed)
#convert_browse_history(wtxn):
#convert_recs(wtxn): #recdb: service and epg fields
#convert_rec_browse_history(wtxn): #recdb: recordings
#convert_live_services(wtxn): #recdb: service field
