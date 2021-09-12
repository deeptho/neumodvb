Database structure

each lmdb database contains at least two tables:
data: contains the actual records
index: contains various indexes

Database of type recdb
is used for remembering the global list of recordings, and for describing data in
individual mpm-recordings and mpm-livebuffers. This database containes additional
tables:
epg_data, epg_index: for storing relevant epg records
service_data, service_index: for storing relevant service records
