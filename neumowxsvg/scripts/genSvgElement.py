##############################################################################
## Name:        genSvgElement.py
## Purpose:     generates svg.h, SVGDTD.h and SVGDocument_CreateElement.cpp
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: genSvgElement.py,v 1.7 2014/03/21 21:15:36 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

import string

import parse_dtd
import cpp
import cppHeader
import cppImpl
import conf

translate_to_classname = {
  "Tbreak": "TBreak",
  "Tspan": "TSpan",
  "Tref": "TRef",
  "Mpath": "MPath",
  "Svg": "SVG",
  "Vkern": "VKern",
  "Hkern": "HKern"
}

def make_cppname(name):
    beg=0
    while 1:
        pos = name.find('-', beg)
        if pos==-1:
            pos = name.find('-', beg)
        if pos>0:
            res = name[:pos]+ name[pos+1].upper() + name[pos+2:]
            name=res
            beg=pos
        else:
            break

    if name[:2]=="Fe":
        name = "FE" + name[2:]
    else:
        try:
            tmp=translate_to_classname[name]
            name = tmp
        except KeyError:
            pass
    return name


all_headers=[]
all_elements = []

for name, entity_types in parse_dtd.attlists.items():
  all_elements.append(name)

includes =''
create = ''
dtdenum = ''

for element_dtd_name in sorted(all_elements):
    classname = element_dtd_name[0].upper()+element_dtd_name[1:]
    classname = make_cppname(classname)
    dtdname = element_dtd_name.replace('-', '_')
    dtdname = dtdname.replace(':', '_')
    includes = includes + '#include "SVG%sElement.h"\n'%(classname)
    dtdenum = dtdenum + '  wxSVG_%s_ELEMENT,\n'%(dtdname.upper())
    create = create + '''if (qualifiedName == wxT("%s"))
    res = new wxSVG%sElement();
  else '''%(element_dtd_name, classname)

create = '''wxSvgXmlElement* wxSVGDocument::CreateElementNS(const wxString& namespaceURI,
  const wxString& qualifiedName)
{
  wxSVGElement* res = NULL;
  ''' + create + '''
	res = new wxSVGGElement();
  return res;
}'''
dtdenum = 'enum wxSVGDTD\n{\n' + dtdenum + '  wxSVG_UNKNOWN_ELEMENT\n};'

impl = cppImpl.Impl("SVGDocument_CreateElement", "genSvgElement.py")
impl.add_content(create)
impl.dump(path=conf.src_dir)

includes = includes + '#include "SVGDocument.h"\n'
header = cppHeader.Header("svg", "genSvgElement.py")
header.add_content(includes)
header.dump(path=conf.include_dir)

header = cppHeader.Header("SVGDTD", "genSvgElement.py")
header.add_content(dtdenum)
header.dump(path=conf.include_dir)

