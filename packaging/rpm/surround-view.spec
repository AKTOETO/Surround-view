%{!?sv_with_client:%global sv_with_client 0}

Name:           surround-view
Version:        0.4.0
Release:        1
Summary:        Surround view research tools and native platform qualification
License:        LicenseRef-Proprietary AND CC0-1.0
Source0:        surround-view-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja
BuildRequires:  boost-devel >= 1.75
BuildRequires:  glm-devel
BuildRequires:  opencv-devel >= 4.5
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
%if %{sv_with_client}
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
Requires:       qt5-qtdeclarative
%endif

%description
Research deployment containing the renderer, replay server, OpenCV camera tools,
and a native report generator. Runtime qualification is performed on the actual
device after installation; cross-built programs are not executed by rpmbuild.
This is a laboratory package, not a certified Aurora application.

%prep
%autosetup

%build
%cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSV_AURORA=ON \
    -DSV_GPU=ON \
    -DSV_CLIENT=%{sv_with_client} \
    -DSV_QT_MAJOR=5 \
    -DSV_PLATFORM_TEST=ON \
    -DSV_PYTHON_TESTS=OFF \
    -DBUILD_TESTING=OFF
%ninja_build

%install
%ninja_install

%files
%defattr(-,root,root,-)
%{_bindir}/sv-project
%{_bindir}/sv-bench
%{_bindir}/sv-server
%{_bindir}/sv-core-tests
%{_bindir}/sv-platform-test
%{_bindir}/sv-calibrate
%{_bindir}/sv-capture
%{_bindir}/sv-scene
%if %{sv_with_client}
%{_bindir}/sv-client
%endif
%{_datadir}/surround-view
