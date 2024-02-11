set breakpoint pending on
#break dvbdev_monitor_t::find_lnb_for_tuning_to_mux
#break  active_mux_t::tune
#set index-cache directory /tmp/index
#set index-cache enabled
#set environment LD_PRELOAD /usr/lib64/clang/14.0.5/lib/linux/libclang_rt.asan-x86_64.so
set print finish off
set environment LD_PRELOAD=/usr/lib64/libasan.so.8
break __sanitizer::Die
exec-file /usr/bin/python3
set args neumodvb.py
set detach-on-fork on
#set environment LD_PRELOAD /usr/lib64/libasan.so.6
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
#break  active_si_stream_t::eit_section_cb
define savebreak
  save breakpoints my.brk
end

define loadbreak
  source breakpoints my.brk
end
break subscriber.cc:132

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
