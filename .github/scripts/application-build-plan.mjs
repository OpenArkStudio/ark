// Keep the existing configuration for every plan. Narrow PRs save compilation
// by invoking check targets, whose dependencies remain owned by CMake. The full
// build still compiles ALL, including archived integration compatibility inputs.
export const applicationConfigureFlags = Object.freeze([
  '-DARK_BUILD_APPLICATIONS=ON',
  '-DARK_BUILD_BENCHMARKS=ON',
  '-DARK_BUILD_PLUGIN_HTTP=ON',
  '-DARK_BUILD_PLUGIN_CONSUL=ON',
  '-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON',
  '-DARK_BUILD_PROTO=ON',
  '-DARK_BUILD_TESTS=ON',
  '-DARK_BUILD_CORE_TESTS=ON',
  '-DARK_BUILD_COMPRESSION_TESTS=ON',
  '-DARK_BUILD_EXAMPLES=ON',
  '-DARK_BUILD_GUEST_ROOM_EXAMPLE=ON',
  '-DARK_BUILD_CONFIG_TOOL=ON',
  '-DARK_BUILD_CONFIG_TOOL_TESTS=ON',
  '-DASIO_STANDALONE=OFF',
]);

const targetsByPath = new Map();
function register(targets, paths) {
  for (const file of paths) {
    if (targetsByPath.has(file)) throw new Error(`duplicate application path: ${file}`);
    targetsByPath.set(file, Object.freeze([...targets]));
  }
}

// Exact reviewed fixtures only. A future file cannot silently inherit a plan
// that omits a newly introduced consumer. Test CMake scripts are runtime inputs;
// CMakeLists.txt and configure/build scripts deliberately remain unregistered.
register(['check-app-shutdown-deadlines'], ['tests/app/shutdown_deadlines.cmake']);
register(['check-app-business-runtime'], ['tests/app/business_runtime.cmake']);
register(['check-app-business-runtime', 'check-app-shutdown-deadlines'], ['tests/app/business_runtime_plugin.cpp']);
register(['check-app-shutdown'], ['tests/app/shutdown_request.cpp', 'tests/app/shutdown_request.cmake']);
register(['check-app-startup-failures'], [
  'tests/app/startup_failure_plugin.cpp', 'tests/app/missing_entry_plugin.cpp', 'tests/app/startup_failures.cmake',
]);
register(['check-startup-cleanup'], [
  'tests/app/startup_cleanup_allocation.cpp', 'tests/app/startup_cleanup_allocation.cmake',
  'tests/core/startup_cleanup.cpp', 'tests/core/headers/startup_cleanup.cpp',
]);
register(['check-app-startup-cleanup'], ['tests/app/startup_cleanup_plugin.cpp', 'tests/app/startup_cleanup.cmake']);
register(['check-app-manifest-failures'], ['tests/app/manifest_failures.cmake']);
register(['check-app-module-dependencies'], ['tests/app/module_dependencies.cmake']);
register(['check-app-presets'], [
  'tests/app/application_presets.cmake', 'tests/app/application_preset_stop.cpp', 'tests/app/application_preset_stop.sh',
]);
register(['check-app-bus-configuration'], ['tests/app/bus_configuration.cmake']);
register(['check-bus-plugin'], ['tests/app/bus_plugin.cpp', 'tests/app/bus_plugin.cmake']);
register(['check-net-plugin'], ['tests/app/net_plugin.cpp', 'tests/app/net_plugin.cmake']);
register(['check-net-http-drain'], ['tests/app/http_client_drain.cpp', 'tests/app/http_client_drain.cmake']);
register(['check-http-plugin'], ['tests/app/http_plugin.cpp', 'tests/app/http_plugin.cmake']);
register(['check-http-examples'], ['tests/app/http_examples.cpp', 'tests/app/http_examples.cmake']);
register(['check-kernel-plugin'], ['tests/app/kernel_plugin.cpp', 'tests/app/kernel_plugin.cmake']);
register(['check-typed-examples'], ['tests/app/typed_examples.cpp', 'tests/app/typed_examples.cmake']);
register(['check-static-services', 'check-system-composition'], ['tests/app/static_services.cpp']);
register([
  'check-bus-plugin', 'check-net-plugin', 'check-net-http-drain', 'check-kernel-plugin',
  'check-typed-examples', 'check-static-services', 'check-system-composition',
], ['tests/app/owner_drain.hpp']);

register(['check-business-runtime'], [
  'tests/core/business_runtime.cpp', 'tests/core/business_runtime_contract.cpp',
  'tests/core/business_runtime_contract.cmake', 'tests/core/headers/business_runtime.cpp',
]);
register(['check-task-sdk'], [
  'tests/core/tasks.cpp', 'tests/core/headers/task.cpp',
  'tests/core/headers/task_scheduler.cpp', 'tests/core/headers/scheduler_task.cpp',
]);
register(['check-scheduler-drain'], [
  'tests/core/scheduler_drain.cpp', 'tests/core/scheduler_failure.cpp', 'tests/core/scheduler_drain.cmake',
]);
register(['check-scheduler-startup-cleanup'], [
  'tests/core/scheduler_startup_cleanup.cpp', 'tests/core/scheduler_startup_cleanup.cmake',
]);
register(['check-scheduler-construction'], ['tests/core/thread_pool_construction.cpp']);
register(['check-native-unload'], ['tests/core/dynamic_library.cpp', 'tests/core/plugin_allocation_entry.cpp']);

register(['check-kernel-initialization'], [
  'tests/core/module_initialization.cpp', 'tests/core/kernel_callbacks.cpp',
  'tests/core/kernel_resource_ownership.cpp', 'tests/core/table_row_allocation.cpp',
  'tests/core/entity_config_copy.cpp', 'tests/core/entity_json_loading.cpp',
  'tests/core/kernel_delayed_sync.cpp', 'tests/core/entity_nested_snapshot.cpp',
  'tests/core/entity_snapshot.cpp', 'tests/core/entity_snapshot_interface.cpp',
  'tests/core/kernel_common_class_callbacks.cpp', 'tests/core/kernel_metadata_files.hpp',
  'tests/core/kernel_snapshot_test_access.hpp',
]);
register(['check-entity-stream'], ['tests/core/entity_stream.cpp', 'tests/core/entity_nested_stream.cpp']);
register(['check-scalar-stream-allocation'], ['tests/core/entity_scalar_stream_allocation.cpp']);
register(['check-nested-stream-allocation'], ['tests/core/entity_nested_stream_allocation.cpp']);
register(['check-nested-wire'], [
  'tests/core/nested_wire.cpp', 'tests/core/nested_wire_macro.cpp', 'tests/core/nested_wire_macro_first.cpp',
]);
register(['check-proto-naming'], ['tests/proto/naming.cpp']);
register(['check-proto-configuration'], ['tests/proto/check_configuration.cmake']);

register(['check-guest-room'], [
  'tests/guest_room/codec.cpp', 'tests/guest_room/server.cpp',
  'examples/guest_room/guest_room_module.cpp', 'examples/guest_room/guest_room_module.hpp',
  'examples/guest_room/guest_room_plugin.cpp', 'examples/guest_room/guest_room_plugin.hpp',
  'examples/guest_room/guest_room_protocol.hpp', 'examples/guest_room/guest_room_wire.hpp',
  'examples/guest_room/wire-vectors.json', 'examples/guest_room/stage.cmake',
]);
// The composed-services fixture also consumes this example's schema.
register(['check-guest-room', 'check-system-composition'], ['examples/guest_room/resource/meta/entity_schema.json']);

export const applicationTargetPaths = Object.freeze([...targetsByPath.keys()]);
export const applicationCheckTargets = Object.freeze([...new Set([
  'check-core', 'check-entity-benchmark-smoke', ...[...targetsByPath.values()].flat(),
])].sort());

export function applicationTargetsForPath(file) {
  const targets = targetsByPath.get(file);
  return targets ? [...targets] : null;
}

export function fullApplicationBuildPlan(reason = 'full-fallback') {
  return {
    version: 1, mode: 'full', reason, buildAll: true,
    configureFlags: [...applicationConfigureFlags],
    checkTargets: ['check-core', 'check-entity-benchmark-smoke'],
  };
}

export function selectApplicationBuildPlan({ eventName, files, scope }) {
  // Never reduce queue/manual/unknown events, even if their supplied metadata
  // resembles a source PR. Invalid or uncertain scope also retains full work.
  if (eventName !== 'pull_request') return fullApplicationBuildPlan('full-event');
  if (scope?.event_name !== eventName || scope.files_known !== true || scope.site_available !== true ||
      !['documentation', 'modules'].includes(scope.validation_stage) ||
      !['documentation-only', 'module-paths'].includes(scope.reason) || !Array.isArray(files)) {
    return fullApplicationBuildPlan('uncertain-scope');
  }
  if (scope.applications_check === false) {
    return { version: 1, mode: 'skip', reason: 'applications-not-selected', buildAll: false,
      configureFlags: [], checkTargets: [] };
  }
  if (scope.applications_check !== true || files.length === 0) return fullApplicationBuildPlan('empty-or-invalid-application-scope');
  const targets = new Set();
  for (const file of files) {
    const selected = applicationTargetsForPath(file);
    if (!selected) return fullApplicationBuildPlan('unrecognized-application-path');
    for (const target of selected) targets.add(target);
  }
  return {
    version: 1, mode: 'targets', reason: 'registered-application-paths', buildAll: false,
    configureFlags: [...applicationConfigureFlags], checkTargets: [...targets].sort(),
  };
}
