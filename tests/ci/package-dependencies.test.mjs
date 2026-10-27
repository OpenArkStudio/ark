// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import test from 'node:test';

const read = path => readFileSync(new URL(`../../${path}`, import.meta.url), 'utf8');
const manifest = JSON.parse(read('vcpkg.json'));
const dependencies = values => values.map(value => typeof value === 'string' ? value : value.name);

test('core packages are shared while application, configuration and JSON dependencies retain separate features', () => {
  const core = dependencies(manifest.dependencies);
  assert.ok(core.includes('spdlog'));
  assert.ok(core.includes('readerwriterqueue'));
  for (const [owners, name] of [[['application'], 'args'], [['config'], 'tomlplusplus'], [['application', 'etcd-client', 'guest-room'], 'nlohmann-json']]) {
    for (const feature of owners) assert.ok(dependencies(manifest.features[feature].dependencies).includes(name));
    assert.ok(!core.includes(name), `${name} must remain optional`);
    for (const [other, value] of Object.entries(manifest.features)) {
      if (!owners.includes(other)) assert.ok(!dependencies(value.dependencies).includes(name), `${name} belongs to ${owners.join('/')}`);
    }
  }
  assert.ok(manifest['default-features'].includes('config'));
  assert.ok(!manifest['default-features'].includes('etcd-client'));
  assert.ok(!manifest['default-features'].includes('guest-room'));
});

test('the public core target propagates package usage requirements without a bundled include root', () => {
  const core = read('src/core/CMakeLists.txt');
  assert.match(core, /find_package\(spdlog 1\.17 CONFIG REQUIRED\)/);
  assert.match(core, /find_package\(readerwriterqueue CONFIG REQUIRED\)/);
  assert.match(core, /target_link_libraries\(ark_core INTERFACE[^)]*spdlog::spdlog_header_only[^)]*readerwriterqueue::readerwriterqueue/s);
  assert.doesNotMatch(core + read('CMakeLists.txt'), /3RD_PARTY|3rdparty/);
  assert.match(read('src/application/CMakeLists.txt'), /target_link_libraries\([^)]*taywee::args\)/);
  assert.match(read('src/plugins/etcd_client/CMakeLists.txt'), /BUILD_PLUGIN_MACRO\(ark_etcd_client_plugin nlohmann_json::nlohmann_json\)/);
  assert.doesNotMatch(read('src/plugins/net/net_service_manager_module_impl.cpp'), /json\.hpp/);
});

function applicationFeatures(workflow) {
  const applications = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(block => block.startsWith('  applications:\n'));
  const cache = applications.match(/VCPKG_CACHE_FEATURES: '([^']+)'/)[1].split(';').sort();
  const configure = applications.match(/-DVCPKG_MANIFEST_FEATURES=([^"\s]+)/)[1].split(';').sort();
  assert.deepEqual(configure, cache, 'cache metadata must describe the actual package features');
  assert.ok(configure.includes('etcd-client'), 'default etcd plugin requires its package feature');
  assert.ok(configure.includes('guest-room'), 'guest-room example requires its package feature');
}

test('CI configures the etcd and guest-room package features and caches exactly those features', () => {
  const workflow = read('.github/workflows/ci.yml');
  applicationFeatures(workflow);
  assert.throws(() => applicationFeatures(workflow.replace('VCPKG_CACHE_FEATURES: \'application;boost;compression-tests;etcd-client;guest-room\'',
    'VCPKG_CACHE_FEATURES: \'application;boost\'')), /cache metadata/);
  assert.throws(() => applicationFeatures(workflow.replaceAll('application;boost;compression-tests;etcd-client;guest-room', 'application;boost')), /requires its package feature/);
  assert.throws(() => applicationFeatures(workflow.replaceAll(';guest-room', '')), /guest-room example requires its package feature/);
  assert.doesNotMatch(workflow, /git submodule|Initialize pinned dependencies/);
  assert.equal(existsSync(new URL('../../.gitmodules', import.meta.url)), false);
});

function guestRoomApplications(workflow) {
  const applications = workflow.split('  applications:\n')[1].split('\n  database:')[0];
  assert.match(applications, /^          -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON$/m,
    'the shared Applications configure must enable guest-room on every selected platform');
  const build = applications.indexOf('run: cmake --build ci-build --parallel 2');
  const check = applications.indexOf('run: cmake --build ci-build --target check-core');
  assert.ok(build >= 0 && check > build, 'build all runtime plugins before running check-core');
}

test('Applications builds guest-room on every platform and runs platform-appropriate checks through check-core', () => {
  const workflow = read('.github/workflows/ci.yml');
  guestRoomApplications(workflow);
  assert.throws(() => guestRoomApplications(workflow.replace('-DARK_BUILD_GUEST_ROOM_EXAMPLE=ON',
    '-DARK_BUILD_GUEST_ROOM_EXAMPLE=OFF')), /must enable guest-room/);
  const examples = read('examples/CMakeLists.txt');
  assert.match(examples, /option\(ARK_BUILD_GUEST_ROOM_EXAMPLE[^\n]* OFF\)/);
  assert.match(examples, /if\(ARK_BUILD_GUEST_ROOM_EXAMPLE\)\s+add_subdirectory\(guest_room\)/);
  assert.match(examples, /add_library\(\$\{project_name\} SHARED \$\{PLUGIN_SRC\}\)/);
  const checks = read('examples/guest_room/CMakeLists.txt');
  const common = checks.split('if(UNIX)')[0];
  assert.match(common, /BUILD_PLUGIN_MACRO\("guest_room_plugin"\)/);
  assert.match(common, /add_executable\(ark_guest_room_codec_tests/);
  assert.doesNotMatch(common, /EXCLUDE_FROM_ALL|if\(WIN32\)/);
  assert.match(checks, /if\(UNIX\)[\s\S]*add_executable\(ark_guest_room_server_tests[\s\S]*add_custom_target\(check-guest-room DEPENDS check-guest-room-codec check-guest-room-server\)\s+else\(\)\s+add_custom_target\(check-guest-room DEPENDS check-guest-room-codec\)/);
  assert.match(read('tests/core/CMakeLists.txt'),
    /if\(TARGET check-guest-room\)\s+add_dependencies\(check-core check-guest-room\)/);
});

test('compression checks are opt-in and Applications CI runs all three codecs through check-core', () => {
  const features = manifest.features;
  assert.deepEqual(dependencies(features['compression-tests'].dependencies).sort(), ['lz4', 'zlib', 'zstd']);
  assert.ok(!dependencies(features.application.dependencies).includes('lz4'));
  assert.ok(!dependencies(manifest.dependencies).includes('lz4'));
  assert.ok(!manifest['default-features'].includes('compression-tests'));
  const root = read('CMakeLists.txt');
  assert.match(root, /option\(ARK_BUILD_COMPRESSION_TESTS[^\n]* OFF\)/);
  assert.match(root, /if\(ARK_BUILD_COMPRESSION_TESTS\)[\s\S]*add_subdirectory\(tests\/compression\)[\s\S]*add_dependencies\(check-core check-compression\)/);
  const checks = read('tests/compression/CMakeLists.txt');
  for (const dependency of ['ZLIB', 'Zstd', 'LZ4']) assert.match(checks, new RegExp(`find_package\\(${dependency} REQUIRED\\)`));
  assert.match(checks, /add_custom_target\(check-compression\s+COMMAND \$<TARGET_FILE:ark_compression_tests>/);
  const applications = read('.github/workflows/ci.yml').split('  applications:\n')[1].split('\n  database:')[0];
  assert.match(applications, /-DVCPKG_MANIFEST_FEATURES=[^\n]*compression-tests/);
  assert.match(applications, /-DARK_BUILD_COMPRESSION_TESTS=ON/);
  assert.match(applications, /run: cmake --build ci-build --target check-core/);
});

test('unsupported PhysX and simulate integrations fail explicitly instead of searching absent bundled SDKs', () => {
  for (const path of ['src/plugins/physic/CMakeLists.txt', 'src/server/simulate/CMakeLists.txt', 'examples/physics_scene/CMakeLists.txt']) {
    const source = read(path);
    assert.match(source, /message\(FATAL_ERROR/);
    assert.match(source, /PhysX 3/);
    assert.doesNotMatch(source, /3rdparty|find_package\(PhysX/);
  }
});

test('Applications CI enables Consul construction and production export checks without a service', () => {
  const applications = read('.github/workflows/ci.yml').split('  applications:\n')[1].split('\n  database:')[0];
  assert.match(applications, /-DARK_BUILD_PLUGIN_CONSUL=ON/);
  assert.match(applications, /run: cmake --build ci-build --target check-core/);
  const checks = read('tests/consul/CMakeLists.txt');
  assert.match(checks, /if\(TARGET ark_consul_plugin\)/);
  assert.match(checks, /add_dependencies\(check-core check-consul-plugin\)/);
  assert.match(checks, /add_dependencies\(check-core check-consul-construction\)/);
});

test('Applications CI explicitly enables Etcd Client ownership and production DSO checks', () => {
  const applications = read('.github/workflows/ci.yml').split('  applications:\n')[1].split('\n  database:')[0];
  assert.match(applications, /-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON/);
  assert.match(applications, /run: cmake --build ci-build --target check-core/);
  const core = read('tests/core/CMakeLists.txt');
  assert.match(core, /if\(TARGET ark_etcd_client_plugin\)[\s\S]*add_subdirectory\(\.\.\/etcd_client/);
  const checks = read('tests/etcd_client/CMakeLists.txt');
  assert.match(checks, /add_dependencies\(check-core check-etcd-client-plugin check-etcd-client-construction\)/);
  assert.match(checks, /etcd_client_impl\.cpp/);
  const mappings = read('src/plugins/CMakeLists.txt').split('set(ark_etcd_client_public_headers')[1].split('\n)')[0];
  assert.doesNotMatch(mappings, /etcd_client_factory/);
  const plugin = read('src/plugins/etcd_client/etcd_client_plugin.cpp');
  assert.match(plugin, /DllEntryPlugin/);
  assert.match(plugin, /DllExitPlugin/);
  assert.doesNotMatch(plugin, /ARK_DECLARE_PLUGIN|ARK_REGISTER_MODULE|ARK_UNREGISTER_MODULE/);
});

test('Optional Kafka construction preserves update selection without public factories', () => {
  const core = read('tests/core/CMakeLists.txt');
  assert.match(core, /if\(TARGET ark_kafka_plugin\)[\s\S]*add_subdirectory\(\.\.\/kafka/);
  const checks = read('tests/kafka/CMakeLists.txt');
  assert.match(checks, /add_dependencies\(check-core check-kafka-plugin check-kafka-construction\)/);
  assert.match(checks, /kafka_module_impl\.cpp/);
  const mappings = read('src/plugins/CMakeLists.txt').split('set(ark_kafka_public_headers')[1].split('\n)')[0];
  assert.doesNotMatch(mappings, /kafka_factory/);
  const plugin = read('src/plugins/kafka/kafka_plugin.cpp');
  assert.match(plugin, /module_update::enabled/);
  assert.match(plugin, /DllEntryPlugin/);
  assert.match(plugin, /DllExitPlugin/);
  assert.doesNotMatch(plugin, /ARK_DECLARE_PLUGIN|ARK_REGISTER_MODULE|ARK_UNREGISTER_MODULE/);
});
