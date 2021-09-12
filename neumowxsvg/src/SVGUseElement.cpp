//////////////////////////////////////////////////////////////////////////////
// Name:        SVGUseElement.cpp
// Purpose:     
// Author:      Alex Thuering
// Created:     2005/09/21
// RCS-ID:      $Id: SVGUseElement.cpp,v 1.2 2006/01/08 12:44:30 ntalex Exp $
// Copyright:   (c) 2005 Alex Thuering
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#include "SVGUseElement.h"

wxSVGRect wxSVGUseElement::GetBBox(wxSVG_COORDINATES coordinates) {
	wxSVGRect bbox;
	wxString href = GetHref();
	if (href.length() == 0 || href.GetChar(0) != wxT('#'))
		return bbox;
	href.Remove(0, 1);
	wxSVGElement* refElem = (wxSVGElement*) GetOwnerSVGElement()->GetElementById(href);
	if (!refElem)
		return bbox;
	bbox = wxSVGLocatable::GetChildrenBBox(refElem, coordinates);
	if (coordinates != wxSVG_COORDINATES_USER)
		bbox = bbox.MatrixTransform(GetMatrix(coordinates));
	return bbox;
}

wxSVGRect wxSVGUseElement::GetResultBBox(wxSVG_COORDINATES coordinates) {
	wxSVGRect bbox;
	wxString href = GetHref();
	if (href.length() == 0 || href.GetChar(0) != wxT('#'))
		return bbox;
	href.Remove(0, 1);
	wxSVGElement* refElem = (wxSVGElement*) GetOwnerSVGElement()->GetElementById(href);
	if (!refElem)
		return bbox;
	bbox = wxSVGLocatable::GetChildrenResultBBox(refElem, coordinates);
	if (coordinates != wxSVG_COORDINATES_USER)
		bbox = bbox.MatrixTransform(GetMatrix(coordinates));
	return bbox;
}
