##############################################################################
## Name:        genCSS.py
## Purpose:     generates CSSStyleDeclaration
## Author:      Alex Thuering
## Created:     2005/06/06
## RCS-ID:      $Id: genCSS.py,v 1.18 2014/06/30 19:10:55 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
##############################################################################

import conf
import string
import cpp
import cppHeader
import cppImpl
import sys
import xml.dom.minidom
from xml.dom.minidom import Node

def generate():
    if len(cssProperties) == 0:
        parseCSSProps()
    genCSSStyleDeclaration()
    genStyles()
    genValues()

####################### parseCSSProps ##############################
cssProperties = []
class Property:
    def __init__(self, dtdName):
        self.dtdName = dtdName
        self.cssType = ''
        self.valueType = ''
        self.defValue = ''
        self.values = []
    def __str__(self):
        return self.dtdName + "," + self.cssType + "," + self.valueType + "," + self.defValue

# loads SVG11CSSpropidx.xhtml and fills cssProperties list
def parseCSSProps():
    doc = xml.dom.minidom.parse(conf.share_dir + "/SVG11CSSpropidx.xhtml")
    tbody = doc.getElementsByTagName('html')[0].getElementsByTagName('body')[0].getElementsByTagName('table')[0].getElementsByTagName('tbody')[0]
    for tr in tbody.childNodes:
        if tr.nodeName == "tr":
            for propNode in tr.getElementsByTagName('td')[0].getElementsByTagName('a'):
                propName = propNode.childNodes[0].childNodes[0].nodeValue
                propName = propName[1:-1]
                if propName == "marker":
                    #tr.getElementsByTagName('td')[1].childNodes[0].nodeValue = "none | inherit | <uri>"
                    #tr.getElementsByTagName('td')[2].childNodes[0].nodeValue = "none"
                    continue
                if propName == "font":
                    #tr.getElementsByTagName('td')[1].childNodes[0].nodeValue = ""
                    #tr.getElementsByTagName('td')[2].childNodes[0].nodeValue = ""
                    continue
                propDefValue = tr.getElementsByTagName('td')[2].childNodes[0].nodeValue
                ## make propValuesStr
                propValuesStr = ''
                propTypes = []
                for child in tr.getElementsByTagName('td')[1].childNodes:
                    if child.nodeName == "#text":
                        propValuesStr = propValuesStr + child.nodeValue
                    elif child.nodeName == "a":
                        if child.getElementsByTagName('span').length > 0:
                            val = child.getElementsByTagName('span')[0].childNodes[0].nodeValue
                            if val[0] == "<":
                                propTypes.append(val[1:-1])
                            elif val[0] != "'" and val != "inherit":
                                propValuesStr = propValuesStr + val
                        else:
                            val = child.childNodes[0].nodeValue
                            if val[0] == "<":
                                propTypes.append(val[1:-1])
                ## delete (...)
                while 1:
                    beg = propValuesStr.find("(")
                    end = propValuesStr.find(")")
                    if beg != -1 and end != -1 and beg < end:
                        propValuesStr = propValuesStr[:beg] + propValuesStr[end+1:]
                    else:
                        break
                ## delete [...], but not at the begin of string and not after "|"
                beg = propValuesStr.find("[")
                end = propValuesStr.rfind("]")
                if beg > 1 and end != -1 and beg < end and propValuesStr[beg-2:beg] != "| ":
                    propValuesStr = propValuesStr[:beg] + propValuesStr[end+1:]
                ## delete all other non-alphanum characters
                tmp = ''
                for c in propValuesStr:
                    if c in string.ascii_letters or c in string.digits or c in "|-_<>":
                        tmp = tmp + c
                propValuesStr = tmp
                # create property and append it to the list
                prop = Property(propName)
                for val in propValuesStr.split('|'):
                    if len(val) == 0:
                        continue
                    elif val[0] == "<":
                        propTypes.append(val[1:-1])
                    elif val not in prop.values:
                        prop.values.append(val)
                setCSSType(prop, propTypes)
                setDefValue(prop, propDefValue)
                cssProperties.append(prop)

def setCSSType(prop, propTypes):
    prop.cssType = 'wxCSSPrimitiveValue'
    prop.valueType = 'wxCSS_VALUE'
    if 'color' in propTypes:
        if 'currentColor' in prop.values:
            prop.cssType = 'wxSVGColor'
            prop.valueType = 'wxSVGColor'
        else:
            prop.valueType = 'wxRGBColor'
    elif 'paint' in propTypes:
        prop.cssType = 'wxSVGPaint'
        prop.valueType = 'wxSVGPaint'
    elif 'dasharray' in propTypes:
        prop.cssType = 'wxCSSValueList'
        prop.valueType = 'wxCSSValueList'
    elif 'opacity-value' in propTypes or 'miterlimit' in propTypes :
        prop.valueType = 'double'
    elif ('length' in propTypes or 'angle' in propTypes) and len(prop.values) == 0:
        prop.valueType = 'double'
    elif 'family-name' in propTypes:
        prop.valueType = 'wxString'
    elif len(propTypes):
        prop.valueType = 'wxCSSPrimitiveValue'

def setDefValue(prop, defValue):
    ## fix some broken defs
    if defValue == "see property description" or defValue == "see prose":
        defValue = "auto"
    ## defValue
    if prop.valueType == 'wxRGBColor':
        defValue = "wxRGBColor()"
    elif prop.valueType == 'wxSVGPaint':
        if defValue == "none":
            defValue = '*s_emptySVGPaint'
        else:
            defValue = '*s_blackSVGPaint'
    elif prop.valueType == 'wxSVGColor':
        defValue = '*s_emptySVGColor'
    elif prop.valueType == 'wxCSSValueList':
        defValue = '*s_emptyValueList'
    elif prop.valueType == 'wxCSSPrimitiveValue':
        defValue = '*s_emptyCSSValue'
    elif prop.valueType == 'wxString':
        defValue = 'wxT("")'
    elif prop.valueType == 'wxCSS_VALUE':
        defValue = valueId(defValue)
    elif prop.valueType == 'double' and defValue == 'medium':
        defValue = '20'
    elif prop.valueType == 'double' and defValue == '0deg':
        defValue = '0'
    prop.defValue = defValue
    
######################### CSSStyleDeclaration.h ##############################
def genCSSStyleDeclaration():
    enum = ''
    for prop in cssProperties:
        if (len(enum)):
            enum = enum + ',\n'
        enum = enum + '  ' + propId(prop.dtdName)
    
    methods = ''
    for prop in cssProperties:
        methodName = makeMethodName(prop.dtdName)
        valueType = prop.valueType
        get = '((%s&)*it->second)'%prop.cssType
        functionName = getFunctionName(prop.valueType)
        if len(functionName):
            get = get + '.Get' + functionName + '()'
        else:
            valueType = "const " + valueType + "&"
        get = '''\
    inline %s Get%s() const
    {
      const_iterator it = find(%s);
      return it != end() ? %s : %s;
    }
    '''%(valueType, methodName, propId(prop.dtdName), get, prop.defValue)
        
        has = 'inline bool Has%s() const { return HasProperty(%s); }\n'%(methodName, propId(prop.dtdName))
        
        if len(functionName):
            valueType = prop.valueType
            if valueType not in cpp.builtin_types and valueType != "wxCSS_VALUE":
                valueType = "const " + valueType + "&"
            ptype = ""
            if prop.valueType == "wxString":
                ptype = "wxCSS_STRING, "
            elif prop.valueType == "double":
                ptype = "wxCSS_NUMBER, "
            set = '''\
    inline void Set%s(%s value)
    {
      iterator it = find(%s);
      if (it != end())
        ((%s*)it->second)->Set%s(%svalue);
      else
        (*this)[%s] = new %s(value);
    }
    '''%(methodName, valueType, propId(prop.dtdName), prop.cssType, functionName, ptype, propId(prop.dtdName), prop.cssType)
        else:
            set = '''\
    inline void Set%s(const %s& value)
    {
      iterator it = find(%s);
      if (it != end())
      {
        delete it->second;
        it->second = new %s(value);
      }
      else
        (*this)[%s] = new %s(value);
    }
    '''%(methodName, prop.valueType, propId(prop.dtdName), prop.valueType, propId(prop.dtdName), prop.valueType)
            
        
        if len(methods):
            methods = methods + '\n\n';
        methods = methods + get + has + set;
        
    output = '''
#include "CSSValue.h"
#include "SVGPaint.h"
#include "SVGAnimatedType.h"
#include <wx/hashmap.h>

enum wxCSS_PROPERTY
{
  wxCSS_PROPERTY_UNKNOWN,
%s
};

WX_DECLARE_HASH_MAP(wxCSS_PROPERTY, wxCSSValue*, wxIntegerHash, wxIntegerEqual, wxHashMapCSSValue);

typedef wxString wxCSSStyler;

class wxCSSStyleDeclaration: public wxHashMapCSSValue
{
  public:
    wxCSSStyleDeclaration() {}
    wxCSSStyleDeclaration(const wxCSSStyleDeclaration& src) { Add(src); }
    ~wxCSSStyleDeclaration();
    wxCSSStyleDeclaration& operator=(const wxCSSStyleDeclaration& src);
    void Add(const wxCSSStyleDeclaration& style);

  public:
    wxString GetCSSText() const;
    void SetCSSText(const wxString& text);
    
    inline wxString GetPropertyValue(const wxString& propertyName) const
    { return GetPropertyValue(GetPropertyId(propertyName)); }
    
    inline const wxCSSValue& GetPropertyCSSValue(const wxString& propertyName) const
    { return GetPropertyCSSValue(GetPropertyId(propertyName)); }
    
    void SetProperty(const wxString& propertyName, const wxString& value)
    { SetProperty(GetPropertyId(propertyName), value); }
    
    void SetProperty(const wxString& propertyName, const wxSVGAnimatedType& value)
    { SetProperty(GetPropertyId(propertyName), value); }
    
    inline bool HasProperty(const wxString& propertyName) const
    { return HasProperty(GetPropertyId(propertyName)); }
    
    inline wxString RemoveProperty(const wxString& propertyName)
    { return RemoveProperty(GetPropertyId(propertyName)); }
  
  public:
    inline wxString GetPropertyValue(wxCSS_PROPERTY propertyId) const
    { const_iterator it = find(propertyId); if (it != end()) return it->second->GetCSSText(); return wxT(""); }
    
    inline const wxCSSValue& GetPropertyCSSValue(wxCSS_PROPERTY propertyId) const
    { const_iterator it = find(propertyId); if (it != end()) return *it->second; return *s_emptyCSSValue; }
    
    void SetProperty(wxCSS_PROPERTY propertyId, const wxString& value);
    void SetProperty(wxCSS_PROPERTY propertyId, const wxSVGAnimatedType& value);
    inline bool HasProperty(wxCSS_PROPERTY propertyId) const { return find(propertyId) != end(); }
    inline wxString RemoveProperty(wxCSS_PROPERTY propertyId) { erase(propertyId); return wxT(""); }
    
    static wxCSS_PROPERTY GetPropertyId(const wxString& propertyName);
    static wxString GetPropertyName(wxCSS_PROPERTY propertyId);
  
  public:
%s
  
  protected:
    static wxCSSPrimitiveValue* s_emptyCSSValue;
    static wxSVGColor* s_emptySVGColor;
    static wxSVGPaint* s_emptySVGPaint;
    static wxSVGPaint* s_blackSVGPaint;
    static wxCSSValueList* s_emptyValueList;

  public:
    static double ParseNumber(const wxString& value);
    static wxRGBColor ParseColor(const wxString& value);
    static void ParseSVGPaint(wxSVGPaint& paint, const wxString& value);
};

/* this class copy only references of css values */
class wxCSSStyleRef: public wxCSSStyleDeclaration
{
  public:
    wxCSSStyleRef() {}
    wxCSSStyleRef(const wxCSSStyleDeclaration& src) { Add(src); }
    ~wxCSSStyleRef();
    void Add(const wxCSSStyleDeclaration& style);
};
'''%(enum, methods) 
    
    header = cppHeader.Header("CSSStyleDeclaration", "genCSS.py")
    header.add_content(output)
    header.dump(path=conf.include_dir)

def getFunctionName(valueType):
    if valueType == 'wxCSS_VALUE':
        return 'IdentValue'
    elif valueType == 'wxRGBColor':
        return 'RGBColorValue'
    elif valueType == 'double':
        return 'FloatValue'
    elif valueType == 'wxString':
        return 'StringValue'
    elif valueType == 'wxCSS_VALUE':
        return 'IdentValue'
    return ''

def propId(name):
    return 'wxCSS_PROPERTY_' + cpp.make_name(name).upper()

def valueId(name):
    return 'wxCSS_VALUE_' + cpp.make_name(name).upper()

def makeMethodName(dtdName):
    methodName = dtdName[0].upper() + dtdName[1:]
    while methodName.find('-') != -1:
        pos = methodName.find('-')
        methodName = methodName[0:pos] + methodName[pos+1].upper() + methodName[pos+2:]
    return methodName

####################### CSSStyleDeclaration_styles.h #########################
def genStyles():
    properties = ''
    for prop in cssProperties:
        if len(properties):
            properties = properties + ',\n'
        properties = properties + '  wxT("%s")'%prop.dtdName
    
    output = '''
wxString s_cssPropertyStrings[] = 
{
%s
};
    '''%properties
    
    impl = cppImpl.Impl("css_properties", "genCSS.py")
    impl.add_content(output)
    impl.dump(path=conf.src_dir)

############################### CSSValues.h ##################################
def genValues():
    cssValues = []
    for prop in cssProperties:
        for val in prop.values:
            if val not in cssValues:
                cssValues.append(val)
    cssValues.sort()
    
    values = ''
    for value in cssValues:
        if len(values):
            values = values + ',\n'
        values = values + '  %s'%valueId(value)
    
    output = '''\
enum wxCSS_VALUE
{
  wxCSS_VALUE_UNKNOWN,
%s
};'''%values
    
    header = cppHeader.Header("CSSValues", "genCSS.py")
    header.add_content(output)
    header.dump(path=conf.include_dir)
    
    values = ''
    for value in cssValues:
        if len(values):
            values = values + ',\n'
        values = values + '  wxT("%s")'%value
    
    output = '''\
wxString s_cssValueStrings[] =
{
%s
};'''%values
    impl = cppImpl.Impl("css_values", "genCSS.py")
    impl.add_content(output)
    impl.dump(path=conf.src_dir)

