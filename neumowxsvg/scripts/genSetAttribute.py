##############################################################################
## Name:        genSetAttribute.py
## Purpose:     generates Elements_SetAttribute.cpp
##              -> SetAttribute() methods for all svg elements
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: genSetAttribute.py,v 1.18 2016/01/24 16:58:49 ntalex Exp $
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
    func_body_anim = ''

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
        
        attr_name = cpp.make_attr_name(attr.name)
        set_attr = attr_name
        typestr = attr.type.name
        anim_pos = -1
        if typestr != "SVGAnimatedType":
            anim_pos = typestr.find('Animated')
        if anim_pos >= 0: # SVGAnimatedTypename
            typestr = typestr[anim_pos + len('Animated'):]
            if typestr not in ["float", "Number", "Integer", "Boolean", "Enumeration", "unsigned short"]:
                set_attr = set_attr + '.GetBaseVal()'
        set_attr_anim = ''
        
        #print classdecl.name, attr.name, typestr
        if typestr in ["Integer", "Boolean", "Enumeration", "unsigned short"]:
            etype = ''
            if typestr == "Integer":
                etype = '(long int) '
            elif typestr == "Boolean":
                etype = '(bool) '
            elif typestr == "Enumeration":
                etype = '(unsigned char) '
            elif typestr == "unsigned short":
                if classdecl.name == "SVGZoomAndPan":
                    etype = '(wxSVG_ZOOMANDPAN) '
                elif classdecl.name == "SVGColorProfileElement":
                    etype = '(wxRENDERING_INTENT) '
            if anim_pos >= 0:
                set_attr = '%s.SetBaseVal(%svalue)'%(set_attr,etype)
                set_attr_anim = '%s.SetAnimVal(%sattrValue.GetLength().GetValue());'%(attr_name,etype)
            else:
                set_attr = '%s = %svalue'%(set_attr,etype)
            if classdecl.name == "SVGGradientElement" and attr.name == "gradientUnits":
                set_attr = '''  {
    wxSVG_UNIT_TYPE value = wxSVG_UNIT_TYPE_UNKNOWN;
    if (attrValue.Lower() == wxT("userspaceonuse"))
        value = wxSVG_UNIT_TYPE_USERSPACEONUSE;
    else if (attrValue.Lower() == wxT("objectboundingbox"))
        value = wxSVG_UNIT_TYPE_OBJECTBOUNDINGBOX;
    %s;
  }'''%set_attr
            elif (classdecl.name + '::' + attr.name) in enum_map.enum_map:
                set_attr2 = ''
                for enum in classdecl.enums:
                    if enum.name == enum_map.enum_map[classdecl.name + '::' + attr.name] and len(enum.const_decls):
                        set_attr2 = '  {\n    %s value = %s;\n'%(cpp.fix_typename(enum.name), cpp.fix_typename('%s'%enum.const_decls[0].name))
                        for const_decl in enum.const_decls[1:]:
                            set_attr2 = set_attr2 + '    '
                            if const_decl.name != enum.const_decls[1].name:
                                set_attr2 = set_attr2 + 'else '
                            set_attr2 = set_attr2 + 'if (attrValue.Lower() == wxT("%s"))\n'%(const_decl.name.split('_')[-1].lower())
                            set_attr2 = set_attr2 + '      value = %s;\n'%(cpp.fix_typename('%s'%const_decl.name))
                        set_attr = set_attr2 + '    %s;\n  }'%set_attr
                        break
                if (not len(set_attr2)):
                    set_attr = '  {\n    long value;\n    if (attrValue.ToLong(&value))\n      %s;\n  }'%set_attr
            else:
                set_attr = '  {\n    long value;\n    if (attrValue.ToLong(&value))\n      %s;\n  }'%set_attr
        elif typestr in ["float", "Number"]:
            if anim_pos >= 0:
                set_attr = '%s.SetBaseVal(value)'%set_attr
                set_attr_anim = '%s.SetAnimVal(attrValue.GetLength().GetValue());'%attr_name
            else:
                set_attr = '%s = value'%set_attr
            calc_proc = ''
            if classdecl.name == "SVGStopElement" and attr.name == "offset":
                calc_proc = 'if (attrValue.Last() == wxT(\'%%\') && attrValue.Left(attrValue.Length()-1).ToDouble(&value))\n    {\n      value = value/100;\n      %s;\n    }\n    else '%set_attr
            set_attr = "  {\n    double value;\n    %sif (attrValue.ToDouble(&value))\n      %s;\n  }"%(calc_proc,set_attr)
        elif typestr == "css::CSSStyleDeclaration":
            set_attr = '    %s.SetCSSText(attrValue);'%set_attr
        elif typestr in ["Rect"] or typestr[-4:] == "List":
            set_attr = '    %s.SetValueAsString(attrValue);'%set_attr
            if anim_pos >= 0:
                set_attr_anim += 'if (attrValue.GetPropertyType() == wxSVG_ANIMATED_LENGTH_LIST) {\n'
                if typestr == "NumberList":
                    set_attr_anim += "\t  wxSVGNumberList list;\n"
                    set_attr_anim += "\t  for (unsigned int i = 0; i < attrValue.GetLengthList().size(); i++) {\n"
                    set_attr_anim += "\t\tlist.push_back(attrValue.GetLengthList()[i].GetValue());\n"
                    set_attr_anim += "\t  }\n"
                    set_attr_anim += '\t  %s.SetAnimVal(list);\n'%attr_name
                    set_attr_anim += '\t} else if (attrValue.GetPropertyType() == wxSVG_ANIMATED_NUMBER_LIST) {\n'
                    set_attr_anim += '\t  %s.SetAnimVal(attrValue.GetNumberList());\n'%attr_name
                elif typestr == "TransformList":
                    set_attr_anim += '\t  %s.SetAnimVal(attrValue.GetTransformList());\n'%attr_name
                elif typestr == "Rect":
                    set_attr_anim += '\t  if (attrValue.GetLengthList().size() >= 4) {\n'
                    set_attr_anim += '\t\tconst wxSVGLengthList& list = attrValue.GetLengthList();\n'
                    set_attr_anim += '\t\t%s.SetAnimVal(wxSVGRect(list[0], list[1], list[2], list[3]));\n'%attr_name
                    set_attr_anim += '\t  }\n'
                else:
                    set_attr_anim += '\t  %s.SetAnimVal(attrValue.GetLengthList());\n'%attr_name
                set_attr_anim += '\t} else {\n'
                set_attr_anim += '\t  %s.ResetAnimVal();\n'%attr_name
                set_attr_anim += '\t}'
        elif typestr in ["SVGLength", "Length", "PreserveAspectRatio", "SVGAnimatedType"]:
            set_attr = '    %s.SetValueAsString(attrValue);'%set_attr
            if anim_pos >= 0:
                if typestr == "PreserveAspectRatio":
                    set_attr_anim += 'if (attrValue.GetPropertyType() == wxSVG_ANIMATED_STRING) {\n'
                    set_attr_anim += '\t  wxSVGPreserveAspectRatio ratio;\n'
                    set_attr_anim += '\t  ratio.SetValueAsString(attrValue.GetString());\n'
                    set_attr_anim += '\t  %s.SetAnimVal(ratio);\n'%attr_name
                else:
                    set_attr_anim += 'if (attrValue.GetPropertyType() == wxSVG_ANIMATED_LENGTH) {\n'
                    set_attr_anim += '\t  %s.SetAnimVal(attrValue.GetLength().GetValue());\n'%attr_name
                set_attr_anim += '\t} else {\n'
                set_attr_anim += '\t  %s.ResetAnimVal();\n'%attr_name
                set_attr_anim += '\t}'
        else:
            set_attr = '\t' + set_attr + ' = attrValue;'
            if anim_pos >= 0:
                set_attr_anim += 'if (attrValue.GetPropertyType() != wxSVG_ANIMATED_UNKNOWN) {\n'
                set_attr_anim += '\t  %s.SetAnimVal(attrValue.GetString());\n'%attr_name
                set_attr_anim += '\t} else {\n'
                set_attr_anim += '\t  %s.ResetAnimVal();\n'%attr_name
                set_attr_anim += '\t}'
        if entity_name == "xlink:href":
            func_body = func_body + 'if (attrName == wxT("%s") || attrName == wxT("href"))\n'%entity_name
        else:
            func_body = func_body + 'if (attrName == wxT("%s"))\n'%entity_name
        func_body = func_body + set_attr + '\n  else '
        if len(set_attr_anim):
            func_body_anim += 'if (attrName == wxT("%s")) {\n'%entity_name
            func_body_anim += '    ' + set_attr_anim + '\n  } else '

    for inh in classdecl.inherits:
        if inh in ["Element", "events::EventTarget", "events::DocumentEvent",
                   "css::ViewCSS", "css::DocumentCSS", "css::CSSValue",
                   "smil::ElementTimeControl", "Document", "events::UIEvent",
                   "css::CSSRule", "events::Event"]:
            continue
        res = process(parse_idl.class_decls[inh])
        if res > 0:
            if nattr == 0:
                includes.append(classdecl.name)
            nattr = nattr + res
            func_body = func_body + "if (wx%s::SetAttribute(attrName, attrValue));\n  else "%(inh)
            has_anim = 0
            if len(parse_idl.class_decls[inh].attributes):
                for attr in parse_idl.class_decls[inh].attributes:
                    if attr in mapDtdIdl.attributes_idl_dtd and attr.type.name[0:11] == "SVGAnimated" and attr.type.name != "SVGAnimatedType":
                        has_anim = 1
                        break
            if has_anim and parse_idl.class_decls[inh].name != "SVGExternalResourcesRequired":
                func_body_anim += "if (wx%s::SetAnimatedValue(attrName, attrValue));\n  else "%(inh)

    if nattr > 0:
        try:
            custom_parser = interfaces.interfaces[classdecl.name].custom_parser
            if custom_parser:
                func_body += "if (SetCustomAttribute(attrName, attrValue));\n  else"
                if classdecl.name not in ["SVGAnimationElement"]:
                    func_body_anim += "if (SetCustomAnimatedValue(attrName, attrValue));\n  else "
        except KeyError:
            pass

        #if it's an element
        #if classdecl.name.find("Element")>0 and classdecl.name != "SVGElement":
        if classdecl in mapDtdIdl.elements_idl_dtd:
            func_body = func_body + '\n  {\n    //wxLogDebug(wxT("unknown attribute %s::") + attrName);\n    return false;\n  }\n'%(classdecl.name)
        else:
            func_body = func_body +'\n      return false;\n'

        output_cpp = '''
// wx%s
bool wx%s::SetAttribute(const wxString& attrName, const wxString& attrValue) {
  %s
  return true;
}\n'''%(classdecl.name, classdecl.name, func_body)
        if len(func_body_anim):
            output_cpp += '''
bool wx%s::SetAnimatedValue(const wxString& attrName, const wxSVGAnimatedType& attrValue) {
  %s{
    return false;
  }
  return true;
}\n'''%(classdecl.name, func_body_anim)

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

impl = cppImpl.Impl("Elements_SetAttribute", "genSetAttribute.py")
impl.add_content(includestr + output_cpp)
impl.dump(path=conf.src_dir)

