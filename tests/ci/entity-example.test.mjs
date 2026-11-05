// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { applicationCommands } from '../../.github/scripts/ci-applications.mjs';
const read = path => readFileSync(new URL(`../../${path}`, import.meta.url), 'utf8');

test('entity benchmark stays optional with an independent CLI smoke entry', () => {
  const cmake = read('benchmarks/CMakeLists.txt');
  assert.match(cmake, /if\(ARK_BUILD_BENCHMARKS AND ARK_BUILD_CONFIG AND TARGET ark_kernel_plugin AND TARGET ark_utility_plugin\)/);
  assert.match(cmake, /add_executable\(ark_entity_operations_benchmark EXCLUDE_FROM_ALL/);
  assert.doesNotMatch(read('tests/core/CMakeLists.txt'), /check-entity-benchmark-smoke|ark_entity_operations_benchmark/);
  assert.match(cmake, /add_custom_target\(check-entity-benchmark-smoke[\s\S]*?-P "\$\{PROJECT_SOURCE_DIR\}\/benchmarks\/entity_operations_smoke\.cmake"/);
  assert.match(read('benchmarks/entity_operations_smoke.cmake'), /COMMAND "\$\{PROGRAM\}" --plugin-dir "\$\{PLUGIN_DIR\}" --smoke/);
});
test('Applications CI compiles the optional entity benchmark and runs only its correctness smoke', () => {
  const workflow = read('.github/workflows/ci.yml').replaceAll('\r\n', '\n');
  const apps = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(block => block.startsWith('  applications:\n'));
  assert.ok(apps);
  const configure = apps.match(/      - name: Configure applications[\s\S]*?(?=\n      - )/)?.[0];
  assert.match(configure ?? '', /run: node \.github\/scripts\/ci-applications\.mjs configure$/m);
  const env = { GITHUB_EVENT_NAME: 'merge_group', VCPKG_ROOT: '/vcpkg' };
  assert.ok(applicationCommands('configure', env)[0].includes('-DARK_BUILD_BENCHMARKS=ON'));
  const steps = apps.split(/(?=^      - )/m).slice(1);
  const smoke = steps.filter(step => step.includes('run: node .github/scripts/ci-applications.mjs check\n'));
  assert.equal(smoke.length, 1);
  assert.doesNotMatch(smoke[0], /continue-on-error:|if:|--iterations/);
  const commands = applicationCommands('check', env).filter(args => args[3] === 'check-entity-benchmark-smoke');
  assert.equal(commands.length, 1, 'the full check phase must include the independent benchmark smoke');
  assert.doesNotMatch(commands[0].join(' '), /--iterations/);
});
