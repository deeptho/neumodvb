Name:    neumodvb
Version: 1.6
Release: 1%{?dist}
Summary: Neumo DVBS/S2/T/C STB program

License: GPLv2
URL: https://github.com/deeptho/neumodvb
Source0: %{name}-%{version}.tar.gz
BuildRoot:          %{_tmppath}/%{name}-%{version}-%{release}-root

BuildRequires:  git clang cmake clang-tools-extra libtool curl-devel log4cxx log4cxx-devel libconfig-devel wxGTK-devel gtk3-devel freeglut-devel librsvg2-devel libexif-devel expat-devel python3-jinja2 python3-sip-devel python3-configobj wxWidgets-devel wxBase-devel mpv-libs-devel libX11-devel libglvnd-devel libdvbcsa-devel redhat-lsb-core libuuid-devel boost-devel curl-devel fmt-devel python3-regex gdb


Requires: boost-program-options boost-regex curl-devel log4cxx libconfig wxGTK wxGTK-devel libexif python3-wxpython4  python3-matplotlib-wx python3-gobject-base  python3-configobj python3-regex python3-matplotlib-wx python3-scipy wxBase ffmpeg-libs libglvnd-devel espeak mesa-dri-drivers tsduck fmt python3-regex python3-cachetools tsduck espeak



ExclusiveArch: x86_64 aarch64



%description
Neumo DVBS/S2/T/C STB program. Viewing and recording services.
Scanning muxes and services. blindscan and spectrumscan

%prep
%setup -q -n %{name}-%{version}

%build
mkdir build
cd build
unset CFLAGS
unset CXXFLAGS
unset LDFLAGS
cmake -DCMAKE_INSTALL_PREFIX="/usr" ..
make -j`nproc`



%install
rm -rf $RPM_BUILD_ROOT
cd build
make -j `nproc`
make install VERBOSE=1 DESTDIR=$RPM_BUILD_ROOT create_packlist=0
find $RPM_BUILD_ROOT -depth -type d -exec rmdir {} 2>/dev/null \;
rm -fr $RPM_BUILD_ROOT/usr/include
%{_fixperms} $RPM_BUILD_ROOT/*



%files
%{_bindir}/neumodvb
%{_bindir}/neumoupgrade
%{_bindir}/neumoupgrade.py
%{_sysconfdir}/neumodvb
%{_libdir}/*
%{_prefix}/lib/*
%changelog
