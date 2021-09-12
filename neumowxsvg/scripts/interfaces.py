##############################################################################
## Name:        interface.py
## Purpose:     
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: interfaces.py,v 1.45 2016/05/16 21:08:51 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:		some modules adapted from svgl project
##############################################################################

class interface:
    def __init__(self):
        self.exclude_methods = []
        self.exclude_attributes = []
        self.include_methods = []
        self.include_methods_protected = []
        self.include_attributes_str = []
        self.include_attributes = []
        self.include_get_set_attributes = []
        self.custom_parser=0
        self.include_includes = []
        self.include_fwd_decls = []
        self.user_defined_constructor=0
        self.user_defined_destructor=0
        self.has_canvas_item=0

interfaces={}

#SVGElement
inter = interface()
interfaces["SVGElement"]=inter
inter.include_methods.append('''    wxSVGElement(wxString tagName = wxT("")): wxSvgXmlElement(wxSVGXML_ELEMENT_NODE, tagName),
      m_ownerSVGElement(NULL), m_viewportElement(NULL) { }
    virtual ~wxSVGElement() {}
    
    virtual wxSVGElement* GetSvgElement() { return this; }
    virtual wxSVGDTD GetDtd() const = 0;

    virtual void AddProperty(const wxString& name, const wxString& value) { SetAttribute(name, value); }
''')
inter.include_fwd_decls=["SVGSVGElement", "SVGDocument"]
inter.include_includes=["SVGDTD"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGLocatable
inter = interface()
interfaces["SVGLocatable"]=inter
inter.include_methods.append('    virtual wxSVGRect GetBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER) = 0;\n')
inter.include_methods.append('    virtual wxSVGRect GetResultBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER) = 0;\n')
inter.include_methods.append('    virtual wxSVGMatrix GetCTM() = 0;\n')
inter.include_methods.append('    virtual wxSVGMatrix GetScreenCTM() = 0;\n')
inter.include_methods.append('    static wxSVGRect GetElementBBox(const wxSVGElement* element, wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
inter.include_methods.append('    static wxSVGRect GetElementResultBBox(const wxSVGElement* element, wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
inter.include_methods.append('    static wxSVGMatrix GetCTM(const wxSVGElement* element);\n')
inter.include_methods.append('    static wxSVGMatrix GetScreenCTM(const wxSVGElement* element);\n')
inter.include_methods.append('    static wxSVGMatrix GetParentMatrix(const wxSVGElement* element);\n')
inter.include_methods_protected.append('    static wxSVGRect GetChildrenBBox(const wxSVGElement* element, wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
inter.include_methods_protected.append('    static wxSVGRect GetChildrenResultBBox(const wxSVGElement* element, wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
inter.include_methods_protected.append('    inline wxSVGMatrix GetMatrix(wxSVG_COORDINATES coordinates)\n')
inter.include_methods_protected.append('    { return coordinates == wxSVG_COORDINATES_SCREEN ? GetScreenCTM() : (coordinates == wxSVG_COORDINATES_VIEWPORT ? GetCTM() : wxSVGMatrix()); }\n')
inter.exclude_methods = ["GetBBox", "GetResultBBox", "GetCTM", "GetScreenCTM"]
inter.include_includes=["SVGCoordinates"]

# SVGTransformable
inter = interface()
interfaces["SVGTransformable"]=inter
inter.include_methods.append('    void Transform(const wxSVGMatrix& matrix);\n')
inter.include_methods.append('    void Translate(double tx, double ty);\n')
inter.include_methods.append('    void Scale(double s);\n')
inter.include_methods.append('    void Scale(double sx, double sy);\n')
inter.include_methods.append('    void Rotate(double angle, double cx = 0, double cy = 0);\n')
inter.include_methods.append('    void SkewX(double angle);\n')
inter.include_methods.append('    void SkewY(double angle);\n')
inter.include_methods.append('    void UpdateMatrix(wxSVGMatrix& matrix) const;\n')
inter.include_methods.append('    static wxSVGTransformable* GetSVGTransformable(wxSVGElement& element);\n')
inter.include_methods.append('    static const wxSVGTransformable* GetSVGTransformable(const wxSVGElement& element);\n')
inter.include_methods_protected.append('    inline wxSVGAnimatedTransformList& GetTransformList() { return m_transform; }\n')
inter.include_methods_protected.append('    friend class wxSVGAnimateTransformElement;\n')

# SVGFitToViewBox
inter = interface()
interfaces["SVGFitToViewBox"]=inter
inter.include_methods.append('    void UpdateMatrix(wxSVGMatrix& matrix, const wxSVGLength& width, const wxSVGLength& height);\n')
inter.include_includes=["SVGLength", "SVGMatrix"]

# SVGStylable
inter = interface()
interfaces["SVGStylable"]=inter
inter.include_methods.append('    static wxSVGStylable* GetSVGStylable(wxSVGElement& element);\n')
inter.include_methods.append('    static const wxSVGStylable* GetSVGStylable(const wxSVGElement& element);\n')
inter.include_methods.append('    static const wxCSSStyleDeclaration& GetElementStyle(const wxSVGElement& element);\n')
inter.include_methods.append('    static wxCSSStyleDeclaration GetResultStyle(const wxSVGElement& element);\n')
inter.include_get_set_attributes = [["wxCSSStyleDeclaration", "animStyle", False, False]]
inter.include_includes=["SVGElement"]
inter.custom_parser=1

# SVGElementInstance
inter = interface()
interfaces["SVGElementInstance"]=inter
inter.include_methods.append('    wxSVGElementInstance() {}\n')
inter.include_methods.append('    virtual ~wxSVGElementInstance() {}\n')
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# wxSVGColor
inter = interface()
interfaces["SVGColor"]=inter
inter.include_methods.append('    wxSVGColor(): wxCSSValue(wxCSS_SVG_COLOR),\n')
inter.include_methods.append('      m_colorType(wxSVG_COLORTYPE_UNKNOWN) {}\n')
inter.include_methods.append('    wxSVGColor(wxRGBColor color): wxCSSValue(wxCSS_SVG_COLOR),\n')
inter.include_methods.append('      m_colorType(wxSVG_COLORTYPE_RGBCOLOR), m_rgbColor(color) {}\n')
inter.include_methods.append('    wxSVGColor(unsigned char r, unsigned char g, unsigned char b):\n')
inter.include_methods.append('      wxCSSValue(wxCSS_SVG_COLOR),\n')
inter.include_methods.append('      m_colorType(wxSVG_COLORTYPE_RGBCOLOR), m_rgbColor(r, g, b) {}\n')
inter.include_methods.append('    virtual ~wxSVGColor() {}\n')
inter.include_methods.append('    wxCSSValue* Clone() const { return new wxSVGColor(*this); }\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    wxString GetCSSText() const;\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline const wxRGBColor& GetRGBColor() const { return m_rgbColor; }\n')
inter.include_methods.append('    virtual void SetRGBColor(const wxRGBColor& rgbColor);\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline const wxSVGICCColor& GetICCColor() const { return m_iccColor; }\n')
inter.include_methods.append('    virtual void SetICCColor(const wxSVGICCColor& iccColor);\n')
inter.include_methods.append('    \n')
inter.exclude_methods = ["GetRgbColor", "SetRgbColor", "GetIccColor", "SetIccColor"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# wxSVGPaint
inter = interface()
interfaces["SVGPaint"]=inter
inter.include_methods.append('    wxSVGPaint(): m_paintType(wxSVG_PAINTTYPE_NONE)\n')
inter.include_methods.append('     { m_cssValueType = wxCSS_SVG_PAINT; }\n')
inter.include_methods.append('    wxSVGPaint(unsigned char r, unsigned char g, unsigned char b):\n')
inter.include_methods.append('      wxSVGColor(r, g, b), m_paintType(wxSVG_PAINTTYPE_RGBCOLOR)\n')
inter.include_methods.append('      { m_cssValueType = wxCSS_SVG_PAINT; }\n')
inter.include_methods.append('    wxSVGPaint(wxRGBColor color):\n')
inter.include_methods.append('      wxSVGColor(color), m_paintType(wxSVG_PAINTTYPE_RGBCOLOR)\n')
inter.include_methods.append('      { m_cssValueType = wxCSS_SVG_PAINT; if (!color.Ok()) m_paintType = wxSVG_PAINTTYPE_NONE; }\n')
inter.include_methods.append('    virtual ~wxSVGPaint() {}\n')
inter.include_methods.append('    wxCSSValue* Clone() const { return new wxSVGPaint(*this); }\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    wxString GetCSSText() const;\n')
inter.include_methods.append('    inline const wxString& GetUri() const { return m_uri; }\n')
inter.include_methods.append('    virtual void SetUri(const wxString& uri);\n')
inter.include_methods.append('    virtual void SetRGBColor(const wxRGBColor& rgbColor);\n')
inter.include_methods.append('    virtual void SetICCColor(const wxSVGICCColor& iccColor);\n')
inter.include_methods.append('    \n')
inter.include_methods.append('''    inline bool Ok() const
    {
	  return m_paintType != wxSVG_PAINTTYPE_UNKNOWN &&
	         m_paintType != wxSVG_PAINTTYPE_NONE;
	}\n''')
inter.include_methods.append('    \n')
inter.exclude_methods = ["GetUri", "SetUri"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGLength
inter = interface()
interfaces["SVGLength"]=inter
inter.include_methods.append('    wxSVGLength() : m_unitType(wxSVG_LENGTHTYPE_UNKNOWN), m_value(0), m_valueInSpecifiedUnits(0) {}\n')
inter.include_methods.append('    wxSVGLength(double v) : m_unitType(wxSVG_LENGTHTYPE_NUMBER), m_value(v), m_valueInSpecifiedUnits(v) {}\n')
inter.include_methods.append('    wxSVGLength(wxSVG_LENGTHTYPE unitType, double v): m_unitType(unitType) { SetValueInSpecifiedUnits(v); }\n')
inter.include_methods.append('    wxSVGLength(const wxSVGLength& l): m_unitType(l.m_unitType), m_value(l.m_value), m_valueInSpecifiedUnits(l.m_valueInSpecifiedUnits) {}\n')
inter.include_methods.append('    virtual ~wxSVGLength() {}\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline double GetValue() const { return m_value; }\n')
inter.include_methods.append('    inline void SetValue(double n) { m_unitType = wxSVG_LENGTHTYPE_NUMBER; m_valueInSpecifiedUnits = n; m_value = n; }\n')
inter.include_methods.append('    inline operator double() const { return GetValue(); }\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    double GetValueInSpecifiedUnits() const { return m_valueInSpecifiedUnits; }\n')
inter.include_methods.append('    void SetValueInSpecifiedUnits(double n);\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    wxString GetValueAsString() const;\n')
inter.include_methods.append('    void SetValueAsString(const wxString& n);\n')
inter.include_methods.append('''    
    inline void ToViewportWidth(float viewportWidth) { m_value = m_valueInSpecifiedUnits*viewportWidth/100; }
	inline void ToViewportHeight(float viewportHeight) { m_value = m_valueInSpecifiedUnits*viewportHeight/100; }
	inline void ToViewportSize(float viewportWidth, float viewportHeight)
	{
	  m_value = m_valueInSpecifiedUnits*
		sqrt(viewportWidth*viewportWidth + viewportHeight*viewportHeight)/sqrt(2.0)/100;
	}\n
''')
inter.exclude_methods = ["GetValue", "SetValue", "GetValueInSpecifiedUnits", "GetValueInSpecifiedUnits"]
inter.exclude_attributes = ["valueAsString"]
inter.include_includes = ["String_wxsvg", "SVGLengthCalculate", "math"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGAngle
inter = interface()
interfaces["SVGAngle"]=inter
inter.include_methods.append('    wxSVGAngle() : m_unitType(wxSVG_ANGLETYPE_UNKNOWN), m_value(0), m_valueInSpecifiedUnits(0) {}\n')
inter.include_methods.append('    wxSVGAngle(double v) : m_unitType(wxSVG_ANGLETYPE_UNSPECIFIED), m_value(v), m_valueInSpecifiedUnits(0) {}\n')
inter.include_methods.append('    virtual ~wxSVGAngle() {}\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline double GetValue() const { return m_value; }\n')
inter.include_methods.append('    inline void SetValue(double n) { m_unitType = wxSVG_ANGLETYPE_UNSPECIFIED; m_valueInSpecifiedUnits = n; m_value = n; }\n')
inter.include_methods.append('    inline operator double() const { return GetValue(); }\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline double GetValueInSpecifiedUnits() const { return m_valueInSpecifiedUnits; }\n')
inter.include_methods.append('    void SetValueInSpecifiedUnits(double n);\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    wxString GetValueAsString() const;\n')
inter.include_methods.append('    void SetValueAsString(const wxString& n);\n')
inter.include_methods.append('    \n')
inter.exclude_methods = ["GetValue", "SetValue", "GetValueInSpecifiedUnits", "GetValueInSpecifiedUnits"]
inter.exclude_attributes = ["valueAsString"]
inter.include_includes = ["String_wxsvg"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGNumber
inter = interface()
interfaces["SVGNumber"]=inter
inter.include_methods.append('    wxSVGNumber(): m_value(0) {}\n')
inter.include_methods.append('    wxSVGNumber(double value): m_value(value) {}\n')
inter.include_methods.append('    virtual ~wxSVGNumber() {}\n')
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGPoint
inter = interface()
interfaces["SVGPoint"]=inter
inter.include_methods.append('    wxSVGPoint(): m_x(0), m_y(0) {}\n')
inter.include_methods.append('    wxSVGPoint(double x, double y): m_x(x), m_y(y) {}\n')
inter.include_methods.append('    virtual ~wxSVGPoint() {}\n')
inter.include_methods.append('    virtual wxSVGPoint MatrixTransform(const wxSVGMatrix& matrix) const;\n')
inter.include_methods.append('    inline bool operator==(const wxSVGPoint& p) const { return m_x == p.m_x && m_y == p.m_y; }\n')
inter.include_methods.append('    inline bool operator!=(const wxSVGPoint& p) const { return m_x != p.m_x || m_y != p.m_y; }\n')
inter.include_methods.append('    inline wxSVGPoint operator-(const wxSVGPoint& p) const { return wxSVGPoint(m_x - p.m_x,  m_y - p.m_y); }\n')
inter.include_methods.append('    inline wxSVGPoint operator+(const wxSVGPoint& p) const { return wxSVGPoint(m_x + p.m_x,  m_y + p.m_y); }\n')
inter.include_methods.append('    inline wxSVGPoint operator*(double n) const { return wxSVGPoint(m_x*n,  m_y*n); }\n')
inter.exclude_methods = ["MatrixTransform"]
inter.include_includes = ["SVGMatrix"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGRect
inter = interface()
interfaces["SVGRect"]=inter
inter.exclude_attributes = ['x', 'y', 'width', 'height']
inter.include_attributes = [["x", "double", "0"], ["y", "double", "0"],\
 ["width", "double", "0"], ["height", "double", "0"], ["empty", "bool", "true"]]
inter.include_attributes_str.append('''\
    inline double GetX() const { return m_x; }
    inline void SetX(double n) { m_x = n; m_empty = false; }

    inline double GetY() const { return m_y; }
    inline void SetY(double n) { m_y = n; m_empty = false; }

    inline double GetWidth() const { return m_width; }
    inline void SetWidth(double n) { m_width = n; m_empty = false; }

    inline double GetHeight() const { return m_height; }
    inline void SetHeight(double n) { m_height = n; m_empty = false; }
    
    inline bool IsEmpty() const { return m_empty; }
    inline void Clear() { m_x = m_y = m_width = m_height = 0; m_empty = true; }\n
''')
inter.include_methods.append('''\
    wxSVGRect(double x, double y, double width, double height):
      m_x(x), m_y(y), m_width(width), m_height(height), m_empty(false) {}
    ~wxSVGRect() {}
    wxString GetValueAsString() const;
    void SetValueAsString(const wxString& value);
    wxSVGRect MatrixTransform(const wxSVGMatrix& matrix) const;
''')
inter.include_includes = ["String_wxsvg", "SVGMatrix"]
inter.user_defined_constructor=0
inter.user_defined_destructor=1

# SVGMatrix
inter = interface()
interfaces["SVGMatrix"]=inter
inter.include_methods.append('    wxSVGMatrix(): m_a(1), m_b(0), m_c(0), m_d(1), m_e(0), m_f(0) {}\n')
inter.include_methods.append('    wxSVGMatrix(double a, double b, double c, double d, double e, double f):\n      m_a(a), m_b(b), m_c(c), m_d(d), m_e(e), m_f(f) {}\n')
inter.include_methods.append('    virtual ~wxSVGMatrix() {}\n')
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGTransform
inter = interface()
interfaces["SVGTransform"]=inter
inter.include_get_set_attributes = [["double", "cx", False, False], ["double", "cy", False, False]]
inter.include_methods.append('    wxSVGTransform(const wxSVGMatrix& matrix): m_type(wxSVG_TRANSFORM_MATRIX), m_matrix(matrix), m_angle(0), m_cx(0), m_cy(0) {}\n')
inter.include_methods.append('    virtual ~wxSVGTransform() {}\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    wxString GetValueAsString() const;\n')
inter.include_methods.append('    void SetValueAsString(const wxString& value);\n')
inter.include_methods.append('    \n')
inter.include_methods.append('    inline void SetMatrix(const wxSVGMatrix& n) { m_type = wxSVG_TRANSFORM_MATRIX; m_matrix = n; }\n')
inter.exclude_methods = ["SetMatrix", "GetCx", "SetCx", "GetCy", "SetCy"]
inter.include_includes = ["String_wxsvg"]
inter.user_defined_destructor=1

# SVGPreserveAspectRatio
inter = interface()
interfaces["SVGPreserveAspectRatio"]=inter
inter.include_methods.append('''    wxSVGPreserveAspectRatio():
      m_align(wxSVG_PRESERVEASPECTRATIO_UNKNOWN), m_meetOrSlice(wxSVG_MEETORSLICE_UNKNOWN) {}
    wxString GetValueAsString() const;
    void SetValueAsString(const wxString& value);
''')
inter.include_includes = ["String_wxsvg"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

# SVGPathSeg
inter = interface()
interfaces["SVGPathSeg"]=inter
inter.include_methods.append('    wxSVGPathSeg(wxPATHSEG type = wxPATHSEG_UNKNOWN) { m_pathSegType = type; }\n')
inter.include_methods.append('    virtual ~wxSVGPathSeg() {}\n')
inter.user_defined_constructor=1
inter.user_defined_destructor=1

## container elements
for name in ["SVGSVGElement", "SVGGElement", "SVGDefsElement", "SVGAElement",
"SVGSwitchElement", "SVGForeignObjectElement", "SVGClipPathElement"]:
  inter = interface()
  interfaces[name] = inter
  inter.include_methods.append('    wxSVGRect GetBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER) { return wxSVGLocatable::GetChildrenBBox(this, coordinates); }\n')
  inter.include_methods.append('    wxSVGRect GetResultBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER) { return wxSVGLocatable::GetChildrenResultBBox(this, coordinates); }\n')
  inter.include_methods.append('    wxSVGMatrix GetCTM() { return wxSVGLocatable::GetCTM(this); }\n')
  inter.include_methods.append('    wxSVGMatrix GetScreenCTM() { return wxSVGLocatable::GetScreenCTM(this); }\n')

## SVGSVGElement
inter = interfaces["SVGSVGElement"]
inter.include_methods.append('    wxSvgXmlElement* GetElementById(const wxString& elementId) const;\n')
inter.include_methods.append('    void UpdateMatrix(wxSVGMatrix& matrix) { wxSVGFitToViewBox::UpdateMatrix(matrix, GetWidth().GetAnimVal(), GetHeight().GetAnimVal()); }\n')
inter.exclude_methods = ["GetElementById"]

## visible elements
for name in ["SVGLineElement", "SVGPolylineElement", "SVGPolygonElement",
"SVGRectElement", "SVGCircleElement", "SVGEllipseElement", "SVGPathElement",
"SVGTextElement", "SVGImageElement", "SVGVideoElement", "SVGUseElement"]:
    inter = interface()
    interfaces[name]=inter
    if name not in ["SVGUseElement"]:
        inter.has_canvas_item=1
        inter.include_attributes_str.append('  public:\n')
        inter.include_attributes_str.append('    inline wxSVGCanvasItem* GetCanvasItem() { return m_canvasItem; }\n')
        inter.include_attributes_str.append('    void SetCanvasItem(wxSVGCanvasItem* canvasItem);\n\n')
        inter.include_attributes = [["canvasItem", "wxSVGCanvasItem*", "NULL"]]
        inter.include_fwd_decls = ["SVGCanvasItem"]
    inter.include_methods.append('    wxSVGRect GetBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
    inter.include_methods.append('    wxSVGRect GetResultBBox(wxSVG_COORDINATES coordinates = wxSVG_COORDINATES_USER);\n')
    inter.include_methods.append('    wxSVGMatrix GetCTM() { return wxSVGLocatable::GetCTM(this); }\n')
    inter.include_methods.append('    wxSVGMatrix GetScreenCTM() { return wxSVGLocatable::GetScreenCTM(this); }\n')

# SVGImageElement
inter = interfaces["SVGImageElement"]
inter.include_fwd_decls.append("ProgressDialog")
inter.include_methods.append('    int GetDefaultWidth(wxProgressDialog* progressDlg = NULL);\n')
inter.include_methods.append('    int GetDefaultHeight(wxProgressDialog* progressDlg = NULL);\n')
inter.include_methods.append('    void SetDefaultSize(wxProgressDialog* progressDlg = NULL);\n')
#inter.custom_parser=1

# SVGVideElement
inter = interfaces["SVGVideoElement"]
inter.include_fwd_decls.append("ProgressDialog")
inter.include_methods.append('    double GetDuration(wxProgressDialog* progressDlg = NULL);\n')

# SVGTextElement
inter = interfaces["SVGTextElement"]
inter.include_methods.append('''\
    long GetNumberOfChars();
    double GetComputedTextLength();
    double GetSubStringLength(unsigned long charnum, unsigned long nchars);
    wxSVGPoint GetStartPositionOfChar(unsigned long charnum);
    wxSVGPoint GetEndPositionOfChar(unsigned long charnum);
    wxSVGRect GetExtentOfChar(unsigned long charnum);
    double GetRotationOfChar(unsigned long charnum);
    long GetCharNumAtPosition(const wxSVGPoint& point);
''')

# SVGTextPositioningElement
inter = interface()
interfaces["SVGTextPositioningElement"]=inter
inter.include_methods.append('    inline void SetX(const wxSVGLength& n) { wxSVGLengthList list; list.Add(n); SetX(list); }\n')
inter.include_methods.append('    inline void SetY(const wxSVGLength& n) { wxSVGLengthList list; list.Add(n); SetY(list); }\n')

# SVGRadialGradientElement
inter = interface()
interfaces["SVGRadialGradientElement"]=inter
inter.include_methods.append('''
    double GetQualifiedR() const;
    double GetQualifiedCx() const;
    double GetQualifiedCy() const;
    double GetQualifiedFx() const;
    double GetQualifiedFy() const;\n
''')

# SVGFEGaussianBlurElement
inter = interface()
interfaces["SVGFEGaussianBlurElement"]=inter
inter.custom_parser=1

# SVGAnimationElement
inter = interface()
interfaces["SVGAnimationElement"]=inter
inter.include_methods.append('''
    virtual void ApplyAnimation();
''')
inter.exclude_attributes = ["targetElement"]
inter.include_attributes = [["repeatCount", "int", "1"], ["values", "wxSVGStringList", ""]]
inter.include_methods.append('''    wxSVGElement* GetTargetElement() const;
''')
inter.custom_parser=1

# SVGAnimateMotionElement
inter = interface()
interfaces["SVGAnimateMotionElement"]=inter
inter.include_methods.append('''    virtual void ApplyAnimation();
''')

# SVGAnimateTransformElement
inter = interface()
interfaces["SVGAnimateTransformElement"]=inter
inter.include_attributes = [["transformIdx", "int", "-1"]]
inter.include_methods.append('''
    virtual void ApplyAnimation();\n
''')

# SVGDocument
inter = interface()
interfaces["SVGDocument"]=inter

inter.exclude_attributes = ["rootElement", "title"]
inter.include_attributes_str.append('''  protected:
    wxSVGCanvas* m_canvas;
    double m_scale;
    double m_scaleY;\n
    wxSVGMatrix m_screenCTM;\n
    double m_time;
    double GetDuration(wxSVGElement* parent);
''')
inter.include_methods.append('''    wxSVGDocument() { Init(); }
    wxSVGDocument(const wxString& filename, const wxString& encoding = wxT("UTF-8")):
      wxSvgXmlDocument(filename, encoding) { Init(); }
    wxSVGDocument(wxInputStream& stream, const wxString& encoding = wxT("UTF-8")):
      wxSvgXmlDocument(stream, encoding) { Init(); }
    wxSVGDocument(const wxSVGDocument& doc);
    virtual ~wxSVGDocument();
    
    virtual bool Load(const wxString& filename, const wxString& encoding = wxT("UTF-8"));
    virtual bool Load(wxInputStream& stream, const wxString& encoding = wxT("UTF-8"));

    void Init();
    inline wxSVGCanvas* GetCanvas() { return m_canvas; }
    inline double GetScale() { return m_scale; }
    inline double GetScaleX() { return m_scale; }
    inline double GetScaleY() { return m_scaleY > 0 ? m_scaleY : m_scale; }
    const wxSVGMatrix& GetScreenCTM() { return m_screenCTM; }
    
    wxString GetTitle();
    void SetTitle(const wxString& n);
    
    wxSVGSVGElement* GetRootElement() { return (wxSVGSVGElement*) GetRoot(); }
    void SetRootElement(wxSVGSVGElement* n) { SetRoot((wxSvgXmlElement*) n); }
    
    wxSVGElement* GetElementById(const wxString& id);
    
    wxSvgXmlElement* CreateElement(const wxString& tagName);
    wxSvgXmlElement* CreateElementNS(const wxString& namespaceURI, const wxString& qualifiedName);
    
    double GetDuration();
    double GetCurrentTime() { return m_time; }
    void SetCurrentTime(double seconds);
    
    /** Renders SVG to bitmap image */
    wxImage Render(int width = -1, int height = -1, const wxSVGRect* rect = NULL, bool preserveAspectRatio = true,
		bool alpha = false, wxProgressDialog* progressDlg = NULL);
    
    static void ApplyAnimation(wxSVGElement* parent, wxSVGSVGElement* ownerSVGElement);
  private:
      DECLARE_DYNAMIC_CLASS(wxSVGDocument)
''')
inter.include_fwd_decls = ["SVGSVGElement","SVGElement","SVGCanvas","ProgressDialog"]
inter.include_includes = ["SVGRect","SVGMatrix","<wx/image.h>"]
inter.user_defined_constructor=1
inter.user_defined_destructor=1

