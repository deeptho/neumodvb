# neumoDVB #
## Reporting bugs ##

To report a bug:

* First check if your problem has already been reported. Reporting the same problem in different tickets
  only wastes developers' time. If the bug was already reported, but you have additional information to
  help understand or fix the bug, then add your information to the existing ticket

* If you report any problem, it is essential to provide the minimal information requested by the ticket template,
  which appears when you open a new ticket on GitHub. It is also essential to clearly explain the problem and if
  possible how it can be reproduced, or under which conditions it occurs.

* Note that "crashes" come in different types. neumoDVB will deliberately stop, after logging an error in /tmp/neumo.log
  if it detects an unexpected condition which it cannot recover from. However, it may also crash without detecting
  such a condition. In this case, the error message on the console is needed to understand the problem.

* If the problem is reproducible, please download and compile the latest version of the software to see
  it still occurs. Also if possible, spend some time analyzing the problem in detail, e.g., experimenting
  with more verbose log settings in the ```neumo.xml``` config file, finding a simpler way to make the problem
  occur, or even using gdb to track down the problem. It is not as difficult as you think and you do not need
  to be a developer to record a stack dump in gdb.


## Debugging neumoDVB ###

### Increasing log verbosity ###
To obtain more information about a bug or a future crash, you can edit ```neumo.xml``` (remember that
the active version could be in different places - see the section on configuration files) to increase
verbosity. The format of this file is explained on <https://logging.apache.org/log4cxx/latest_stable/usage.html>.
neumoDVB uses multiple ```loggers``` which roughly correspond to the threads in the program. For instance,
service specific debug information is handled by the ```service logger```, SI processing and tuning by the
```tuner``` logger and so on. By changing the value of ```priority``` from ```error``` to ```debug``` more messages
will be logged. Changes to ```neumo.xml``` will be activated as soon as you save the file. There is no need to
restart neumoDVB.

### Debugging ###

In general neumoDVB problems come in two flavors: those in C++ code will usually cause the program to end,
typically reporting an assertion. Errors in python code will tend to show long messages on the console but
do not necessarily cause the program to end.

To debug python problems, use an IDE like pycharm. The text below deals with the more complicated
process of debugging C++ problems.


When neumoDVB crashes in a reproducible way, the most valuable information comes from the debugger.
Even if you have never used ```gdb``` before, it should be easy to provide minimal crash information
as follows:

* First install cgdb and gdb.
* Then create ``~/gdbinit``` with the following content:
```set auto-load safe-path /:.
   set pagination off
   set auto-load local-gdbinit on
```
* Now inspect the content of ```~/neumodvb/gui/.gdbinit```. Here you can specify breakpoints if needed,
  but more importantly you specify how the debugger should start neumoDVB. This is a bit complicated because
  it consists of a mixture of python and C++ code. The defaults in  ```~/neumodvb/gui/.gdbinit``` should be
  fine as they are.

* Then run neumoDVB under cgdb as follows:
```
cd ~/neumodvb/gui/
cgdb
run
```
These commands first start the debugger. I use ```cgdb``` but you can use ```gdb``` as well. I recommend
the latest version of cgdb from github. Note that recent versions of bash introduce weird problems. If you see
text like ```[?2004h``` then install cgdb from GitHub, and use the ```libvterm``` branch instead of the default one.
Or use ```gdb``` instead. The main advantage of using ```cgdb``` is that it better shows the source lines
on which the debugger has stopped.


If neumoDVB crashes, this will be evident in the console (bottom half of cgdb screen).
Often, the cause will be an assertion.  at this point, type commands like
```bt full``` (shows were the assertion occurred) and ```info thread``` (shows what threads
were running or not). Other useful commands include ```up``` (to go to the calling function)
and ```down.``` If an assertion contains conditions like ```assert(x==0 || y==3)```, a very useful
action is to find out the values of x and y, using the commands ```print x``` and ```print y```.
This requires first using ```up``` a few times until the assertion is displayed on the top part
of the screen.

### Advanced debugging ###

If you need to track down a rare bug it is not practical to run neumoDVB under the debugger.
In this case, you can edit ```src/CMakeLists.txt``` and uncomment the line
```
#add_compile_options(-D__assert_fail=assert_fail_stop)
```

Then you need to rebuild.
Then when an assertion occurs, neumoDVB will not crash as usual, but instead put itself to sleep:
all threads will stop what they are doing but the program will not end.
You can now attach a debugger to the program as follows:


```
cd ~/neumodvb/gui/
cgdb
!pidof -s neumodvb
attach 1664795

```
The number ```1664795``` is the one printed by the ```pidof -s...``` command.
At this stage, ```info threads``` will show one thread which is stuck in a sleep function
(look for clock or sleep). Using the command ```thread X```, where X is the thread number,
you can make that thread current, and after using ```up``` a few times, you can inspect
variables as in regular debugging.

One problem you may encounter is that some debug commands may cause other threads to briefly resume
and then receive a ```stop``` signal. This annoying because it makes another thread current, forcing you
to re-issue commands like ```thread X``` to get back to the thread you were inspecting.

To prevent this behavior, you can type the command ```set scheduler-locking on```.


## Feature requests ##
Please take into account that neumoDVB is a project developed by volunteers with limited time.
In general, priority will be given to fixing bugs, and to implementing features which are requested
by many users, or which the developers like themselves.

If you have a good idea for a new feature, then

* First check if a similar feature has been requested. If so, add your comments to that ticket. Otherwise
  you will just waste the developers time

* Be clear about what the feature should do.

* Explain what is the benefit of the feature.
