import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import test from 'node:test';
import {
  applicationCheckTargets, applicationConfigureFlags, applicationTargetPaths, applicationTargetsForPath, selectApplicationBuildPlan,
} from '../../.github/scripts/application-build-plan.mjs';
import { selectScope } from '../../.github/scripts/ci-policy.mjs';

const site = { present: true, available: true };
const scope = (files, eventName = 'pull_request', extra = {}) => selectScope({ files, eventName, site, ...extra });
const plan = (files, eventName = 'pull_request', extra = {}) => JSON.parse(scope(files, eventName, extra).app_plan_json);
const checkFull = actual => {
  assert.equal(actual.mode, 'full');
  assert.equal(actual.buildAll, true);
  assert.deepEqual(actual.configureFlags, applicationConfigureFlags);
  assert.deepEqual(actual.checkTargets, ['check-core', 'check-entity-benchmark-smoke']);
};

test('full plans retain all configuration including archived integration compatibility and benchmarks', () => {
  assert.deepEqual(applicationConfigureFlags, [
    '-DARK_BUILD_APPLICATIONS=ON', '-DARK_BUILD_BENCHMARKS=ON', '-DARK_BUILD_PLUGIN_HTTP=ON',
    '-DARK_BUILD_PLUGIN_CONSUL=ON', '-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON', '-DARK_BUILD_PROTO=ON',
    '-DARK_BUILD_TESTS=ON', '-DARK_BUILD_CORE_TESTS=ON', '-DARK_BUILD_COMPRESSION_TESTS=ON',
    '-DARK_BUILD_EXAMPLES=ON', '-DARK_BUILD_GUEST_ROOM_EXAMPLE=ON', '-DARK_BUILD_CONFIG_TOOL=ON',
    '-DARK_BUILD_CONFIG_TOOL_TESTS=ON', '-DASIO_STANDALONE=OFF',
  ]);
  for (const eventName of ['merge_group', 'workflow_dispatch', 'push', '', 'unknown']) {
    for (const files of [undefined, [], ['tests/app/shutdown_deadlines.cmake'], ['README.md']]) {
      checkFull(plan(files, eventName));
    }
  }
  checkFull(JSON.parse(selectScope({ eventName: undefined, files: ['tests/app/shutdown_deadlines.cmake'], site }).app_plan_json));
});

test('specific application fixture changes run their actual regression dependencies without ALL', () => {
  const cases = [
    ['tests/app/shutdown_deadlines.cmake', ['check-app-shutdown-deadlines']],
    ['tests/app/business_runtime_plugin.cpp', ['check-app-business-runtime', 'check-app-shutdown-deadlines']],
    ['tests/app/startup_failure_plugin.cpp', ['check-app-startup-failures']],
    ['tests/app/startup_cleanup_allocation.cpp', ['check-startup-cleanup']],
    ['tests/app/application_preset_stop.sh', ['check-app-presets']],
    ['tests/app/static_services.cpp', ['check-static-services', 'check-system-composition']],
    ['tests/core/business_runtime.cpp', ['check-business-runtime']],
    ['tests/core/scheduler_drain.cmake', ['check-scheduler-drain']],
    ['tests/core/entity_snapshot.cpp', ['check-kernel-initialization']],
    ['tests/core/nested_wire_macro_first.cpp', ['check-nested-wire']],
    ['tests/proto/check_configuration.cmake', ['check-proto-configuration']],
    ['tests/proto/naming.cpp', ['check-proto-naming']],
    ['examples/guest_room/guest_room_wire.hpp', ['check-guest-room']],
    ['examples/guest_room/wire-vectors.json', ['check-guest-room']],
    ['examples/guest_room/stage.cmake', ['check-guest-room']],
    ['tests/guest_room/server.cpp', ['check-guest-room']],
    ['examples/guest_room/resource/meta/entity_schema.json', ['check-guest-room', 'check-system-composition']],
  ];
  for (const [file, expected] of cases) {
    const selected = scope([file]);
    assert.equal(selected.validation_stage, 'modules', file);
    assert.equal(selected.applications_check, true, file);
    assert.equal(selected.network_check, false, file);
    assert.equal(selected.database_check, false, file);
    const actual = JSON.parse(selected.app_plan_json);
    assert.equal(actual.mode, 'targets', file);
    assert.equal(actual.buildAll, false, file);
    assert.deepEqual(actual.checkTargets, expected, file);
    assert.deepEqual(actual.configureFlags, applicationConfigureFlags, file);
  }
});

test('mixed fixture changes union and sort targets while unrelated modules and docs do not enlarge Applications', () => {
  const files = ['tests/app/shutdown_deadlines.cmake', 'tests/app/business_runtime_plugin.cpp',
    'tests/guest_room/server.cpp', 'tests/network/rudp.cpp', 'tests/database/redis_service.cpp', 'docs/guide.md'];
  const actual = plan(files);
  assert.equal(actual.mode, 'targets');
  assert.deepEqual(actual.checkTargets, ['check-app-business-runtime', 'check-app-shutdown-deadlines', 'check-guest-room']);
  assert.equal(new Set(actual.checkTargets).size, actual.checkTargets.length);
  assert.deepEqual(plan([...files].reverse()), actual);
  assert.deepEqual(plan([...files, ...files]), actual);
});

test('public interfaces, production code, build, dependency, CI and unreviewed neighbors fail closed', () => {
  const files = [
    'src/core/plugin_manager.hpp', 'include/ark/kernel/entity.hpp', 'src/plugins/kernel/entity_snapshot.hpp',
    'src/plugins/kernel/kernel_module_impl.cpp', 'src/proto/proto/entity_sync.proto',
    'src/application/main.cpp', 'src/config/application_manifest.cpp',
    'CMakeLists.txt', 'src/plugins/kernel/CMakeLists.txt', 'tests/core/CMakeLists.txt',
    'tests/app/CMakeLists.txt', 'examples/guest_room/CMakeLists.txt', 'cmake/helpers.cmake',
    'vcpkg.json', 'vcpkg-configuration.json', 'package-lock.json',
    '.github/workflows/ci.yml', '.github/scripts/application-build-plan.mjs', 'tests/ci/application-build-plan.test.mjs',
    'tests/app/future_regression.cpp', 'tests/app/shutdown_deadlines.cmake.bak',
    'tests/app/nested/shutdown_deadlines.cmake', 'tests/core/main.cpp', 'tests/core/protobuf_messages.cpp',
    'examples/guest_room/new_runtime_input.json', 'tests/guest_room/new_fixture.hpp', 'unknown.bin',
  ];
  for (const file of files) {
    checkFull(plan([file]));
    checkFull(plan(['tests/app/shutdown_deadlines.cmake', file]));
  }
});

test('unavailable or malformed enumeration and scope cannot authorize target selection', () => {
  for (const files of [undefined, null, [], [''], ['/tests/app/shutdown_deadlines.cmake'],
    ['tests/app/../app/shutdown_deadlines.cmake'], ['tests\\app\\shutdown_deadlines.cmake'],
    ['tests/app/shutdown_deadlines.cmake\n'], [42], [null]]) checkFull(plan(files));
  checkFull(plan(['tests/app/shutdown_deadlines.cmake'], 'pull_request', { enumerationFailed: true }));
  checkFull(plan(['tests/app/shutdown_deadlines.cmake'], 'pull_request', { site: { present: false, available: false } }));
  const valid = scope(['tests/app/shutdown_deadlines.cmake']);
  for (const patch of [
    { event_name: 'merge_group' }, { files_known: false }, { files_known: 'true' },
    { site_available: false }, { validation_stage: 'full' }, { reason: 'unknown-path' },
    { applications_check: undefined }, { applications_check: 'true' },
  ]) {
    checkFull(selectApplicationBuildPlan({ eventName: 'pull_request', files: ['tests/app/shutdown_deadlines.cmake'],
      scope: { ...valid, ...patch } }));
  }
});

test('genuine documentation and unrelated-module PRs expose an explicit empty Applications plan', () => {
  for (const files of [['README.md'], ['tests/network/rudp.cpp'], ['tests/database/redis_service.cpp']]) {
    const actual = plan(files);
    assert.equal(actual.mode, 'skip');
    assert.equal(actual.buildAll, false);
    assert.deepEqual(actual.configureFlags, []);
    assert.deepEqual(actual.checkTargets, []);
  }
});

test('registered paths are existing files and every current app fixture has a reviewed plan', () => {
  for (const file of applicationTargetPaths) {
    assert.equal(existsSync(new URL(`../../${file}`, import.meta.url)), true, file);
    const targets = applicationTargetsForPath(file);
    assert.ok(targets.length > 0, file);
    for (const target of targets) assert.match(target, /^check-[a-z][a-z0-9-]*$/, file);
  }
  for (const entry of readdirSync(new URL('../app/', import.meta.url), { withFileTypes: true })) {
    if (entry.isFile()) assert.ok(applicationTargetsForPath(`tests/app/${entry.name}`), entry.name);
  }
  const helper = plan(['tests/app/owner_drain.hpp']);
  assert.deepEqual(helper.checkTargets, ['check-bus-plugin', 'check-kernel-plugin', 'check-net-http-drain',
    'check-net-plugin', 'check-static-services', 'check-system-composition', 'check-typed-examples']);
});

test('registered check targets are defined by CMake and returned arrays cannot mutate later plans', () => {
  const cmake = ['tests/core/CMakeLists.txt', 'examples/guest_room/CMakeLists.txt', 'tests/core/native_loader/CMakeLists.txt']
    .map(file => readFileSync(new URL(`../../${file}`, import.meta.url), 'utf8')).join('\n');
  const defined = new Set([...cmake.matchAll(/add_custom_target\((check-[a-z0-9-]+)/g)].map(match => match[1]));
  for (const file of applicationTargetPaths) {
    for (const target of applicationTargetsForPath(file)) {
      assert.ok(defined.has(target), `${file}: ${target}`);
      assert.ok(applicationCheckTargets.includes(target), `${file}: ${target}`);
    }
  }
  assert.equal(new Set(applicationCheckTargets).size, applicationCheckTargets.length);
  assert.ok(applicationCheckTargets.includes('check-core'));
  assert.ok(applicationCheckTargets.includes('check-entity-benchmark-smoke'));
  const original = plan(['tests/app/business_runtime_plugin.cpp']);
  applicationTargetsForPath('tests/app/business_runtime_plugin.cpp').push('check-injected');
  const mutable = plan(['tests/app/business_runtime_plugin.cpp']);
  mutable.configureFlags.push('-DARK_BUILD_TESTS=OFF');
  mutable.checkTargets.push('check-injected');
  assert.deepEqual(plan(['tests/app/business_runtime_plugin.cpp']), original);
});
