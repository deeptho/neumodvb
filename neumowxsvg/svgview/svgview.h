//////////////////////////////////////////////////////////////////////////////
// Name:        test.h
// Purpose:     Widget to display svg documents using svg library
// Author:      Alex Thuering
// Created:     2005/05/07
// RCS-ID:      $Id: svgview.h,v 1.7 2017/01/22 21:03:54 ntalex Exp $
// Copyright:   (c) Alex Thuering
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#ifndef SVGVIEW_H
#define SVGVIEW_H

#include <wx/wx.h>
#include <wxSVG/svgctrl.h>

class SVGViewApp: public wxApp {
public:
    bool OnInit();
};

class SVGCtrl: public wxSVGCtrl
{
public:
    SVGCtrl(wxWindow* parent);
    bool Load(const wxString& filename);
	void SetShowHitPopup(bool);

private:
	bool m_ShowHitPopup;
    wxString m_filename;
    void OnMouseLeftUp(wxMouseEvent & event);
    void OnKeyDown(wxKeyEvent& event);
    DECLARE_EVENT_TABLE()
};

class MainFrame: public wxFrame {
public:
    MainFrame(wxWindow *parent, const wxString& title, const wxPoint& pos,
      const wxSize& size, long style = wxDEFAULT_FRAME_STYLE);

private:
    SVGCtrl* m_svgCtrl;
    wxToolBar* m_toolbar;
    wxTimer* m_timer;
    double m_duration;
    bool Open(const wxString& filename);
    void UpdateToolbar();
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnExport(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnTimerTimeout(wxTimerEvent& event);
    void Fit(wxCommandEvent& event);
    void Hittest(wxCommandEvent& event);
    void OnStart(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnPause(wxCommandEvent& event);

    DECLARE_EVENT_TABLE()
};

#endif //SVGVIEW_H
