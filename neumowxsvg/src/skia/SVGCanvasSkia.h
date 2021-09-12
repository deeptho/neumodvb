#pragma once
#include "SVGCanvas.h"
#include "SVGCanvasPathSkia.h"
#include "SkCanvas.h"
#include "Gradient.h"
class wxSVGCanvasSkia : public wxSVGCanvas {
public:
    wxSVGCanvasSkia();
    virtual ~wxSVGCanvasSkia();

    void Init(int width, int height, bool alpha = false);
    int GetWidth();
    int GetHeight();
    wxImage GetImage();
   	unsigned char* GetBuffer();
    void Clear(wxRGBColor color = wxRGBColor(0xFF, 0xFF, 0xFF));

    wxSVGCanvasPath* CreateCanvasPath(wxSVGMatrix* matrix = NULL);
    wxSVGCanvasItem* CreateItem(wxSVGTextElement* element, const wxCSSStyleDeclaration* style = NULL,
        wxSVGMatrix* matrix = NULL);
    wxSVGCanvasItem* CreateItem(wxSVGImageElement* element, const wxCSSStyleDeclaration* style = NULL,
        wxProgressDialog* progressDlg = NULL);
    wxSVGCanvasItem* CreateItem(wxSVGVideoElement* element, const wxCSSStyleDeclaration* style = NULL,
        wxProgressDialog* progressDlg = NULL);

    void DrawItem(wxSVGCanvasItem& item, wxSVGMatrix& matrix, const wxCSSStyleDeclaration& style,
        wxSVGSVGElement& svgElem) override;

protected:
    void SetStopValue(unsigned int index, float offset, float opacity, const wxRGBColor& rgbColor);
    void AllocateGradientStops(unsigned int stop_count);

private:
    void DrawCanvasPath(wxSVGCanvasPathSkia& canvasPath, wxSVGMatrix& matrix, const wxCSSStyleDeclaration& style,
        wxSVGSVGElement& svgElem);
    void SetPaint(const wxSVGPaint & paint, SkPaint& spaint, float opacity, wxSVGCanvasPathSkia& canvasPath,
        wxSVGSVGElement& svgElem, const wxSVGMatrix& matrix);
    void DestroyGradient();
    void ApplyStrokeStyle(const wxCSSStyleDeclaration& style);

private:
    Gradient*   m_pGraient;
    SkCanvas*   m_pCanvas;
    SkBitmap*   m_pBitmap;
    int         m_nWidth;
    int         m_nHeight;
    bool        m_bAlpha;
};
