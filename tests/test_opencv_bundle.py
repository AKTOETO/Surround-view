"""Native ELF fixture checks; no OpenCV download and no Aurora ABI claim."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


class OpenCVBundle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='sv-opencv-bundle-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.directory = Path(cls.tmp.name)
        source = cls.directory / 'libraries'
        source.mkdir()
        (source / 'core.c').write_text('int core_value(void) { return 7; }\n')
        (source / 'imgproc.c').write_text('extern int core_value(void); int image_value(void) { return core_value()+3; }\n')
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(BundleLibraries C)
add_library(opencv_fixture_core SHARED core.c)
add_library(opencv_fixture_imgproc SHARED imgproc.c)
target_link_libraries(opencv_fixture_imgproc PRIVATE opencv_fixture_core)
set_target_properties(opencv_fixture_core opencv_fixture_imgproc PROPERTIES VERSION 4.10.0 SOVERSION 410)
install(TARGETS opencv_fixture_core opencv_fixture_imgproc LIBRARY DESTINATION lib)
''')
        cls.prefix = cls.directory / 'prepared'
        for command in [('cmake', '-S', str(source), '-B', str(source / 'build'),
                         f'-DCMAKE_INSTALL_PREFIX={cls.prefix}', '-DCMAKE_INSTALL_RPATH=$ORIGIN'),
                        ('cmake', '--build', str(source / 'build')),
                        ('cmake', '--install', str(source / 'build'))]:
            result = run(*command)
            if result.returncode:
                raise RuntimeError(result.stdout)
        config = cls.prefix / 'lib/cmake/opencv4'
        config.mkdir(parents=True)
        (config / 'OpenCVConfig.cmake').write_text('''set(OpenCV_VERSION 4.10.0)
set(OpenCV_FOUND TRUE)
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(OpenCV_LIBS opencv_fixture_imgproc)
set(OpenCV_INCLUDE_DIRS "${_root}/include")
if(NOT TARGET opencv_fixture_imgproc)
  add_library(opencv_fixture_imgproc SHARED IMPORTED)
  set_target_properties(opencv_fixture_imgproc PROPERTIES IMPORTED_LOCATION "${_root}/lib/libopencv_fixture_imgproc.so")
endif()
''')
        (cls.prefix / 'licenses').mkdir()
        (cls.prefix / 'licenses/LICENSE').write_text('Synthetic ELF test fixture, not OpenCV.\n')
        (cls.prefix / 'include/glm').mkdir(parents=True)
        (cls.prefix / 'include/glm/glm.hpp').write_text('// GLM discovery fixture\n')
        cls.app = cls.directory / 'app'
        cls.app.mkdir()
        (cls.app / 'main.c').write_text('extern int image_value(void); int main(void) { return image_value()!=10; }\n')
        (cls.app / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(BundleConsumer C)
include(GNUInstallDirs)
if(SV_FIXTURE_PROCESSOR)
  set(CMAKE_SYSTEM_PROCESSOR "${{SV_FIXTURE_PROCESSOR}}")
endif()
set(SV_BUNDLE_OPENCV ON)
set(SV_FETCH_MISSING_DEPS OFF)
set(SV_GLM_INCLUDE_DIR "${{SV_OPENCV_BUNDLE_ROOT}}/include")
include("{ROOT}/cmake/FetchMissingDependencies.cmake")
sv_find_glm()
sv_find_opencv()
include("{ROOT}/cmake/BundleOpenCV.cmake")
sv_install_bundled_opencv()
add_executable(bundle-consumer main.c)
target_link_libraries(bundle-consumer PRIVATE ${{SV_OPENCV_LIBRARIES}})
sv_opencv_private_rpath(bundle-consumer)
install(TARGETS bundle-consumer RUNTIME DESTINATION bin)
''')

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(dir=self.directory))
        self.prefix_copy = self.case / 'prefix'
        shutil.copytree(self.prefix, self.prefix_copy, symlinks=True)

    def configure(self, *extra):
        return run('cmake', '-S', str(self.app), '-B', str(self.case / 'build'),
                   f'-DSV_OPENCV_BUNDLE_ROOT={self.prefix_copy}', '-DSV_OPENCV_MIN_VERSION=4.6',
                   '-DCMAKE_INSTALL_LIBDIR=lib', f'-DCMAKE_INSTALL_PREFIX={self.case}/install', *extra)

    def test_installed_app_uses_private_transitive_libraries_after_relocation(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout)
        for command in [('cmake', '--build', str(self.case / 'build')),
                        ('cmake', '--install', str(self.case / 'build'))]:
            result = run(*command)
            self.assertEqual(result.returncode, 0, result.stdout)
        relocated = self.case / 'relocated'
        shutil.move(self.case / 'install', relocated)
        shutil.move(self.prefix_copy, self.case / 'hidden-prefix')
        environment = os.environ.copy()
        environment.pop('LD_LIBRARY_PATH', None)
        environment.pop('LD_PRELOAD', None)
        result = subprocess.run([str(relocated / 'bin/bundle-consumer')], env=environment,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        private = relocated / 'lib/surround-view/opencv'
        self.assertTrue((private / 'libopencv_fixture_core.so.410').is_symlink())
        self.assertTrue((private / 'libopencv_fixture_imgproc.so.410').exists())
        self.assertFalse((relocated / 'include').exists())
        self.assertTrue((relocated / 'share/licenses/surround-view/opencv-bundle/LICENSE').exists())

    def test_bundle_without_license_notices_is_rejected(self):
        shutil.rmtree(self.prefix_copy / 'licenses')
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('must include upstream and third-party notices', ' '.join(result.stdout.split()))

    def test_missing_transitive_opencv_library_is_rejected(self):
        for path in (self.prefix_copy / 'lib').glob('libopencv_fixture_core.so*'):
            path.unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing private dependency', result.stdout)

    def test_absolute_symlink_is_rejected(self):
        link = self.prefix_copy / 'lib/libopencv_fixture_core.so'
        link.unlink()
        link.symlink_to(self.prefix_copy / 'lib/libopencv_fixture_core.so.4.10.0')
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('symlinks must be relative', result.stdout)

    def test_missing_soname_link_is_rejected(self):
        (self.prefix_copy / 'lib/libopencv_fixture_core.so.410').unlink()
        (self.prefix_copy / 'lib/libopencv_fixture_core.so').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing SONAME link', result.stdout)

    def test_non_elf_shared_library_is_rejected(self):
        (self.prefix_copy / 'lib/libopencv_fixture_core.so.4.10.0').write_text('not an ELF library')
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('architecture/type mismatch', result.stdout)

    def test_package_config_cannot_link_a_library_outside_bundle(self):
        config = self.prefix_copy / 'lib/cmake/opencv4/OpenCVConfig.cmake'
        config.write_text(config.read_text().replace('${_root}/lib/libopencv_fixture_imgproc.so',
                                                    str(self.prefix / 'lib/libopencv_fixture_imgproc.so')))
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('escapes validated bundle', result.stdout)

    def test_package_config_cannot_use_headers_outside_bundle(self):
        config = self.prefix_copy / 'lib/cmake/opencv4/OpenCVConfig.cmake'
        config.write_text(config.read_text().replace('${_root}/include', str(self.prefix / 'include')))
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('include directory escapes bundle', result.stdout)

    def test_wrong_target_architecture_is_rejected_without_execution(self):
        other = 'x86_64' if platform.machine() in ('aarch64', 'arm64') else 'aarch64'
        result = self.configure(f'-DSV_FIXTURE_PROCESSOR={other}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('architecture/type mismatch', result.stdout)

    def test_old_version_never_falls_back_to_system_opencv(self):
        result = self.configure('-DSV_OPENCV_MIN_VERSION=6.0')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no system/network fallback', ' '.join(result.stdout.split()))

    @unittest.skipUnless(shutil.which('patchelf'), 'patchelf needed to inject bad RUNPATH')
    def test_absolute_runpath_is_rejected(self):
        result = run('patchelf', '--set-rpath', '/tmp/build-only',
                     str(self.prefix_copy / 'lib/libopencv_fixture_imgproc.so.4.10.0'))
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RUNPATH must be exactly', result.stdout)

    def test_aurora_rejects_network_fetch_before_dependency_discovery(self):
        result = run('cmake', '-S', str(ROOT), '-B', str(self.case / 'network'),
                     '-DSV_AURORA=ON', '-DSV_FETCH_MISSING_DEPS=ON')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Aurora builds are offline', result.stdout)


class RpmBundleProfiles(unittest.TestCase):
    def test_spec_conditionals_keep_platform_dependencies_and_private_payload(self):
        local = ROOT / 'artifacts/rpm-tools/usr/bin/rpmspec'
        executable = shutil.which('rpmspec') or (str(local) if local.exists() else None)
        if not executable:
            self.skipTest('rpmspec unavailable; ELF/CMake checks still run')
        environment = os.environ.copy()
        if executable == str(local):
            environment['LD_LIBRARY_PATH'] = str(local.parent.parent / 'lib')
            environment['RPM_CONFIGDIR'] = str(local.parent.parent / 'lib/rpm')
        for name in ('surround-view', 'surround-view-cpu'):
            for opencv in (False, True):
                for glm in (False, True):
                    with self.subTest(profile=name, opencv=opencv, glm=glm):
                        command = [executable, '-P', '--with' if opencv else '--without',
                                   'bundled_opencv', '--with' if glm else '--without',
                                   'bundled_glm', str(ROOT / 'packaging/rpm' / (name + '.spec'))]
                        result = subprocess.run(command, env=environment, text=True,
                                                capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        text = result.stdout
                        self.assertEqual('BuildRequires:  opencv-devel' in text, not opencv)
                        self.assertEqual('BuildRequires:  glm-devel' in text, not glm)
                        self.assertEqual('Source1:' in text, opencv)
                        self.assertEqual('Source2:' in text, glm)
                        self.assertEqual('-DSV_BUNDLE_OPENCV=ON' in text, opencv)
                        self.assertEqual('-DSV_GLM_INCLUDE_DIR=' in text, glm)
                        self.assertIn('-DSV_FETCH_MISSING_DEPS=OFF', text)
                        self.assertIn('BuildRequires:  boost-devel', text)
                        self.assertIn('BuildRequires:  pkgconfig(openssl)', text)
                        self.assertIn('%license /usr/share/licenses/surround-view', text)
                        files = text.split('%files', 1)[1]
                        self.assertEqual('/surround-view/opencv' in files, opencv)


if __name__ == '__main__':
    unittest.main()
