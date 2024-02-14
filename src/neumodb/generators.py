# Neumo dvb (C) 2019-2024 deeptho@gmail.com
# Copyright notice:
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
from collections import OrderedDict
from pprint import pformat
from jinja2 import Template, Environment, FileSystemLoader, PackageLoader, select_autoescape
import regex as re
import os
from itertools import chain
import importlib

#gen_options= dict(dbname=None, env=None, template_dir = None)
templates = {}
def multi_replace(source, replacements):
    finder = re.compile("|".join(re.escape(k) for k in replacements.keys())) # matches every string we want replaced
    result = []
    pos = 0
    while True:
        match = finder.search(source, pos)
        if match:
            # cut off the part up until match
            result.append(source[pos : match.start()])
            # cut off the matched part and replace it in place
            result.append(replacements[source[match.start() : match.end()]])
            pos = match.end()
        else:
            # the rest after the last match
            result.append(source[pos:])
            break
    return "".join(result)


def set_env(dbname, this_dir, db_type_id, output_dir):
    #global gen_options
    gen_options= dict(dbname=None, env=None, template_dir = None)
    gen_options['dbname'] = dbname
    gen_options['db_type_id'] = ord(db_type_id) << 16
    gen_options['this_dir'] = this_dir
    gen_options['template_dir'] = os.path.join(this_dir, '../templates')
    if output_dir is None:
        output_dir = os.path.abspath(os.path.join(gen_options['this_dir'],
                                      '../../../build/src/neumodb',
                                      gen_options['dbname']))

    gen_options['output_dir'] = output_dir
    env = Environment(
        loader=FileSystemLoader(gen_options['template_dir']),
        trim_blocks=True,
        lstrip_blocks=True
        #,autoescape=select_autoescape(['html', 'xml']
    )
    gen_options['env'] =env
    return gen_options
def normalize_type(_type):
    match = re.match(r'(ss::vector)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
    if match is None:
        match = re.match(r'(ss::string)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        if match is None:
            match = re.match(r'(ss::bytebuffer)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
            if match is None:
                return _type
        return 'ss::string<>'
    match=match.groups()
    subtype = match[2].split(',')[0]
    return "{}<{}>".format(match[0], normalize_type(subtype))

class db_include(object):

    def __init__(self, db, fname, include):
        self.include = include
        self.db = db
        self.fname = fname
        lst = self.db.all_includes_by_file.get(fname,[])
        lst.append(self)
        self.db.all_includes_by_file[self.fname]=lst #one entry per file


class db_enum(object):

    def __init__(self, db, name, type_id, storage, version, fields, replace={'_':' '}):
        self.db = db
        self.name = name
        self.type_id = type_id | self.db.db_type_id
        self.storage = storage
        self.record_version = version
        self.values = []
        self.fname='enums'
        self._next_offset_id = 0
        lst = self.db.all_enums_by_file.get(self.fname, [])
        lst.append(self)
        self.db.all_enums[self.name]=self #one entry per structure
        self.db.all_enums_by_file[self.fname]=lst #one entry per file
        for field in fields:
            if type(field) == str:
                field = (field, None, None)
            if type(field) == tuple and len(field) == 2:
                field =(*field, None)
            self.add(*field)
        prefix = os.path.commonprefix([value['name'] for value in self.values])
        if not prefix.endswith('_'):
            prefix=""
        for value in self.values:
            short_name = re.sub('^{}'.format(prefix), '', value['name'])
            value['short_name'] = short_name
            if value['display_name'] is None:
                value['display_name']= short_name

    def add(self, name, val=None, display_name=None):
        self.values.append(dict(name=name, val=val, display_name=display_name))

    def __repr__(self):
        return "enum {}\n{}".format(self.name, pformat(self.values))


class db_struct(object):

    def __init__(self, db, name, fname, type_id, version, fields, primary_key=None, keys=[],
                 is_table=False, filter_fields=[], ignore_for_equality_fields=[]):
        self.hpp_template = db.get_template("structs.h")
        self.cpp_template = db.get_template("structs.cc")
        self.pybind_cpp_template = db.get_template("structs_pybind.cc")
        self.db = db
        self.name = name
        self.class_name = '{}_t'.format(name)
        self.fname = fname
        self.type_id = type_id | self.db.db_type_id
        self.record_version = version
        self.primary_key = primary_key
        self.is_table = (primary_key is not None)
        self.keys = keys
        self.fields = []
        self.ignore_for_equality_fields = ignore_for_equality_fields
        self.subfields = None
        self.substructs = {}
        self.filter_fields = []
        self.keys = []
        self._next_offset_id = 0
        lst = self.db.all_structs_by_file.get(fname,[])
        lst.append(self)
        self.db.all_structs[self.class_name]=self #one entry per structure
        self.db.all_structs_by_file[self.fname]=lst #one entry per file
        for field in fields:
            try:
                self.add_field(*field)
            except:
                print(f"Illegal field definition: {field}")
        if primary_key is not None:
            self.primary_key = self.add_key(None, *primary_key, primary=True)
        for key in keys:
            self.add_key(*key)
        for filter_field in filter_fields:
            self.add_filter_field(*filter_field)
        #self.dump()
    def get_field_type(self, field_name):
        try:
            parts=field_name.split('.')
            if len(parts) == 1:
                return next(field for field in self.fields if field['name'] == field_name)['scalar_type']
            field = next(field for field in self.fields if field['name'] == parts[0])
            fielddb, fieldstruct = self.db.db_and_struct_for_field(field)
            #parent_struct_name = field['type']
            subname = '.'.join(parts[1:])
            #return self.db.all_structs[parent_struct_name].get_field_type(subname)
            return fieldstruct.get_field_type(subname)
        except:
            print(f"{self.name} Cannot get_field_type for {field_name}")

    def add_filter_field(self, name, key):
        """
        name is a human readable name without dots, e.g., sat_pos
        key can be e.g., mux.sat_pos
        """
        key_fields = []

        self.filter_fields.append(dict(name=name, key=key))

    def add_field(self, field_id, _type, _name, _default=None):
        assert type(field_id) == int
        assert type(_type) == str
        assert type(_name) == str
        assert _default is None or type(_default) == str or type(_default) == int
        match_variant = re.match(r'(std::variant)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        match_optional = re.match(r'(std::optional)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        match_string = re.match(r'(ss::string)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        match_int = re.match(r'([u]{0,1}int(8|16|32|64)_t)', _type)
        match_vector = re.match(r'(ss::vector)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        match_bytebuffer = re.match(r'(ss::bytebuffer)(<((?:(?>[^<>]+)|(?2))*)>)', _type)
        is_variant = match_variant is not None
        is_optional = match_optional is not None
        is_int = match_int is not None
        is_string = match_string is not None
        is_vector = False
        is_bytebuffer = match_bytebuffer is not None
        is_vector_of_strings = False
        variant_types = None
        namespace = ""
        if match_optional is not None:
            scalar_type = match_optional.groups()[2]
            match_namespace = re.match(r'([^:]+::).+', scalar_type)
            namespace = match_namespace.group(1) if match_namespace else ""
            scalar_type = normalize_type(scalar_type)
            if namespace=="ss::":
                namespace = ""
            elif namespace!="":
                scalar_type = scalar_type.split(namespace)[-1]
            is_vector=False
            namespace = ""
        elif match_variant is not None:
            variant_type_names = match_variant.groups()[2].split(',')
            variant_types =[]
            for t in variant_type_names:
                match_vector = re.match(r'(ss::vector[_]*)(<((?:(?>[^<>]+)|(?2))*)>)', t)
                if match_vector is not None:
                    is_vector=True
                    scalar_type = normalize_type(match_vector.groups()[2].split(',')[0])
                    match_namespace = re.match(r'([^:]+::).+', scalar_type)
                    namespace = match_namespace.group(1) if match_namespace else ""
                    if namespace=="ss::":
                        namespace = ""
                    elif namespace!="":
                        scalar_type = scalar_type.split(namespace)[-1]
                    assert type(scalar_type)!=list
                else:
                    is_vector=False
                    match_namespace = re.match(r'([^:]+::).+', t)
                    namespace = match_namespace.group(1) if match_namespace else ""
                    scalar_type = normalize_type(t)
                    if namespace=="ss::":
                        namespace = ""
                    elif namespace!="":
                        scalar_type = scalar_type.split(namespace)[-1]
                    assert type(scalar_type)!=list
                variant_types.append(dict(variant_type=t, scalar_type=scalar_type, namespace=namespace))
            scalar_type = 'variant'
            is_vector=False
            namespace = ""
        elif match_vector is not None:
            scalar_type = normalize_type(match_vector.groups()[2].split(',')[0])
            assert type(scalar_type)!=list
            match_string = re.match(r'(ss::string)(<((?:(?>[^<>]+)|(?2))*)>)', scalar_type)
            is_vector = True
            assert type(scalar_type)!=list
            is_vector_of_strings = match_string is not None
        else:
            match_namespace = re.match(r'([^:]+::).+', _type)
            namespace = match_namespace.group(1) if match_namespace else ""
            scalar_type = normalize_type(_type)
            if namespace=="ss::":
                namespace = ""
            elif namespace!="":
                scalar_type = scalar_type.split(namespace)[-1]
            assert type(scalar_type)!=list

        has_variable_size = _type.startswith('ss::') or _type.startswith('std::variant') \
            or _type.startswith('std::optional')
        self.fields.append(dict(field_id=field_id, type=_type, name=_name, default=_default,
                                namespace = namespace,
                                is_vector=is_vector, is_vector_of_strings=is_vector_of_strings,
                                is_string=is_string, is_int = is_int,
                                is_optional = is_optional,
                                is_variant = is_variant,
                                variant_types=variant_types,
                                is_bytebuffer = is_bytebuffer,
                           #primary_key = self.primary_key,
                                scalar_type=scalar_type, has_variable_size=has_variable_size))

    def add_key_helper(self, index_id, index_name,
                       fields, primary, full):
        """
        index_def is a tuple of field names; each field_name can be prepended by a conversion function
        We have the following types of keys
        full=True: original key as specified in dbdefs.py
        full=False: shortened version of an original key key with some of its final
                        fields removed

        """
        key_fields = []
        for idx in fields:
            ret = re.match(r'(?:([^:]+):){0,1}(.*)', idx)
            fun, field_name = ret.groups()
            if fun is None:
                pass #print('{} SIMPLE: {}'.format(index_name, field_name))
            else:
                if fun == 'tolower':
                    fun = 'ss::tolower' #hack
                pass # print('{} CONF: {} {}'.format(index_name, fun,field_name))
            _,_,_, namespace = self.db.type_of_field(self.class_name, field_name)
            scalar_type =  self.get_field_type(field_name)

            assert type(scalar_type)!=list
            key_fields.append(dict(fun=fun, name = field_name, scalar_type=scalar_type, namespace=namespace))

        key = dict(index_id=index_id, index_name=index_name,
                   fields=key_fields, primary=primary, full=full)
        return key

    def expand_key(self, fields):
        """
        recursive function which finds key prefixes, with compacted field names
        """
        n = len(fields)
        #prefixes = []
        suffixes = []
        variants =[]
        if len(fields) == 0:
            return variants

        variant_name = '_'.join([f['short_name'] for f in fields])
        variants.append({'name': variant_name, 'fields': fields})
        expanded_fields =  []
        def make_new_field(field_namem, f):
            ret = f.copy()
            ret['name'] = '{}.{}'.format(field_name, f['name'])
            ret['short_name'] =f ['name']
            return ret
        while True:
            #we wish to strip of one field, but only after fully expanding the last field
            fielddb = self.db
            while True:
                field = fields[-1]
                field_name = field['name']
                #field_type = self.get_field_type(field_name)
                #ss = find_substruct(field['name'])
                ss = self.substructs.get(field_name, None)
                if ss is not None:
                    field['namespace'] = ss['namespace']
                    fielddb, fieldstruct = fielddb.db_and_struct_for_field(field)
                    assert fielddb is not None
                    #field can be expanded, so expand it
                    newfields = [ make_new_field(field_name, f) for f in fieldstruct.fields]
                    expanded_fields = newfields + expanded_fields[1:]
                    variant_name = '_'.join([f['short_name'] for f in fields[:-1] + expanded_fields])
                    variants.append({'name': variant_name, 'fields': fields[:-1] + expanded_fields})
                    fields = fields[:-1] + newfields
                else:
                    break
            if len(fields)==1:
                break
            fields = fields[:-1]
            #prefixes.append(fields)
        return variants

    def compute_key_prefixes(self, fields):
        """
        a key prefix is a partial key which runs from the start until a specific field
        This fields is called last_field
        There is a akey prefix for each such field
        Each prefix also has a name, for use in the c++ code
        It is possible that different keys have a similarly named key_prefix.
        The calling code will mark all but one of such keys as duplicate.
        key prefixes are stored with the longest one first
        """
        n = len(fields)
        prefixes = []
        prefix_name =  '_'.join([f['short_name'] for f in fields])
        newprefix = { 'prefix_name': prefix_name,
                      'is_full_key' : True,
                      'fields' : fields}
        if len(fields) ==0:
            return prefixes
        prefixes.append(newprefix)
        suffixes = []
        variants =[]
        while True:
            #we wish to strip of one field, but only after fully expanding the last field
            #equivalents = []
            while True:
                field = fields[-1]
                field_name = field['name']
                field_type = self.get_field_type(field_name)
                fielddb, fieldstruct = self.db.db_and_struct_for_field(field)
                substructs = {} if type(fieldstruct) != db_struct else fielddb.all_structs[field_type].substructs
                def get_ns(field):
                    if field['name'] in substructs:
                        return substructs[field['name']]['namespace']
                    else:
                        return field['namespace']
                pass
                if type(fieldstruct) == db_struct:
                    #field can be expanded, so expand it
                    newfields = [
                        { 'name': '{}.{}'.format(field_name, f['name']),
                          'short_name':  f['name'],
                          'scalar_type':  f['scalar_type'],
                          'namespace':  get_ns(f),
                        } for f in fielddb.all_structs[field_type].fields]
                    fields = fields[:-1] + newfields
                    #prefix_name =  '_'.join([f['short_name'] for f in fields[:-1]])
                    #equivalents.append (prefix_name)
                else:
                    break
            #last_field is the fully expanded version of the last field in this partial key
            newprefix['last_field'] = field #set what was stored already
            if len(fields)==1:
                break
            fields = fields[:-1]
            prefix_name =  '_'.join([f['short_name'] for f in fields])
            newprefix={ 'prefix_name': prefix_name,
                        'is_full_key' : False,
                        #'equivalents' : equivalents,
                        'fields' : fields}
            prefixes.append(newprefix)
        return prefixes

    def add_key(self, index_id, index_name, fields, primary=False):
        """
        index_def is a tuple of field names; each field_name can be prepended by a conversion function
        """
        key_fields = []

        key = self.add_key_helper(index_id, index_name, fields, primary=primary, full=True)
        self.keys.append(key)
        return key

    def remove_duplicate_keys(self):
        """
        by adding prefix keys, we may have introduced keys with the same name, which are prefixes
        of multiple keys. We filter out the duplicates, prefering primary keys

        This needs to be called after all keys have been added!
        """
        self.keys_by_master = {}
        return
        keys_by_name = {}
        keys_by_master = {}
        #self.dump()
        for key in self.keys:
            existing = keys_by_name.get(key['index_name'], None)
            if existing is None:
                keys_by_name[key['index_name']] = key
                #assert(key['full'])
                s = keys_by_master.get(key['master_index_name'], [])
                s.append(key)
                keys_by_master[key['master_index_name']] = s

        self.full_keys = list(keys_by_name.values())
        self.keys = self.full_keys
        self.keys_by_master = keys_by_master

        #self.dump()
    def __repr__(self):
        return "struct {}\n{}".format(self.name, pformat(self.fields))
    def dump(self):
        if self.name=='service':
            print("====full keys: {}================". format(self.name))
            for key in self.keys:
                print("name={} full={} exp={} fields={}".format
                      (key['index_name'],
                       key['full'], key['expanded'],
                       [f"{field['name']} {field['fun']}" for field in key['fields']]))

    def cpp_hpp(self, template):
        fixed_size_fields = [x for x in self.fields if not x['is_vector']]
        variable_size_fields = [x for x in self.fields if x['is_vector']]
        fields = fixed_size_fields + variable_size_fields
        offsets = [ f for f in fixed_size_fields]
        self.fields = fields
        #self.dump()
        return template.render(trim_blocks=True, lstrip_blocks=True,
                               struct = self,
                               dbname = self.db.gen_options['dbname'],
                               offset_array_len = self._next_offset_id)
        #trim_blocks and lstrip_blocks enable
    def pybind_cpp(self):
        return self.cpp_hpp(self.pybind_cpp_template)
    def cpp(self):
        return self.cpp_hpp(self.cpp_template)
    def hpp(self):
        return self.cpp_hpp(self.hpp_template)

class db_db(object):

    def __init__(self, gen_options):
        self.is_external = False
        self.gen_options = gen_options
        self.hpp_template = self.get_template("db.h")
        self.cpp_template = self.get_template("db.cc")
        self.pybind_cpp_template = self.get_template("db_pybind.cc")
        self.key_hpp_template = self.get_template("db_keys.h")
        self.key_cpp_template = self.get_template("db_keys.cc")
        self.enums_hpp_template = self.get_template("enums.h")
        self.enums_cpp_template = self.get_template("enums.cc")
        self.enums_pybind_cpp_template = self.get_template("enums_pybind.cc")
        self.dbname = self.gen_options['dbname']
        self.db_type_id = gen_options['db_type_id']
        self.db_version = '1.0'
        self.all_structs = OrderedDict()
        self.all_structs_by_file = OrderedDict()
        self.all_includes_by_file = OrderedDict()
        self.all_enums = OrderedDict()
        self.all_enums_by_file = OrderedDict()
        self.enums_fixed = False
        self.external_dbs = dict()
    def is_enum(self, field_type):
        return field_type in self.all_enums

    def get_template(self,hname):
        template = templates.get(hname, None)
        if template is None:
            template = self.gen_options['env'].get_template(hname)
            templates[hname] = template
        return template

    def type_of_field(self, calling_struct, fieldname):
        """
        fieldname: e.g., service.mux_id.sat
        """
        parts = fieldname.split('.')
        struct = self.all_structs[calling_struct]
        parent_field_type = struct.class_name
        namespace = ""
        fielddb = self
        for part in parts:
            fielddb, fieldstruct = fielddb.db_and_struct_for_field_type(parent_field_type)
            field_desc = list(filter(lambda field: field['name'] == part, fieldstruct.fields))
            if len(field_desc) != 1:
                msg = f"Field {part} not found in {parent_field_type}"
                print(msg)
                raise ValueError(msg)
            field_desc = field_desc[0]
            assert not field_desc['is_vector']
            field_type = field_desc['type']
            scalar_type = field_desc['scalar_type']
            assert type(scalar_type)!=list
            parent_field_type = field_type
            namespace = field_desc['namespace'] if namespace == "" else namespace
        fielddb, fieldstruct = fielddb.db_and_struct_for_field_type(field_type)
        if fielddb is None:
            namespace=""
        return part, field_type, scalar_type, namespace

    def __repr__(self):
        return "db {}\n".format(self.dbname)
    def fix_enums(self):

        enums = self.all_enums
        if enums is None:
            return enums
        if self.enums_fixed:
            return enums
        self.enums_fixed = True
        for enum_name, enum in enums.items():
            cur = 0
            prefix = None
            for desc in enum.values:
                if desc['val'] is not None:
                    cur= 1
                    prefix =  f"{desc['val']}" #preserve as c-string
                elif prefix is None:
                    desc['val'] = cur
                    cur = cur +1
                else:
                    desc['val'] = f'{prefix} + {cur}'
                    cur = cur +1

        return enums
    def enums_hpp(self):
        enums = self.fix_enums()
        if enums is None:
            return enums
        return self.enums_hpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                              dbname = self.dbname,
                                              db_version = self.db_version,
                                              enums=enums.values())
    def enums_cpp(self):
        enums = self.fix_enums()
        if enums is None:
            return enums
        return self.enums_cpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                              dbname = self.dbname,
                                              db_version = self.db_version,
                                              enums=enums.values())
        #trim_blocks and lstrip_blocks enable
    def enums_pybind_cpp(self):
        enums = self.all_enums
        if enums is None:
            return enums
        return self.enums_pybind_cpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                                     dbname = self.dbname,
                                                     db_version = self.db_version,
                                                     enums=enums.values())
        #trim_blocks and lstrip_blocks enable
    def pybind_cpp(self):
        #structs = list(chain(*self.all_structs_by_file.values()))
        structs = self.all_structs.values()
        return self.pybind_cpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                               dbname = self.dbname,
                                               db_version = self.db_version,
                                               structs=structs)
        #trim_blocks and lstrip_blocks enable
    def cpp(self):
        structs_by_file = self.all_structs_by_file
        structs = self.all_structs.values()
        return self.cpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                        dbname = self.dbname,
                                        db_version = self.db_version,
                                        structs_by_file=structs_by_file, structs=structs,
                                        external_dbs = self.external_dbs)
    #trim_blocks and lstrip_blocks enable
    def hpp(self):
        structs_by_file = self.all_structs_by_file
        includes_by_file = self.all_includes_by_file
        structs = self.all_structs.values()
        return self.hpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                        dbname = self.dbname,
                                        db_version = self.db_version,
                                        includes_by_file = includes_by_file,
                                        structs_by_file=structs_by_file, structs=structs)
    def expand_keys(self):
        structs = self.all_structs.values()

        for idx,struct in enumerate(structs):
            if len(struct.keys) == 0 :
                continue
            prefixes = set()
            for idx,key in enumerate(struct.keys):
                fields  =[
                    { 'name': f['name'],
                      'fun': f['fun'],
                      'namespace': f['namespace'],
                      'scalar_type': f['scalar_type'],
                      'short_name':  f['name'].split('.')[-1]
                    } for f in key['fields']
                    ]
                key['variants'] = struct.expand_key(fields)
                key['key_prefixes'] = struct.compute_key_prefixes(fields)
                for pref in key['key_prefixes']:
                    pref['duplicate'] = pref['prefix_name'] in prefixes
                    prefixes.add(pref['prefix_name'])
                    for field in pref['fields']:
                        if not field.get('scalar_type', False):
                            field['scalar_type'] =  struct.get_field_type(field['name'])
                            assert type(field['scalar_type'])!=list
                for variant in key['variants']:
                    for field in variant['fields']:
                        field['scalar_type'] =  struct.get_field_type(field['name'])
                        assert type(field['scalar_type'])!=list
            struct.key_prefixes = list(prefixes)


    def db_and_struct_for_field_type(self, field_type):
        if field_type in self.all_structs:
            return self, self.all_structs[field_type]
        if field_type in self.all_enums:
            return self, self.all_enums[field_type]
        parts=field_type.split('::')
        if len(parts)==2 and parts[0] not in (['ss']):
            #e.g. parts[0] = 'chdb' and parts[1]='service'
            if parts[0] == self.dbname:
                field_type = parts[1]
                if field_type in self.all_structs:
                    return self, self.all_structs[field_type]
                elif field_type in self.all_enums:
                    return self, self.all_enums[field_type]
                else:
                    return self, field_type
            otherdb = self.external_dbs.get(parts[0])
            if otherdb is None:
                mod = importlib.import_module(f"{parts[0]}.dbdefs")
                otherdb = getattr(mod, 'db')
                otherdb.is_external =  True
                self.external_dbs[parts[0]] = otherdb
                otherdb.check_structs()
                otherdb.prepare()
            field_type = parts[1]
            if field_type in otherdb.all_structs:
                return otherdb, otherdb.all_structs[field_type]
        return None, None
    def db_and_struct_for_field(self, field):
        field_type = f"{field['namespace']}{field['scalar_type']}"
        return self.db_and_struct_for_field_type(field_type)

    def compute_subfields_for_struct(self, struct):
        subfields = []
        struct.substructs = {}
        struct.alfas ='test'
        for field in struct.fields:
            #if field['is_vector'] or
            if field['is_bytebuffer'] or field['is_variant'] or field['is_optional']:
                continue
            field_type = f"{field['namespace']}{field['scalar_type']}"
            fielddb, fieldstruct = self.db_and_struct_for_field(field)

            if not(field['is_vector']) and type(fieldstruct) == db_struct: # this is a structure
                field['is_simple'] =False
                if fieldstruct.subfields is None:
                    #If the child structure's metadata was not yet computed, do so now
                    fielddb.compute_subfields_for_struct(fieldstruct)
                    assert fieldstruct.subfields is not None
                def make_subfield(prefix, s):
                    ret = s.copy()
                    ret['namespace'] = ""
                    ret['name']= f"{prefix}.{s['name']}"
                    ret['is_external']= fielddb.is_external
                    return ret
                def make_substruct(prefix, s):
                    ret = s.copy()
                    ret['namespace'] = field['namespace']# if field['namespace'] != "" else parent_namespace
                    ret['name']= f"{prefix}.{s['name']}"
                    ret['is_external']= fielddb.is_external
                    return ret
                namespace = f"{fieldstruct.db.dbname}::" if fieldstruct.db.is_external else ""
                struct.substructs[f"{field['name']}"] = \
                    {'namespace' : namespace, 'type': fieldstruct.class_name}
                for name,s in fieldstruct.substructs.items():
                    struct.substructs[f"{field['name']}.{name}"] = s
                #if len(fieldstruct.substructs) != 0:
                #    substructs += [make_substruct(field['name'], s) for s in fieldstruct.substructs]
                subfields += [make_subfield(field['name'], s) for s in fieldstruct.subfields]
                field['is_struct'] = True
            else:
                subfield = field.copy()
                field['is_simple'] = not field['scalar_type'].startswith('ss::string')
                subfield['is_vector'] = field['is_vector']
                subfield['namespace'] = ""
                assert fielddb is None or type(fieldstruct) == db_enum or field['is_vector']
                subfield['is_external'] = struct.db.is_external
                subfield['is_simple'] = field['is_simple']
                subfields.append(subfield)
                field['is_struct'] = False
        struct.subfields = subfields
        #struct.substructs = substructs
    def compute_subfields(self):
        structs = self.all_structs.values()
        processed = {}
        for idx,struct in enumerate(structs):
            self.compute_subfields_for_struct(struct)
    def compute_subfield_keys_for_struct(self, struct):
        def key_for_field(struct, field_name):
            if struct.db.is_external:
                return None
            for k in struct.keys:
                for v in k['variants']:
                    for f in v['fields']:
                        if f['name'] == field_name:
                            return v['name']
            return None
        for field in struct.subfields:
            if field['is_vector'] or field['is_bytebuffer']:
                field['key'] = None
            elif field['is_external']:
                field['key'] = None
            else:
                field['key'] = key_for_field(struct, field['name'])
    def compute_subfield_keys(self):
        structs = self.all_structs.values()
        processed = {}
        for idx,struct in enumerate(structs):
            self.compute_subfield_keys_for_struct(struct)
    def prepare_keys(self):
        """
        compute additional info on each key
        """
        return
        structs = self.all_structs.values()
        for idx,struct in enumerate(structs):
            if len(struct.keys) == 0 :
                continue
            #struct.dump()
            cum_fields = dict()
            keys_by_name = dict()
            for idx,key in enumerate(struct.keys):
                accu=[]
                #struct.keys[idx]['cum_fields']=[]
                keys_by_name[key['index_name']] = key
                for idxf, field in enumerate(key['fields']):
                    short_name, type_, scalar_type_, namespace_ = self.type_of_field(struct.class_name, field['name'])
                    assert scalar_type_ !=list
                    accu.append(short_name)
                    struct.keys[idx]['fields'][idxf]['short_name'] = short_name
                    struct.keys[idx]['fields'][idxf]['type'] = type_
                    struct.keys[idx]['fields'][idxf]['namespace'] = namespace_
                    struct.keys[idx]['fields'][idxf]['scalar_type'] = scalar_type_
                    struct.keys[idx]['fields'][idxf]['key_prefix'] = '_'.join(accu)
                    cum_fields['_'.join(accu)] = dict()
                if key['parent_index_name'] != key['index_name']:
                    cum_fields['_'.join(accu)]['parent_index_name']= key['parent_index_name']
                #struct.add_prefix_keys(key, key['fields']) #to phase out?
            struct.key_prefixes = []

            for idx, cum_field in cum_fields.items():
                if cum_field.get('parent_index_name', False):
                    master_key_prefix = keys_by_name[cum_field['parent_index_name']]['fields'][-1]['key_prefix']
                    cum_field[ 'parent_key_prefix'] = master_key_prefix
                    del cum_field['parent_index_name']
                else:
                    cum_field[ 'parent_key_prefix'] = None
                struct.key_prefixes.append(dict(label=idx, value=cum_field[ 'parent_key_prefix']))


    def remove_duplicate_keys(self):
        structs = self.all_structs.values()
        for idx,struct in enumerate(structs):
            if len(struct.keys) == 0 :
                continue
            struct.remove_duplicate_keys()

    def key_hpp(self):
        #structs = list(chain(*self.all_structs_by_file.values()))
        structs_by_file = self.all_structs_by_file
        structs = self.all_structs.values()
        return self.key_hpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                            dbname = self.dbname,
                                            db_version = self.db_version,
                                            structs_by_file=structs_by_file, structs=structs)

    def key_cpp(self):
        #structs = list(chain(*self.all_structs_by_file.values()))
        structs_by_file = self.all_structs_by_file
        structs = self.all_structs.values()
        return self.key_cpp_template.render(trim_blocks=True, lstrip_blocks=True,
                                            dbname = self.dbname,
                                            db_version = self.db_version,
                                            structs_by_file=structs_by_file,
                                            structs=structs)
    def save_db(self):
        for fname, fn in ((f'{self.dbname}_db.cc', self.cpp),
               (f'{self.dbname}_db.h', self.hpp),
               (f'{self.dbname}_db_pybind.cc', self.pybind_cpp),
               (f'{self.dbname}_keys.h', self.key_hpp),
               (f'{self.dbname}_keys.cc', self.key_cpp)):
            self.write_db(fname, fn)

#trim_blocks and lstrip_blocks enable
    def write_struct(self, filename, structs, includes, suffix, method):
        fname = os.path.join(self.gen_options['output_dir'], "{}{}".format(filename, suffix))
        with open(fname, 'w') as f:
            if includes is not None:
                for include in includes:
                    f.write('#include "{}"'.format(include))
            for struct in structs:
                f.write(getattr(struct, method)())
        os.system("clang-format -i {}".format(fname))

    def write_db(self, filename, fn):
        fname = os.path.join(self.gen_options['output_dir'], filename)
        with open(fname, 'w') as f:
            f.write(fn())
        os.system("clang-format -i --sort-includes=0 {}".format(fname))

    def save_structs(self, includes=None):
        for filename, structs in  self.all_structs_by_file.items():
            self.write_struct(filename, structs, includes, '.h', "hpp")
            self.write_struct(filename, structs, None, '.cc', "cpp")
            self.write_struct(filename, structs, None, '_pybind.cc', "pybind_cpp")


    def check_structs(self):
        type_ids = {}
        index_ids = {}
        for struct_name, struct in  self.all_structs.items():

            #struct.dump()
            if struct.type_id in type_ids:\
                raise ValueError("type_id={} is used by more than one struct: {} and {}".
                                 format(struct.type_id, type_ids[struct.type_id].name, struct.name))
            if type(struct.type_id)!= int:
                raise ValueError("type_id={} has wrong type: {}".
                                 format(struct.type_id, type(struct.type_id)))

            if struct.type_id >= 0x20000000:
                raise ValueError("type_id={} is too large".
                                 format(struct.type_id))

            type_ids[struct.type_id] = struct
            for index in struct.keys:
                if index['index_id'] is None:
                    continue
                if index['index_id'] in index_ids:
                    raise ValueError("index_id={} is used by more than one index: {} and {}".
                                     format(chr(index['index_id']),
                                            index_ids[index['index_id']]['index_name'], index['index_name']))
                index_ids[index['index_id']] = index

    def prepare(self):
        self.compute_subfields()
        self.expand_keys()
        #self.prepare_keys()
        self.compute_subfield_keys()
        self.remove_duplicate_keys()
    def save_enums(self):
        fname = os.path.join(self.gen_options['output_dir'], "enums.cc")
        with open(fname, 'w') as f:
            towrite = self.enums_cpp()
            if towrite is not None:
                f.write(towrite)
        os.system("clang-format -i {}".format(fname))

        fname = os.path.join(self.gen_options['output_dir'], "enums.h")
        with open(fname, 'w') as f:
            towrite = self.enums_hpp()
            if towrite is not None:
                f.write(towrite)
        os.system("clang-format -i {}".format(fname))

        fname = os.path.join(self.gen_options['output_dir'], "enums_pybind.cc")
        with open(fname, 'w') as f:
            towrite = self.enums_pybind_cpp()
            if towrite is not None:
                f.write(towrite)
        os.system("clang-format -i {}".format(fname))

if False:
    self =db

    for fname, structs in db.all_structs_by_file.items():
        for struct in structs:
            if len(struct.keys)>0:
                print("KEYS found: {}:{}".format(fname, struct.name))
