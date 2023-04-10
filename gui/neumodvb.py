#!/usr/bin/python3
# Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
import os
import wx.glcanvas #needed for the created mpvglcanvas
import wx
import gettext
import signal
from functools import lru_cache
#the following import also sets up import path
import neumodvb

from neumodvb.util import load_gtk3_stylesheet, dtdebug, dterror, maindir, get_object
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
        self.main_menubar.epg_record_menu_item = self.main_menubar.get_menu_item('ToggleRecord')
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
        self.bouquet_being_edited=None
        self.panels = [
            self.servicelist_panel,
            self.chgmlist_panel,
            self.chepg_panel, self.live_panel,
            self.dvbs_muxlist_panel, self.dvbc_muxlist_panel, self.dvbt_muxlist_panel,
            self.lnblist_panel,
            self.chglist_panel,
            self.satlist_panel, self.frontendlist_panel, self.statuslist_panel,
            self.mosaic_panel,
            self.reclist_panel, self.spectrumlist_panel]
        self.grids = [
            self.servicegrid, self.chgmgrid,
            self.recgrid, self.spectrumgrid, self.chepggrid,
            self.dvbs_muxgrid, self.dvbc_muxgrid, self.dvbt_muxgrid,
            self.lnbgrid,
            self.satgrid, self.chggrid, self.frontendgrid, self.statusgrid
        ]
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
        self.info_windows = [self.chepginfo_text, self.recinfo_text]
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
        if type(data) == pyreceiver.scan_stats_t:
            st = data
        elif type(data) == pyreceiver.scan_report_t:
            st = data.scan_stats
        elif type(data) == str:
            ShowMessage("Error", data)
            return
        else:
            st = None
        if st is not None:
            done = st.pending_muxes + st.active_muxes == 0
            pending = st.pending_muxes
            ok = st.locked_muxes
            active = st.active_muxes
            self.app.scan_in_progress = pending+active > 0
            self.app.last_scan_text = f" ok={ok} failed={st.failed_muxes} pending={pending} active={active}" \
                if self.app.scan_in_progress else None
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
        items_to_toggle= {}
        for panel in self.panels:
            if panel not in panelstoshow:
                self.EnablePanelSpecificMenus(panel, items_to_toggle, False)
                panel.Hide()
            else:
                panel.Show()
                self.EnablePanelSpecificMenus(panel, items_to_toggle, True)
                if hasattr(panel, 'main_grid'):
                    panel.main_grid.SetFocus()
                else:
                    wx.CallAfter(panel.SetFocus)
        for item_name, onoff in items_to_toggle.items():
            item = getattr(self.main_menubar, item_name)
            item.Enable(onoff)
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

    def EnablePanelSpecificMenus(self, panel, items_to_toggle, onoff):
        if(hasattr(panel, 'grid')):
           for item_name in panel.grid.grid_specific_menu_items:
               #always turn item on when onoff==True else turn it off, unless it is turned on
               items_to_toggle[item_name] = onoff if onoff else items_to_toggle.get(item_name, False)

    def Stop(self):
        self.app.MuxScanStop()
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
        #self.parent.OnTimer(evt)
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
        self.OnExit()
        dtdebug('Calling Destroy')
        #self.Destroy()
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
        assert 0

    def OnExit(self, event=None):
        dtdebug(f"Asking receiver to exit receiver={self.app.receiver}")
        self.app.receiver.stop()
        dtdebug("OnExit done")
        return 0

    def CmdInspect(self, event):
        dtdebug("CmdInspect")
        self.app.CmdInspect()

    def CmdChannelScreenshot(self, event):
        dtdebug("CmdChannelScreenshot")
        self.app.current_mpv_player.screenshot()

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
        dtdebug("CmdFullScreen")
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
        self.ShowPanel([self.chepg_panel, self.mosaic_panel], info_windows=self.chepginfo_text)

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

    def CmdSatList(self, event):
        dtdebug("CmdSatList")
        self.ShowPanel(self.satlist_panel)

    def CmdChgList(self, event):
        dtdebug("CmdChgList")
        self.ShowPanel(self.chglist_panel)

    def CmdFrontendList(self, event):
        dtdebug("CmdFrontendList")
        self.ShowPanel(self.frontendlist_panel)

    def CmdRecList(self, event):
        dtdebug("CmdRecList")
        self.ShowPanel(self.reclist_panel)

    def CmdNew(self, event):
        dtdebug("CmdNew")
        if not self.edit_mode:
            self.SetEditMode(True)
        panel = self.current_panel()
        if panel is not None:
            return panel.grid.OnNew(event)
        assert 0

    def CmdScan(self, event):
        dtdebug('CmdScan')
        panel = self.current_panel()
        if panel in (self.dvbs_muxlist_panel, self.dvbc_muxlist_panel, self.dvbt_muxlist_panel):
            panel.grid.CmdScan(event)
        else:
            dterror(f"Scanning on bad panel {panel}")

    def CmdPause(self, event):
        dtdebug('CmdPause')
        return wx.GetApp().Pause()

    def CmdToggleOverlay(self, event):
        dtdebug('CmdToggleOverlay')
        return wx.GetApp().ToggleOverlay()

    def CmdStop(self, event):
        dtdebug('CmdStop')
        return wx.GetApp().Stop()

    def CmdAudioLang(self, event):
        dtdebug('CmdAudioLang')
        dark_mode = self.current_panel() == self.live_panel
        return wx.GetApp().AudioLang(dark_mode)

    def CmdSubtitleLang(self, event):
        dtdebug('CmdSubtitleLang')
        dark_mode = self.current_panel() == self.live_panel
        return wx.GetApp().SubtitleLang(dark_mode)
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

    def CmdDelete(self, event):
        dtdebug("CmdDelete")
        panel = self.current_panel()
        if panel is not None and hasattr(panel, 'grid'):
            return panel.grid.OnDelete(event)
        return False

    def CmdUndo(self, event):
        dtdebug("CmdUndo")
        panel = self.current_panel()
        if panel is not None and hasattr(panel, 'grid'):
            return panel.grid.OnUndo(event)

    def CmdToggleRecord(self, event):
        dtdebug("CmdToggleRecord")
        m = self.get_panel_method('OnToggleRecord')
        dtdebug(f'CmdToggleRecord: {m}')
        if m is not None:
            m(event)

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
    def get_sats(self):
        for retry in False, True:
            txn = self.chdb.rtxn()
            self.sats = pychdb.sat.list_all_by_key(txn)
            del txn
            if len(self.sats) == 0 and not retry:
                from neumodvb.init_db import init_db
                init_db()
            else:
                return self.sats

    def get_cards(self):
        txn = wx.GetApp().devdb.rtxn()
        ret={}
        for a in  pydevdb.fe.list_all_by_card_mac_address(txn):
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

    def __init__(self, *args, **kwds):
        self.receiver = pyreceiver.receiver_t(options.receiver)
        self.currently_selected_rec = None
        self.currently_selected_spectrum = None
        self.live_service_screen = LiveServiceScreen(self)
        self.live_recording_screen_ = None
        self.scan_subscriber_ = None
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

    @property
    def scan_subscriber(self):
        if self.scan_subscriber_ is None:
            self.scan_subscriber_ = pyreceiver.subscriber_t(self.receiver, self.frame)
        return self.scan_subscriber_

    @property
    def live_recording_screen(self):
        if self.live_recording_screen_ is None:
            self.live_recording_screen_ = LiveRecordingScreen(self)
        return self.live_recording_screen_

    @property
    def current_mpv_player(self):
        return self.frame.live_panel.mosaic_panel.current_mpv_player

    def PlayRecording(self, rec):
        self.current_mpv_player.play_recording(rec)
        dtdebug(f"SUBSCRIBED to recording {rec}")

    def ServiceTune(self, service_or_chgm, replace_running=True):
        self.frame.live_panel.ServiceTune(service_or_chgm, replace_running)
        dtdebug(f"SUBSCRIBED to service {service_or_chgm}")

    def MuxTune(self, mux):
        self.current_mpv_player.play_mux(mux)
        dtdebug(f"Requested subscription to mux {mux}")

    def MuxScan(self, muxlist):
        ret = self.scan_subscriber.scan_muxes(muxlist)
        print(f'MuxScan')
        if ret < 0:
            from neumodvb.neumo_dialogs import ShowMessage
            ShowMessage("Muxscan failed", self.scan_subscriber.error_message) #todo: record error message
        dtdebug(f"Requested subscription to scan mux {muxlist}")

    def MuxScanStop(self):
        print(f'MuxScanStop')
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
