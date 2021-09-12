//////////////////////////////////////////////////////////////////////////////
// Name:        svgview.cpp
// Purpose:     
// Author:      Alex Thuering
// Created:     15/01/2005
// RCS-ID:      $Id: svgview.cpp,v 1.20 2017/02/25 10:24:59 ntalex Exp $
// Copyright:   (c) Alex Thuering
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#include "svgview.h"
#include <wx/wx.h>
#include <wx/mstream.h>
#include <wx/artprov.h>
#include <wxSVG/svg.h>
#ifdef USE_LIBAV
#include <wxSVG/mediadec_ffmpeg.h>
#endif
#include "../resources/wxsvg.png.h"
#include "../resources/pause.png.h"
#include "../resources/start.png.h"
#include "../resources/stop.png.h"
#define ICON_PAUSE wxBITMAP_FROM_MEMORY(pause)
#define ICON_START wxBITMAP_FROM_MEMORY(start)
#define ICON_STOP wxBITMAP_FROM_MEMORY(stop)

#define wxICON_FROM_MEMORY(name) wxGetIconFromMemory(name##_png, sizeof(name##_png))
#define wxBITMAP_FROM_MEMORY(name) wxGetBitmapFromMemory(name##_png, sizeof(name##_png))

inline wxBitmap wxGetBitmapFromMemory(const unsigned char *data, int length) {
	wxMemoryInputStream is(data, length);
	return wxBitmap(wxImage(is, wxBITMAP_TYPE_ANY, -1), -1);
}

inline wxIcon wxGetIconFromMemory(const unsigned char *data, int length) {
	wxIcon icon;
	icon.CopyFromBitmap(wxGetBitmapFromMemory(data, length));
	return icon;
}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////  Application /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

IMPLEMENT_APP(SVGViewApp)

bool SVGViewApp::OnInit() {
	wxGetApp();
#ifndef __WXWINCE__
	setlocale(LC_NUMERIC, "C");
#endif
	//wxLog::SetActiveTarget(new wxLogStderr);
	wxInitAllImageHandlers();
#ifdef USE_LIBAV
	wxFfmpegMediaDecoder::Init();
#endif

	MainFrame* mainFrame = new MainFrame(NULL, wxT("SVG Viewer"), wxDefaultPosition, wxSize(500, 400));
	SetTopWindow(mainFrame);

	return true;
}

//////////////////////////////////////////////////////////////////////////////
////////////////////////////////  MainFrame //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
enum {
	FIT_ID = 1, HITTEST_ID, TIMER_ID, EXPORT_ID, START_ID, STOP_ID, PAUSE_ID,
};

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
  EVT_MENU(wxID_OPEN, MainFrame::OnOpen)
  EVT_MENU(wxID_SAVE, MainFrame::OnSave)
  EVT_MENU(EXPORT_ID, MainFrame::OnExport)
  EVT_MENU(wxID_EXIT, MainFrame::OnExit)
  EVT_MENU(FIT_ID, MainFrame::Fit)
  EVT_MENU(HITTEST_ID, MainFrame::Hittest)
  EVT_MENU(START_ID, MainFrame::OnStart)
  EVT_MENU(STOP_ID, MainFrame::OnStop)
  EVT_MENU(PAUSE_ID, MainFrame::OnPause)
  EVT_TIMER(TIMER_ID, MainFrame::OnTimerTimeout)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(SVGCtrl, wxSVGCtrl)
  EVT_LEFT_UP(SVGCtrl::OnMouseLeftUp)
  EVT_KEY_DOWN(SVGCtrl::OnKeyDown)
END_EVENT_TABLE()

MainFrame::MainFrame(wxWindow *parent, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
		wxFrame(parent, wxID_ANY, title, pos, size, style) {
	// Make a menubar
	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(wxID_OPEN, _T("&Open...\tCtrl-O"));
	fileMenu->Append(wxID_SAVE, _T("&Save as...\tCtrl-S"));
	fileMenu->Append(EXPORT_ID, _T("&Export...\tCtrl-E"));
	fileMenu->AppendSeparator();
	fileMenu->Append(wxID_EXIT, _T("&Exit\tAlt-X"));
	fileMenu->AppendSeparator();
	fileMenu->AppendCheckItem(FIT_ID, _T("&FitToFrame"))->Check();
	fileMenu->AppendCheckItem(HITTEST_ID, _T("&Hit-Test"));

	wxMenuBar* menuBar = new wxMenuBar;
	menuBar->Append(fileMenu, _T("&File"));
	SetMenuBar(menuBar);

#ifndef __WXMSW__
	SetIcon(wxICON_FROM_MEMORY(wxsvg));
#else
	SetIcon(wxICON(wxsvg));
#endif

	m_timer = new wxTimer(this, TIMER_ID);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_svgCtrl = new SVGCtrl(this);
	mainSizer->Add(m_svgCtrl, 1, wxEXPAND);

	m_toolbar = new wxToolBar(this, -1, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_FLAT | wxTB_BOTTOM);
	m_toolbar->SetToolBitmapSize(wxSize(22, 22));
	m_toolbar->AddTool(START_ID, wxT(""), ICON_START, wxNullBitmap, wxITEM_NORMAL, _("Start"), wxT(""));
	m_toolbar->AddTool(STOP_ID, wxT(""), ICON_STOP, wxNullBitmap, wxITEM_NORMAL, _("Stop"), wxT(""));
	m_toolbar->AddTool(PAUSE_ID, wxT(""), ICON_PAUSE, wxNullBitmap, wxITEM_NORMAL, _("Pause"), wxT(""));
	m_toolbar->Realize();
	mainSizer->Add(m_toolbar, 0, wxEXPAND);

	SetSizer(mainSizer);

	if (wxTheApp->argc > 1)
	Open(wxTheApp->argv[1]);
	else
	Open(_T("tiger.svg"));

	Center();
	Show(true);
}

bool MainFrame::Open(const wxString& filename) {
	bool ok = m_svgCtrl->Load(filename);
	m_duration = ok ? m_svgCtrl->GetSVG()->GetDuration() : 0;
	if (m_duration > 0) {
		wxCommandEvent evt;
		OnStart(evt);
	} else {
		wxCommandEvent evt;
		OnStop(evt);
	}
	return ok;
}

void MainFrame::OnOpen(wxCommandEvent& event) {
	wxString filename = wxFileSelector(_T("Choose a file to open"), _T(""), _T(""), _T(""),
			_T("SVG files (*.svg)|*.svg|All files (*.*)|*.*"));
	if (!filename.empty())
		Open(filename);
}

void MainFrame::OnSave(wxCommandEvent& event) {
	wxString filename = wxFileSelector(_T("Choose a file to save"), _T(""), _T(""), _T(""),
			_T("SVG files (*.svg)|*.svg|All files (*.*)|*.*"), wxFD_SAVE);
	if (!filename.empty())
		m_svgCtrl->GetSVG()->Save(filename);
}

void MainFrame::OnExport(wxCommandEvent& event) {
	wxString filename = wxFileSelector(_T("Choose a file to save"), _T(""), _T(""), _T(""),
			_T("PNG files (*.png)|*.png|JPEG files (*.jpg)|*.png|All files (*.*)|*.*"), wxFD_SAVE);
	if (filename.empty())
		return;
	wxImage img = m_svgCtrl->GetSVG()->Render(-1, -1, NULL, true, true);
	img.Rescale(22, 22, wxIMAGE_QUALITY_HIGH);
	img.SaveFile(filename);
}

void MainFrame::OnTimerTimeout(wxTimerEvent& event) {
	m_svgCtrl->GetSVG()->SetCurrentTime((double) m_svgCtrl->GetSVG()->GetCurrentTime() + 0.05);
	m_svgCtrl->Refresh();
}

void MainFrame::Hittest(wxCommandEvent& event) {
	m_svgCtrl->SetShowHitPopup(event.IsChecked());
}

void MainFrame::Fit(wxCommandEvent& event) {
	m_svgCtrl->SetFitToFrame(event.IsChecked());
	m_svgCtrl->Refresh();
}

void MainFrame::OnExit(wxCommandEvent& event) {
	m_timer->Stop();
	Close(true);
}

void MainFrame::OnStart(wxCommandEvent& event) {
	m_timer->Start(50);
	UpdateToolbar();
}

void MainFrame::OnStop(wxCommandEvent& event) {
	m_timer->Stop();
	m_svgCtrl->GetSVG()->SetCurrentTime(0);
	m_svgCtrl->Refresh();
	UpdateToolbar();
}

void MainFrame::OnPause(wxCommandEvent& event) {
	m_timer->Stop();
	UpdateToolbar();
}
void MainFrame::UpdateToolbar() {
	m_toolbar->EnableTool(START_ID, m_duration > 0 && !m_timer->IsRunning());
	m_toolbar->EnableTool(STOP_ID, m_duration > 0 && m_timer->IsRunning());
	m_toolbar->EnableTool(PAUSE_ID, m_duration > 0 && m_timer->IsRunning());
}

SVGCtrl::SVGCtrl(wxWindow* parent) :
		wxSVGCtrl(parent), m_ShowHitPopup(false) {
}

void SVGCtrl::SetShowHitPopup(bool show) {
	m_ShowHitPopup = show;
}

bool SVGCtrl::Load(const wxString& filename) {
	m_filename = filename;
	return wxSVGCtrl::Load(filename);
}

void SVGCtrl::OnMouseLeftUp(wxMouseEvent & event) {
	if (m_ShowHitPopup) {
		wxSVGDocument* svgDoc = GetSVG();
		wxSVGSVGElement* root = svgDoc->GetRootElement();
		wxSVGRect rect(event.m_x / GetScaleX(), event.m_y / GetScaleY(), 1, 1);
		wxNodeList clicked = root->GetIntersectionList(rect, *root);
		wxString message;
		message.Printf(_T("Click : %d,%d -> %g,%g\n"), event.m_x, event.m_y,
				event.m_x / GetScaleX(), event.m_y / GetScaleY());
		for (unsigned int i = 0; i < clicked.GetCount(); i++) {
			wxString desc;
			wxSVGElement* obj = clicked.Item(i);
			if (obj->GetId().length())
				desc.Printf(_T("%s, id: %s\n"), obj->GetName().c_str(), obj->GetId().c_str());
			else
				desc.Printf(_T("%s\n"), obj->GetName().c_str());
			message.Append(desc);
		}
		wxMessageBox(message, _T("Hit Test (objects bounding box)"));
	}
}

void SVGCtrl::OnKeyDown(wxKeyEvent& event) {
	switch (event.GetKeyCode()) {
		case 'R':
			if (event.ControlDown() && m_filename.length() > 0) {
				Load(m_filename);
			}
			break;
		default:
			event.Skip();
			break;
	}
}

