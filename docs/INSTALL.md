# neumoDVB #
## Installation ##

### Install prerequisite software ###

This software depends on several other software packages. If you wish to compile from source,
also a number of development packages are needed. The names of the packages depend on your linux
distribution.

The below software list may be incomplete or may contain no longer needed packages.
Please open a ticket if you discover mistakes


#### Fedora 38 ####
On Fedora 38:  install at least the following RPMs with `sudo dnf install -y <PACKAGE>`:

```bash
sudo dnf install -y redhat-lsb-core cmake libuuid-devel clang clang-tools-extra libtool boost-program-options \
boost-devel boost-regex boost-context curl-devel log4cxx log4cxx-devel libconfig libconfig-devel \
wxGTK3 wxGTK-devel gtk3-devel freeglut-devel librsvg2-devel libexif-devel libexif gobject-introspection \
expat-devel python3-wxpython4 python3-jinja2 python3-matplotlib-wx python3-sip-devel  python3-cachetools \
python3-gobject-base python3-configobj python3-regex python3-matplotlib-wx python3-scipy wxWidgets-devel \
wxBase3 wxBase-devel libX11-devel libglvnd-devel espeak mesa-dri-drivers mpv-libs-devel  libdvbcsa-devel \
ffmpeg-devel mpv-libs-devel tsduck fmt fmt-devel
```

Also make sure that the following packages are **not** installed, as they might lead to compiling or linking with the wrong
libraries, resulting in crashes:

```bash
sudo dnf remove wxGTK3-devel wxBase3-devel wxsvg-devel wxGTK3-devel
```

Some of these packages are provided by rpmfusion, which can be installed using the instructions at
<https://rpmfusion.org/Configuration>



In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```

And some bugs need to be fixed by upgrading python3-matplotlib-wx to at least version 3.5.2:

```bash
sudo dnf update --enablerepo=updates-testing python3-matplotlib-wx
```
#### Fedora 39 ####
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

Then install all packages needed to build and run neumoDVB:

```bash
sudo dnf install -y \
    https://github.com/tsduck/tsduck/releases/download/v3.36-3528/tsduck-3.36-3528.fc38.x86_64.rpm
sudo dnf install -y git clang cmake clang-tools-extra libtool curl-devel log4cxx log4cxx-devel \
libconfig-devel wxGTK-devel gtk3-devel freeglut-devel librsvg2-devel libexif-devel expat-devel \
python3-jinja2 python3-sip-devel python3-configobj wxWidgets-devel wxBase-devel mpv-libs-devel \
libX11-devel libglvnd-devel libdvbcsa-devel redhat-lsb-core libuuid-devel boost-devel curl-devel \
fmt-devel python3-regex gdb boost-program-options boost-regex curl-devel log4cxx libconfig wxGTK \
wxGTK-devel libexif python3-wxpython4  python3-matplotlib-wx python3-gobject-base  \
python3-configobj python3-regex python3-matplotlib-wx python3-scipy wxBase ffmpeg-libs \
libglvnd-devel espeak mesa-dri-drivers fmt python3-regex python3-cachetools
```

The last command may fail due to some conflicts with already installed ffmpeg libraties, which need
to be replaced by those in `rpmfusion`. In that case try adding `--allowerasing` at teh end of the
command that fails.

The following may also be needed:
```
sudo dnf install -y boost-context  ffmpeg-devel
```

In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```

#### Ubuntu 23.10 ####

If you wish to install Ubuntu from scratch then install it from
`ubuntu-mate-23.10-desktop-amd64.iso` to ensure you have a decent starting point.

Install the following packages for building and running neumoDVB:
```
sudo apt install -y git  clang clang-16 clang-tools-16 clang-format python3-matplotlib mpv libmpv-dev python3-mpl-scatter-density cmake libboost-all-dev libgtk-3-dev libwxgtk3.2-dev libexif-dev liblog4cxx-dev python3-jinja2 python3-regex python3-sip-dev libconfig-dev libconfig++-dev libdvbcsa-dev freeglut3-dev python3-configobj  python3-cachetools python3-wxgtk-media4.0 python3-setuptools espeak
wget https://github.com/tsduck/tsduck/releases/download/v3.36-3528/tsduck_3.36-3528.ubuntu23_amd64.deb
sudo apt install ./tsduck_3.36-3528.ubuntu23_amd64.deb
```

#### Ubuntu 22.04.1 LTS ####

The latest neumoDVB version may not work anymore because of an outdated libstdc++. You may need to
upgrade Ubuntu or install an older version of neumoDVB.

```bash
sudo  apt install -y libboost-all-dev libgtk-3-0 libgtk-3-dev curl libcurl4-openssl-dev  libwxgtk-media3.0-gtk3-dev \
gettext libexif-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev   python3-configobj python3-cachetools \
python3-jinja2 python3-pip clang-format python3-sip-dev libconfig-dev libconfig++-dev libdvbcsa-dev  libmpv-dev \
freeglut3-dev libwxgtk3.0-gtk3-dev  python3-wxgtk-media4.0 python3-wxgtk-webview4.0 python3-wxgtk4.0 python3-scipy \
clang lsb-core lsb-release python3-regex liblog4cxx12 liblog4cxx-dev freeglut3 fmt fmt-dev espeak
```
In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```

#### Ubuntu 20.04.4 LTS ####

The latest neumoDVB version may not work anymore because of an outdated libstdc++. You may need to
upgrade ubuntu or install an older version of neumoDVB.

The following instructions may be helpful when installing an older version of neumoDVB.

Install clang and clang++ with a version number of at least 14. The newest neumoDVB code does not compile
with the clang included in ubuntu

```bash
sudo apt install -y libboost-all-dev libgtk-3-0 libgtk-3-dev curl libcurl4-openssl-dev libwxgtk-media3.0-gtk3-dev \
gettext libexif-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev python3-jinja2 python3-pip clang-format \
python3-sip-dev libconfig-dev libconfig++-dev libdvbcsa-dev libmpv-dev freeglut3-dev python-wxgtk3.0 python3-wxgtk-media4.0 \
python3-wxgtk-webview4.0 python3-wxgtk4.0 python3-scipy clang lsb-core lsb-release python3-regex fmt fmt-dev
```

In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```


### Download and compile neumodvb ###

The software can be downloaded as follows

```bash
    cd ~/
    git clone --recursive https://github.com/deeptho/neumodvb
```

Next, build NeumoDVB as follows:

```bash
    cd ~/neumodvb
    mkdir build
    mkdir build_ext
    cd ~/neumodvb/build
    cmake ..
    make -j`nproc`
```
In case of problems, first check for missing pre-requisite software. For some of the pre-requisite
software, fairly recent versions are needed.


For troubleshooting, the following information may be useful:

* Some of the source code being compiled is generated by a python script `src/neumodb/neumodb.py`,
  which takes its input from configuration files `dbdefs.py` located in `src/neumodb/chdb`, and similar
  directories. The resulting code is placed in `build/src/neumodb/chdb` and similar places
* To speed up compilation, some C++ headers are pre-compiled. Usually this works fine, but in some
  cases, after making changes to header files, the precompiled headers become out of date and
  need to be rebuild, Usually  this will be clear from  compiler errors, but in rare cases, the
  result will be worse: neumoDVB will compile and then crash in strange ways.  When this happens,
  remove all  files under `/build`, run `cmake ..` again in the build directory.

  Alternatively, modify the CMakeList.txt files to remove header pre-compilation
* That being said, the normal and fastest way of recompiling after making changes to sources -- which should work
  in most cases -- is simply as follows:

```bash
    cd ~/neumodvb/build;
    make -j`nproc`
```

* In any case, if your distribution uses `ccache`, compilation will be much faster the second time around,
  even after you clear `build/` completely.
* NeumoDVB contains copies of some external software packages, some of which have been modified.
 * Specifically, the database code `lmdb` (renamed to `neumolmdb` to avoid confusion with any existing lmdb
  on your system) contains a few small, but crucial changes compared to the source (e.g., related to setting the
  `FD_CLOEXEC` flag.
 * The `wxsvg` library (renamed to `neumowxsvg`) has been more heavily modified. The modifications
  consist of added code to allow for efficient embedded of the mpv video player.


After neumoDVB has been built successfully, you can install it if you wish. Installation is optional:
you also run the software directly from the build tree. This makes it easier to debug problems and
most users indeed run into problems by installing neumoDVB (e.g., because older installed versions
interfere with newer non installed versions, or because changes in cmake or python lead to unexpected changes
in install paths).

**For this reason `make install` is deliberately disabled now***. By editing one line somewhere in the
code it can be re-enabled. Then installation works as follows:

```bash
    cd ~/neumodvb/build
    sudo make install
```

For packaging purposes, commands like `make install DESTDIR=/tmp/temporary_location` also work fine.


### Install blindscan drivers ###

neumoDVB can work with the regular linuxDVB drivers and supports all cards supported by these drivers.

If you own a supported card, it is best to install the neumo blindscan kernel drivers.
These drivers are available at
[https://github.com/deeptho/blindscan](https://github.com/deeptho/blindscan).

Without these drivers, features like blindscan and spectral analysis will not work.
neumoDVB has not been tested with the regular kernel drivers. Crashes may occur if you try
to use unsupported features. These are bugs. Report them on the issue tracker.
