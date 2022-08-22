https://stackoverflow.com/questions/33837688/debugging-gdb-pretty-printers


sudo pip3 install ipykernel
sudo pip3 install jupyter_console


add to gdb


define ipython_embed
  python
import sys
print(sys.version)
# helper functions
import gdb
def gdb_run(cmd):
  print(gdb.execute(cmd, to_string=True))
def gdb_eval(expression):
  return gdb.parse_and_eval(expression)
def gdb_vis(value):
  return gdb.default_visualizer(value)
import IPython
IPython.embed_kernel()
  end
  # gdb command prompt is basically unusable after the ipython kernel stops, so just exit gdb
  quit
end

ipython3 console  --existing kernel-808832.json
gdb_eval('run')
