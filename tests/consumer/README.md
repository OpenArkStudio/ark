# SDK consumer

This is an independent CMake project using `ark::core`, `ark::net`, `ark::kernel`,
`ark::mysql`, `ark::redis` and `ark::utility`. It checks public headers and transitive build-tree
usage requirements without building ark applications or database plugin libraries.
Run `cmake --build <build-dir> --target check-sdk` after configuration.

Database and Utility public headers are staged from explicit lists beside their implementations;
the consumer also checks Utility public/helper mappings and rejects visible implementation/assembly headers. Redis's interface is
checked in all builds; the MySQL compile check requires `ARKNET_ENABLE_SSL=ON`.
This is an `add_subdirectory` build-tree consumer, not an installed-package test.

Build instructions: [English](../../docs/reference/directory-structure.md) · [简体中文](../../docs/reference/directory-structure_CN.md).

`network/` is an independent installed-package entry using only `find_package(ark)`
and public API targets. `check-network-installed` loads the matching production
Net, Bus, Kernel and Utility DSOs from an installed prefix, reads the canonical
guest-room Bus/JSON resources, and checks one TCP round trip and normal stop/unload.
Its native UNIX runtime requires explicit plugin/resource paths; see the bilingual
[installation guide](../../docs/get-started/installation.md#install-and-consume-the-cmake-package).

`etcd_client/` consumes only `ark::etcd_client` with its runtime plugin and Protobuf disabled. It compiles all five retained public headers independently, including implementation and assembly declarations. Run `cmake --build <build-dir> --target check-etcd-client-headers` after configuration. This header consumer does not contact an etcd service. Consul, Kafka, OSS SDK and Physic header mappings are checked by the configure-only `tests/ci/plugin-layout.test.mjs`; that check does not establish native or runtime compatibility for those optional integrations.
