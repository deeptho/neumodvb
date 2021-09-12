#include "iostream"
#include "SVGCanvasPathSkia.h"

wxSVGCanvasPathSkia::wxSVGCanvasPathSkia()
{
    
}


wxSVGCanvasPathSkia::~wxSVGCanvasPathSkia()
{
    
}

SkPath& wxSVGCanvasPathSkia::GetPath()
{
    return m_path;
}

wxSVGRect wxSVGCanvasPathSkia::GetBBox(const wxSVGMatrix * matrix)
{
    if (!m_path.isEmpty())
    {
        SkRect rc = m_path.getBounds();
        return wxSVGRect(rc.fLeft, rc.fTop, rc.width(), rc.height());
    }
    else
    {
        return wxSVGRect(0, 0, 0, 0);
    }
}

void wxSVGCanvasPathSkia::MoveToImpl(double x, double y)
{
    m_path.moveTo(x, y);
}

void wxSVGCanvasPathSkia::LineToImpl(double x, double y)
{
    m_path.lineTo(x, y);
}

void wxSVGCanvasPathSkia::CurveToCubicImpl(double x1, double y1, double x2, double y2, double x, double y)
{
    m_path.cubicTo(x1, y1, x2, y2, x, y);
}

bool wxSVGCanvasPathSkia::ClosePathImpl()
{
    m_path.close();
    return false;
}

void wxSVGCanvasPathSkia::End()
{
}
