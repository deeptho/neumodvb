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
import os
import sys
import gettext
import signal
from functools import lru_cache
#the following import also sets up import path
import neumodvb

from neumodvb.util import load_gtk3_stylesheet, dtdebug, dterror, maindir, get_object, get_last_scan_text_dict
from neumodvb.config import options, get_configfile
from neumodvb.neumo_dialogs import ShowMessage

import pydevdb
import pychdb
import pyepgdb
import pyrecdb
import pystatdb
import pyneumompv
import pyreceiver
pyneumompv.init_threads()

import wx.glcanvas #needed for the created mpvglcanvas
import wx

from neumodvb.viewer_gui import mainFrame
import time

from neumodvb.livescreen import LiveServiceScreen, LiveRecordingScreen

#suppress message: Debug: Adding duplicate image handler for 'Windows bitmap file'
wx.Log.EnableLogging(False)

# to surpress a warning
wx._core.WindowIDRef.__index__ = wx._core.WindowIDRef.__int__

def debug_signal_handler(signal, frame):
    import pdb
    pdb.set_trace()
    signal.signal(signal.SIGINT, lambda sig, frame: pdb.Pdb().set_trace(frame))


class neumoMainFrame(mainFrame):
    def __init__(self, parent, *args, **kwds):
        super().__init__(*args, **kwds)
        from neumodvb.neumomenu import NeumoMenuBar
        self.main_menubar = NeumoMenuBar(self)
        self.main_menubar.edit_mode_checkbox = self.main_menubar.get_menu_item('EditMode')
        self.main_menubar.on_new = self.main_menubar.get_menu_item('New')
        self.edit_menu = self.main_menubar.get_menu('Edit')
        self.parent = parent
        self.chepggrid.infow = self.chepginfo_text
        self.recgrid.infow = self.recinfo_text
        self.dvbs_muxgrid.infow = self.dvbs_muxinfo_text
        self.dvbc_muxgrid.infow = self.dvbc_muxinfo_text
        self.dvbt_muxgrid.infow = self.dvbt_muxinfo_text
        self.servicegrid.infow = self.serviceinfo_text
        self.chgmgrid.infow = self.chgminfo_text
        self.bouquet_being_edited = None
        self.command_being_edited = None
        self.panel_names = [ 'servicelist', 'chgmlist',
                             'chepg', 'live',
                             'dvbs_muxlist', 'dvbc_muxlist', 'dvbt_muxlist',
                             'lnblist', 'chglist', 'satlist',
                             'frontendlist', 'streamlist', 'statuslist',
                             'mosaic', 'reclist', 'autoreclist', 'spectrumlist',
                             'dishlist', 'scancommandlist']

        self.panels = [ getattr(self, f'{n}_panel') for n in self.panel_names]
        self.grids = [*filter(lambda xx: xx is not None,
                            [ getattr(self, f'{n.removesuffix("list")}grid',None) for n in self.panel_names])]

        for grid in self.grids:
            panel = grid
            while panel is not None:
                parent = panel.Parent
                if type(parent) == wx.Panel:
                    parent.main_grid = grid
                    panel = None
        self.service_sat_sel.window_for_computing_width=self.servicegrid
        self.dvbs_muxlist_sat_sel.window_for_computing_width = self.dvbs_muxgrid

        self.chgm_chg_sel.window_for_computing_width = self.chgmgrid
        self.chepg_service_sel.window_for_computing_width =self.chepggrid

        self.panels_onscreen=[]
        self.previous_panels_onscreen=[]
        self.previous_info_windows_onscreen = []
        self.info_windows_onscreen = []
        self.info_windows = [self.recinfo_text]
        self.app = wx.GetApp()
        self.mosaic_sizer = wx.GridSizer(cols=1)
        parent_sizer = self.mosaic_panel.GetSizer()
        parent_sizer.Insert(0, self.mosaic_sizer,1, wx.EXPAND,0)
        mosaic = False
        if not mosaic:
            num = len(sys.argv) -1
            if num > 0:
                for argv in sys.argv[1:]:
                    self.AddMpvPlayer(play_file=argv)
                    #time.sleep(0.1)
            else:
                if False:
                    self.AddMpvPlayer()
        self.ShowPanel([self.live_panel])
        wx.CallAfter(self.live_panel.CmdLiveChannels, None)
        self.SetEditMode(False)
        if mosaic:
            self.createMosaic()
        self.Layout()
        #self.set_accelerators(True)
        self.Bind(wx.EVT_CLOSE, self.OnClose)
        self.timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.OnTimer) #used to refresh list on screen
        self.timer.Start(2000)
        self.Bind(wx.EVT_COMMAND_ENTER, self.OnSubscriberCallback)

    @property
    @lru_cache(maxsize=None)
    def accel_tbl(self):
        #Wx.TextCtrl does not seem to respect menu shortcuts
        #which causes Gridepg to not respond to accelerators such as CTRL-R for record
        #Note that some accelerator keys are regular characters. This may interfere
        #with text editing. Call set_accelerators(False) to disable accels in this case
        #and set_accelerators(True) to restore
        #
        #The following code overcomes those issues
        accel_tbl = self.main_menubar.make_accels()
        self.SetAcceleratorTable(accel_tbl)
        return accel_tbl

    def set_accelerators(self, on):
        if on:
            dtdebug('ACCEL ON')
            self.SetAcceleratorTable(self.accel_tbl)
        else:
            dtdebug('ACCEL OFF')
            accel_tbl = wx.AcceleratorTable()
            self.SetAcceleratorTable(accel_tbl)

    def OnSubscriberCallback(self, evt):
        data = get_object(evt)
        if type(data) == pydevdb.scan_stats.scan_stats:
            st = data
        elif type(data) == str:
            ShowMessage("Error", data)
            return
        else:
            st = None
        if st is not None:
            self.app.scan_in_progress = not st.finished
            pending = st.pending_muxes + st.pending_peaks
            ok = st.locked_muxes
            if not self.app.scan_in_progress:
                self.app.last_scan_text_dict={}
                msgs=[]
                msgs.append(f"Scanned {st.locked_muxes+st.failed_muxes} muxes (ok={st.locked_muxes} "
                            f"failed={st.failed_muxes})")
                ShowMessage("Mux scan finished", "\n".join(msgs))
            else:
                self.app.last_scan_text_dict = get_last_scan_text_dict(st)

            panel =self.current_panel()
            if panel is None:
                return
            if hasattr(panel, "main_grid"):
                grid = panel.main_grid
                if hasattr(grid, "infow") and grid.infow is not None:
                    grid.infow.ShowScanRecord(panel)

    def current_panel(self):
        for panel in self.panels_onscreen:
            if panel != self.mosaic_panel:
                return panel
        return None

    def get_panel_method(self, method_name):
        panel = self.current_panel()
        if panel is not None:
            if hasattr(panel, method_name):
                return getattr(panel, method_name)
            elif hasattr(panel, 'grid'):
                if hasattr(panel.grid,method_name):
                    return getattr(panel.grid, method_name)
        else:
            return None

    def ShowPanel(self, panelstoshow, info_windows=[]):
        """
        show_info: show info window below the mosaic_sizer
        """
        if type(panelstoshow) is not list:
            panelstoshow = [panelstoshow]
        self.previous_panels_onscreen = self.panels_onscreen
        self.panels_onscreen = panelstoshow
        self.previous_info_windows_onscreen = self.info_windows_onscreen
        self.info_windows_onscreen = info_windows
        if type(panelstoshow) is not list:
            panelstoshow = [panelstoshow]
        if type(info_windows) is not list:
            info_windows = [info_windows]
        for window in self.info_windows:
            if window not in info_windows:
                window.Hide()
            else:
                window.Show()
        for panel in self.panels:
            if panel not in panelstoshow:
                panel.Hide()
            else:
                panel.Show()
                if hasattr(panel, 'main_grid'):
                    panel.main_grid.SetFocus()
                else:
                    wx.CallAfter(panel.SetFocus)
        if self.live_panel in panelstoshow:
            self.set_accelerators(True)
        else:
            self.set_accelerators(False)
        self.Layout()

    def ToggleEditMode(self):
        self.SetEditMode(not self.edit_mode)

    def SetEditMode(self, edit_mode):
        self.edit_mode = edit_mode
        menu_item= self.main_menubar.items['EditMode'][1]
        if menu_item.IsChecked() != edit_mode:
            menu_item.Check(edit_mode)
        for grid in self.grids:
            grid.EnableEditing(self.edit_mode)
        self.main_menubar.edit_mode(edit_mode)
        #menu = event.GetEventObject()
        if False:
            menu = self.edit_menu
            self.EnableMenu(self.edit_menu, edit_mode, skip_items=[self.main_menubar.edit_mode_checkbox, self.main_menubar.on_new])

    def Stop(self):
        self.app.ScanStop()
        self.app.current_mpv_player.stop_play()

    def colPopupOFF(self, col, evt):
        """(col, evt) -> display a popup menu when a column label is
        right clicked"""
        x = self.GetColSize(col)/2
        menu = wx.Menu()
        id1 = wx.NewIdRef()
        sortID = wx.NewIdRef()

        xo, yo = evt.GetPosition()
        self.SelectCol(col)
        cols = self.GetSelectedCols()
        self.Refresh()
        menu.Append(id1, "Filter Column")
        menu.Append(sortID, "Sort Column")

        def filter(event, self=self, col=col):
            dtdebug("filter called")
            cols = self.GetSelectedCols()
            dlg = SatFilterDialog(self, -1, "Sat Filter")
            val = dlg.ShowModal()

            if val == wx.ID_OK:
                self.log.WriteText("You pressed OK\n")
            else:
                self.log.WriteText("You pressed Cancel\n")

            dlg.Destroy()

        def sort(event, self=self, col=col):
            self._table.set_sort_column(col)
            self.Reset()

        self.Bind(wx.EVT_MENU, filter, id=id1)

        if len(cols) == 1:
            self.Bind(wx.EVT_MENU, sort, id=sortID)

        self.PopupMenu(menu)
        menu.Destroy()
        return

    def OnTimer(self, evt):
        panel =self.current_panel()
        if panel is None:
            return
        if hasattr(panel, "main_grid"):
            panel.main_grid.OnTimer(evt)
        elif  hasattr(panel, "OnTimer"):
            panel.OnTimer(evt)

    def OnClose(self, event):
        dtdebug(f'closing veto={event.CanVeto()}')
        self.timer.Stop()
        wx.CallAfter(self.OnExit)
        dtdebug('Calling Destroy')
        dtdebug('closing done')
        event.Skip(True)

    def OnGroupShowAll(self, event):
        dtdebug('closing')
        panel = event.GetEventObject().GetParent()
        panel.GetChildren()[0].show_all()
        event.Skip()

    def OnPlay(self, event):
        event.Skip()

    def OnLiveService(self, evt):
        self.ShowPanel([self.live_panel])
        self.live_panel.ShowVideo(evt)
        event.Skip()

    def OnSubtitles(self, event):
        return wx.GetApp().Subtitles()

    def OnFix(self, event):
        self.spectrum_plot.OnFix(event)

    def OnRefresh(self, event):
        panel = self.current_panel()
        if panel is not None:
            if hasattr(panel, "grid"):
                return panel.grid.OnRefresh(event)
            else:
                return panel.OnRefresh(event)

    def OnExit(self, event=None):
        dtdebug(f"Asking receiver to exit receiver={self.app.receiver}")
        self.app.receiver.stop()
        dtdebug("OnExit done")
        return 0

    def CmdInspect(self, event):
        dtdebug("CmdInspect")
        self.app.CmdInspect()

    def CmdEditOptions(self, event):
        dtdebug("CmdEditOptions")
        from neumodvb.preferences_dialog import show_preferences_dialog
        show_preferences_dialog(self)
        pass

    def CmdLiveChannels(self, event):
        dtdebug("CmdLiveChannels")
        self.ShowPanel([self.live_panel])
        self.live_panel.CmdLiveChannels(event)
        #event.Skip()

    def CmdLiveScreen(self, event):
        dtdebug("CmdLiveChannels")
        self.ShowPanel([self.live_panel])
        self.live_panel.CmdLiveScreen(event)
        #event.Skip()

    def CmdLiveRecordings(self, event):
        dtdebug("CmdLiveRecordings")
        self.ShowPanel([self.live_panel])
        self.live_panel.CmdLiveRecordings(event)

    def FullScreen(self):
        dtdebug("FullScreen")
        if not self.IsFullScreen():
            if self.current_panel() == self.live_panel:
                self.ShowPanel([self.live_panel])
            else:
                self.ShowPanel([self.mosaic_panel])
            self.ShowFullScreen(True)
            self.Layout()
        else:
            self.ShowPanel(self.previous_panels_onscreen, self.previous_info_windows_onscreen)
            self.ShowFullScreen(False)
            self.Layout()
        #event.Skip()

    def CmdChEpg(self, event):
        dtdebug("CmdChEpg")
        self.ShowPanel([self.chepg_panel, self.mosaic_panel])

    def CmdLiveEpg(self, evt):
        dtdebug("CmdLiveEpg")
        self.ShowPanel([self.live_panel])
        self.live_panel.CmdLiveEpg(evt)

    def CmdStatusList(self, event):
        dtdebug("CmdStatusList")
        self.ShowPanel(self.statuslist_panel)

    def CmdServiceList(self, event):
        dtdebug("CmdServiceList")
        self.ShowPanel(self.servicelist_panel)

    def CmdChgmList(self, event):
        dtdebug("CmdChgmList")
        self.ShowPanel(self.chgmlist_panel)

    def CmdSpectrumList(self, event):
        dtdebug("CmdSpectrumList")
        self.ShowPanel(self.spectrumlist_panel)

    def CmdDvbsMuxList(self, event):
        dtdebug("CmdDvbsMuxList")
        self.ShowPanel(self.dvbs_muxlist_panel)

    def CmdDvbcMuxList(self, event):
        dtdebug("CmdDvbcMuxList")
        self.ShowPanel(self.dvbc_muxlist_panel)

    def CmdDvbtMuxList(self, event):
        dtdebug("CmdDvbtMuxList")
        self.ShowPanel(self.dvbt_muxlist_panel)

    def CmdLnbList(self, event):
        dtdebug("CmdLnbList")
        self.ShowPanel(self.lnblist_panel)

    def CmdDishList(self, event):
        dtdebug("CmdDishList")
        self.ShowPanel(self.dishlist_panel)

    def CmdSatList(self, event):
        dtdebug("CmdSatList")
        self.ShowPanel(self.satlist_panel)

    def CmdChgList(self, event):
        dtdebug("CmdChgList")
        self.ShowPanel(self.chglist_panel)

    def CmdFrontendList(self, event):
        dtdebug("CmdFrontendList")
        self.ShowPanel(self.frontendlist_panel)

    def CmdStreamList(self, event):
        dtdebug("CmdStreamList")
        self.ShowPanel(self.streamlist_panel)

    def CmdRecList(self, event):
        dtdebug("CmdRecList")
        self.ShowPanel(self.reclist_panel)

    def CmdAutoRecList(self, event):
        dtdebug("CmdAutoRecList")
        self.ShowPanel(self.autoreclist_panel)

    def CmdScanCommandList(self, event):
        dtdebug("CmdScanCommandList")
        self.ShowPanel(self.scancommandlist_panel)

    def CmdStop(self, event):
        dtdebug('CmdStop')
        return wx.GetApp().Stop()

    def CmdExit(self, event):
        dtdebug("CmdExit")
        if self.current_panel() != self.live_panel:
            dtdebug("OnClose")
            self.current_panel().grid.OnClose()
        self.live_panel.OnClose(event)
        self.Close()
        event.Skip(False) #needed to prevent being executed multiple times
    def CmdEditMode(self, is_checked):
        dtdebug("CmdEditMode")
        self.SetEditMode(is_checked)
        return True

    def CmdUndo(self, event):
        dtdebug("CmdUndo")
        panel = self.current_panel()
        if panel is not None and hasattr(panel, 'grid'):
            return panel.grid.OnUndo(event)

class NeumoBitmaps(object):
    def __init__(self):
        self.expired_bitmap = wx.Bitmap()
        self.encrypted_bitmap = wx.Bitmap()
        self.rec_scheduled_bitmap = wx.Bitmap()
        self.rec_inprogress_bitmap = wx.Bitmap()
        self.expired_bitmap = wx.Bitmap(get_configfile('images/trash-empty.svg'))
        self.encrypted_bitmap = wx.Bitmap(get_configfile('images/kt-encrypted.svg'))
        self.rec_scheduled_bitmap = wx.Bitmap(get_configfile('images/player_record_scheduled.svg'))
        self.rec_inprogress_bitmap = wx.Bitmap(get_configfile('images/player_record_in_progress.svg'))


class NeumoGui(wx.App):
    def save_option_to_db(self, par, val):
        opts =  self.receiver.get_options()
        if getattr(opts, par) != val:
            devdb_wtxn = self.receiver.devdb.wtxn()
            setattr(opts, par, val)
            opts.save_to_db(devdb_wtxn)
            devdb_wtxn.commit()
            self.receiver.set_options(opts)

    def get_sats(self):
        for retry in False, True:
            txn = self.chdb.rtxn()
            self.sats = pychdb.sat.list_all_by_key(txn)
            del txn
            if len(self.sats) <= 2 and not retry:
                from neumodvb.init_db import init_db
                init_db()
            else:
                return self.sats

    def get_sat_poses(self):
        sats= self.get_sats()
        ret =[]
        found = set()
        for sat in sats:
            if not sat.sat_pos in found:
                found.add(sat.sat_pos)
                ret.append(sat.sat_pos)
        return ret

    def get_dishes(self):
        txn = self.devdb.rtxn()
        self.dishes = pydevdb.dish.list_dishes(txn)
        del txn
        return self.dishes

    def get_cards(self, available_only=False):
        txn = wx.GetApp().devdb.rtxn()
        ret={}
        for a in  pydevdb.fe.list_all_by_card_mac_address(txn):
            if available_only and not a.can_be_used:
                continue
            ret[f'C{a.card_no}: {a.card_short_name}' ] = a.card_mac_address
        txn.abort()
        return ret

    def get_cards_with_rf_in(self):
        txn = wx.GetApp().devdb.rtxn()
        ret={}
        for a in  pydevdb.fe.list_all_by_card_mac_address(txn):
            for rf_in in a.rf_inputs:
                ret[f'C{a.card_no}#{rf_in} {a.card_short_name}' ] = (a.card_mac_address, rf_in)
        txn.abort()
        return ret

    def get_adapters(self):
        txn = wx.GetApp().devdb.rtxn()
        ret={}
        for a in  pydevdb.fe.list_all_by_adapter_no(txn):
            ret[f'{a.adapter_no}: {a.adapter_name}' ] = a.k.adapter_mac_address
        txn.abort()
        return ret
    @property
    def neumo_drivers_installed(self):
        return self.receiver.get_api_type()[0] == "neumo"

    @property
    def devdb(self):
        return self.receiver.devdb

    @property
    def chdb(self):
        return self.receiver.chdb

    @property
    def epgdb(self):
        return self.receiver.epgdb

    @property
    def recdb(self):
        return self.receiver.recdb

    @property
    def statdb(self):
        return self.receiver.statdb

    def get_menu_item(self, name):
        return self.frame.main_menubar.get_menu_item(name)

    def __init__(self, *args, **kwds):
        self.receiver = pyreceiver.receiver_t(options.receiver)
        if self.receiver.db_upgrade_info is not None:
            i = self.receiver.db_upgrade_info
            from neumodvb.upgrade.upgrade import major_upgrade
            dtdebug(f'Need db upgrade from {i.stored_db_version} to {i.current_db_version}')
            ret, msg = major_upgrade(i.stored_db_version, i.current_db_version)
            if ret:
                if self.receiver.init():
                    print(f'Major upgrade from {i.stored_db_version} to {i.current_db_version} SUCCEEDED')
                    dtdebug(msg)
                else:
                    sys.exit(-1)
            else:
                dterror(msg)
                print(msg)
                sys.exit(-1)
        self.currently_selected_rec = None
        self.currently_selected_spectrum = None
        self.live_service_screen = LiveServiceScreen(self)
        self.live_recording_screen_ = None
        self.scan_subscriber_ = None
        self.stream_subscriber_ = None
        self.last_scan_text = ""
        self.scan_in_progress = False
        super().__init__(*args, **kwds)
        self.bitmaps = NeumoBitmaps()
        if False:
            self.presLan_en = gettext.translation("neumodvb", "./locale", languages=['en'])
            self.presLan_fr = gettext.translation("neumodvb", "./locale", languages=['fr'])
            self.presLan_fr.install()
            self.wxLocale('FR')
        self.global_subscriber_ = pyreceiver.global_subscriber(self.receiver, self.frame) #catch global error messages
        self.get_sats() #force create sat table of it does not exist

    @property
    def scan_subscriber(self):
        if self.scan_subscriber_ is None:
            self.scan_subscriber_ = pyreceiver.subscriber_t(self.receiver, self.frame)
        return self.scan_subscriber_


    @property
    def stream_subscriber(self):
        if self.stream_subscriber_ is None:
            self.stream_subscriber_ = pyreceiver.subscriber_t(self.receiver, self.frame)
        return self.stream_subscriber_

    @property
    def live_recording_screen(self):
        if self.live_recording_screen_ is None:
            self.live_recording_screen_ = LiveRecordingScreen(self)
        return self.live_recording_screen_

    @property
    def current_mpv_player(self):
        return self.frame.live_panel.mosaic_panel.current_mpv_player

    def PlayRecording(self, rec):
        dtdebug(f"SUBSCRIBED to recording {rec}")
        ret = self.current_mpv_player.play_recording(rec)
        if ret < 0:
            from neumodvb.neumo_dialogs import ShowMessage
            ShowMessage("Mux scan failed", self.scan_subscriber.error_message) #todo: record error message

    def ServiceTune(self, service_or_chgm, replace_running=True):
        self.frame.live_panel.ServiceTune(service_or_chgm, replace_running)
        dtdebug(f"SUBSCRIBED to service {service_or_chgm}")

    def MuxTune(self, mux):
        self.current_mpv_player.play_mux(mux)
        dtdebug(f"Requested subscription to mux {mux}")

    def MuxScan(self, muxlist, tune_options=None):
        ret = self.scan_subscriber.scan_muxes(muxlist, tune_options)
        dtdebug(f'MuxScan')
        if ret < 0:
            from neumodvb.neumo_dialogs import ShowMessage
            ShowMessage("Mux scan failed", self.scan_subscriber.error_message) #todo: record error message
        dtdebug(f"Requested subscription to scan muxes {muxlist}")

    def MuxesOnSatScan(self, satlist, tune_options, band_scan_options):
        chdb_rtxn = self.receiver.chdb.rtxn()
        dtdebug(f"MuxesOnSatScan: sats={satlist}")
        ret = self.scan_subscriber.scan_muxes_on_sats(chdb_rtxn, satlist, tune_options, band_scan_options)
        chdb_rtxn.commit()
        if ret < 0:
            from neumodvb.neumo_dialogs import ShowMessage
            ShowMessage("Sat scan failed", self.scan_subscriber.error_message) #todo: record error message

    def BandsOnSatScan(self, satlist, tune_options, band_scan_options):
        dtdebug(f"BandsOnSatScan: sats={satlist}")
        ret = self.scan_subscriber.scan_bands_on_sats(satlist, tune_options, band_scan_options)
        if ret < 0:
            from neumodvb.neumo_dialogs import ShowMessage
            ShowMessage("Satellite bandscan failed", self.scan_subscriber.error_message) #todo: record error message

    def ScanStop(self):
        dtdebug(f'ScanStop')
        if self.scan_subscriber_ is not None:
            self.scan_subscriber.unsubscribe()
            #TODO => what about subscription ids?
            dtdebug(f"Requested mux_scanning to stop")

    def ToggleOverlay(self):
        self.current_mpv_player.toggle_overlay()

    def Pause(self):
        self.current_mpv_player.pause()

    def Stop(self):
        self.frame.Stop()

    def Jump(self, seconds):
        self.current_mpv_player.jump(seconds)

    def AudioLang(self, dark_mode):
        langs = self.current_mpv_player.audio_languages()
        from neumodvb.language_dialog import show_audio_language_dialog
        show_audio_language_dialog(self.frame, dark_mode)

    def SubtitleLang(self, dark_mode):
        langs = self.current_mpv_player.subtitle_languages()
        from neumodvb.language_dialog import show_subtitle_language_dialog
        show_subtitle_language_dialog(self.frame, dark_mode)

    def CmdInspect(self):
        dtdebug("CmdInspect")
        import wx.lib.inspection
        wx.lib.inspection.InspectionTool().Show()

    def OnInit(self, inspect=False):
        self.frame = neumoMainFrame(self, None, wx.ID_ANY, "")
        self.SetTopWindow(self.frame)
        if inspect:
            self.Inspect()
        self.frame.Show()
        return True



if __name__ == "__main__":
    pyreceiver.set_process_name("neumodvb")
    gettext.install("neumodvb") # replace with the appropriate catalog name
    neumodvb = NeumoGui()
    load_gtk3_stylesheet(options.css)
    neumodvb.MainLoop()
    dtdebug("Successfully exited wxPython MainLoop")
    neumodvb.OnExit()
    if False:
        #show that we can restart (future work: can we detach; stop Xsession and reattach?)
        del neumodvb
        neumodvb = NeumoGui()
        neumodvb.MainLoop()

"""
PYTHONMALLOC=malloc valgrind --tool=memcheck   python3 neumodvb.py
PYTHONMALLOC=malloc valgrind --tool=memcheck  --error-limit=no --track-origins=yes --log-file=/tmp/valgrind.log  python3 neumodvb.py
"""
