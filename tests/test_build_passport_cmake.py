"""Exercise generated per-configuration metadata without fetching project dependencies."""
import json
from pathlib import Path
import re
import subprocess
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildPassportCMake(unittest.TestCase):
    def configure(self, directory, setup, generator=None):
        source = directory / 'source'
        source.mkdir()
        (source / 'dummy.cpp').write_text('#include \"sv/build_configuration.hpp\"\nconst char* configured() { return sv::build_detail::configured_json; }\n')
        (source / 'driver.cpp').write_text('#include <iostream>\nconst char* configured(); int main() { std::cout << configured(); }\n')
        (source / 'toolchain.cmake').write_text('# declaration-only fixture\n')
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(PassportTest LANGUAGES CXX)
add_library(sv-core STATIC dummy.cpp)
add_executable(dump driver.cpp)
target_link_libraries(dump PRIVATE sv-core)
target_include_directories(sv-core PRIVATE "${CMAKE_BINARY_DIR}/generated")
set(CMAKE_CXX_STANDARD 17)
set(SV_SOURCE_REVISION fixture)
set(SV_SOURCE_FINGERPRINT "''' + 'a' * 64 + '''")
set(SV_OPENCV_CONFIGURED_VERSION "fixture")
set(SV_OPENCV_PROVISION system)
set(SV_GLM_PROVISION system)
''' + setup + '\ninclude("' + str(ROOT / 'cmake/BuildPassport.cmake') + '")\nsv_configure_build_passport()\n')
        command = ['cmake', '-S', str(source), '-B', str(directory / 'build')]
        if generator:
            command += ['-G', generator]
        result = subprocess.run(command,
                                text=True, capture_output=True, timeout=30)
        return result

    def header(self, directory, config):
        text = (directory / 'build/generated' / config / 'sv/build_configuration.hpp').read_text()
        match = re.search(r'R"(SV_BUILD_\d+)\((.*)\)\1";', text, re.S)
        self.assertIsNotNone(match)
        return json.loads(match[2]), match[1]

    def test_quotes_backslashes_newlines_and_raw_delimiter_are_data(self):
        with tempfile.TemporaryDirectory(prefix='sv-build-cmake-') as temporary:
            directory = Path(temporary)
            flags = '-DNAME="quoted" C:\\sdk\\include\n$<CONFIG>\t;literal'
            setup = 'set(CMAKE_CXX_FLAGS [=[' + flags + ']=])\nset(SV_SDK_ID [=[value)SV_BUILD_0]=])'
            result = self.configure(directory, setup)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            descriptor, delimiter = self.header(directory, '')
            self.assertEqual(descriptor['configuration']['name'], 'unspecified')
            self.assertEqual(descriptor['configuration']['cxx_flags'], flags + ' ')
            self.assertEqual(descriptor['target']['sdk_id'], 'value)SV_BUILD_0')
            self.assertNotEqual(delimiter, 'SV_BUILD_0')

    def test_debug_and_release_have_distinct_flags_and_source_relative_toolchain_hash(self):
        with tempfile.TemporaryDirectory(prefix='sv-build-configs-') as temporary:
            directory = Path(temporary)
            result = self.configure(directory, '''set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_TOOLCHAIN_FILE "toolchain.cmake")
''')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            debug, _ = self.header(directory, 'Debug')
            release, _ = self.header(directory, 'Release')
            self.assertEqual(debug['configuration']['name'], 'Debug')
            self.assertEqual(release['configuration']['name'], 'Release')
            self.assertEqual(debug['configuration']['cxx_flags'], ' -O0 -g')
            self.assertEqual(release['configuration']['cxx_flags'], ' -O3 -DNDEBUG')
            import hashlib
            self.assertEqual(debug['toolchain']['toolchain_sha256'], hashlib.sha256(
                (directory / 'source/toolchain.cmake').read_bytes()).hexdigest())
            self.assertNotEqual(debug, release)

    @unittest.skipUnless(shutil.which('ninja'), 'Ninja Multi-Config requires ninja')
    def test_actual_multi_config_compilation_uses_its_own_header_before_stale_root(self):
        with tempfile.TemporaryDirectory(prefix='sv-build-multi-') as temporary:
            directory = Path(temporary)
            result = self.configure(directory, '', 'Ninja Multi-Config')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            stale = directory / 'build/generated/sv'
            stale.mkdir(exist_ok=True)
            (stale / 'build_configuration.hpp').write_text('#error stale header must not be selected\n')
            for config in ['Debug', 'Release']:
                build = subprocess.run(['cmake', '--build', str(directory / 'build'), '--config', config],
                                       text=True, capture_output=True, timeout=30)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                output = subprocess.check_output([str(directory / 'build' / config / 'dump')], text=True)
                self.assertEqual(json.loads(output)['configuration']['name'], config)

    def test_excessive_configured_metadata_fails_at_configure(self):
        with tempfile.TemporaryDirectory(prefix='sv-build-limit-') as temporary:
            result = self.configure(Path(temporary), 'set(SV_SDK_ID "' + 'x' * 13000 + '")')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('exceeds', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
