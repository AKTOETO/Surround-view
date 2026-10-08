%{!?sv_with_client:%global sv_with_client 0}

Name:           surround-view
Version:        0.6.0
Release:        3
Summary:        Surround view research tools and native platform qualification
License:        LicenseRef-Proprietary AND CC0-1.0 AND Apache-2.0 AND MIT
Source0:        surround-view-%{version}.tar.gz

BuildRequires:  gcc-c++
# CMake is a build-host tool supplied by the Aurora SDK/mb2 environment.
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
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_lib} \
    -DSV_AURORA=ON \
    -DSV_GPU=ON \
    -DSV_CLIENT=%{sv_with_client} \
    -DSV_QT_MAJOR=5 \
    -DSV_PLATFORM_TEST=ON \
    -DSV_FETCH_MISSING_DEPS=OFF \
    -DSV_PYTHON_TESTS=OFF \
    -DBUILD_TESTING=OFF
ninja -C build %{?_smp_mflags}

%install
DESTDIR="%{buildroot}" cmake --install build

%files
%defattr(-,root,root,-)
%{_bindir}/sv-client-probe
%{_bindir}/svctl
%{_bindir}/sv-project
%{_bindir}/sv-bench
%{_bindir}/sv-server
%{_bindir}/sv-core-tests
%{_bindir}/sv-source-tests
%{_bindir}/sv-platform-test
%{_bindir}/sv-calibrate
%{_bindir}/sv-capture
%{_bindir}/sv-scene
%if %{sv_with_client}
%{_bindir}/sv-client
%{_datadir}/applications/surround-view.desktop
%{_datadir}/icons/hicolor/128x128/apps/surround-view.png
%endif
%{_libdir}/libsv-client-lib.a
%{_libdir}/libsv-wire.a
%{_libdir}/cmake/svClient
%{_includedir}/sv
%{_datadir}/surround-view
