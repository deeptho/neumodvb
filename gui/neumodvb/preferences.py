#!/usr/bin/env python3


import wx
import  wx.lib.newevent


# begin wxGlade: dependencies
import gettext
# end wxGlade

# begin wxGlade: extracode
# end wxGlade

#the following import also sets up import path
#import neumodvb

from neumodvb.neumowidgets import TextCtrl, LongitudeTextCtrl, LatitudeTextCtrl, EVT_VALUE_CHANGED

class PreferencesDialog(wx.Dialog):
    def add_notebook_page(self, page_name, entries):
        pane = wx.Panel(self.preferences_notebook, wx.ID_ANY)
        self.preferences_notebook.AddPage(pane, _(page_name))
        grid_sizer = wx.FlexGridSizer(len(entries), 2, 5, 5)
        for widget_class, label, prop, val, tooltip in entries:
            label_widget = wx.StaticText(pane, wx.ID_ANY, _(label))
            grid_sizer.Add(label_widget, 0, wx.ALIGN_CENTER_VERTICAL, 0)
            widget = widget_class(pane, wx.ID_ANY)
            if widget_class == wx.CheckBox:
                widget.Bind(wx.EVT_CHECKBOX, self.OnValueChanged)
                widget.SetValue(val)
                pass
            else:
                widget.SetValue(val)
                widget.Bind(EVT_VALUE_CHANGED, self.OnValueChanged)
            setattr(self, prop, widget)
            widget.key = prop
            widget.SetMinSize((250, -1))
            if tooltip is not None:
                widget.SetToolTip(_(tooltip))
            grid_sizer.Add(widget, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer.AddGrowableCol(1)
        pane.SetSizer(grid_sizer)
        return pane

    def OnValueChanged(self, evt):
        prop = getattr(evt, "widget", evt.GetEventObject())
        key = prop.key
        val = prop.GetValue()
        print(f'OnValueChanged: {key}={val}')

    def __init__(self, *args, **kwds):
        # begin wxGlade: PreferencesDialog.__init__
        kwds["style"] = kwds.get("style", 0) | wx.DEFAULT_DIALOG_STYLE
        wx.Dialog.__init__(self, *args, **kwds)
        self.SetSize((633, 636))
        self.SetTitle(_("NeumoDVB preferences"))

        main_sizer = wx.BoxSizer(wx.VERTICAL)

        self.preferences_notebook = wx.Notebook(self, wx.ID_ANY)
        main_sizer.Add(self.preferences_notebook, 1, wx.ALL | wx.EXPAND, 5)
        # self.main_sizer = wx.BoxSizer(wx.VERTICAL)


        self.storage_pane = self.add_notebook_page("Storage", [
            (TextCtrl, 'Database', 'database_path_text', '/mnt/neumo/db', None),
            (TextCtrl, 'Live buffers', 'live_path_text', '/mnt/neumo/live', None),
            (TextCtrl, 'Recordings', 'rec_path', '/mnt/neumo/recordings', None),
            (TextCtrl, 'Spectra', 'spectrum_path', '/mnt/neumo/spectrum', None),
            (TextCtrl, 'Config path', 'config_path', '~/.config/neumodvb/', None),
        ])

        self.configfile_pane = self.add_notebook_page("Config files", [
            (TextCtrl, 'Live overlay skin', 'gui_svg_path', 'gui.svg', None),
            (TextCtrl, 'mpv config', 'svg_config_path', 'mpv/mpv.conf', None),
            (TextCtrl, 'Log4cxx config', 'log4cxx_path', 'neumo.xml', None),
        ])

        self.record_pane = self.add_notebook_page("Record", [
            (TextCtrl, 'Default record time (min)',
             'record_time_text', '120', 'Duration of unscheduled recordings (min)'),
            (TextCtrl, 'Pre-record time (min)', 'pre_record_time_text', '1',
             'Time to start recording before start of program (min or min:sec)'),
            (TextCtrl, 'Post-record time (min)', 'post_record_time_text', '5',
             'Time to continue recording after end of program (min or min:sec)'),
            (TextCtrl, 'Timeshift duration (min)', 'timeshift_duration_text', '120',
             'Time for which to keep timeshift data (min)'),
            (TextCtrl, 'Timeshift buffer retention', 'timeshift_retention_text', '5',
             'Time for which to preserve livebuffers after tuning to another service (min)'),
            (TextCtrl, 'Timeshift resolution (min)', 'timeshift_resolution_text', '5', None),
        ])

        self.tune_pane = self.add_notebook_page("Tune", [

            (wx.CheckBox, 'Alllow moving dish', 'allow_moving_dish_checkbox', 1,
             'When tuning to service allow moving dish'),
            (LongitudeTextCtrl, 'Usals Longitude', 'usals_longitude', 4.0, None),
            (LatitudeTextCtrl, 'Usals Latitude', 'usals_latitude', 51.0, None),
        ])


        button_sizer = wx.StdDialogButtonSizer()
        main_sizer.Add(button_sizer, 0, wx.ALIGN_RIGHT | wx.ALL, 4)

        self.button_OK = wx.Button(self, wx.ID_OK, "")
        self.button_OK.SetDefault()
        button_sizer.AddButton(self.button_OK)

        self.button_CANCEL = wx.Button(self, wx.ID_CANCEL, "")
        button_sizer.AddButton(self.button_CANCEL)

        button_sizer.Realize()

        self.SetSizer(main_sizer)

        self.SetAffirmativeId(self.button_OK.GetId())
        self.SetEscapeId(self.button_CANCEL.GetId())

        self.Layout()
        # end wxGlade

# end of class PreferencesDialog

def show_preferences_dialog():
    dlg = PreferencesDialog(None, wx.ID_ANY, "")
    dlg.ShowModal()
    dlg.Destroy()
    return None

if False:
    class MyApp(wx.App):
        def OnInit(self):
            self.preferences_dialog = PreferencesDialog(None, wx.ID_ANY, "")
            self.SetTopWindow(self.preferences_dialog)
            self.preferences_dialog.ShowModal()
            self.preferences_dialog.Destroy()
            return True

    # end of class MyApp



    if __name__ == "__main__":
        gettext.install("app") # replace with the appropriate catalog name

        app = MyApp(0)
        app.MainLoop()
