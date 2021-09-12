##############################################################################
## Name:        genAnimated.py
## Purpose:     generates all SVGAnimated*.h
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: genAnimated.py,v 1.9 2014/03/27 08:38:04 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

import conf
import cpp
import cppHeader

def getBaseType(name):
    if name=="Enumeration":
        return "unsigned char"
    elif name=="Boolean":
        return "bool"
    elif name=="Integer":
        return "long"
    elif name=="Number":
        return "float"
    typename = name
    if typename != "String":
        typename = "SVG" + typename
    return cpp.fix_typename(typename)


def generate(name):
    typename = getBaseType(name)
    include = ''
    if name == "String":
        include = '#include "String_wxsvg.h"\n'
    elif typename not in cpp.builtin_types:
        include = '#include "SVG%s.h"\n'%name
    
    output = ''
    if typename in cpp.number_types:
        output = '''%s
class wxSVGAnimated%s
{
  public:
    wxSVGAnimated%s(): m_baseVal(0), m_animVal(0) {}
	wxSVGAnimated%s(%s value): m_baseVal(value), m_animVal(value) {}
	
    inline %s GetBaseVal() const { return m_baseVal; };
	inline void SetBaseVal(%s value) { m_baseVal = m_animVal = value; }
	
    inline %s GetAnimVal() const { return m_animVal; }
	inline void SetAnimVal(%s value) { m_animVal = value; }
    
  public:
    inline operator %s() const { return GetAnimVal(); }
    
  protected:
    %s m_baseVal;
    %s m_animVal;
};
'''%(include,name,name,name,typename,typename,typename,typename,typename,typename,typename,typename)
    else:
        output = '''%s
class wxSVGAnimated%s
{
  public:
    wxSVGAnimated%s(): m_animVal(NULL) {}
    wxSVGAnimated%s(const %s& value): m_baseVal(value), m_animVal(NULL) {}
    wxSVGAnimated%s(const wxSVGAnimated%s& value): m_baseVal(value.m_baseVal), m_animVal(NULL)
    { if (value.m_animVal != NULL) m_animVal = new %s(*value.m_animVal); }
    ~wxSVGAnimated%s() { ResetAnimVal(); }
    
    inline wxSVGAnimated%s& operator=(const wxSVGAnimated%s& value)
    { m_baseVal = value.m_baseVal; m_animVal = value.m_animVal != NULL ? new %s(*value.m_animVal) : NULL; return *this; }
    
    inline %s& GetBaseVal() { return m_baseVal; }
    inline const %s& GetBaseVal() const { return m_baseVal; }
    inline void SetBaseVal(const %s& value) { m_baseVal = value; ResetAnimVal(); }
    
    inline %s& GetAnimVal()
    {
      if (!m_animVal)
        m_animVal = new %s(m_baseVal);
      return *m_animVal;
    }
    inline const %s& GetAnimVal() const
    {
        return m_animVal ? *m_animVal : m_baseVal;
    }
    inline void SetAnimVal(const %s& value)
    {
      if (!m_animVal)
        m_animVal = new %s(value);
      else
        *m_animVal = value;
    }
    inline void ResetAnimVal()
    {
      if (m_animVal)
      {
        delete m_animVal;
        m_animVal = NULL;
      }
    }
    
  public:
    inline operator const %s&() const { return GetAnimVal(); }
    
  protected:
    %s m_baseVal;
    %s* m_animVal;
};
'''%(include,name,name,name,typename,name,name,typename,name,name,name,typename,\
     typename,typename,typename,typename,\
     typename,typename,typename,typename,typename,typename,typename)
    
    header = cppHeader.Header("SVGAnimated%s"%name, "genAnimated.py")
    header.add_content(output)
    header.dump(path=conf.include_dir)

