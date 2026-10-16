%bcond_with bundled_opencv
%bcond_with bundled_glm
%{!?sv_opencv_min_version:%global sv_opencv_min_version 4.5}
%{!?sv_opencv_version:%global sv_opencv_version 4.10.0}
%{!?sv_glm_version:%global sv_glm_version 1.0.1}
%if %{with bundled_opencv}
# Private OpenCV must not satisfy dependencies of unrelated installed applications.
%global __provides_exclude ^libopencv_.*
%global __requires_exclude ^libopencv_.*
%endif

Name:           surround-view-cpu
Version:        0.6.0
Release:        3
Summary:        CPU and camera qualification tools for Aurora
License:        LicenseRef-Proprietary AND CC0-1.0 AND Apache-2.0 AND MIT
Source0:        surround-view-%{version}.tar.gz
%if %{with bundled_opencv}
Source1:        surround-view-opencv-%{sv_opencv_version}-%{_target_cpu}.tar.gz
%endif
%if %{with bundled_glm}
Source2:        surround-view-glm-%{sv_glm_version}.tar.gz
%endif
Conflicts:      surround-view

BuildRequires:  gcc-c++
# CMake is a build-host tool supplied by the Aurora SDK/mb2 environment.
BuildRequires:  ninja
BuildRequires:  boost-devel >= 1.75
%if !%{with bundled_glm}
BuildRequires:  glm-devel
%endif
%if !%{with bundled_opencv}
BuildRequires:  opencv-devel >= %{sv_opencv_min_version}
%else
BuildRequires:  binutils
%endif
BuildRequires:  pkgconfig(openssl)

%description
CPU-first research package for checking the target toolchain, camera model,
configuration, frame synchronisation, framing, and report generation before
qualifying an EGL backend. GPU and display results are explicitly unavailable.

%prep
%autosetup -n surround-view-%{version}

%if %{with bundled_opencv}
mkdir -p vendor
tar -xf %{SOURCE1} -C vendor
%endif
%if %{with bundled_glm}
mkdir -p vendor
tar -xf %{SOURCE2} -C vendor
%endif

%build
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_lib} \
    -DSV_AURORA=ON \
    -DSV_GPU=OFF \
    -DSV_CLIENT=OFF \
    -DSV_SIMULATOR=OFF \
    -DSV_PLATFORM_TEST=ON \
    -DSV_FETCH_MISSING_DEPS=OFF \
    -DSV_OPENCV_MIN_VERSION=%{sv_opencv_min_version} \
%if %{with bundled_opencv}
    -DSV_BUNDLE_OPENCV=ON \
    -DSV_OPENCV_BUNDLE_ROOT="$PWD/vendor/opencv" \
%endif
%if %{with bundled_glm}
    -DSV_GLM_INCLUDE_DIR="$PWD/vendor/glm" \
%endif
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
%{_bindir}/sv-core-tests
%{_bindir}/sv-source-tests
%{_bindir}/sv-platform-test
%{_bindir}/sv-calibrate
%{_bindir}/sv-capture
%{_bindir}/sv-scene
%{_libdir}/libsv-client-lib.a
%{_libdir}/libsv-wire.a
%{_libdir}/cmake/svClient
%{_includedir}/sv
%{_datadir}/surround-view

%license %{_datadir}/licenses/surround-view
%if %{with bundled_opencv}
%{_libdir}/surround-view/opencv
%endif
