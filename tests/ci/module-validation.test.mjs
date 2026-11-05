import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import test from 'node:test';
import { selectScope, validateGate } from '../../.github/scripts/ci-policy.mjs';
import { applicationCommands } from '../../.github/scripts/ci-applications.mjs';

const site = { present: true, available: true };
const jobs = ['network', 'applications', 'database'];
const plan = (files, eventName = 'pull_request', extra = {}) => selectScope({ files, eventName, site, ...extra });
const selected = scope => jobs.filter(job => scope[`${job}_check`]);
const results = scope => ({
  scope: { result: 'success', outputs: Object.fromEntries(Object.entries(scope).map(([key, value]) => [key, String(value)])) },
  ...Object.fromEntries(jobs.map(job => [job, { result: scope[`${job}_check`] ? 'success' : 'skipped' }])),
  validation: { result: 'success', outputs: {
    formatting_outcome: scope.format_check ? 'success' : 'skipped',
    documentation_outcome: scope.docs_check ? 'success' : 'skipped',
    readme_outcome: scope.readme_check ? 'success' : 'skipped',
  } },
});
const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8').replaceAll('\r\n', '\n');
const jobBlock = name => workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(block => block.startsWith(`  ${name}:\n`));

function matrices(name) {
  const block = jobBlock(name);
  const expression = block.match(/^      matrix: \$\{\{ fromJSON\(github.event_name == 'pull_request' && needs.scope.result == 'success' && needs.scope.outputs.validation_stage == 'modules' && '([^']+)' \|\| '([^']+)'\) \}\}$/m);
  assert.ok(expression, `${name}: choose the reduced matrix only from the actual PR event`);
  return { source: JSON.parse(expression[1]).include, full: JSON.parse(expression[2]).include };
}

test('actual workflow output bindings provide complete plans to the required gate', () => {
  const outputs = [...jobBlock('scope').matchAll(/^      (\w+): \$\{\{ steps\.scope\.outputs\.(\w+) \}\}$/gm)];
  assert.ok(outputs.length > 0);
  for (const scope of [plan(['tests/network/rudp.cpp']), plan(['include/ark/core/module.hpp']),
    plan(['README.md']), plan(['AGENTS.md']), plan(['AGENTS.md'], 'merge_group'),
    plan(['tests/database/redis_service.cpp'], 'merge_group'),
    plan(['README.md'], 'merge_group'), plan([], 'workflow_dispatch'), plan([], 'pull_request')]) {
    const needs = results(scope);
    needs.scope.outputs = Object.fromEntries(outputs.map(([, name, source]) => [name, String(scope[source])]));
    validateGate(needs, scope.event_name);
  }
});

test('source paths select actual module regressions and propagate plugin dependencies', () => {
  const cases = [
    ['tests/core/containers.cpp', ['applications']],
    ['tests/core/CMakeLists.txt', ['applications']],
    ['src/application/main.cpp', ['applications']],
    ['src/plugins/utility/timer_module.hpp', ['applications']],
    ['src/plugins/consul/consul_module.hpp', ['applications']],
    ['src/plugins/consul/consul_module_impl.hpp', ['applications']],
    ['src/plugins/consul/consul_plugin.hpp', ['applications']],
    ['src/plugins/etcd_client/etcd_client.hpp', ['applications']],
    ['src/plugins/etcd_client/etcd_client_impl.hpp', ['applications']],
    ['src/plugins/etcd_client/etcd_client_module.hpp', ['applications']],
    ['src/plugins/etcd_client/etcd_client_module_impl.hpp', ['applications']],
    ['src/plugins/etcd_client/etcd_client_plugin.hpp', ['applications']],
    ['src/plugins/kafka/kafka.hpp', ['applications']],
    ['src/plugins/kafka/kafka_module.hpp', ['applications']],
    ['src/plugins/kafka/kafka_module_impl.hpp', ['applications']],
    ['src/plugins/kafka/kafka_plugin.hpp', ['applications']],
    ['src/plugins/osssdk/oss_sdk_module.hpp', ['applications']],
    ['src/plugins/osssdk/oss_sdk_module_impl.hpp', ['applications']],
    ['src/plugins/osssdk/oss_sdk_plugin.hpp', ['applications']],
    ['src/plugins/physic/physic_config_module.hpp', ['applications']],
    ['src/plugins/physic/physic_config_module_impl.hpp', ['applications']],
    ['src/plugins/physic/physic_define.hpp', ['applications']],
    ['src/plugins/physic/physic_module.hpp', ['applications']],
    ['src/plugins/physic/physic_module_impl.hpp', ['applications']],
    ['src/plugins/physic/physic_plugin.hpp', ['applications']],
    ['src/plugins/physic/wave_front.hpp', ['applications']],
    ['src/application/shutdown_request.hpp', ['applications']],
    ['src/application/CMakeLists.txt', ['applications']],
    ['src/config/application_manifest.cpp', ['applications']],
    ['src/server/game/game_module_impl.cpp', ['applications']],
    ['src/proto/CMakeLists.txt', ['applications']],
    ['examples/timers/timers.cpp', ['applications']],
    ['include/ark/kernel/node.hpp', ['applications']],
    ['tests/network/rudp.cpp', ['network']],
    ['tests/network/CMakeLists.txt', ['network']],
    ['benchmarks/network_buffers.cpp', ['network']],
    ['src/plugins/net/rudp_client.cpp', ['network', 'applications']],
    ['include/ark/net/rudp_server.hpp', ['network', 'applications']],
    ['src/plugins/net/rudp_server.hpp', ['network', 'applications']],
    ['include/ark/http/http_server_module.hpp', ['network', 'applications']],
    ['src/plugins/bus/msg_module_impl.cpp', ['network', 'applications']],
    ['src/plugins/bus/bus_module.hpp', ['network', 'applications']],
    ['src/plugins/http/http_server_module.hpp', ['network', 'applications']],
    ['tests/database/transactions_awaitable.cpp', ['database']],
    ['tests/database/CMakeLists.txt', ['database']],
    ['benchmarks/database.cpp', ['database']],
    ['src/plugins/mysql/mysql_module.cpp', ['applications', 'database']],
    ['src/plugins/mysql/mysql_module.hpp', ['applications', 'database']],
    ['src/plugins/redis/redis_module.hpp', ['applications', 'database']],
    ['src/plugins/redis/detail/redis_service.hpp', ['applications', 'database']],
    ['include/ark/redis/redis_module.hpp', ['applications', 'database']],
    ['src/plugins/detail/database_awaitable.hpp', ['applications', 'database']],
    ['examples/mysql_sample/main.cpp', ['applications', 'database']],
  ];
  for (const [file, expected] of cases) {
    const scope = plan([file]);
    assert.deepEqual(selected(scope), expected, file);
    assert.equal(scope.validation_stage, 'modules', file);
    assert.equal(scope.format_check, true, file);
    validateGate(results(scope), 'pull_request');
  }
  assert.deepEqual(selected(plan(['tests/network/rudp.cpp', 'tests/database/redis_service.cpp'])), ['network', 'database']);
  const mixed = plan(['tests/core/containers.cpp', 'docs/reference/kernel.md']);
  assert.deepEqual(selected(mixed), ['applications']);
  assert.equal(mixed.docs_check, true);
});

test('public core headers, build/dependency/CI inputs and unknown paths retain every source regression', () => {
  for (const file of ['include/ark/container/vector.hpp', 'include/ark/task.hpp', 'include/ark/hash/crc.hpp',
    'src/core/CMakeLists.txt', 'src/core/module.hpp', 'src/core/container/vector.hpp', 'CMakeLists.txt', 'cmake/utils.cmake', 'vcpkg.json', '.gitmodules',
    '3rdparty/spdlog', 'ports/asio/portfile.cmake', '.github/workflows/ci.yml',
    'config/timers.app.toml', 'resources/excel/item.xlsx', 'resources/photon/proto/protocol.proto',
    'tools/gen-config.sh', 'deployment/scripts/run.sh',
    '.github/scripts/ci-policy.mjs', 'tests/ci/module-validation.test.mjs', 'src/AGENTS.md', '.clang-format',
    'application/unknown.bin', 'application/build.cmake', 'applications/main.cpp', 'src/app/main.cpp',
    'src/plugins/new_plugin/new_module.cpp', 'src/plugins/net/unknown.bin', 'scripts/new-check.sh',
    'docs/example.cpp', 'docs/site/package-lock.json', 'docs/unknown.bin', 'LICENSE']) {
    const scope = plan([file]);
    assert.deepEqual(selected(scope), jobs, file);
    assert.equal(scope.validation_stage, 'modules', file);
    assert.equal(scope.reason, 'unknown-path', file);
    validateGate(results(scope), 'pull_request');
  }
});

test('unavailable, invalid or empty source enumeration cannot skip native regressions or formatting', () => {
  for (const scope of [plan(undefined, 'pull_request', { enumerationFailed: true }), plan([]),
    plan([null]), plan(['../tests/network/test.cpp']), plan(['tests/network/../database/example.cpp']),
    plan(['tests/network/test.cpp\n']), plan(['tests/network/test.cpp'], 'pull_request', { site: { present: true, available: false } })]) {
    assert.deepEqual(selected(scope), jobs);
    assert.equal(scope.format_check, true);
    assert.equal(scope.validation_stage, 'full');
    validateGate(results(scope), 'pull_request');
    for (const job of jobs) {
      const invalid = results(scope);
      invalid.scope.outputs[`${job}_check`] = 'false';
      invalid[job].result = 'skipped';
      assert.throws(() => validateGate(invalid, 'pull_request'));
    }
  }
});

test('queue code and manual runs require all modules while documentation-only queue candidates stay lightweight', () => {
  for (const file of ['tests/network/rudp.cpp', 'tests/core/containers.cpp', 'tests/database/mysql_service.cpp',
    'include/ark/kernel/node.hpp', 'CMakeLists.txt', 'unknown.bin']) {
    const scope = plan([file], 'merge_group');
    assert.deepEqual(selected(scope), jobs, file);
    assert.equal(scope.validation_stage, 'full');
    validateGate(results(scope), 'merge_group');
  }
  const manual = plan(['docs/guide.md'], 'workflow_dispatch');
  assert.deepEqual(selected(manual), jobs);
  assert.equal(manual.validation_stage, 'full');
  validateGate(results(manual), 'workflow_dispatch');
  for (const eventName of ['pull_request', 'merge_group']) {
    const docs = plan(['README.md', 'docs/guide.md'], eventName);
    assert.deepEqual(selected(docs), []);
    assert.equal(docs.validation_stage, 'documentation');
    assert.equal(docs.format_check, false);
    assert.equal(docs.docs_check, true);
    assert.equal(docs.readme_check, true);
    validateGate(results(docs), eventName);
  }
});

test('required results distinguish source modules from queue acceptance and reject event/stage substitution', () => {
  const source = results(plan(['tests/network/rudp.cpp']));
  assert.equal(source.network.result, 'success');
  assert.equal(source.applications.result, 'skipped');
  assert.equal(source.database.result, 'skipped');
  validateGate(source, 'pull_request');
  assert.throws(() => validateGate(source, 'merge_group'));
  assert.throws(() => validateGate(source, 'workflow_dispatch'));
  for (const eventName of [undefined, '', 'unknown', 'push']) assert.throws(() => validateGate(source, eventName));
  for (const stage of [undefined, '', 'full', 'documentation', 'unknown']) {
    const invalid = structuredClone(source);
    invalid.scope.outputs.validation_stage = stage;
    assert.throws(() => validateGate(invalid, 'pull_request'));
  }
  const queue = results(plan(['tests/network/rudp.cpp'], 'merge_group'));
  for (const job of jobs) {
    const invalid = structuredClone(queue);
    invalid.scope.outputs[`${job}_check`] = 'false';
    invalid[job].result = 'skipped';
    assert.throws(() => validateGate(invalid, 'merge_group'));
  }
  for (const reason of [undefined, '', 'unknown']) {
    const invalid = structuredClone(source);
    invalid.scope.outputs.reason = reason;
    assert.throws(() => validateGate(invalid, 'pull_request'));
  }
});

test('unavailable site outputs cannot qualify for module-only validation', () => {
  const invalid = results(plan(['tests/network/rudp.cpp']));
  invalid.scope.outputs.site_available = 'false';
  assert.throws(() => validateGate(invalid, 'pull_request'));
});

test('source module, full queue and manual plans reject failed cancelled skipped and unknown required results', () => {
  for (const scope of [plan(['tests/network/rudp.cpp']), plan(['tests/database/redis_service.cpp']),
    plan(['tests/core/containers.cpp']), plan(['unknown.bin']), plan(['tests/network/rudp.cpp'], 'merge_group'),
    plan([], 'workflow_dispatch')]) {
    const valid = results(scope);
    for (const job of [...jobs, 'validation']) {
      for (const state of ['success', 'skipped', 'failure', 'cancelled', 'unknown', 'neutral', 'timed_out', '', undefined]) {
        if (state === valid[job].result) continue;
        const invalid = structuredClone(valid);
        invalid[job].result = state;
        assert.throws(() => validateGate(invalid, scope.event_name), `${scope.validation_stage}/${job}/${state}`);
      }
      const absent = structuredClone(valid);
      delete absent[job];
      assert.throws(() => validateGate(absent, scope.event_name));
    }
  }
});

test('workflow source jobs reuse Linux Boost and full acceptance retains exactly all twelve native variants', () => {
  const network = matrices('network');
  assert.deepEqual(network.source, [{ os: 'ubuntu-latest', sanitizer: 'none' }]);
  assert.deepEqual(network.full, [
    { os: 'ubuntu-latest', sanitizer: 'none' }, { os: 'macos-latest', sanitizer: 'none' },
    { os: 'windows-latest', sanitizer: 'none' }, { os: 'ubuntu-latest', sanitizer: 'address' },
    { os: 'ubuntu-latest', sanitizer: 'thread' },
  ]);
  const applications = matrices('applications');
  assert.deepEqual(applications.source, [{ os: 'ubuntu-latest' }]);
  assert.deepEqual(applications.full, [{ os: 'ubuntu-latest' }, { os: 'macos-latest' }, { os: 'windows-latest' }]);
  const database = matrices('database');
  assert.deepEqual(database.source, [{ os: 'ubuntu-latest', asio: 'Boost', standalone: 'OFF', features: 'database;boost' }]);
  assert.deepEqual(database.full, [
    { os: 'ubuntu-latest', asio: 'Boost', standalone: 'OFF', features: 'database;boost' },
    { os: 'ubuntu-latest', asio: 'Standalone', standalone: 'ON', features: 'database' },
    { os: 'macos-latest', asio: 'Boost', standalone: 'OFF', features: 'database;boost' },
    { os: 'windows-latest', asio: 'Boost', standalone: 'OFF', features: 'database;boost' },
  ]);
  assert.equal(network.full.length + applications.full.length + database.full.length, 12);
  for (const job of jobs) {
    assert.ok(jobBlock(job).includes(`needs.scope.result != 'success' || needs.scope.outputs.${job}_check != 'false'`),
      'missing/failed classifier outputs still run regressions');
    assert.ok(workflow.includes(`      ${job}_check: \${{ steps.scope.outputs.${job}_check }}`));
  }
  assert.match(jobBlock('network'), /-fsanitize=address,undefined -fno-omit-frame-pointer/);
  assert.match(jobBlock('network'), /-fsanitize=thread -fno-omit-frame-pointer/);
  assert.match(jobBlock('network'), /TSAN_OPTIONS: halt_on_error=1:second_deadlock_stack=1/);
  assert.match(jobBlock('network'), /run: cmake --build ci-build --target check-network/);
  assert.match(jobBlock('applications'), /run: node \.github\/scripts\/ci-applications\.mjs build\n/);
  assert.match(jobBlock('applications'), /run: node \.github\/scripts\/ci-applications\.mjs check\n/);
  for (const eventName of ['merge_group', 'workflow_dispatch']) {
    const env = { GITHUB_EVENT_NAME: eventName, ARK_BUILD_JOBS: '4' };
    assert.deepEqual(applicationCommands('build', env), [['--build', 'ci-build', '--parallel', '4']]);
    assert.deepEqual(applicationCommands('check', env).map(command => command[3]), ['check-core', 'check-entity-benchmark-smoke']);
  }
  assert.match(jobBlock('database'), /run: cmake --build ci-database --target check-database\n/);
  assert.match(jobBlock('database'), /run: cmake --build ci-database --target check-database-benchmark/);
});

test('network display names expand sanitizer labels while retaining the machine matrix values', () => {
  const template = jobBlock('network').match(/^    name: (.+)$/m)?.[1];
  assert.ok(template);
  const labels = { none: 'none', address: 'AddressSanitizer + UndefinedBehaviorSanitizer', thread: 'ThreadSanitizer' };
  const names = matrices('network').full.map(matrix => {
    const before = { ...matrix };
    const name = template.replace(/\$\{\{ (.+?) \}\}/g, (_, expression) => runInNewContext(expression, { matrix }));
    assert.equal(name, `Network - ${matrix.os} - Boost - ${labels[matrix.sanitizer]}`);
    assert.deepEqual(matrix, before, 'display evaluation must not change matrix inputs');
    return name;
  });
  assert.equal(new Set(names).size, names.length, 'each profile keeps a unique check name');
});

test('Applications CI reaches the actual HTTP DSO lifecycle through check-core', () => {
  const applications = jobBlock('applications');
  const configure = applications.match(/      - name: Configure applications[\s\S]*?(?=\n      - )/)?.[0];
  assert.ok(configure, 'Applications must configure the native runtime');
  assert.match(configure, /run: node \.github\/scripts\/ci-applications\.mjs configure$/m);
  const env = { GITHUB_EVENT_NAME: 'merge_group', VCPKG_ROOT: '/vcpkg' };
  assert.ok(applicationCommands('configure', env)[0].includes('-DARK_BUILD_PLUGIN_HTTP=ON'));
  assert.match(applications, /run: node \.github\/scripts\/ci-applications\.mjs check\n/);
  assert.ok(applicationCommands('check', env).some(command => command[3] === 'check-core'));
  const core = readFileSync(new URL('../core/CMakeLists.txt', import.meta.url), 'utf8').replaceAll('\r\n', '\n');
  const http = core.match(/if\(TARGET ark_http_plugin\)\n([\s\S]*?)\nendif\(\)/)?.[1];
  assert.ok(http, 'the optional HTTP target must expose its real-DSO check');
  assert.match(http, /add_custom_target\(check-http-plugin\b/);
  assert.match(http, /-DPROGRAM=\$<TARGET_FILE:ark_http_plugin_tests>/);
  assert.match(http, /-DPLUGIN_DIR=\$<TARGET_FILE_DIR:ark_http_plugin>/);
  assert.match(http, /DEPENDS\s+ark_http_plugin_tests\s+ark_http_plugin\b/);
  assert.match(http, /add_dependencies\(check-core\s+check-http-plugin\)/);
});

test('validation is fatal and required alongside event-aware module results', () => {
  assert.doesNotMatch(jobBlock('validation'), /continue-on-error:/);
  assert.match(jobBlock('required'), /needs: \[scope, network, applications, database, validation\]/);
  assert.match(jobBlock('required'), /if: always\(\)/);
  const script = readFileSync(new URL('../../.github/scripts/ci-policy.mjs', import.meta.url), 'utf8');
  assert.match(script, /validateGate\(needs, process.env.GITHUB_EVENT_NAME, admission\)/);
});

test('module flags and workflow matrix guards keep uncertain source ranges and queue candidates on full acceptance', () => {
  const count = (scope, scopeResult = 'success') => jobs.reduce((total, job) => {
    if (scopeResult === 'success' && !scope[`${job}_check`]) return total;
    const variants = matrices(job);
    return total + (scopeResult === 'success' && scope.event_name === 'pull_request' && scope.validation_stage === 'modules' ?
      variants.source.length : variants.full.length);
  }, 0);
  assert.equal(count(plan(['tests/network/rudp.cpp'])), 1);
  assert.equal(count(plan(['tests/network/rudp.cpp']), 'failure'), 12);
  assert.equal(count(plan(['src/plugins/net/rudp_client.cpp'])), 2);
  assert.equal(count(plan(['src/plugins/mysql/mysql_module.cpp'])), 2);
  assert.equal(count(plan(['tests/core/containers.cpp'])), 1);
  assert.equal(count(plan(['include/ark/container/vector.hpp'])), 3);
  assert.equal(count(plan(['unknown.bin'])), 3);
  assert.equal(count(plan(undefined, 'pull_request', { enumerationFailed: true })), 12);
  assert.equal(count(plan([])), 12);
  assert.equal(count(plan(['../bad.cpp'])), 12);
  const databaseManualChange = ['tests/database/public_plugin_cancellation.cpp',
    'tests/database/cancellation.md', 'tests/database/cancellation_CN.md'];
  assert.equal(count(plan(databaseManualChange)), 1);
  assert.equal(count(plan(databaseManualChange, 'merge_group')), 12);
  assert.equal(count(plan(['tests/database/cancellation.md', 'tests/database/cancellation_CN.md'])), 0);
  assert.equal(count(plan(['tests/database/cancellation.md', 'tests/database/future-manual.md'])), 3);
  assert.equal(count(plan(['tests/network/rudp.cpp'], 'merge_group')), 12);
  assert.equal(count(plan(['docs/guide.md'], 'workflow_dispatch')), 12);
  assert.equal(count(plan(['docs/guide.md'], 'merge_group')), 0);
});
