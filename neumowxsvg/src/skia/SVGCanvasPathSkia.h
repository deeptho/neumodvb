#pragma once
#include "SVGCanvasItem.h"
#include "SkPath.h"
class wxSVGCanvasPathSkia:public wxSVGCanvasPath
{
public:
    wxSVGCanvasPathSkia();
    ~wxSVGCanvasPathSkia();

public:
    SkPath& GetPath();
    wxSVGRect GetBBox(const wxSVGMatrix* matrix = NULL);
//private:
//    HGPATH CreatePath();
//    void   DestroyPath();

private:
    virtual void MoveToImpl(double x, double y);
    virtual void LineToImpl(double x, double y);
    virtual void CurveToCubicImpl(double x1, double y1, double x2, double y2, double x, double y);
    virtual bool ClosePathImpl();
    virtual void End();

private:
    SkPath m_path;
};

