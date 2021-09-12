##############################################################################
## Name:        cpp.py
## Purpose:     
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: cpp.py,v 1.6 2014/03/21 21:15:35 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:		some modules adapted from svgl project
##############################################################################

import string

builtin_types = ["float", "double", "int", "unsigned long", "long", "unsigned short", "char", "unsigned char", "bool", "void"]
number_types = ["float", "double", "int", "unsigned long", "long", "unsigned short", "char", "unsigned char", "bool"]

def make_name(name):
    tmp=name
    tmp = tmp.replace('-','_')
    tmp = tmp.replace(':','_')
    return tmp

def make_attr_name(name):
    return 'm_' + make_name(name)

def fix_typename(name):
    pos = name.find("::")
    if pos>0:
        name = name[pos+2:]
    if name == "boolean":
        name = "bool"
    elif name == "float":
        name = "double"
    elif name == "DOMString" or name == "String":
        name = "wxString"
    elif name == "Document" or name == "Element":
        name = "wxSvgXml" + name
    elif name not in builtin_types:  #elif name[:3]=="SVG":
        name = "wx" + name
    return name

