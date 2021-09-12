##############################################################################
## Name:        genGetAttributes.py
## Purpose:     generates Elements_GetAttributes.cpp
##              -> GetAttributes() methods for all svg elements
## Author:      Alex Thuering
## Created:     2005/09/27
## RCS-ID:      $Id: genGetAttributes.py,v 1.14 2016/01/24 16:58:49 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:		some modules adapted from svgl project
##############################################################################

import parse_idl
import mapDtdIdl
import string
import conf
import genFile
import cpp
import cppImpl
import enum_map
import interfaces

includes = ["String_wxsvg"]
already_done={}
output_cpps = {}

def process(classdecl):
    if classdecl.name in already_done.keys():
        return already_done[classdecl.name];
    
    already_done[classdecl.name] = 0
    nattr=0
    func_body = ''

    for attr in classdecl.attributes:
        try:
            entity_name = mapDtdIdl.attributes_idl_dtd[attr]
        except KeyError:
            #print classdecl.name, attr ###### TODO
            #print classdecl
            #print mapDtdIdl.attributes_idl_dtd
            #raise ""
            continue

        if nattr == 0:
            includes.append(classdecl.name)
        nattr = nattr + 1
        
        get_attr = cpp.make_attr_name(attr.name)
        typestr =attr.type.name
        anim_pos = typestr.find('Animated')
        if anim_pos>=0 and typestr != "SVGAnimatedType": # SVGAnimatedTypename
            typestr = typestr[anim_pos+len('Animated'):]
            get_attr = get_attr + '.GetBaseVal()'
        
        #print classdecl.name + '::' + attr.name, typestr
        check = ''
        if typestr in ["String", "DOMString"]:
            check = '!%s.IsEmpty()'%get_attr
        elif typestr in ["Integer", "Boolean", "Enumeration", "unsigned short"]:
            etype = ''
            if typestr == "Integer":
                etype = '(long int) '
            elif typestr == "Boolean":
                check = get_attr
                etype = '(bool) '
            elif typestr == "Enumeration":
                check = '%s != 0'%get_attr
                etype = '(char) '
            elif typestr == "unsigned short":
                if classdecl.name == "SVGZoomAndPan":
                    check = '%s != wxSVG_ZOOMANDPAN_UNKNOWN'%get_attr
                    etype = '(wxSVG_ZOOMANDPAN) '
                elif classdecl.name == "SVGColorProfileElement":
                    etype = '(wxRENDERING_INTENT) '
            if classdecl.name == "SVGGradientElement" and attr.name == "gradientUnits":
                check = '%s != wxSVG_UNIT_TYPE_UNKNOWN && %s != wxSVG_UNIT_TYPE_OBJECTBOUNDINGBOX'%(get_attr,get_attr)
                get_attr = 'wxT("userSpaceOnUse")'
            elif (classdecl.name + '::' + attr.name) in enum_map.enum_map:
                def_enum = ''
                for enum in classdecl.enums:
                    if enum.name == enum_map.enum_map[classdecl.name + '::' + attr.name] and len(enum.const_decls):
                        def_enum = cpp.fix_typename('%s'%enum.const_decls[0].name)
                if (len(def_enum)):
                    check = '%s != %s'%(get_attr,def_enum)
                    get_attr = 'GetAttribute(wxT("%s"))'%attr.name
                else:
                    get_attr = etype + get_attr
                    get_attr = 'wxString::Format(wxT("%%d"), %s)'%get_attr
            else:
                get_attr = etype + get_attr
                get_attr = 'wxString::Format(wxT("%%d"), %s)'%get_attr
        elif typestr in ["float", "Number"]:
            check = '%s > 0'%get_attr
            get_attr = 'wxString::Format(wxT("%%g"), %s)'%get_attr
        elif typestr == "css::CSSStyleDeclaration":
            check = '!%s.empty()'%get_attr
            get_attr = '%s.GetCSSText()'%get_attr
        elif typestr in ["SVGLength", "Length", "Rect", "PreserveAspectRatio", "SVGAnimatedType"]  or typestr[-4:] == "List":
            if typestr == "Length" or typestr == "SVGLength":
                check = '%s.GetUnitType() != wxSVG_LENGTHTYPE_UNKNOWN'%get_attr
            elif typestr == "PreserveAspectRatio":
                check = '%s.GetAlign() != wxSVG_PRESERVEASPECTRATIO_UNKNOWN'%get_attr
            elif typestr == "SVGAnimatedType":
                check = '%s.GetPropertyType() != wxSVG_ANIMATED_UNKNOWN'%get_attr
            else:
                check = '!%s.IsEmpty()'%get_attr
            get_attr = '%s.GetValueAsString()'%get_attr
        if len(check)>0:
            func_body = func_body + '  if (%s)\n  '%check
        func_body = func_body + '  attrs.Add(wxT("%s"), %s);\n'%(entity_name, get_attr)
    try:
        custom_parser = interfaces.interfaces[classdecl.name].custom_parser
        if custom_parser:
            func_body = func_body + '  attrs.Add(GetCustomAttributes());\n'
    except KeyError:
        pass
    for inh in classdecl.inherits:
        if inh in ["Element", "events::EventTarget", "events::DocumentEvent",
                   "css::ViewCSS", "css::DocumentCSS", "css::CSSValue",
                   "smil::ElementTimeControl", "Document", "events::UIEvent",
                   "css::CSSRule", "events::Event"]:
            continue
        res = process(parse_idl.class_decls[inh])
        if res>0:
            if nattr==0:
                includes.append(classdecl.name)
            nattr = nattr+res
            func_body = func_body + '  attrs.Add(wx%s::GetAttributes());\n'%inh

    if nattr>0:
        output_cpp = '''
// wx%s
wxSvgXmlAttrHash wx%s::GetAttributes() const
{
  wxSvgXmlAttrHash attrs;
%s\
  return attrs;
}
'''%(classdecl.name, classdecl.name, func_body)

        output_cpps[classdecl.name]=output_cpp

    already_done[classdecl.name] = nattr
    return nattr


if len(parse_idl.class_decls):
    cnames = parse_idl.class_decls.keys()
    for name in cnames:
        process(parse_idl.class_decls[name])

output_cpp=""
if len(output_cpps):
    for name in sorted(output_cpps.keys()):
        output_cpp = output_cpp + output_cpps[name]

includestr=''
for i in includes:
    includestr = includestr + '#include "%s.h"\n'%i

impl = cppImpl.Impl("Elements_GetAttributes", "genGetAttributes.py")
impl.add_content(includestr + output_cpp)
impl.dump(path=conf.src_dir)

