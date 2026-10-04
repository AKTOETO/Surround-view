Name:           surround-view-cpu
Version:        0.2.0
Release:        1
Summary:        CPU and camera qualification tools for Aurora
License:        LicenseRef-Proprietary AND CC0-1.0
Source0:        surround-view-%{version}.tar.gz
Conflicts:      surround-view

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja
BuildRequires:  boost-devel >= 1.75
BuildRequires:  glm-devel
BuildRequires:  opencv-devel >= 4.5
BuildRequires:  pkgconfig(openssl)

%description
CPU-first research package for checking the target toolchain, camera model,
configuration, frame synchronisation, framing, and report generation before
qualifying an EGL backend. GPU and display results are explicitly unavailable.

%prep
%autosetup -n surround-view-%{version}

%build
%cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSV_AURORA=ON \
    -DSV_GPU=OFF \
    -DSV_CLIENT=OFF \
    -DSV_PLATFORM_TEST=ON \
    -DSV_PYTHON_TESTS=OFF \
    -DBUILD_TESTING=OFF
%ninja_build

%install
%ninja_install

%files
%defattr(-,root,root,-)
%{_bindir}/sv-project
%{_bindir}/sv-core-tests
%{_bindir}/sv-platform-test
%{_bindir}/sv-calibrate
%{_bindir}/sv-capture
%{_bindir}/sv-scene
%{_datadir}/surround-view
