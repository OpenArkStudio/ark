import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const families = ['game', 'login', 'proxy', 'world', 'master', 'dir', 'router', 'simulate'];
const source = path => readFileSync(join(root, path), 'utf8');

// Configure-only evidence complements the native construction/DSO tests; no
// compiler, protocol services, PhysX or MessagePack dependency is involved.
test('server compatibility forwarding preserves every old header without publishing factory headers', () => {
  const fixture = mkdtempSync(join(tmpdir(), 'ark-server-mapping-'));
  try {
    writeFileSync(join(fixture, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(server_mapping LANGUAGES NONE)
include("${root}/src/server/compat_headers.cmake")
get_target_property(roots ark_server_compat_headers INTERFACE_INCLUDE_DIRECTORIES)
file(WRITE "\${CMAKE_BINARY_DIR}/roots.txt" "\${roots}")
`);
    const build = join(fixture, 'build');
    const mapping = join(build, 'compat-include');
    mkdirSync(mapping, { recursive: true });
    writeFileSync(join(mapping, 'obsolete.hpp'), 'stale mapping');
    execFileSync('cmake', ['-S', fixture, '-B', build], { stdio: 'pipe' });
    assert.equal(readFileSync(join(build, 'roots.txt'), 'utf8'), mapping);
    assert.equal(existsSync(join(mapping, 'obsolete.hpp')), false);
    let headers = 0;
    for (const family of families) {
      for (const header of readdirSync(join(root, 'src/server', family)).filter(name => name.endsWith('.hpp') && !name.endsWith('_factory.hpp'))) {
        const category = header.endsWith('_impl.hpp') || header.endsWith('_plugin.hpp') || header.includes('_define.hpp') ? 'include' : 'interface';
        const mapped = readFileSync(join(mapping, family, category, header), 'utf8');
        assert.match(mapped, new RegExp(`#include "${root}/src/server/${family}/${header}"`));
        headers++;
      }
      assert.equal(existsSync(join(mapping, family, 'include', `${family}_module_factory.hpp`)), false);
      assert.equal(existsSync(join(root, 'src/server', family, 'include')), false);
      assert.equal(existsSync(join(root, 'src/server', family, 'interface')), false);
      assert.equal(existsSync(join(root, 'src/server', family, 'src')), false);
    }
    assert.equal(headers, 28);
    execFileSync('cmake', ['-S', fixture, '-B', build], { stdio: 'pipe' });
    assert.equal(existsSync(join(mapping, 'login/include/login_plugin.hpp')), true);
  } finally {
    rmSync(fixture, { recursive: true, force: true });
  }
});

test('all eight server assemblies use typed factories, disabled updates and explicit dynamic entry points', () => {
  for (const family of families) {
    const assembly = source(`src/server/${family}/${family}_plugin.cpp`);
    const declaration = source(`src/server/${family}/${family}_plugin.hpp`);
    assert.match(declaration, new RegExp(`public plugin_base<${family}_plugin>`));
    assert.match(assembly, /ARK_EXPORT_FUNC void DllEntryPlugin\(plugin_manager\* manager, const std::string& name, logger\* log\)/);
    assert.match(assembly, /logger::init\(log\)/);
    assert.match(assembly, new RegExp(`manager->register_plugin<${family}_plugin>\\(name\\)`));
    assert.match(assembly, /ARK_EXPORT_FUNC void DllExitPlugin\(plugin_manager\* manager\)/);
    assert.match(assembly, new RegExp(`manager->unregister_plugin<${family}_plugin>\\(\\)`));
    assert.doesNotMatch(assembly + declaration, /ARK_DECLARE_PLUGIN|ARK_REGISTER_MODULE|ARK_UNREGISTER_MODULE/);
    const modules = [`${family}_net_module`, ...(family === 'simulate' ? ['simulate_module'] : [])];
    for (const module of modules) {
      assert.match(assembly, new RegExp(`register_module\\(detail::make_${module}\\(\\), module_update::disabled\\)`));
      assert.match(assembly, new RegExp(`unregister_module<${module}>\\(\\)`));
      const implementation = source(`src/server/${family}/${module}_impl.hpp`);
      assert.match(implementation, new RegExp(`public module_base<${module}>`));
      assert.doesNotMatch(implementation, /ARK_DECLARE_MODULE_FUNCTIONS|bool update\(\)/);
      assert.match(source(`src/server/${family}/${module}_impl.cpp`), new RegExp(`return std::make_unique<${module}_impl>\\(\\)`));
    }
  }
});

test('native acceptance covers every enabled server without enabling unsupported simulate integrations', () => {
  const cmake = source('src/server/CMakeLists.txt');
  const tests = source('tests/server/CMakeLists.txt');
  for (const family of families.filter(name => name !== 'simulate')) {
    assert.match(cmake, new RegExp(`^add_subdirectory\\(${family}\\)`, 'm'));
    assert.match(tests, new RegExp(`\\b${family}\\b`));
    assert.equal(source(`src/server/${family}/CMakeLists.txt`).trim(), `BUILD_SERVER_MACRO(ark_${family}_plugin)`);
  }
  assert.doesNotMatch(cmake, /^add_subdirectory\(simulate\)/m);
  assert.match(source('src/server/simulate/CMakeLists.txt'), /message\(FATAL_ERROR/);
  assert.match(source('tests/core/CMakeLists.txt'), /add_dependencies\(check-core check-server-plugins\)/);
});

test('DSO export inspection stays in a core-only process separate from the real-factory host', () => {
  const cmake = source('tests/server/CMakeLists.txt');
  const construction = source('tests/server/construction.cpp');
  const exports = source('tests/server/exports.cpp');
  assert.match(cmake, /add_executable\(ark_server_exports_tests exports\.cpp\)/);
  // Replace inherited aggregate linkage rather than appending core to ark::ark:
  // the export host must not register static generated Protobuf descriptors.
  assert.match(cmake, /set_property\(TARGET ark_server_exports_tests PROPERTY LINK_LIBRARIES ark::core\)/);
  assert.match(cmake, /COMMAND \$<TARGET_FILE:ark_server_exports_tests> "\$<TARGET_FILE_DIR:ark_\$\{server\}_plugin>\/" "ark_\$\{server\}_plugin"\s+COMMAND \$\{CMAKE_COMMAND\} "-DPROGRAM=\$<TARGET_FILE:\$\{test_target\}>" "-DROLE=ark_\$\{server\}_plugin"\s+-P "\$\{PROJECT_SOURCE_DIR\}\/tests\/server\/construction\.cmake"/);
  assert.doesNotMatch(construction, /dynamic_library|\.load\(|get_symbol\(/);
  assert.doesNotMatch(exports, /server_test_types|ark\/proto|_module_factory|_module_impl/);
  for (const entry of ['DllEntryPlugin', 'DllExitPlugin']) {
    assert.ok(exports.includes(`get_symbol("${entry}")`));
  }
  // Keep the actual manager-loader path with the controlled Login factory.
  assert.match(source('tests/server/entry.cpp'), /manager\.start\(/);
  assert.match(source('tests/server/entry.cpp'), /manager\.stop\(/);
});
