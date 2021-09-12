#include <iostream>
#include "SVGCanvasSkia.h"
#include "SkBitmap.h"
#include "SkColor.h"
#include "SkShader.h"

unsigned char* wxSVGCanvasSkia::GetBuffer()
	return nullptr;
}


wxSVGCanvasSkia::~wxSVGCanvasSkia()
{
    DestroyGradient();
    delete m_pCanvas;
    m_pCanvas = nullptr;
    delete m_pBitmap;
    m_pBitmap = nullptr;
}

wxSVGCanvasSkia::wxSVGCanvasSkia()
{
    m_pGraient = nullptr;
}

void wxSVGCanvasSkia::Init(int width, int height, bool alpha)
{
    m_nWidth = width;
    m_nHeight = height;
    m_bAlpha = alpha;

    m_pBitmap = new SkBitmap;
    m_pBitmap->setInfo(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kOpaque_SkAlphaType));
    m_pBitmap->allocPixels();
    m_pBitmap->eraseColor(0x00000000);
    m_pCanvas = new SkCanvas(*m_pBitmap);
}

int wxSVGCanvasSkia::GetWidth()
{
    return m_nWidth;
}

int wxSVGCanvasSkia::GetHeight()
{
    return m_nHeight;
}

wxImage wxSVGCanvasSkia::GetImage()
{
    wxImage img;
    img.Create(m_nWidth, m_nHeight);
    if (m_bAlpha)
        img.InitAlpha();

    unsigned char* src = (unsigned char*)m_pBitmap->getPixels();
    unsigned char* dst = img.GetData();
    unsigned char* dstAlpha = img.GetAlpha();
    for (int i = 0; i < m_nWidth * m_nHeight; i++) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        if (dstAlpha != NULL) {
            *dstAlpha = src[3];
            dstAlpha++;
        }
        src += 4;
        dst += 3;
    }
    return img;
}

void wxSVGCanvasSkia::Clear(wxRGBColor color)
{
}

wxSVGCanvasPath * wxSVGCanvasSkia::CreateCanvasPath(wxSVGMatrix * matrix)
{
    return new wxSVGCanvasPathSkia();
}

wxSVGCanvasItem * wxSVGCanvasSkia::CreateItem(wxSVGTextElement * element, const wxCSSStyleDeclaration * style, wxSVGMatrix * matrix)
{
    return nullptr;
}

wxSVGCanvasItem * wxSVGCanvasSkia::CreateItem(wxSVGImageElement * element, const wxCSSStyleDeclaration * style, wxProgressDialog * progressDlg)
{
    return nullptr;
}

wxSVGCanvasItem * wxSVGCanvasSkia::CreateItem(wxSVGVideoElement * element, const wxCSSStyleDeclaration * style, wxProgressDialog * progressDlg)
{
    return nullptr;
}

void wxSVGCanvasSkia::DrawItem(wxSVGCanvasItem & item, wxSVGMatrix & matrix, const wxCSSStyleDeclaration & style, wxSVGSVGElement & svgElem)
{
    if (wxSVG_CANVAS_ITEM_PATH == item.GetType())
    {
        DrawCanvasPath((wxSVGCanvasPathSkia&)item, matrix, style, svgElem);
    }
}

void wxSVGCanvasSkia::SetStopValue(unsigned int index, float offset, float opacity, const wxRGBColor & rgbColor)
{
    if (m_pGraient)
    {
        unsigned char nOpacity = opacity * 255;
        m_pGraient->AddColorStop(offset, SkColorSetARGB(nOpacity, rgbColor.Red(), rgbColor.Green(), rgbColor.Blue()));
    }
}

void wxSVGCanvasSkia::AllocateGradientStops(unsigned int stop_count)
{
}

void wxSVGCanvasSkia::DrawCanvasPath(wxSVGCanvasPathSkia & canvasPath, wxSVGMatrix & matrix, const wxCSSStyleDeclaration & style, wxSVGSVGElement & svgElem)
{
    wxCSS_VALUE_EVENODD;
    wxCSS_VALUE cssFileType = style.GetFillRule();
    m_pCanvas->save();
    if (wxCSS_VALUE_EVENODD == cssFileType)
    {
        canvasPath.GetPath().setFillType(SkPath::kEvenOdd_FillType);
    }

    SkMatrix mx;
    mx.setAll(matrix.GetA(), matrix.GetC(), matrix.GetE(), matrix.GetB(), matrix.GetD(), matrix.GetF(), 0, 0, 1);
    m_pCanvas->setMatrix(mx);

    // Filling
    SkPaint paint;
    paint.setAntiAlias(true);
    if (canvasPath.GetFill() && style.GetFill().Ok() && style.GetFill().GetPaintType() != wxSVG_PAINTTYPE_NONE)
    {
        paint.setStyle(SkPaint::kFill_Style);
        SetPaint(style.GetFill(), paint, style.GetOpacity()*style.GetFillOpacity(), canvasPath, svgElem, matrix);
        m_pCanvas->drawPath(canvasPath.GetPath(), paint);
    }

    // Stroking
    if (style.GetStroke().Ok() && style.GetStrokeWidth() > 0)
    {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(style.GetStrokeWidth());
        SetPaint(style.GetStroke(), paint, style.GetOpacity()*style.GetStrokeOpacity(), canvasPath, svgElem, matrix);
        ApplyStrokeStyle(style);
        m_pCanvas->drawPath(canvasPath.GetPath(), paint);
    }

    // marker
    /*if (style.HasMarkerStart()) {
        DrawMarker(style.GetMarkerStart().GetStringValue(), wxSVGMark::START, canvasPath, matrix, style, svgElem);
    }
    if (style.HasMarkerMid()) {
        DrawMarker(style.GetMarkerMid().GetStringValue(), wxSVGMark::MID, canvasPath, matrix, style, svgElem);
    }
    if (style.HasMarkerEnd()) {
        DrawMarker(style.GetMarkerEnd().GetStringValue(), wxSVGMark::END, canvasPath, matrix, style, svgElem);
    }*/

    m_pCanvas->restore();
}

void wxSVGCanvasSkia::SetPaint(const wxSVGPaint & paint, SkPaint& spaint, float opacity, wxSVGCanvasPathSkia& canvasPath,
    wxSVGSVGElement& svgElem, const wxSVGMatrix& matrix)
{
    if (paint.GetPaintType() >= wxSVG_PAINTTYPE_URI_NONE && paint.GetPaintType() <= wxSVG_PAINTTYPE_URI)
    {
        wxSVGGradientElement* gradElem = GetGradientElement(svgElem, paint.GetUri());
        if (nullptr == gradElem)
        {
            return;
        }

        DestroyGradient();
        switch (gradElem->GetDtd())
        {
        case wxSVG_LINEARGRADIENT_ELEMENT:
        {
            wxSVGPoint p1, p2;
            GetLinearGradientVector(p1, p2, (wxSVGLinearGradientElement&)*gradElem, canvasPath);
            m_pGraient = Gradient::Create(p1.GetX(), p1.GetY(), p2.GetX(), p2.GetY());
            break;
        }
        case wxSVG_RADIALGRADIENT_ELEMENT:
        {
            wxSVGRect bbox = canvasPath.GetBBox();
            wxSVGRadialGradientElement* radialGradElem = (wxSVGRadialGradientElement*)gradElem;
            bool bPercentage = radialGradElem->GetCx().GetBaseVal().GetUnitType() == wxSVG_LENGTHTYPE_PERCENTAGE;
            double r = 0, cx = 0, cy = 0, fx = 0, fy = 0;
            if (bPercentage && (gradElem->GetGradientUnits().GetAnimVal() == wxSVG_UNIT_TYPE_UNKNOWN
                || gradElem->GetGradientUnits().GetAnimVal() == wxSVG_UNIT_TYPE_OBJECTBOUNDINGBOX))
            {
                wxSVGRect bbox = canvasPath.GetBBox();
                r = (sqrt(bbox.GetWidth() * bbox.GetWidth() + bbox.GetHeight() * bbox.GetHeight()))/ sqrt(2.0f);
                cx = bbox.GetX() + radialGradElem->GetCx().GetAnimVal().GetValueInSpecifiedUnits() / 100.0f *bbox.GetWidth();
                cy = bbox.GetY() + radialGradElem->GetCy().GetAnimVal().GetValueInSpecifiedUnits() / 100.0f *bbox.GetHeight();
                fx = bbox.GetX() + radialGradElem->GetFx().GetAnimVal().GetValueInSpecifiedUnits() / 100.0f *bbox.GetWidth();
                fy = bbox.GetY() + radialGradElem->GetFy().GetAnimVal().GetValueInSpecifiedUnits() / 100.0f *bbox.GetHeight();
            }
            else
            {
                r = radialGradElem->GetQualifiedR();
                cx = radialGradElem->GetQualifiedCx();
                cy = radialGradElem->GetQualifiedCy();
                fx = radialGradElem->GetQualifiedFx();
                fy = radialGradElem->GetQualifiedFy();
            }

            wxSVGMatrix lg_matrix/* = matrix*/;
            const wxSVGTransformList& transforms = radialGradElem->GetGradientTransform().GetAnimVal();
            for (int i = 0; i < (int)transforms.Count(); i++)
                lg_matrix = lg_matrix.Multiply(transforms[i].GetMatrix());

            float fMatrix[6] = { 0 };
            fMatrix[0] = lg_matrix.GetA();
            fMatrix[1] = lg_matrix.GetB();
            fMatrix[2] = lg_matrix.GetC();
            fMatrix[3] = lg_matrix.GetD();
            fMatrix[4] = lg_matrix.GetE();
            fMatrix[5] = lg_matrix.GetF();
            m_pGraient = Gradient::Create(fx, fy, 0, cx, cy, r, fMatrix);
            break;
        }
        default:
            break;
        }

        int nstopCount = GetGradientStops(svgElem, gradElem, opacity);
        if (nstopCount)
        {
            spaint.setShader(m_pGraient->GetShader());
        }
    }
    else
    {
        spaint.setShader(nullptr);
        wxRGBColor color = paint.GetRGBColor();
        unsigned char nOpacity = opacity ? opacity * 255 : 255;
        spaint.setColor(SkColorSetARGB(nOpacity, color.Red(), color.Green(), color.Blue()));
    }
}

void wxSVGCanvasSkia::DestroyGradient()
{
    if (m_pGraient)
    {
        delete m_pGraient;
        m_pGraient = nullptr;
    }
}

void wxSVGCanvasSkia::ApplyStrokeStyle(const wxCSSStyleDeclaration& style)
{
    /*if (style.GetStrokeDasharray().GetLength() > 0)
    {
        float* dashed = new float[style.GetStrokeDasharray().GetLength()];
        for (int i = 0; i < style.GetStrokeDasharray().GetLength(); i++)
        {
            dashed[i] = static_cast<float>(style.GetStrokeDasharray().Item(i).GetFloatValue());
        }

        ::SetDashPattern(m_hCanvas, dashed, style.GetStrokeDasharray().GetLength());
        delete[] dashed;
    }
    else
    {
        ::SetDashStyle(m_hCanvas, DASH_NONE);
    }*/
}
