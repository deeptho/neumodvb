catch load .*philips.*
set $my_bp = $bpnum
commands
    silent
    python
import gdb, re
# Look through currently loaded libraries known to GDB
# and find the one matching your specific philips path requirement
libs = gdb.execute("info sharedlibrary", to_string=True)
for line in libs.splitlines():
  if "philips" in line and "No" in line:
      # Extract the actual library filename/path from the info output
      parts = line.split()
      if parts:
        lib_path = parts[-1]
        print(f"[+] Auto-loading symbols for: {lib_path}")
        gdb.execute(f"sharedlibrary {lib_path}")
end
continue
end
define hook-run
  # Ensures your specific breakpoint is enabled on every run
  enable $my_bp
end

set env TSAN_OPTIONS="abort_on_error=1"
catch syscall exit_group
set print finish off

# Force GDB to completely fail loading anything by default
set auto-solib-add off
set breakpoint pending on
#break dvbdev_monitor_t::find_lnb_for_tuning_to_mux
#break  active_mux_t::tune
#set index-cache directory /tmp/index
#set index-cache enabled
#set environment LD_PRELOAD /usr/lib64/clang/14.0.5/lib/linux/libclang_rt.asan-x86_64.so
break active_si_stream_t::sdt_process_service
#suppress "missing debuginfo"
#set build-id-verbose 0
set env TSAN_OPTIONS="abort_on_error=1"
catch syscall exit_group
set print finish off
#set environment LD_PRELOAD=/usr/lib64/libasan.so.8
break __sanitizer::Die
exec-file /usr/bin/python3
set args neumodvb.py
set detach-on-fork on
#set environment LD_PRELOAD /usr/lib64/libasan.so.8
#break nit_parser_t::parse_payload_unit
#break active_si_stream.cc:752
#break devmanager.cc:574
#break cursors.h:383
#break __sanitizer::Die
dir $cdir:../
set logging file /tmp/x.log
set logging enabled on
set debuginfod enabled off
set pagination off
source prettyprint.py
set print pretty

define load-neumo
  sharedlib pyspectrum.cpython-313-x86_64-linux-gnu.so
  sharedlib libneumoreceiver.so
  sharedlib pyreceiver.cpython-313-x86_64-linux-gnu.so
  sharedlib pyneumompv.cpython-313-x86_64-linux-gnu.so
  sharedlib libneumoutil.so
  sharedlib pyepgdb.cpython-313-x86_64-linux-gnu.so
  sharedlib libepgdb.so
  sharedlib pydeser.cpython-313-x86_64-linux-gnu.so
  sharedlib pyrecdb.cpython-313-x86_64-linux-gnu.so
  sharedlib librecdb.so
  sharedlib libstatdb.so
  sharedlib pystatdb.cpython-313-x86_64-linux-gnu.so
  sharedlib libchdb.so
  sharedlib pychdb.cpython-313-x86_64-linux-gnu.so
  sharedlib pydevdb.cpython-313-x86_64-linux-gnu.so
  sharedlib libdevdb.so
  sharedlib libnanobind.so
  sharedlib libschema.so
  sharedlib pyschemadb.cpython-313-x86_64-linux-gnu.so
  sharedlib libneumodb.so
  sharedlib pyneumodb.cpython-313-x86_64-linux-gnu.so
  sharedlib libstackstring.so
end


#define hook-stop
#  cont
#end


#set auto-solib-add off
#load-neumo
#set index-cache on
#set symbol-cache on
#set symbol-cache-size 204800
set verbose off
set index-cache directory ~/.cache/gdb
set breakpoint pending on

#break  active_si_stream_t::eit_section_cb
define savebreak
  save breakpoints my.brk
end

define loadbreak
  source breakpoints my.brk
end

define pp
  if $argc == 1
    print $arg0
  end
  if $argc == 2
    print $arg0 $arg1
  end
  if $argc == 3
    print $arg0 $arg1 $arg2
  end
  if $argc == 4
    print $arg0 $arg1 $arg2 $arg3
  end
  if $argc == 5
    print $arg0 $arg1 $arg2 $arg3 $arg4
  end
  if $argc == 6
    print $arg0 $arg1 $arg2 $arg3 $arg4 $arg5
  end
  if $argc == 7.
    print $arg0 $arg1 $arg2 $arg3 $arg4 $arg5 $arg6
  end
  if $argc == 8
    print $arg0 $arg1 $arg2 $arg3 $arg4 $arg5 $arg6 $arg7
  end
  if $argc == 9
    print $arg0 $arg1 $arg2 $arg3 $arg4 $arg5 $arg6 $arg7 $arg8
  end
end

 define printall
  set $n = 0
  while $n < $argc
    eval "print $arg%d", $n
    set $n = $n + 1
  end
 end
# set demangle-style none
