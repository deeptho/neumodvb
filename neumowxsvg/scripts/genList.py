##############################################################################
## Name:        genList.py
## Purpose:     generates all SVG*List.h/cpp
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: genList.py,v 1.8 2016/01/24 16:58:49 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

import conf
import genAnimated
import cpp
import cppHeader
import cppImpl

def generate(name):
    if name == "PathSeg" or name == "ElementInstance":
        return
    typename = genAnimated.getBaseType(name)
    include = ""
    if name == "String":
        include = '#include "String_wxsvg.h"\n'
    elif typename not in cpp.builtin_types:
        include = '#include "SVG%s.h"\n'%name
    
    addGetParam = ""
    addSetParam = ""
    if name == "String":
        addGetParam = "wxChar delimiter = wxT(',')"
        addSetParam = ", wxChar delimiter = wxT(',')"
    
    output = '''%s#include "String_wxsvg.h"
#include <wx/dynarray.h>
WX_DECLARE_OBJARRAY(%s, wxSVG%sListBase);

class wxSVG%sList: public wxSVG%sListBase
{
  public:
    wxSVG%sList() {}
    
    wxString GetValueAsString(%s) const;
    void SetValueAsString(const wxString& value%s);
};'''%(include, typename, name, name, name, name, addGetParam, addSetParam)

    header = cppHeader.Header("SVG%sList"%name, "genList.py")
    header.add_content(output)
    header.dump(path=conf.include_dir)
    
    output = '''#include "SVG%sList.h"
#include <wx/arrimpl.cpp>
WX_DEFINE_OBJARRAY(wxSVG%sListBase);

wxString wxSVG%sList::GetValueAsString()
{
  wxString value;
  return value;
}

void wxSVG%sList::SetValueAsString(const wxString& value)
{

}'''%(name,name,name,name)
#    impl = cppImpl.Impl("SVG%sList"%name, "genList.py")
#    impl.add_content(output)
#    impl.dump(path=conf.src_dir)

