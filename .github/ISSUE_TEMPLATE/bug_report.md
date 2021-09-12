---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

Please remove the example text below, but keep the section titles.

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior:
1. Press 'C' to enter channel list
2. Type '101' to mive to 'BBC One'
3. Press 'Ctrl-M' to tune channel
4. Program crashes

**Detailed explanation of why this is a bug**
Did the program crash, or did it remain on screen? Were errors reported on the console?

If this is a GUI bug, add a screenshot and/or explain in words what is the problem.

If this is a problem with SI information on a specific service, what information does neumoDVB
show on the service and mux in question.

**Screenshots**
If applicable, add screenshots to help explain your problem.

**Information about your setup:**
 - OS: e.g., Fedora 34
 -  neumoDVB version (ESSENTIAL). Use ```grep BUILD /tmp/neumo.log``` to extract a version string like
```BUILD=6c7a6ae+ TAG= neumodvb-0.6.1 BRANCH=master```
 - if relevant, the version numbers of important libraries such as wxWidgets or wxPython if the problem
 is a GUI bug

**Log file**

By default, neumoDVB stores its log file in ```/tmp/neumo.log```. If the file is not very big, rename it to
```neumo.txt``` and attach it to the ticket (gitHub does accept the .log file name extension).
Otherwise first extra a relevant portion (e.g., last 2000 lines before the crash) and attach those.


**Additional context**
In some cases output of programs like ```dvbsnoop``` or ```tsduck``` could be useful to debug 
problems on specific streams. Similarly recordings, live buffers or even transport streams could 
be of use.

Those files could be very big. *Do not attach them to the ticket* but  upload them to https://0x0.st/
or similar sites instead and provide the link in the ticket.
