// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { applicationCommands } from '../../.github/scripts/ci-applications.mjs';
const read = path => readFileSync(new URL(`../../${path}`, import.meta.url), 'utf8');

test('HTTP example regressions build the real DSO sources without enabling application examples', () => {
  const cmake = read('tests/core/CMakeLists.txt');
  assert.match(cmake, /option\(ARK_BUILD_HTTP_EXAMPLE_TESTS "[^"]+" \$\{ark_http_examples_default\}\)/);
  const block = cmake.slice(cmake.indexOf('if(ARK_BUILD_HTTP_EXAMPLE_TESTS)'));
  assert.match(block, /add_subdirectory\("\$\{PROJECT_SOURCE_DIR\}\/examples\/http_client"/);
  assert.match(block, /add_subdirectory\("\$\{PROJECT_SOURCE_DIR\}\/examples\/http_server"/);
  assert.match(block, /DEPENDS ark_http_examples_tests http_client_sample_plugin http_server_plugin/);
  assert.match(block, /add_dependencies\(check-core check-http-examples\)/);
  assert.match(read('examples/CMakeLists.txt'), /^#add_subdirectory\(http_client\)$/m);
  assert.match(read('examples/CMakeLists.txt'), /^#add_subdirectory\(http_server\)$/m);
  for (const [folder, stem] of [['http_client', 'http_client_sample'], ['http_server', 'http_server']]) {
    assert.match(read(`examples/${folder}/CMakeLists.txt`),
      new RegExp(`add_library\\(${stem}_plugin SHARED ${stem}_module\\.cpp ${stem}_plugin\\.cpp\\)`));
  }
});


test('HTTP example test default follows core Boost prerequisites and honors an explicit OFF', () => {
  const cmake = read('tests/core/CMakeLists.txt');
  const defaults = cmake.slice(cmake.indexOf('set(ark_http_examples_default OFF)'),
    cmake.indexOf('if(ARK_BUILD_HTTP_EXAMPLE_TESTS)'));
  const fixture = mkdtempSync(join(tmpdir(), 'ark-http-examples-default-'));
  try {
    for (const [index, standalone, missing, explicitOff, expected] of [
      [0, false, '', false, 'ON'],
      [1, false, '', true, 'OFF'],
      [2, true, '', false, 'OFF'],
      [3, false, 'http', false, 'OFF'],
      [4, false, 'net', false, 'OFF'],
      [5, false, 'etcd_client', false, 'OFF'],
    ]) {
      writeFileSync(join(fixture, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(http_examples_default LANGUAGES NONE)
set(ASIO_STANDALONE ${standalone ? 'ON' : 'OFF'})
${['http', 'net', 'etcd_client'].filter(name => name !== missing)
  .map(name => `add_library(ark::${name} INTERFACE IMPORTED)`).join('\n')}
${defaults}
if(NOT ARK_BUILD_HTTP_EXAMPLE_TESTS STREQUAL "${expected}")
    message(FATAL_ERROR "Unexpected HTTP example test selection")
endif()
`);
      execFileSync('cmake', ['-S', fixture, '-B', join(fixture, `build-${index}`),
        ...(explicitOff ? ['-DARK_BUILD_HTTP_EXAMPLE_TESTS=OFF'] : [])], { stdio: 'pipe' });
    }
  } finally {
    rmSync(fixture, { recursive: true, force: true });
  }
});

test('existing Applications CI reaches HTTP example regressions through core Boost validation', () => {
  const workflow = read('.github/workflows/ci.yml').replaceAll('\r\n', '\n');
  const applications = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m)
    .find(block => block.startsWith('  applications:\n'));
  assert.ok(applications);
  const configure = applications.match(/      - name: Configure applications[\s\S]*?(?=\n      - )/)?.[0];
  assert.match(configure ?? '', /run: node \.github\/scripts\/ci-applications\.mjs configure$/m);
  const env = { GITHUB_EVENT_NAME: 'merge_group', VCPKG_ROOT: '/vcpkg' };
  const [args] = applicationCommands('configure', env);
  assert.ok(args.includes('-DARK_BUILD_CORE_TESTS=ON'));
  assert.ok(args.includes('-DASIO_STANDALONE=OFF'));
  assert.ok(!args.includes('-DARK_BUILD_HTTP_EXAMPLE_TESTS=OFF'));
  assert.match(applications, /run: node \.github\/scripts\/ci-applications\.mjs check\n/);
  assert.ok(applicationCommands('check', env).some(command => command[3] === 'check-core'));
});
