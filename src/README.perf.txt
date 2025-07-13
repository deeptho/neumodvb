
rm -fr /mnt/neumo/db/epgdb.mdb/*;perf record -F 10000 -s -g ./neumodvb.py
perf report   --comms=tuner  --percentage relative


-------------------------
perf top -F100 -d10 -p$(pgrep -d, a.out)
sudo perf top --comms=tuner -g -K

https://dev.to/franckpachot/linux-perf-top-basics-understand-the-316l


https://mariadb.com/kb/en/profiling-with-linux-perf-tool/

https://chrisdietri.ch/post/performance-profiling-perf/

https://devpress.csdn.net/linux/62ebbaf689d9027116a0f697.html
https://gist.github.com/coderplay/c2e2dd89e33c302b581716f5758245c4
https://opensource.com/article/18/7/fun-perf-and-python
-------------------------------------
sudo  perf probe -x ../build/src/receiver/libneumoreceiver.so --funcs
sudo  perf probe -x ../build/src/receiver/libneumoreceiver.so --funcs --no-demangle --filter='*'

sudo perf probe --no-demangle -x ../build/src/receiver/libneumoreceiver.so --add startx=_Z20opentv_decode_stringRN2ss7string_EPhj19opentv_table_type_t --add endx=_Z20opentv_decode_stringRN2ss7string_EPhj19opentv_table_type_t%return

sudo mount -o remount,mode=755 /sys/kernel/tracing/
#sudo chmod -R o+r /sys/kernel/tracing
#sudo find /sys/kernel/tracing -type d -exec chmod o+x {} \;
sudo sysctl kernel.perf_event_paranoid=-1


rm -fr  /mnt/neumo/db/epgdb.mdb; perf record -e probe_libneumoreceiver:startx -e probe_libneumoreceiver:endx__return -aR ./neumodvb.py
perf script --gen-script python


sudo perf probe --del=*
sudo perf probe -x ./simple --add main --add main%return --add fwait --add fwait%return
sudo perf script -s perf-script.py
perf script --gen-script python
