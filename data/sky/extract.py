#!/usr/bin/python3
import json
import os

file='/mnt/scratch/tvheadend/data/conf/epggrab/opentv/dict/skyeng'
file='/mnt/scratch/tvheadend/data/conf/epggrab/opentv/dict/skyit'
file='/mnt/scratch/tvheadend/data/conf/epggrab/opentv/dict/skynz'
fp = open(file, 'r', encoding='ascii')

data=json.load(fp)

data=sorted(data, key= lambda x: x['code'])
sys.stdout.reconfigure(encoding='utf-8')

print("static std::vector<huff_entry_t>  sky_ukentries{{")
for d in data:
    c = d['code']
    c1="0"*(32-len(c))
    code = int(f'{c}{c1}', 2)
    s = d['data']
    s = " " if s=='' else s
    try:
        s = s.encode('utf-8')
        l = len(s)
    except:
        l = len(s)
    print(f'{{0x{code:8x}, {len(c)}, {l}, "{s}"}},')
print("}};")
