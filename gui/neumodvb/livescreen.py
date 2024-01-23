#!/usr/bin/python3
# Neumo dvb (C) 2019-2024 deeptho@gmail.com
# Copyright notice:
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

import wx
import wx.grid
import sys
import os
import copy
from collections import namedtuple, OrderedDict
import numbers
import datetime
from dateutil import tz
import regex as re

from neumodvb import satlist
from neumodvb.util import setup, lastdot
from neumodvb.util import dtdebug, dterror
from neumodvb import neumodbutils
from neumodvb.neumo_dialogs_gui import ChannelNoDialog_
from neumodvb.neumodbutils import enum_to_str

import pychdb
import pyrecdb
import pyepgdb



class LiveServiceScreen(object):
    def __init__(self, app):
        self.app = app
        self.chdb =  self.app.chdb
        self.use_channels = False #channels or services
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        service = None
        chgm = None
        sat = None
        chg = None
        self.tuned_entry_ = None
        self.num_services_on_screen = 10
        #self.top_idx  = None
        self.sat_screen_ = None
        self.chg_screen_ = None
        self.selected_service_or_channel_ = None
        self.selected_sat_ = None
        self.selected_chg_ = None
        self.make_screen()
        self.tuned_entry_ = self.selected_service_or_channel

    @property
    def selected_service_or_channel(self):
        return self.selected_service_or_channel_

    @selected_service_or_channel.setter
    def selected_service_or_channel(self, val):
        if val !=self.selected_service_or_channel_:
            dtdebug(f'live selected service/chgm from {self.selected_service_or_channel_} to {val}')
        self.selected_service_or_channel_ =val

    def make_screen(self, sort_order=None):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if ft == t.SAT_SERVICES:
            service = h.last_service()
            self.set_service_screen(service, sort_order=sort_order)
        elif ft == t.BOUQUET_CHANNELS:
            chgm = h.last_chgm()
            self.set_chgm_screen(chgm, sort_order=sort_order)
        else: #ft == t.ALL_SERVICES or ft == t.ALL_CHANNELS
            service = h.last_service()
            self.set_service_screen(service, sort_order=sort_order)

    def set_sort_columns(self, cols):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if ft in (t.SAT_SERVICES, t.ALL_SERVICES):
            return self.set_sort_columns_service(cols)
        else:
            return self.set_sort_columns_chgm(cols)

    def set_sort_columns_service(self, cols):
        h = self.app.receiver.browse_history
        shift = 24
        sort_order = 0
        if type(cols) == str:
            cols = (cols,)
        for col in cols:
            assert shift >= 0
            sort_order |= pychdb.service.subfield_from_name(col) << shift
            shift -= 8
        self.make_screen(sort_order=sort_order)

    def set_sort_columns_chgm(self, cols):
        h = self.app.receiver.browse_history
        shift = 24
        sort_order = 0
        if type(cols) == str:
            cols = (cols,)
        for col in cols:
            assert shift >= 0
            sort_order |= pychdb.chgm.subfield_from_name(col) << shift
            shift -= 8
        self.make_screen(sort_order=sort_order)

    def get_sort_columns(self):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if ft in (t.SAT_SERVICES, t.ALL_SERVICES):
            return self.get_sort_columns_service()
        else:
            return self.get_sort_columns_chgm()
    def get_sort_columns_service(self):
        shift = 24
        h = self.app.receiver.browse_history
        sort_order = 0
        cols = []
        while shift >=0:
            val = (h.h.service_sort_order & (0xff << shift)) >> shift
            if val ==0:
                break
            val = enum_to_str(pychdb.service.column(val))
            cols.append(val)
            shift -= 8
        return cols

    def get_sort_columns_chgm(self):
        h = self.app.receiver.browse_history
        shift = 24
        sort_order = 0
        cols = []
        while shift >=0:
            val = (h.h.chgm_sort_order & (0xff << shift)) >> shift
            if val ==0:
                break
            val = enum_to_str(pychdb.chgm.column(val))
            cols.append(val)
            shift -= 8
        return cols

    @property
    def sat_screen(self):
        if self.sat_screen_ is None:
            txn = self.chdb.rtxn()
            self.sat_screen_ = pychdb.sat.screen(txn, sort_order=int(pychdb.sat.column.sat_pos) << 24)
            txn.abort()
            del txn
        return self.sat_screen_

    @property
    def chg_screen(self):
        if self.chg_screen_ is None:
            txn = self.chdb.rtxn()
            self.chg_screen_ = pychdb.chg.screen(txn, sort_order=int(pychdb.chg.column.name)<<24)
            txn.abort()
            del txn
        return self.chg_screen_

    def reportOFF(self, label):
        h = self.app.receiver.browse_history
        dtdebug(f'{label}: list_type{h.h.list_filter_type} filter_chg={h.h.chglist_filter_chg}' )
    def set_sat_filter(self, sat):
        """
        """
        txn = self.chdb.rtxn()
        service = self.selected_service_or_channel if \
            type(self.selected_service_or_channel) == pychdb.service.service else None
        if service is not None and sat is not None  and service.k.mux.sat_pos != sat.sat_pos:
            service = None

        ret = self.set_service_screen_(txn, service, sat)
        txn.abort()
        del txn
        return ret

    def entry_for_ch_order(self, chno):
        if chno >= 65535:
            return None #must fit  in uint16_t
        h = self.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        txn = self.chdb.rtxn()
        if h.h.list_filter_type ==  t.BOUQUET_CHANNELS:
             ret = pychdb.chgm.find_by_chgm_order(txn, chno)
        else:
            ret = pychdb.service.find_by_ch_order(txn, chno)
        txn.abort()
        del txn
        return ret
    def set_service_screen(self, service, sort_order=None):
        """
        """
        h = self.app.receiver.browse_history
        txn = self.chdb.rtxn()
        sat = None if  h.h.servicelist_filter_sat.sat_pos == pychdb.sat.sat_pos_none else  h.h.servicelist_filter_sat
        if service is not None and sat is not None  and service.k.mux.sat_pos != sat.sat_pos:
            #a sat filter is in place, but it is not compatible with service
            #we update the filter
            service = None
        ret = self.set_service_screen_(txn, service, sat, sort_order)
        txn.abort()
        del txn
        return ret

    def set_service_screen_(self, txn , service, sat, sort_order=None):
        """
        """
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        sort_order = h.h.service_sort_order if sort_order is None else sort_order
        new_type =  t.SAT_SERVICES if sat else t.ALL_SERVICES
        if sat is None:
            self.screen = pychdb.service.screen(txn, sort_order=sort_order)
        else:
            assert h.h.service_sort_order != 0
            ref = pychdb.service.service()
            ref.k.mux.sat_pos = sat.sat_pos
            self.screen = pychdb.service.screen(txn, sort_order=sort_order,
                                                key_prefix_type=pychdb.service.service_prefix.sat_pos,
                                                key_prefix_data=ref)
        sat_pos = pychdb.sat.sat_pos_none if sat is None else sat.sat_pos
        if service is None and  type(self.selected_service_or_channel) == pychdb.service.service:
            service = self.selected_service_or_channel
            if sat is not None and service.k.mux.sat_pos != sat_pos:
                service = None
        if service is None:
            service_idx=0
            if self.screen.list_size>0:
                service = self.screen.record_at_row(0)
        else:
            service_idx = self.screen.set_reference(service)
        self.selected_service_or_channel = service

        #print(f'new_type={new_type} {h.h.list_filter_type} {sat_pos} {h.h.servicelist_filter_sat.sat_pos} {sort_order} { h.h.service_sort_order}')
        if new_type != h.h.list_filter_type or sat_pos != h.h.servicelist_filter_sat.sat_pos \
           or sort_order != h.h.service_sort_order:
            h.h.servicelist_filter_sat = pychdb.sat.sat() if sat is None else sat
            h.h.list_filter_type = new_type
            h.h.service_sort_order = int(sort_order)
            h.save()
        return service_idx
    def set_chgm_or_service_screen(self, entry, sort_order=None):
        if type(entry) == pychdb.service.service:
            dtdebug(f'SERVICE screen {entry}')
            return self.set_service_screen(entry, sort_order)
        elif type(entry) == pychdb.chgm.chgm:
            dtdebug(f'CHGM screen {entry}')
            return self.set_chgm_screen(entry, sort_order)
        dtdebug('NO screen')
    def set_chgm_screen(self, chgm, sort_order=None):
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        txn = self.chdb.rtxn()
        chg = h.h.chglist_filter_chg if h.h.chglist_filter_chg.k.bouquet_id>0 else None
        if chgm is not None and chg is not None and chgm.k != chg.k:
            #a chg filter is in place, but it is not compatible with chgm
            #we update the filter
            chg = pychdb.chg.find_by_key(txn, chgm.k.chg)

        ret = self.set_chgm_screen_(txn, chgm, chg, sort_order)
        txn.abort()
        del txn
        return ret

    def set_chg_filter(self, chg):
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        txn = self.chdb.rtxn()
        chgm = self.selected_service_or_channel if type(self.selected_service_or_channel) == pychdb.chgm.chgm else None
        if chgm is not None and chg is not None and chgm.k != chg.k:
            chgm = None

        ret = self.set_chgm_screen_(txn, chgm, chg)
        txn.abort()
        del txn
        return ret

    def set_chgm_screen_(self, txn, chgm, chg, sort_order=None):
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        new_type =  t.BOUQUET_CHANNELS if chg else t.ALL_CHANNELS
        sort_order = h.h.chgm_sort_order if sort_order is None else sort_order
        assert h.h.chgm_sort_order != 0
        if chg is None:
            self.screen = pychdb.chgm.screen(txn, sort_order=sort_order)
        else:
            ref = pychdb.chgm.chgm()
            ref.k.chg = chg.k
            self.screen = pychdb.chgm.screen(txn, sort_order=sort_order,
                                             key_prefix_type=pychdb.chgm.chgm_prefix.chg,
                                             key_prefix_data=ref)
        #if chgm is None:
        #    chgm = self.selected_chgm
        if chgm is None and  type(self.selected_service_or_channel) == pychdb.chgm.chgm:
            chgm = self.selected_service_or_channel
            if chg is not None and (chg.k.bouquet_id != chgm.k.chg.bouquet_id or \
                                    chg.k.group_type != chgm.k.chg.group_type):
                chgm = None
        if chgm is None:
            chgm_idx = 0
            if self.screen.list_size>0:
                chgm = self.screen.record_at_row(0)
        else:
            chgm_idx = self.screen.set_reference(chgm)
        self.selected_service_or_channel = chgm

        if new_type != h.h.list_filter_type or h.h.chglist_filter_chg != chg or h.h.chgm_sort_order != sort_order:
            h.h.chgmlist_filter_chg = pychdb.chg.chg() if chg is None else chg
            h.h.list_filter_type =  new_type
            h.h.chgm_sort_order = int(sort_order)
            h.save()
        return chgm_idx

    def SelectServiceOrChannel(self, service_or_chgm):
        """
        Remembers the selection of a specific entry (but only for this session)
        """
        self.selected_service_or_channel = service_or_chgm

    def SelectChg(self, chg):
        """
        Remembers the selection of a specific entry (but only for this session)
        """
        self.selected_chg_ = chg

    def SelectSat(self, sat):
        """
        Remembers the selection of a specific entry (but only for this session)
        """
        self.selected_sat_ = sat

    def Tune(self, service_or_chgm):
        """
        Returns the service to tune to and saves browsing/tuning history
        """
        h = self.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        self.selected_service_or_channel = service_or_chgm
        if type(service_or_chgm) == pychdb.chgm.chgm:
            assert h.h.chglist_filter_chg is not None #needs to be set earlier
            h.h.list_filter_type = t.BOUQUET_CHANNELS
            chgm =service_or_chgm
            txn = self.chdb.rtxn()
            service = pychdb.service.find_by_key(txn, chgm.service.mux, chgm.service.service_id)
            txn.abort()
            del txn
            h.save(chgm)
            h.save(service)
            return service
        elif type(service_or_chgm) == pychdb.service.service:
            if h.h.list_filter_type  not in (t.ALL_SERVICES, t.SAT_SERVICES):
                h.h.list_filter_type = t.ALL_SERVICES if h.h.servicelist_filter_sat is None else t.SAT_SERVICES
            h.save(service_or_chgm)

            return service_or_chgm
        else:
            assert 0
    @property
    def tuned_entry(self):
        return self.tuned_entry_ # service or channel

    @property
    def selected_service(self):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if ft == t.BOUQUET_CHANNELS:
            chgm = h.last_chgm()
            txn = self.chdb.rtxn()
            service = h.last_service() if chgm is None else \
                pychdb.service.find_by_key(txn, chgm.service.mux, chgm.service.service_id)
            txn.abort()
            del txn
            if False:
                self.set_service_screen(service)
                return self.selected_service_or_channel
            else:
                return service
        else:
            return h.last_service()

    @property
    def selected_chgm(self):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if ft == t.BOUQUET_CHANNELS:
            return self.selected_service_or_channel
        else:
            ret = h.last_chgm() #last tuned chgm
            if False:
                self.set_chgm_screen(ret)
                return self.selected_service_or_channel
            else:
                return ret
    @property
    def selected_chg(self):
        if False:
            ft = self.app.receiver.browse_history.h.list_filter_type
            t = pychdb.list_filter_type_t
            h = self.app.receiver.browse_history
            chg =  None if h.h.chglist_filter_chg.k.bouquet_id == 0 else h.h.chglist_filter_chg
            if chg is not None:
                return chg
            if self.chg_screen is not None and self.chg_screen.list_size>0:
                chg = self.chg_screen.record_at_row(0)
                return chg if type(chg) == pychdb.chg.chg else None
        return self.selected_chg_

    @property
    def selected_sat(self):
        if False:
            ft = self.app.receiver.browse_history.h.list_filter_type
            t = pychdb.list_filter_type_t
            h = self.app.receiver.browse_history
            sat = None if  h.h.servicelist_filter_sat.sat_pos == pychdb.sat.sat_pos_none else  h.h.servicelist_filter_sat
            if sat is not None:
                return sat
            if self.sat_screen is not None and self.sat_screen.list_size>0:
                sat = self.sat_screen.record_at_row(0)
                return sat if type(sat) == pychdb.sat.sat else None
            return None
        return self.selected_sat_

    @property
    def filter_sat(self):
        b = self.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        return b.h.servicelist_filter_sat if b.h.list_filter_type == t.SAT_SERVICES else None #restrict services to this sat

    @filter_sat.setter
    def filter_sat(self, val):
        self.set_sat_filter(val)

    @property
    def filter_chg(self):
        b = self.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        chg =  b.h.chglist_filter_chg if b.h.list_filter_type == t.BOUQUET_CHANNELS else None
        return chg if chg is None or chg.k.bouquet_id>0 else None #restrict services to this sat

    @filter_chg.setter
    def filter_chg(self, val):
        self.set_chg_filter(val)
        b = self.app.receiver.browse_history
        t = pychdb.list_filter_type_t
        b.h.chglist_filter_chg = val
        b.h.list_filter_type = t.BOUQUET_CHANNELS #restrict services to this sat
        b.save()

    @property
    def service_sort_column(self):
        return 'ch_order' #or service_name
    @property
    def channel_sort_column(self):
        return 'ch_order' #or service_name

    def service_for_entry(self, entry):
        ft = self.app.receiver.browse_history.h.list_filter_type
        t = pychdb.list_filter_type_t
        h = self.app.receiver.browse_history
        if entry is None:
            return None
        if type(entry) == pychdb.service.service:
            return entry
        txn = self.chdb.rtxn()
        service = pychdb.service.find_by_key(txn, entry.service.mux, entry.service.service_id)
        txn.abort()
        del txn
        return service

class LiveRecordingScreen(object):
    def __init__(self, app):
        self.app = app
        self.recdb =  self.app.recdb
        self.playing_entry_ = None
        self.selected_recording = None
        self.make_screen()
        self.playing_entry_ = self.selected_recording

        t = pyepgdb.rec_status_t
        ft = pyrecdb.list_filter_type_t

        self.filter_type_for_rec_status = {
            t.SCHEDULED: ft.SCHEDULED_RECORDINGS,
            t.IN_PROGRESS: ft.IN_PROGRESS_RECORDINGS,
            t.FINISHING: ft.IN_PROGRESS_RECORDINGS,
            t.FINISHED: ft.COMPLETED_RECORDINGS,
            t.FAILED: ft.FAILED_RECORDINGS
        }

        self.rec_status_for_filter_type = {
            ft.SCHEDULED_RECORDINGS:  t.SCHEDULED,
            ft.IN_PROGRESS_RECORDINGS: t.IN_PROGRESS,
            ft.COMPLETED_RECORDINGS:  t.FINISHED,
            ft.FAILED_RECORDINGS: t.FAILED
        }


    def rec_matches_filter_type(self, rec, filter_type):
        t = pyepgdb.rec_status_t
        ft = pyrecdb.list_filter_type_t
        if rec is None:
            return False
        return self.filter_type_for_rec_status.get(rec.epg.rec_status, None) == filter_type

    def make_screen(self, sort_order=None):
        recording = None
        self.set_recordings_screen(recording, sort_order=sort_order)

    def set_sort_columns(self, cols):
        h = self.app.receiver.rec_browse_history
        shift = 24
        sort_order = 0
        if type(cols) == str:
            cols = (cols,)
        for col in cols:
            assert shift >= 0
            sort_order |= pyrecdb.rec.subfield_from_name(col) << shift
            shift -= 8
        self.make_screen(sort_order=sort_order)

    def get_sort_columns(self):
        ft = self.app.receiver.rec_browse_history.h.list_filter_type
        t = pyrecdb.list_filter_type_t
        h = self.app.receiver.rec_browse_history
        shift = 24
        h = self.app.receiver.rec_browse_history
        sort_order = 0
        cols = []
        while shift >=0:
            val = (h.h.rec_sort_order & (0xff << shift)) >> shift
            if val ==0:
                break
            val = enum_to_str(pyrecdb.rec.column(val))
            cols.append(val)
            shift -= 8
        return cols

    def set_recordings_screen(self, recording, sort_order=None):
        """
        """
        h = self.app.receiver.rec_browse_history
        txn = self.recdb.rtxn()

        ret = self.set_recordings_screen_(txn, recording, sort_order)
        txn.abort()
        del txn
        return ret

    def set_recordings_screen_(self, txn, recording, sort_order=None, filter_type=None):
        """
        """
        t = pyrecdb.list_filter_type_t
        h = self.app.receiver.rec_browse_history
        sort_order = h.h.rec_sort_order if sort_order is None else sort_order
        filter_type = t.ALL_RECORDINGS if filter_type is None else filter_type
        new_type =  filter_type
        if filter_type == t.ALL_RECORDINGS:
            self.screen = pyrecdb.rec.screen(txn, sort_order=sort_order)
        else:
            rs = self.rec_status_for_filter_type[filter_type]
            assert rs is not None
            assert h.h.rec_sort_order != 0
            ref = pyrecdb.rec.rec()
            ref.epg.rec_status = rs
            self.screen = pyrecdb.rec.screen(txn, sort_order=sort_order,
                                             key_prefix_type=pyrecdb.rec.rec_prefix.rec_status,
                                             key_prefix_data=ref)
        if recording is None:
            recording = self.selected_recording
            if not self.rec_matches_filter_type(recording, filter_type):
                recording = None
        if recording is None:
            recording_idx=0
            if self.screen.list_size>0:
                recording = self.screen.record_at_row(0)
        else:
            recording_idx = self.screen.set_reference(recording)
        self.selected_recording = recording

        if new_type != h.h.list_filter_type  \
           or sort_order != h.h.rec_sort_order:
            h.h.list_filter_type = new_type
            h.h.rec_sort_order = int(sort_order)
            h.save()


    def set_recordings_filter(self, filter_type):
        t = pyrecdb.list_filter_type_t
        h = self.app.receiver.rec_browse_history
        txn = self.recdb.rtxn()
        ret = self.set_recordings_screen_(txn, None, filter_type=filter_type)
        txn.abort()
        del txn
        return ret

    def SelectRecording(self, recording):
        """
        Remembers the selection of a specific entry (but only for this session)
        """
        self.selected_recording = recording

    def Play(self, recording):
        """
        Returns the recording to play to and saves browsing/tuning history
        """
        h = self.app.receiver.rec_browse_history
        t = pyrecdb.list_filter_type_t
        self.selected_recording = recording
        assert type(recording) == pyrecdb.rec.recording
        h.save(recording)

    @property
    def playing_entry(self):
        return self.playing_entry_ # recording or channel

    @property
    def recording_sort_column(self):
        return 'real_time_start' #or recording_name

    def recording_for_entry(self, entry):
        return entry
