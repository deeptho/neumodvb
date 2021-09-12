##############################################################################
## Name:        generate.py
## Purpose:     generates the most headers from idl, but with some changes
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: generate.py,v 1.28 2016/01/24 16:58:49 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

##############################################################################
## generates the most headers from idl, but with some changes:
##  - all properties in idl are "readonly", but I generate not only
##    get methods, but also set methods.
##    This allow you to call:
##      rect->SetWidth(100)
##    instead of
##      rect->SetAttrubite("width", "100")
##  - for animated properties (f.e.: SVGAnimatedLength width), in addition to
##    the method SetWidth(const SVGAnimatedLength&) will be generated the
##    method SetWidth(const SVGLength&) to set the base value directly
##############################################################################

import parse_idl
import cpp
import cppHeader
import cppImpl
import enum_map
import string
import mapDtdIdl
import interfaces
import conf
import genAnimated
import genList
import genCSS
import genFile
import os

used_lists = []
used_animated = []
copy_constructor_impl = ''
copy_constructor_includes = ['SVGCanvasItem']

def find_dtd_attr_in_inherit(classdecl):
    if len(classdecl.attributes):
        for attr in classdecl.attributes:
            if attr in mapDtdIdl.attributes_idl_dtd:
                return 1

    for inh in classdecl.inherits:
        try:
            res = find_dtd_attr_in_inherit(parse_idl.class_decls[inh])
            if res > 0:
                return 1
        except KeyError:
            pass
    return 0

def find_anim_dtd_attr_in_inherit(classdecl):
    if len(classdecl.attributes):
        for attr in classdecl.attributes:
            if attr in mapDtdIdl.attributes_idl_dtd and attr.type.name[0:11] == "SVGAnimated" and attr.type.name != "SVGAnimatedType":
                return 1

    for inh in classdecl.inherits:
        try:
            inh_classdecl = parse_idl.class_decls[inh];
            res = inh_classdecl.name != "SVGExternalResourcesRequired" and find_anim_dtd_attr_in_inherit(inh_classdecl)
            if res > 0:
                return 1
        except KeyError:
            pass
    return 0

if len(parse_idl.class_decls):
    for (classname, classdecl) in parse_idl.class_decls.items():
        if classname.find("Animated") >=0 and classname not in ["SVGAnimatedPathData","SVGAnimatedPoints"]:
            used_animated.append(classname)
            continue
        if classname.find("List") >=0:
            used_lists.append(classname)
            continue

        # print classdecl
        header = cppHeader.Header(classname, "generate.py")

        includes=[]
        fwd_decls=[]
        doGetAttrByName=0

        output = ''
        cpp_output = ''
		
		# enums
        for enum in classdecl.enums:
            output = output + '\nenum %s\n{\n'%(cpp.fix_typename(enum.name))
            if len(enum.const_decls):
                output = output + '  %s'%(cpp.fix_typename('%s'%enum.const_decls[0]))
                if len(enum.const_decls[1:]):
                    for const_decl in enum.const_decls[1:]:
                        output = output + ',\n  %s'%(cpp.fix_typename('%s'%const_decl))
            output = output + '\n};\n\n'

        # inheritance
        output = output + "class %s"%(cpp.fix_typename(classname))
        copy_constr_init = ''
        if len(classdecl.inherits):
            first = 1
            for inherit in classdecl.inherits:
                pos = inherit.find("::")
                if pos>0:
                    inherit = inherit[pos+2:]
                if first:
                    output = output + ':\n'
                    copy_constr_init = copy_constr_init + ':\n'
                    first = 0
                else:
                    output = output + ',\n'
                    copy_constr_init = copy_constr_init + ',\n' 
                output = output + '  public %s'%(cpp.fix_typename(inherit))
                copy_constr_init = copy_constr_init + '  %s(src)'%(cpp.fix_typename(inherit))
                includes.append(inherit)
        output = output + '\n{\n'
        
        public = ''
        protected = ''

        ######## attributes ##########        
        attributes=[]

        exclude_attributes=[] # really method that set/get
        try:
            exclude_attributes=interfaces.interfaces[classname].exclude_attributes
        except KeyError:
            pass


        for attr in classdecl.attributes:
            if (attr.name in exclude_attributes):
                continue
            typename = attr.type.name
            
            # is Attribute an Element ?
            ispointer = False
            isenum = False
            pos = attr.type.name.find('Element')
            if pos > 0:
                fwd_decls.append(typename)
                typename = typename+'*'
                ispointer=1
            else:
                if attr.type.name=="unsigned short":# or attr.type.name.find('Enumeration')>=0:
                    try:
                        enumname = enum_map.enum_map[classname+'::'+attr.name]
                        isenum = True
                        if attr.type.name=="unsigned short":
                            typename = enumname
                        else:
                            pos = attr.type.name.find('Animated')
                            if pos >= 0:
                                typename = 'SVGAnimated%s'%enumname
                                isenum = False
                            else:
                                typename = typename.replace('Enumeration', enumname)
                            # typename = classdecl.enums[numtype].name
                    except KeyError: # gen enum_map
                        try:
                            tmp = classdecl.enums[0].name
                        except IndexError:
                            # maybe a UnitType
                            for inh in ["SVGUnitTypes", "SVGRenderingIntent"]:                            
                                if inh in classdecl.inherits:
                                    tmp = parse_idl.class_decls[inh].enums[0].name
                                    break
                                else:
                                    tmp = "0"
                        print("enum_map.py is not up-to-date. Please remove it, copy and paste what's dumped by this script in enum_map.py then rerun again")
                        print('"'+classname+'::'+attr.name +'": "' + tmp + '" ,')

                elif typename=="DOMString":
                    if "String_wxsvg" not in includes:
                        includes.append("String_wxsvg")
                else:
                    if cpp.fix_typename(typename) not in cpp.builtin_types:
                        if typename not in includes:
                            includes.append(typename)
            typename = cpp.fix_typename(typename)


            attributes.append((typename, attr.name, ispointer, isenum))
            # get by name
            try:
                entity_name = mapDtdIdl.attributes_idl_dtd[attr]
                doGetAttrByName=1
            except KeyError:
                pass
        
        # exclude_methods
        exclude_methods=[]
        try:
            exclude_methods=interfaces.interfaces[classname].exclude_methods
        except KeyError:
            pass
        
        # include_get_set_attributes
        include_get_set_attributes=[]
        try:
            for attr in interfaces.interfaces[classname].include_get_set_attributes:
                attributes.append(attr)
        except KeyError:
            pass

        ############### get/set methods #################
        if len(attributes)>0:
            
            # fields
            for (typestr, attrname, ispointer, isenum) in attributes:
                protected += '    ' + typestr + ' ' + cpp.make_attr_name(attrname) + ';\n'
            
            # get/set methods
            for (typestr, attrname, ispointer, isenum) in attributes:
                animated = 0
                typestrBase = ""
                pos = typestr.find('Animated')
                if pos>=0 and typestr != "wxSVGAnimatedType": # SVGAnimatedTypename
                    typestrBase = typestr[pos+len('Animated'):]
                    animated = 1
                
                # get
                methodName = 'Get' + attrname[0].upper()+attrname[1:]
                if(methodName in exclude_methods):
                    continue
                exclude_methods.append(methodName)
                
                ret_type = typestr
                if (typestr[0:5] == "wxSVG" or typestr[0:5] == "wxCSS" or typestr == "wxString") and \
                    typestr[0:6] != "wxSVG_" and not ispointer:
                        ret_type = 'const ' + ret_type + '&'
                #if typestr[0:13] == "wxSVGAnimated":
                #    protected = protected + '    inline %s& %s() { %sreturn %s; }\n'%(typestr,methodName,calc,attrname_cpp)

                attrname_cpp = cpp.make_attr_name(attrname)
                calc = ''
                if classname[-7:] == "Element" and typestr in ["wxSVGAnimatedLength", "wxSVGAnimatedLengthList"]:
                    if "SVGSVGElement" not in includes:
                        includes.append("SVGSVGElement")
                    l = ''
                    if typestr == "wxSVGAnimatedLengthList":
                        l = '_LIST'
                    if attrname[len(attrname)-1].lower() == 'x' or attrname[-5:].lower() == 'width' or \
                       attrname[len(attrname)-2].lower() == 'x' or attrname in ["startOffset", "textLength"]:
                        calc = 'WX_SVG_ANIM_LENGTH%s_CALC_WIDTH(%s, GetViewportElement()); '%(l,attrname_cpp)
                    elif attrname[len(attrname)-1].lower() == 'y' or attrname[-6:].lower() == 'height' or \
                         attrname[len(attrname)-2].lower() == 'y':
                        calc = 'WX_SVG_ANIM_LENGTH%s_CALC_HEIGHT(%s, GetViewportElement()); '%(l,attrname_cpp)
                    elif attrname == 'r':
                        calc = 'WX_SVG_ANIM_LENGTH%s_CALC_SIZE(%s, GetViewportElement()); '%(l,attrname_cpp)
                    else:
                        calc = 'WX_SVG_ANIM_LENGTH%s_CALC_SIZE(%s, GetViewportElement()); '%(l,attrname_cpp)
                        print("Warning: unknown lengthtype of attribute " + classname + '::' + attrname)
                #if classname[-7:] == "Element" and typestr == "wxSVGAnimatedLengthList":
                #    print classname + " - " + typestr
                public = public + '    inline %s %s() const { %sreturn %s; }\n'%(ret_type,methodName,calc,attrname_cpp)
                    
                # set
                methodName = 'Set' + attrname[0].upper()+attrname[1:]
                if(methodName in exclude_methods):
                    continue
                exclude_methods.append(methodName)
                
                param_type = typestr
                if typestr not in cpp.builtin_types and ispointer==0:
                    param_type = 'const ' + param_type + '&'
                
                dirty = ''
                #if classname[-7:] == "Element" and classname != "SVGElement":
                #    dirty = ' SetDirty();'
                
                public = public + '    inline void %s(%s n) { %s = n;%s }\n'%(methodName,param_type,attrname_cpp,dirty)
                if animated:
                    param_type = genAnimated.getBaseType(typestrBase)
                    if param_type not in cpp.builtin_types and ispointer==0:
                        param_type = 'const ' + param_type + '&'
                    public = public + '    inline void %s(%s n) { %s.SetBaseVal(n);%s }\n'%(methodName,param_type,attrname_cpp,dirty)
                public = public + '\n'
        
        try:
            for (attrname, attrtype, init_value) in interfaces.interfaces[classname].include_attributes:
                if len(attrtype):
                    protected += "    %s %s;\n"%(attrtype, cpp.make_attr_name(attrname))
        except KeyError:
            pass
            
        try:
            for i in interfaces.interfaces[classname].include_attributes_str:
                public = public+i
        except KeyError:
            pass
        
        ################# wxSVGStylable ####################
        if classname == "SVGStylable":
            public = public + "  public:\n"
            genCSS.parseCSSProps()
            for prop in genCSS.cssProperties:
                methodName = genCSS.makeMethodName(prop.dtdName)
                valueType = prop.valueType
                if valueType not in cpp.builtin_types and valueType != "wxCSS_VALUE":
                    valueType = "const " + valueType + "&"
                public = public + '    inline void Set%s(%s value) { m_style.Set%s(value); }\n'%(methodName, valueType, methodName)
                valueType = prop.valueType
                if len(genCSS.getFunctionName(prop.valueType)) == 0:
                    valueType = "const " + valueType + "&"
                public = public + '    inline %s Get%s() { return m_style.Get%s(); }\n'%(valueType, methodName, methodName)
                public = public + '    inline bool Has%s() { return m_style.Has%s(); }\n'%(methodName, methodName)
                public = public + '    \n'
        

        ################# constructor #######################
        methods_str = ''
        
        has_constructor = 0
        try:
            has_constructor=interfaces.interfaces[classname].user_defined_constructor
        except KeyError:
            pass
        if has_constructor==0:
            init_attibutes = ""
            try:
                for (typestr, attrname, ispointer, isenum) in attributes:
                    attrname=cpp.make_attr_name(attrname)
                    if typestr in cpp.number_types:
                        init_attibutes = init_attibutes + ", %s(0)"%attrname
                    elif ispointer:
                        init_attibutes = init_attibutes + ", %s(NULL)"%attrname
                    elif isenum:
                        init_attibutes = init_attibutes + ", %s(%s(0))"%(attrname,typestr)
                for (attrname, attrtype, init_value) in interfaces.interfaces[classname].include_attributes:
                    if len(init_value)>0:
                        attrname=cpp.make_attr_name(attrname)
                        init_attibutes = init_attibutes + ", %s(%s)"%(attrname, init_value)
            except KeyError:
                pass
            cname = cpp.fix_typename(classname)
            if classname.find("Element")>0:
                # first find the directly inherited "Element" class
                # generally SVGElement, but it can be  SVGGradientElement for ex.
                element_inherit=None
                for inh in classdecl.inherits:
                    if inh.find("Element")>0:
                        element_inherit=cpp.fix_typename(inh)
                        break
                element_string=''
                if classdecl in mapDtdIdl.elements_idl_dtd:
                    element_string=mapDtdIdl.elements_idl_dtd[classdecl]
                methods_str = methods_str + '    %s(wxString tagName = wxT("%s")):\n      %s(tagName)%s {}\n'%(cname, element_string, element_inherit, init_attibutes)
            elif classname[0:10] == "SVGPathSeg" and len(classname)>10:
                seg_type = classname[10:].upper()
                strs = ["ABS", "REL", "HORIZONTAL", "VERTICAL", "CUBIC", "QUADRATIC", "SMOOTH"]
                for s in strs:
                    pos = seg_type.find(s)
                    if pos>0:
                        seg_type = seg_type[:pos] + "_" + seg_type[pos:]  
                seg_type = "wxPATHSEG_" + seg_type
                methods_str = methods_str + '    %s():\n      wxSVGPathSeg(%s)%s {}\n'%(cname, seg_type, init_attibutes)
            elif len(init_attibutes)>0:
                methods_str = methods_str + '    %s(): %s {}\n'%(cname, init_attibutes[2:])
        
        has_canvas_item = 0
        try:
            has_canvas_item=interfaces.interfaces[classname].has_canvas_item
        except KeyError:
            pass
        if has_canvas_item==1:
            methods_str = methods_str + '    %s(%s& src);\n'%(cname, cname)
            if classname not in copy_constructor_includes:
                copy_constructor_includes.append(classname)
            attr_init = ''
            for (typestr, attrname, ispointer, isenum) in attributes:
                attrname=cpp.make_attr_name(attrname)
                attr_init = attr_init + "\n  %s = src.%s;"%(attrname,attrname)
            copy_constructor_impl = copy_constructor_impl + '''
// %s
%s::%s(%s& src)%s
{%s
  m_canvasItem = NULL;
}

%s::~%s()
{
  if (m_canvasItem)
    delete m_canvasItem;
}
'''%(cname, cname, cname, cname, copy_constr_init, attr_init, cname, cname)
        
        ################# destructor #######################
        has_destructor = 0
        try:
            has_destructor=interfaces.interfaces[classname].user_defined_destructor
        except KeyError:
            pass
        if has_destructor == 0:
            if has_canvas_item == 0:
                methods_str = methods_str + '    virtual ~%s() {}\n'%cname
            else:
                methods_str = methods_str + '    virtual ~%s();\n'%cname
        
        ################# CloneNode #######################
        if classname.find("Element")>0 and classdecl in mapDtdIdl.elements_idl_dtd:
            methods_str = methods_str + '    wxSvgXmlNode* CloneNode(bool deep = true) { return new %s(*this); }\n'%cpp.fix_typename(classname)
        
        ################### methods #########################
        try:
            for i in interfaces.interfaces[classname].include_methods:
                methods_str = methods_str+i
        except KeyError:
            pass
        
        for meth in classdecl.methods:
            method_ret = ''
            return_type = cpp.fix_typename(meth.return_type.name)
            if return_type not in cpp.builtin_types:
                if meth.return_type.name in ["DOMString"]: # confusion between typedef and class
                    if "String_wxsvg" not in includes:
                        includes.append("String_wxsvg")
                else:
                    if meth.return_type.name not in includes:
                        includes.append(meth.return_type.name)
            if meth.return_type.name in ["SVGDocument", "Element"]:
                method_ret = return_type + '* '
            elif meth.return_type.name in ["css::CSSValue"]:
                method_ret = 'const ' + return_type + '& '
            else:
                method_ret = return_type + ' '
            
            method_name = meth.name[0].upper() + meth.name[1:];
            if method_name in exclude_methods:
                continue
            method_decl = method_name + '('
            count=0
            for arg in meth.args:
                if count>0:
                    method_decl = method_decl +', '
                arg_type = cpp.fix_typename(arg.type.name)
                if arg_type not in cpp.builtin_types:
                    if arg.inout == 'in':
                        method_decl = method_decl + 'const %s& %s'%(arg_type, arg.name)
                    elif arg.inout == 'inout':
                        method_decl = method_decl + '%s* %s'%(arg_type, arg.name)
                    elif arg.inout == 'out':
                        method_decl = method_decl + '%s* %s'%(arg_type, arg.name)

                    if arg.type.name in ["DOMString"]: # confusion between typedef and class
                        if "String_wxsvg" not in includes:
                            includes.append("String_wxsvg")
                    else:
                        if arg.type.name not in includes:
                            includes.append(arg.type.name)
                else:
                    if arg_type == "unsigned short":
                        arg_type = cpp.fix_typename(enum_map.enum_map[classname+'::'+arg.name])
                    method_decl = method_decl + '%s %s'%(arg_type, arg.name)
                count = count+1
            
            method_decl += ')'
            if return_type == "wxSVGMatrix" or method_name[0:6] == "Create":
                method_decl += ' const'
            methods_str = methods_str + '    virtual ' + method_ret + method_decl + ';\n';
            method_decl = cpp.fix_typename(classname) + "::" + method_decl;
            cpp_output = cpp_output + method_ret + method_decl + '\n{\n';
            if return_type != "void":
                if method_ret.find("*") < 0:
                    cpp_output = cpp_output + '  ' + method_ret + 'res'
                    if return_type in cpp.builtin_types:
                        cpp_output = cpp_output + ' = 0';
                    cpp_output = cpp_output + ';\n'
                else:
                    cpp_output = cpp_output + '  ' + method_ret + 'res = new ' + return_type + '();\n'
                cpp_output = cpp_output + '  ' + 'return res;'
            cpp_output = cpp_output + '\n}\n\n'
        
        has_dtd_attributes=find_dtd_attr_in_inherit(classdecl)
        if has_dtd_attributes==1 and "SetAttribute" not in exclude_methods:
            methods_str = methods_str + '    bool HasAttribute(const wxString& name) const;\n';
            methods_str = methods_str + '    wxString GetAttribute(const wxString& name) const;\n';
            methods_str = methods_str + '    bool SetAttribute(const wxString& name, const wxString& value);\n';
            methods_str = methods_str + '    wxSvgXmlAttrHash GetAttributes() const;\n';
            if "String_wxsvg" not in includes:
                includes.append("String_wxsvg")
            if "Element" not in includes:
                includes.append("Element")
            doGetAttrByName=1
            try:
                custom_parser = interfaces.interfaces[classname].custom_parser
                if custom_parser:
                    protected += '    bool HasCustomAttribute(const wxString& name) const;\n';
                    protected += '    wxString GetCustomAttribute(const wxString& name) const;\n';
                    protected += '    bool SetCustomAttribute(const wxString& name, const wxString& value);\n';
                    protected += '    wxSvgXmlAttrHash GetCustomAttributes() const;\n';
                    if classdecl.name not in ["SVGAnimationElement"]:
                        protected += '    bool SetCustomAnimatedValue(const wxString& name, const wxSVGAnimatedType& value);\n';
            except KeyError:
                pass
            if find_anim_dtd_attr_in_inherit(classdecl):
                methods_str = methods_str + '    bool SetAnimatedValue(const wxString& name, const wxSVGAnimatedType& value);\n';
                includes.append("SVGAnimatedType")
            elif classname == "SVGElement":
                methods_str = methods_str + '    virtual bool SetAnimatedValue(const wxString& name, const wxSVGAnimatedType& value) { return false; }\n';
                includes.append("SVGAnimatedType")

        element_string=None
        if classdecl in mapDtdIdl.elements_idl_dtd:
            element_string=mapDtdIdl.elements_idl_dtd[classdecl]
            typename = cpp.fix_typename("SVGDTD")
            dtdname = "SVG_" + element_string.upper() + "_ELEMENT"
            dtdname = cpp.fix_typename(cpp.make_name(dtdname))
            methods_str = methods_str + '    virtual %s GetDtd() const { return %s; }\n'%(typename,dtdname)
        
        try:
            fwds = interfaces.interfaces[classname].include_fwd_decls
            for toto in fwds:
                if toto not in fwd_decls:
                    fwd_decls.append(toto)
        except KeyError:
            pass
        
        if len(methods_str):
            if len(public):
                public = public + '  public:\n'
            public = public + methods_str
        
        # protected
        methods_str = ''
        try:
            for i in interfaces.interfaces[classname].include_methods_protected:
                methods_str = methods_str+i
        except KeyError:
            pass
        if len(methods_str):
            if len(public):
                public = public + '\n  protected:\n'
            public = public + methods_str
        
        # end struct
        public = public + '};'
        
        ###################### includes #############################
        includestr=""
        for inc in includes:
            if inc==classname:
                continue
            realname = inc
            pos = inc.find("::")
            if pos>0:
                realname = inc[pos+2:]
            includestr = includestr + "#include \"%s.h\"\n"%(realname)

        try:
            incs = interfaces.interfaces[classname].include_includes
            for i in incs:
                if i[0] == '<':
                    includestr = includestr + '#include %s\n'%(i)
                else:
                    includestr = includestr + '#include "%s.h"\n'%(i)
        except KeyError:
            pass
        if len(includestr):
            includestr = includestr + '\n'
        
        fwd_decls_namespace = { "svg":[] }
        for i in fwd_decls:
            pos = i.find("::")
            nspace = i[:pos]
            if pos>=0:
                if nspace in fwd_decls_namespace:
                    fwd_decls_namespace[nspace].append(i[pos+2:])
                else:
                    fwd_decls_namespace[nspace]=[i[pos+2:]]
            else:
                if i!='Element':
                    fwd_decls_namespace['svg'].append(i)

        if len(fwd_decls_namespace) > 0:
            fwd_decl_str = ''
            for (nspace, classnames) in fwd_decls_namespace.items():
                for i in classnames:
                    fwd_decl_str = fwd_decl_str + 'class %s;\n'%cpp.fix_typename(i)
        if len(fwd_decl_str):
          fwd_decl_str = fwd_decl_str + '\n'
        
        ##################### write header files #############################
        if len(protected):
            output = output + '  protected:\n' + protected + '\n'
        output = output + '  public:\n' + public
        header.add_content(fwd_decl_str + includestr + output)
        header.dump(path=conf.include_dir)
        
        
        ############## write cpp files (disabled) ############################
        if cpp_output != '' and classname.find("List") < 0:
            cpp_output = '''
/////////////////////////////////////////////////////////////////////////////
// Name:        %s.cpp
// Purpose:     
// Author:      Alex Thuering
// Created:     2005/04/29
// RCS-ID:      $Id: generate.py,v 1.28 2016/01/24 16:58:49 ntalex Exp $
// Copyright:   (c) 2005 Alex Thuering
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "%s.h"\n
'''%(classname,classname) + cpp_output
            #f = genFile.gfopen(os.path.join(conf.src_dir, "%s.cpp"%classname),'w')
            #f.write(cpp_output)

###################### Generate copy constructor  ############################
includes = ''
for include in copy_constructor_includes:
    includes = includes + '#include "%s.h"\n'%include  
impl = cppImpl.Impl("Elements_CopyConstructors", "generate.py")
impl.add_content(includes + copy_constructor_impl)
impl.dump(path=conf.src_dir)

###################### Generate animated, lists, setattribute, ... ##########
for i in used_animated:
    genAnimated.generate(i.replace('SVGAnimated',''))

for i in used_lists:
    name = i.replace('List','').replace('SVG','')
    genList.generate(name)

genCSS.generate()

import genHasAttribute
import genGetAttribute
import genSetAttribute
import genGetAttributes
import genSvgElement

