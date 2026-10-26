import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const run = args => execFileSync('cmake', args, { stdio: 'pipe' });
const files = dir => readdirSync(dir, { withFileTypes: true }).flatMap(entry => entry.isDirectory() ? files(join(dir, entry.name)) : [join(dir, entry.name)]);

test('language-free source dependency export installs only ark and remains relocatable', () => {
  const work = mkdtempSync(join(tmpdir(), 'ark-install-policy-'));
  try {
    const producer = join(work, 'producer');
    mkdirSync(producer);
    for (const name of ['cmake', 'config', 'LICENSE', 'README.md', 'README_CN.md']) cpSync(join(root, name), join(producer, name), { recursive: true });
    mkdirSync(join(producer, 'dep'));
    writeFileSync(join(producer, 'dep/CMakeLists.txt'), `add_library(arknet INTERFACE)
add_library(arknet::arknet ALIAS arknet)
install(TARGETS arknet EXPORT arknetTargets)
install(EXPORT arknetTargets NAMESPACE arknet:: DESTINATION lib/cmake/arknet)
`);
    writeFileSync(join(producer, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(ark VERSION 0.2.2 LANGUAGES NONE)
include(GNUInstallDirs)
set(ARK_INSTALL ON)
set(ARK_BUILD_APPLICATIONS OFF)
set(ARK_BUILD_CONFIG OFF)
set(ARK_BUILD_PROTO OFF)
set(ARKNET_USE_BOOST_ASIO ON)
set(ARKNET_ENABLE_SSL OFF)
add_subdirectory(dep EXCLUDE_FROM_ALL)
function(find_package name)
  if(name STREQUAL "spdlog")
    add_library(spdlog::spdlog_header_only INTERFACE IMPORTED GLOBAL)
  elseif(name STREQUAL "readerwriterqueue")
    add_library(readerwriterqueue::readerwriterqueue INTERFACE IMPORTED GLOBAL)
  endif()
endfunction()
add_library(RapidXML::RapidXML INTERFACE IMPORTED)
add_subdirectory("${root}/src/config" config-build)
add_subdirectory("${root}/src/core" core-build)
add_library(ark_sdk INTERFACE)
add_library(ark::ark ALIAS ark_sdk)
target_link_libraries(ark_sdk INTERFACE ark::core)
add_subdirectory("${root}/src/plugins" plugin-build)
include(cmake/ark_install.cmake)
`);
    const build = join(work, 'build');
    const prefix = join(work, 'prefix');
    const relocated = join(work, 'relocated');
    run(['-S', producer, '-B', build]);
    run(['--install', build, '--prefix', prefix]);
    assert.equal(existsSync(join(prefix, 'lib/cmake/arknet')), false, 'arknet must remain externally installed');
    const cpackConfig = readFileSync(join(build, 'CPackConfig.cmake'), 'utf8');
    assert.match(cpackConfig, /CPACK_GENERATOR "TGZ;ZIP"/);
    for (const generator of ['TGZ', 'ZIP']) {
      execFileSync('cpack', ['--config', join(build, 'CPackConfig.cmake'), '-G', generator], { stdio: 'pipe' });
    }
    const archives = readdirSync(join(build, 'packages')).filter(name => /\.(tar\.gz|zip)$/.test(name));
    assert.equal(archives.length, 2);
    for (const archive of archives) {
      const inventory = run(['-E', 'tar', 'tf', join(build, 'packages', archive)]).toString();
      assert.match(inventory, /include\/ark\/mysql\/mysql_module\.hpp/);
      assert.doesNotMatch(inventory, /CMakeCache|CMakeFiles|\/src\/|\/arknet\//);
    }
    renameSync(prefix, relocated);
    const installed = files(relocated);
    for (const file of installed.filter(file => file.endsWith('.cmake'))) {
      const text = readFileSync(file, 'utf8');
      for (const forbidden of [root, producer, build, prefix]) assert.ok(!text.includes(forbidden), `${file}: ${forbidden}`);
    }
    for (const name of ['mysql/mysql_module.hpp', 'kernel/kernel_plugin.hpp', 'etcd_client/etcd_client_plugin.hpp', 'etcd_client/etcd_client_module_impl.hpp', 'kafka/kafka.hpp', 'kafka/kafka_module.hpp', 'kafka/kafka_module_impl.hpp', 'kafka/kafka_plugin.hpp', 'physic/physic_module_impl.hpp', 'application_manifest.hpp']) {
      assert.ok(existsSync(join(relocated, 'include/ark', name)), name);
    }
    for (const name of ['mysql/mysql_plugin.hpp', 'bus/bus_config_parser.hpp', 'http/http_plugin.hpp', 'net/http_client_module_impl.hpp', 'net/http_client_factory.hpp', 'kernel/kernel_factory.hpp', 'etcd_client/etcd_client_factory.hpp', 'kafka/kafka_factory.hpp', 'configuration_reader.hpp']) {
      assert.equal(existsSync(join(relocated, 'include/ark', name)), false, name);
    }
    rmSync(producer, { recursive: true });
    rmSync(build, { recursive: true });
    const consumer = join(work, 'consumer');
    mkdirSync(consumer);
    writeFileSync(join(consumer, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(config_consumer LANGUAGES NONE)
find_package(ark CONFIG REQUIRED COMPONENTS config_headers)
if(NOT TARGET ark::config_headers OR TARGET ark::core OR TARGET arknet::arknet)
  message(FATAL_ERROR "config headers must not require or import core/arknet")
endif()
`);
    run(['-S', consumer, '-B', join(work, 'consumer-build'), `-DCMAKE_PREFIX_PATH=${relocated}`]);
    // Mock only external dependency discovery to test the real installed export.
    writeFileSync(join(consumer, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(core_consumer LANGUAGES NONE)
function(find_package name)
  if(name STREQUAL "ark")
    _find_package(\${ARGV})
    return()
  endif()
  set(\${name}_FOUND TRUE PARENT_SCOPE)
  if(name STREQUAL "arknet")
    add_library(arknet::arknet INTERFACE IMPORTED)
    set(ARKNET_USE_BOOST_ASIO ON PARENT_SCOPE)
    set(ARKNET_ENABLE_SSL OFF PARENT_SCOPE)
  elseif(name STREQUAL "spdlog")
    add_library(spdlog::spdlog_header_only INTERFACE IMPORTED)
  elseif(name STREQUAL "readerwriterqueue")
    add_library(readerwriterqueue::readerwriterqueue INTERFACE IMPORTED)
  elseif(name STREQUAL "RapidXML")
    add_library(RapidXML::RapidXML INTERFACE IMPORTED)
  endif()
endfunction()
find_package(ark CONFIG REQUIRED COMPONENTS core mysql)
foreach(name IN ITEMS core mysql redis utility kernel physic)
  if(NOT TARGET ark::\${name})
    message(FATAL_ERROR "Missing public target: \${name}")
  endif()
endforeach()
`);
    run(['-S', consumer, '-B', join(work, 'core-consumer-build'), `-DCMAKE_PREFIX_PATH=${relocated}`]);
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
});

test('install verification rejects an existing work directory without changing it', () => {
  const work = mkdtempSync(join(tmpdir(), 'ark-install-existing-'));
  try {
    const sentinel = join(work, 'keep.txt');
    writeFileSync(sentinel, 'preserve existing user files\n');
    for (const script of ['check_install.cmake', 'run_isolated.cmake']) {
      assert.throws(() => run([
        `-DARK_BUILD_DIR=${work}/build`, `-DARK_SOURCE_DIR=${root}`, `-DARK_TEST_WORK=${work}`,
        '-P', join(root, 'tests/install', script),
      ]), error => error.stderr.toString().includes('ARK_TEST_WORK must not already exist'));
      assert.equal(readFileSync(sentinel, 'utf8'), 'preserve existing user files\n');
    }
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
});
