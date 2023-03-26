# neumoDVB #
## Installation ##

### Install prerequisite software ###

This software depends on several other software packages. If you wish to compile from source,
also a number of development packages are needed. The names of the packages depend on your linux
distribution.

The below software list may be incomplete or may contain no longer needed packages.
Please open a ticket if you discover mistakes

#### Fedora 33 or 34 ####

On Fedora 33 or 34, install at least the following RPMs with `sudo dnf install -y <PACKAGE>`:

```shell
sudo dnf install -y clang clang-tools-extra libtool boost-program-options boost-regex curl-devel \
log4cxx log4cxx-devel libconfig libconfig-devel wxGTK3 wxGTK3-devel gtk3-devel freeglut-devel \
librsvg2-devel libexif-devel libexif gobject-introspection expat-devel python3-wxpython4 \
python3-jinja2 python3-matplotlib-wx python3-sip-devel  python3-gobject-base  python3-configobj \
python3-regex python3-matplotlib-wx python3-scipy wxWidgets-devel wxBase3 wxBase3-devel mpv-libs-devel \
ffmpeg-devel ffmpeg-libs libX11-devel libglvnd-devel libdvbcsa-devel espeak mesa-dri-drivers \
https://github.com/tsduck/tsduck/releases/download/v3.28-2551/tsduck-3.28-2551.fc34.x86_64.rpm
```

In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```

#### Fedora 36 ####
On Fedora 36:  install at least the following RPMs with `sudo dnf install -y <PACKAGE>`:

``` bash
sudo dnf install -y redhat-lsb-core cmake clang clang-tools-extra libtool boost-program-options \
boost-devel boost-regex boost-context curl-devel log4cxx log4cxx-devel libconfig libconfig-devel \
wxGTK3 wxGTK3-devel gtk3-devel freeglut-devel librsvg2-devel libexif-devel libexif \
gobject-introspection expat-devel python3-wxpython4  python3-jinja2 python3-matplotlib-wx \
python3-sip-devel  python3-cachetools python3-gobject-base  python3-configobj \
python3-regex python3-matplotlib-wx python3-scipy wxWidgets-devel wxBase3 \
wxBase3-devel libX11-devel libglvnd-devel espeak mesa-dri-drivers mpv-libs-devel  libdvbcsa-devel \
ffmpeg-devel mpv-libs-devel tsduck
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

#### Fedora 37 ####
On Fedora 37:  install at least the following RPMs with `sudo dnf install -y <PACKAGE>`:

```bash
sudo dnf install -y redhat-lsb-core cmake clang clang-tools-extra libtool boost-program-options \
boost-devel boost-regex boost-context curl-devel log4cxx log4cxx-devel libconfig libconfig-devel \
wxGTK3 wxGTK-devel gtk3-devel freeglut-devel librsvg2-devel libexif-devel libexif gobject-introspection \
expat-devel python3-wxpython4 python3-jinja2 python3-matplotlib-wx python3-sip-devel  python3-cachetools \
python3-gobject-base python3-configobj python3-regex python3-matplotlib-wx python3-scipy wxWidgets-devel \
wxBase3 wxBase-devel libX11-devel libglvnd-devel espeak mesa-dri-drivers mpv-libs-devel  libdvbcsa-devel \
ffmpeg-devel mpv-libs-devel https://github.com/tsduck/tsduck/releases/download/v3.30-2710/tsduck-3.30-2710.fc35.x86_64.rpm \
https://github.com/tsduck/tsduck/releases/download/v3.30-2710/tsduck-devel-3.30-2710.fc35.x86_64.rpm
```

Also make sure that the following packages are **not** installed, as they might lead to compiling or linking with the wrong
libraries, resulting in crashes:

```bash
sudo dnf remove wxGTK3-devel
```

Some of these pacakges are provided by rpmfusion, which can be installed using the instructions at
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
#### Debian ####

To install neumodvb in a Debian distro (Ubuntu, Linux Mint), first ensure that you are running a
recent version:

```bash
sudo apt update
sudo apt upgrade
sudo apt install build-essential
sudo add-apt-repository universe
sudo apt update
```

Then  install pre-requisite software:

```bash
sudo apt install -y clang clang-tools libtool libboost-program-options libboost libboost-regex libboost-context \
curl libcurl4 libcurl4-opensst-dev liblog4cxx-dev liblog4cxx libconfig-libconfig-dev libwxgtk3.0-gtk3 libgtk-dev \
freeglut3 freeglut3-dev librsvg2-dev libexif-dev libegobject-introspection libexpat1-dev python3-wxgtk2.4 \
python3-configobj  python3-sip-dev python3--matplot-lib python3-jinja2 python3-regex python3-scipy wxbase3-dev-wxbase3 \
wxgtk3 libmp3-dev ffmpeg libx11-dev libglvnd-dev libdvbcsa-dev espeak cmake python3-cachetools
```

Also, install a recent clang compiler:

```bash
readonly llvmVersion=14
sudo apt-get update -y
sudo apt-get install -y lsb-release wget software-properties-common apt-utils
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add -
sudo add-apt-repository -y "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-${llvmVersion} main"
sudo apt-get update -y
sudo apt-get install clang-${llvmVersion}
sudo apt remove  clang
sudo ln -fs /usr/bin/clang-14  /usr/bin/clang
sudo ln -fs /usr/bin/clang++-14  /usr/bin/clang++
```

In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```
sudo pip3 regex mpl_scatter_density
```

#### Ubuntu 22.04.1 LTS ####

```bash
sudo  apt install -y libboost-all-dev libgtk-3-0 libgtk-3-dev curl libcurl4-openssl-dev  libwxgtk-media3.0-gtk3-dev \
gettext libexif-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev   python3-configobj python3-cachetools \
python3-jinja2 python3-pip clang-format python3-sip-dev libconfig-dev libconfig++-dev libdvbcsa-dev  libmpv-dev \
freeglut3-dev libwxgtk3.0-gtk3-dev  python3-wxgtk-media4.0 python3-wxgtk-webview4.0 python3-wxgtk4.0 python3-scipy \
clang lsb-core lsb-release python3-regex liblog4cxx12 liblog4cxx-dev freeglut3
```
In addition, some python code needs to be installed using `sudo pip3 install <PACKAGE>`;
at least the following packages are needed:

```bash
sudo pip3 install mpl_scatter_density
```

#### Ubuntu 20.04.4 LTS ####

Install clang and clang++ with a version number of at least 14. The newest neumoDVB code does not compile
with the clang included in ubuntu

```bash
sudo apt install -y libboost-all-dev libgtk-3-0 libgtk-3-dev curl libcurl4-openssl-dev libwxgtk-media3.0-gtk3-dev \
gettext libexif-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev python3-jinja2 python3-pip clang-format \
python3-sip-dev libconfig-dev libconfig++-dev libdvbcsa-dev libmpv-dev freeglut3-dev python-wxgtk3.0 python3-wxgtk-media4.0 \
python3-wxgtk-webview4.0 python3-wxgtk4.0 python3-scipy clang lsb-core lsb-release python3-regex
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


After NeumoDVB has been built successfully, you can install it if you wish. Installation is optional:
you also run the software directly from the build tree. This makes it easier to debug problems.

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
