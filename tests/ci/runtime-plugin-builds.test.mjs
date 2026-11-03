import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const runtime = ['kernel', 'net', 'bus', 'utility', 'mysql', 'redis', 'http', 'physic', 'consul', 'etcd_client', 'kafka', 'osssdk'];
const servers = ['game', 'login', 'proxy', 'world', 'master', 'dir', 'router'].map(name => `ark_${name}_plugin`);

function fixture(t) {
  const work = mkdtempSync(join(tmpdir(), 'ark-runtime-builds-')).replaceAll('\\', '/');
  t.after(() => rmSync(work, { recursive: true, force: true }));
  writeFileSync(join(work, 'rapidxml.hpp'), '// Configure-only dependency fixture.\n');
  // Exercise the actual root/subdirectory graph. Substitute only external
  // discovery: this fixture neither compiles nor fetches dependencies.
  writeFileSync(join(work, 'dependencies.cmake'), `
function(find_package name)
  set(@{name}_FOUND TRUE PARENT_SCOPE)
  if(name STREQUAL "Protobuf")
    add_library(protobuf::libprotobuf INTERFACE IMPORTED GLOBAL)
    add_executable(protobuf::protoc IMPORTED GLOBAL)
    set_target_properties(protobuf::protoc PROPERTIES IMPORTED_LOCATION "@{CMAKE_COMMAND}")
  elseif(name STREQUAL "RapidXML")
    add_library(RapidXML::RapidXML INTERFACE IMPORTED GLOBAL)
    set(RapidXML_INCLUDE_DIR "@WORK@" PARENT_SCOPE)
  elseif(name STREQUAL "ZLIB" OR name STREQUAL "Zstd")
    set(@{name}_INCLUDE_DIRS "@WORK@" PARENT_SCOPE)
  else()
    set(names_arknet arknet::arknet)
    set(names_spdlog spdlog::spdlog_header_only)
    set(names_readerwriterqueue readerwriterqueue::readerwriterqueue)
    set(names_tomlplusplus tomlplusplus::tomlplusplus)
    set(names_nlohmann_json nlohmann_json::nlohmann_json)
    set(names_args taywee::args)
    set(names_OpenXLSX OpenXLSX::OpenXLSX)
    set(names_Boost Boost::nowide)
    set(names_Threads Threads::Threads)
    if(DEFINED names_@{name})
      foreach(target IN LISTS names_@{name})
        if(NOT TARGET @{target})
          add_library(@{target} INTERFACE IMPORTED GLOBAL)
        endif()
      endforeach()
    elseif(NOT name MATCHES "^(pugixml|miniz)$")
      message(FATAL_ERROR "Unexpected external dependency: @{name}")
    endif()
  endif()
endfunction()
function(record_graph directory)
  get_property(targets DIRECTORY "@{directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    file(APPEND "@{CMAKE_BINARY_DIR}/targets.txt" "@{target}\\n")
  endforeach()
  get_property(children DIRECTORY "@{directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    record_graph("@{child}")
  endforeach()
endfunction()
cmake_language(DEFER CALL record_graph "@{CMAKE_SOURCE_DIR}")
`.replaceAll('@{', '${').replaceAll('@WORK@', work));
  return (name, options = {}, { defaults = false, error } = {}) => {
    const build = join(work, name);
    const settings = {
      ARK_INSTALL: 'OFF', ARK_USE_INSTALLED_ARKNET: 'ON', ARKNET_ENABLE_SSL: 'ON',
      ARK_BUILD_TESTS: 'OFF', ARK_BUILD_CORE_TESTS: 'OFF', ARK_BUILD_EXAMPLES: 'OFF',
      ARK_BUILD_CONFIG_TOOL_TESTS: 'OFF', RAPIDXML_INCLUDE_DIR: work,
      CMAKE_PROJECT_INCLUDE: join(work, 'dependencies.cmake'),
      ...(!defaults && Object.fromEntries(runtime.map(plugin => [`ARK_BUILD_PLUGIN_${plugin.toUpperCase()}`, ['mysql', 'redis'].includes(plugin) ? 'ON' : 'OFF']))),
      ...options,
    };
    const result = spawnSync('cmake', ['-S', root, '-B', build, ...Object.entries(settings).map(([key, value]) => `-D${key}=${value}`)], { encoding: 'utf8' });
    const output = `${result.stdout}\n${result.stderr}`;
    if (error) {
      assert.notEqual(result.status, 0, output);
      assert.match(output, error);
      return;
    }
    assert.equal(result.status, 0, output);
    return {
      targets: new Set(readFileSync(join(build, 'targets.txt'), 'utf8').trim().split('\n')),
      cache: readFileSync(join(build, 'CMakeCache.txt'), 'utf8'),
    };
  };
}

test('application/runtime switches independently select the actual root target graph', t => {
  const configure = fixture(t);
  for (const apps of ['OFF', 'ON']) for (const plugins of ['OFF', 'ON']) {
    const { targets, cache } = configure(`${apps}-${plugins}`, {
      ARK_BUILD_APPLICATIONS: apps, ARK_BUILD_RUNTIME_PLUGINS: plugins,
      ARK_BUILD_EXAMPLES: 'ON',
    });
    for (const target of ['app', ...servers, 'entity_operations_plugin']) assert.equal(targets.has(target), apps === 'ON', target);
    for (const target of ['ark_mysql_plugin', 'ark_redis_plugin']) assert.equal(targets.has(target), plugins === 'ON', target);
    assert.equal(targets.has('AFProto'), apps === 'ON' || plugins === 'ON');
    assert.equal(targets.has('ark_config'), apps === 'ON' || plugins === 'ON');
    assert.equal(targets.has('ark_kernel_json'), false);
    assert.ok(targets.has('ark_mysql_api'));
    for (const name of ['CONSUL', 'ETCD_CLIENT', 'KAFKA', 'OSSSDK']) assert.match(cache, new RegExp(`ARK_BUILD_PLUGIN_${name}:BOOL=OFF`));
  }
  const core = configure('standalone-core', {
    ARK_BUILD_APPLICATIONS: 'OFF', ARK_BUILD_RUNTIME_PLUGINS: 'OFF',
    ARK_BUILD_CORE_TESTS: 'ON', ARK_BUILD_PROTO: 'OFF', ASIO_STANDALONE: 'ON',
  });
  assert.ok(core.targets.has('check-core'));
  assert.ok(core.targets.has('ark_config'));
  for (const target of ['app', 'AFProto', 'ark_mysql_plugin', 'ark_redis_plugin']) assert.equal(core.targets.has(target), false, target);
});

test('runtime default follows applications and retains the original default plugins', t => {
  const configure = fixture(t);
  for (const apps of ['OFF', 'ON']) {
    const { targets, cache } = configure(apps, { ARK_BUILD_APPLICATIONS: apps }, { defaults: true });
    assert.match(cache, new RegExp(`ARK_BUILD_RUNTIME_PLUGINS:BOOL=${apps}`));
    for (const name of ['kernel', 'net', 'bus', 'utility']) assert.equal(targets.has(`ark_${name}_plugin`), apps === 'ON', name);
    assert.equal(targets.has('ark_kernel_json'), apps === 'ON');
    for (const name of ['consul', 'etcd_client', 'kafka', 'osssdk']) assert.equal(targets.has(`ark_${name}_plugin`), false);
  }
});

test('runtime dependencies fail early and database-only builds can omit the reader', t => {
  const configure = fixture(t);
  const base = { ARK_BUILD_APPLICATIONS: 'OFF', ARK_BUILD_RUNTIME_PLUGINS: 'ON' };
  configure('proto', { ...base, ARK_BUILD_PROTO: 'OFF' }, { error: /ARK_BUILD_RUNTIME_PLUGINS requires ARK_BUILD_PROTO/ });
  configure('standalone', { ...base, ASIO_STANDALONE: 'ON' }, { error: /runtime plugins and network tests require Boost/ });
  configure('ssl', { ...base, ARKNET_ENABLE_SSL: 'OFF' }, { error: /MySQL plugin requires ARKNET_ENABLE_SSL/ });
  for (const name of ['BUS', 'HTTP']) configure(name, { ...base, ARK_BUILD_CONFIG: 'OFF', [`ARK_BUILD_PLUGIN_${name}`]: 'ON' }, { error: /runtime plugins require the configuration reader/ });
  const { targets } = configure('database-only', { ...base, ARK_BUILD_CONFIG: 'OFF' });
  assert.equal(targets.has('ark_config'), false);
  assert.ok(targets.has('ark_mysql_plugin') && targets.has('ark_redis_plugin'));
});

test('the exporter-only early return does not swallow enabled runtime plugins', t => {
  const configure = fixture(t);
  const base = { ARK_BUILD_APPLICATIONS: 'OFF', ARK_BUILD_CONFIG_TOOL: 'ON', ARK_BUILD_CONFIG: 'OFF' };
  const tool = configure('tool', { ...base, ARK_BUILD_RUNTIME_PLUGINS: 'OFF', ARK_BUILD_PROTO: 'OFF' });
  assert.ok(tool.targets.has('ark_config_tool'));
  assert.equal(tool.targets.has('ark_core'), false);
  const both = configure('tool-runtime', { ...base, ARK_BUILD_RUNTIME_PLUGINS: 'ON' });
  for (const name of ['ark_config_tool', 'ark_core', 'ark_mysql_plugin', 'ark_redis_plugin']) assert.ok(both.targets.has(name), name);
});

test('guest-room server acceptance diagnoses missing runtime providers', t => {
  const configure = fixture(t);
  const base = {
    ARK_BUILD_APPLICATIONS: 'ON', ARK_BUILD_EXAMPLES: 'ON',
    ARK_BUILD_GUEST_ROOM_EXAMPLE: 'ON',
  };
  if (process.platform === 'win32') {
    const { targets } = configure('codec', { ...base, ARK_BUILD_RUNTIME_PLUGINS: 'OFF' });
    assert.ok(targets.has('check-guest-room-codec'));
    assert.equal(targets.has('check-guest-room-server'), false);
  } else {
    configure('missing', { ...base, ARK_BUILD_RUNTIME_PLUGINS: 'OFF' }, { error: /check-guest-room-server requires ark_kernel_plugin/ });
    for (const name of ['kernel', 'utility', 'bus', 'net']) {
      configure(`missing-${name}`, {
        ...base, ARK_BUILD_RUNTIME_PLUGINS: 'ON', [`ARK_BUILD_PLUGIN_${name.toUpperCase()}`]: 'OFF',
      }, { defaults: true, error: new RegExp(`check-guest-room-server requires ark_${name}_plugin`) });
    }
  }
  const { targets } = configure('providers', { ...base, ARK_BUILD_RUNTIME_PLUGINS: 'ON' }, { defaults: true });
  assert.ok(targets.has('check-guest-room-codec'));
  assert.equal(targets.has('check-guest-room-server'), process.platform !== 'win32');
});
