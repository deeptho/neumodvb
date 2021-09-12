##############################################################################
## Name:        parse_idl.py
## Purpose:     parses idl file
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: parse_idl.py,v 1.5 2014/03/21 21:15:35 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

import re
import string
import os.path
import idl
import conf
import collections

try:
	import cPickle as pickle
except:
	import pickle


interface_re = re.compile("interface\s+(\w+)\s+(:\s+[^{]+)?{")
attribute_re = re.compile("(readonly)?\s+attribute\s+([^;]+);")
const_as_enum_re = re.compile("const\s+unsigned\s+short\s+([^\s]+)\s+=\s+(\d+)")
method_re = re.compile("^\s+([^\(/]+)\(([^\)]+)\)(\s*raises\s*\(([^(]+)\))?", re.MULTILINE)

bracket_re = re.compile("{|}")

def get_close_bracket(content):
	depth=1

	while depth!=0:
		m=bracket_re.search(content)
		if m.group()=='{':
			depth=depth+1
		else:
			depth=depth-1
	return m.end()

pathtoidl= conf.share_dir+"/svg.idl"

f=open(pathtoidl, 'r')
content = f.read()


class_decls={}
class_decls["SVGTBreakElement"] = idl.class_decl(name="SVGTBreakElement")
class_decls["SVGTBreakElement"].inherits = ["SVGElement"]

while 1:
	# search for "interface"
	m = interface_re.search(content)
	if m==None:
		break
	content = content[m.end():]
	interface_name = m.group(1)
	the_class_decl = idl.class_decl(name=interface_name)
	class_decls[interface_name] = the_class_decl

	# inheritance
	inherits = m.group(2)
	inherits_names = []
	plaininherits = []
	if inherits!=None:
		tmp = inherits[1:].split(',')
		for i in tmp:
			realname = i.strip()
			plaininherits.append(realname)

		the_class_decl.inherits = plaininherits

	end_interface = get_close_bracket(content)

	# const as enum
	first_enum=1
	did_enum=0
	beg=0

	plain_enums = []
	theenum=idl.enum_decl(class_decl=the_class_decl)
	theenum.const_decls=[] # ?? why ?????!!!!!!

	while 1:
		m = const_as_enum_re.search(content, beg, end_interface)
		if m==None:
			break
		beg=m.end()
		const_name = m.group(1)
		const_value = int(m.group(2))

		if first_enum==1 and const_value!=0:
			# not an enum
			continue

		did_enum=1
		if const_value==0:
			if first_enum==0: # new enum
				plain_enums.append(theenum)				
				theenum=idl.enum_decl(class_decl=the_class_decl)
				theenum.const_decls=[] # ?? why ?????!!!!!!
                                
			first_enum=0
			pos = const_name.rfind('_')
			theenum.name = const_name[:pos]
		theenum.const_decls.append(idl.const_decl(const_name, str(const_value)))

	if did_enum==1:
		plain_enums.append(theenum)

	the_class_decl.enums = plain_enums

	# attributes
	beg=0
	has_attributes=0
	plain_attributes=[]
	while 1:
		m = attribute_re.search(content, beg, end_interface)
		if m==None:
			break
		has_attributes=1
#		content = content[m.end():]
		beg=m.end()
		readonly=m.group(1)
		attr_type_and_name = m.group(2)
		attr_spec = attr_type_and_name.split()
		attr_type=' '.join(attr_spec[:-1]).strip()

		attr_name=attr_spec[-1].strip()
		tmpconst=0
		if readonly=='readonly':
			tmpconst=1
		theattr = idl.arg_decl(name=attr_name, type=idl.type_decl(name=attr_type, const=tmpconst))
		plain_attributes.append(theattr)


	if has_attributes==1:
		the_class_decl.attributes=plain_attributes

	# methods
	# beg=0

	plain_methods=[]
	
	while 1:
		m = method_re.search(content, beg, end_interface)
		if m==None:
			break
		beg=m.end()
		return_type_and_name = m.group(1).strip()
		tmp = return_type_and_name.split()
		meth_name = tmp[-1]
		return_type = ' '.join(tmp[:-1]).strip()

		themeth = idl.method_decl(name = meth_name, return_type=idl.type_decl(name=return_type))

		args = m.group(2).strip()
		raises=[]
		if (m.group(4)):
			tmp = m.group(4).split(',')
			for i in tmp:
				raises.append(i.strip())
		themeth.exceptions=raises

		theargs=[]
		if len(args):
			args=args.split(',')
			for arg in args:
				spec = arg.split()

				inout = spec[0].strip()
				typename = ' '.join(spec[1:-1])
				savtypename = typename
				varname = spec[len(spec)-1].strip()

				theargs.append(idl.arg_decl(name=varname, inout=inout, type=idl.type_decl(name=savtypename )))


		themeth.args=theargs
		plain_methods.append(themeth)
		

		the_class_decl.methods = plain_methods
	content=content[end_interface:]
class_decls = collections.OrderedDict(sorted(class_decls.items()))
