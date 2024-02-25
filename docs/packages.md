# neumoDVB #
## Installation from packages ##

rpm and deb packages are now built for fedora 39 and for Ubuntu 23.10. These packaages
are experimental and have seen little testing. It is possible that they won't install or run
due to missing dependencies or due to files being in other locations than when
neumoDVB is started from its source directory.

### Fedora 39 ###

If you wish to install fedora from scratch then immediately after installation
you will need to switch from Wayland to X11 as Wayland lacks several important
features. You also may wish to install mate desktop, which provides a better
user experience and which is based on X11:

```bash
sudo dnf groupinstall -y 'MATE Desktop'
```
Then log out or reboot and at the login screen, press some icon that brings up a menu
allowing you to select `mater session`. If all goes well, a menu will appear on top of the
screen.

Some of the packages needed for neumoDVB are provided by rpmfusion, which can be installed
using the instructions at
<https://rpmfusion.org/Configuration>:

```bash
dnf install -y https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-39.noarch.rpm \
https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-39.noarch.rpm
```
neumoDVB also needs tsduck and mpl_scatter_density:

```bash
sudo dnf install https://github.com/tsduck/tsduck/releases/download/v3.36-3528/tsduck-3.36-3528.fc38.x86_64.rpm
sudo pip3 install mpl_scatter_density
```

Now install neumoDVB itself. Download it from the github release page
and install it as follows (the version may differ of course):

```
sudo dnf install -y neumodvb-1.6.fc39.x86_64.rpm
```

The last command may fail due to some conflicts with already installed `ffmpeg` libraries, which need
to be replaced by those in `rpmfusion`. Try adding `--allowerasing` at the end of the
command to solve this problem.


#### Ubuntu 23.10 ####

If you wish to install Ubuntu from scratch, then install it from
`ubuntu-mate-23.10-desktop-amd64.iso` to ensure you have a decent starting point.

neumoDVB needs `tsduck`. Install it as follows:

```bash
wget https://github.com/tsduck/tsduck/releases/download/v3.36-3528/tsduck_3.36-3528.ubuntu23_amd64.deb
sudo apt install ./tsduck_3.36-3528.ubuntu23_amd64.deb
```

Now install neumoDVB itself. Download the .deb package from the github release page
and install it as follows (the version may differ of course):

```bash
sudo apt install ~/neumodvb/deb/neumodvb-1.6.deb
```
