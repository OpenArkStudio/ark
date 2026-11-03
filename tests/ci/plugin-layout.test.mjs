import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const retainedHeaders = {
  consul: ['consul_module.hpp', 'consul_module_impl.hpp', 'consul_plugin.hpp'],
  etcd_client: ['etcd_client.hpp', 'etcd_client_impl.hpp', 'etcd_client_module.hpp', 'etcd_client_module_impl.hpp', 'etcd_client_plugin.hpp'],
  kafka: ['kafka.hpp', 'kafka_module.hpp', 'kafka_module_impl.hpp', 'kafka_plugin.hpp'],
  osssdk: ['oss_sdk_module.hpp', 'oss_sdk_module_impl.hpp', 'oss_sdk_plugin.hpp'],
  physic: ['physic_config_module.hpp', 'physic_config_module_impl.hpp', 'physic_define.hpp', 'physic_module.hpp', 'physic_module_impl.hpp', 'physic_plugin.hpp', 'wave_front.hpp'],
};
const plugins = readdirSync(join(root, 'src/plugins')).filter(name => existsSync(join(root, 'src/plugins', name, 'CMakeLists.txt'))).sort();

test('all plugin APIs map headers without enabling runtime plugins or their driver dependencies', () => {
  const fixture = mkdtempSync(join(tmpdir(), 'ark-plugin-mapping-'));
  try {
    // A language-free harness exercises the real plugin mapping, without finding
    // native dependencies or compiling unsupported legacy integrations.
    writeFileSync(join(fixture, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(plugin_mapping_consumer LANGUAGES NONE)
set(ROOT_DIR "${root}")
set(ARK_BUILD_APPLICATIONS ON)
set(ARK_BUILD_RUNTIME_PLUGINS ON)
set(ARK_BUILD_PROTO OFF)
add_library(core INTERFACE)
add_library(ark::core ALIAS core)
add_library(ark_sdk INTERFACE)
${plugins.map(name => `set(ARK_BUILD_PLUGIN_${name.toUpperCase()} OFF CACHE BOOL "Runtime plugin disabled" FORCE)`).join('\n')}
add_subdirectory("${root}/src/plugins" plugins)
foreach(name IN ITEMS ${plugins.join(" ")})
    if(NOT TARGET ark::\${name})
        message(FATAL_ERROR "Missing API target: \${name}")
    endif()
    get_target_property(kind ark_\${name}_api TYPE)
    if(NOT kind STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "API target must remain header-only: \${name}")
    endif()
    get_target_property(roots ark_\${name}_api INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(dependencies ark_\${name}_api INTERFACE_LINK_LIBRARIES)
    file(WRITE "\${CMAKE_BINARY_DIR}/\${name}.txt" "\${roots}\n\${dependencies}\n")
endforeach()
get_property(targets DIRECTORY "${root}/src/plugins" PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS targets)
    get_target_property(kind \${target} TYPE)
    if(NOT kind STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "Disabled runtime plugin produced a build target: \${target}")
    endif()
endforeach()
`);
    const build = join(fixture, 'build');
    const staging = join(build, 'plugins/sdk-include/ark');
    const stale = join(staging, 'etcd_client/removed.hpp');
    const removedHttpImplementation = join(staging, 'net/http_client_module_impl.hpp');
    const unmanaged = join(staging, 'unmanaged/keep.hpp');
    for (const file of [stale, removedHttpImplementation, unmanaged]) {
      mkdirSync(dirname(file), { recursive: true });
      writeFileSync(file, 'obsolete or external mapping\n');
    }
    execFileSync('cmake', ['-S', fixture, '-B', build], { stdio: 'pipe' });
    for (const name of plugins) {
      const properties = readFileSync(join(build, `${name}.txt`), 'utf8');
      assert.match(properties, /plugins\/sdk-include/);
      assert.doesNotMatch(properties, /include\/ark|ROOT_DIR/);
      assert.match(properties, /ark::core/);
    }
    assert.match(readFileSync(join(build, 'etcd_client.txt'), 'utf8'), /ark::net/);
    assert.match(readFileSync(join(build, 'physic.txt'), 'utf8'), /ark::utility/);
    assert.equal(existsSync(join(staging, 'net/http_client_module_impl.hpp')), false);
    assert.equal(existsSync(join(staging, 'net/http_client_factory.hpp')), false);
    assert.ok(existsSync(join(staging, 'net/http_client_module.hpp')));
    assert.equal(existsSync(join(staging, 'kernel/kernel_factory.hpp')), false);
    assert.ok(existsSync(join(staging, 'kernel/kernel_plugin.hpp')));
    for (const [name, headers] of Object.entries(retainedHeaders)) {
      assert.equal(existsSync(join(root, 'include/ark', name)), false);
      assert.deepEqual(readdirSync(join(staging, name)).sort(), [...headers].sort());
      for (const header of headers) {
        assert.deepEqual(readFileSync(join(staging, name, header)), readFileSync(join(root, 'src/plugins', name, header)), `${name}/${header}`);
      }
    }
    assert.equal(existsSync(stale), false);
    assert.equal(existsSync(unmanaged), true);
    // Reconfiguration must preserve the same complete public view.
    execFileSync('cmake', ['-S', fixture, '-B', build], { stdio: 'pipe' });
    for (const [name, headers] of Object.entries(retainedHeaders)) {
      assert.deepEqual(readdirSync(join(staging, name)).sort(), [...headers].sort());
    }
  } finally {
    rmSync(fixture, { recursive: true, force: true });
  }
});
