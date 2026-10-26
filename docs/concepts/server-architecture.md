# Server architecture

ark can assemble different server processes from the same plugin/module interfaces. The topology below is a reference for assigning responsibilities in a multi-process deployment. It is not a turnkey cluster, an implemented Registry service, or a measured capacity result.

![Reference process roles and deployment boundaries](../assets/server-topology.svg)

## Separate roles before scaling

| Reference role | Responsibility | Application decisions |
| --- | --- | --- |
| Entry / directory | Direct a client to an environment or entry endpoint | Authentication, endpoint selection and discovery source |
| Cluster services | Coordinate work shared across zones, such as a master or shared business service | Ownership, state recovery and failover |
| Router | Forward selected messages between zone and cluster processes | Destination IDs, allowed routes, congestion handling |
| Zone services | Run a zone's world, game, proxy or login responsibilities | Sharding, player placement and zone boundaries |
| Storage services | Store application records independently from process routing | Data model, replication, durability and backup |

These roles need not be separate binaries for every application. Begin with the processes required by the workload; introduce a boundary when its ownership, scaling or failure behavior needs to differ. A plugin boundary alone is not a network or high-availability boundary.

## What the repository supplies

`src/server/` contains example process roles such as `master`, `router`, `world`, `game`, `proxy`, `login` and `dir`. Their corresponding `config/*.app.toml` manifests select plugins. A role's source directory and configuration demonstrate composition; they do not certify that every service in the reference topology is complete.

The Bus plugin represents process addresses and routing destinations. Use the [Bus reference](../reference/bus.md) to configure local process relationships, create a connection, and send a message with the required identifiers. The [network plugin](../reference/network.md) supplies transport integration. Module-to-module calls inside a process continue to use direct interfaces, as shown in [application architecture](application-architecture.md).

## Start from a verified pair

1. Prepare one application's runtime directory using the [quick start](../get-started/quick-start.md).
2. Give each process its own Bus identity, manifest and endpoints. A shared library filename is not a process identity.
3. Verify a local client/server or Bus connection with a small workload and inspect both sides' logs before adding routing layers.
4. Add multiple zones or routers only with explicit ownership and recovery rules. Keep the target workload and failure tests with the deployment configuration.

Discovery is a separate integration decision. The framework does not require every deployment to start Consul. Configuration-driven endpoints, platform discovery and an optional integration have different operational requirements; none makes a custom multi-node Registry or automatic failover already available. See the [routing design proposal](../project/network-design.md) for the historical cluster/router/zone concept, not a released HA contract.

## Limits to verify in a deployment

Endpoint connectivity alone does not verify authentication, message delivery after reconnect, state transfer, split-brain behavior or storage consistency. The 100,000–200,000 concurrent-user target is a multi-process deployment goal, not a measured single-process limit. [Benchmarks and acceptance](../performance/benchmark.md) distinguish local correctness/performance tools from cross-host capacity and endurance tests.

## Verify server construction

The built-in server plugins use typed module factories and explicit dynamic-library entry points. Construction keeps the existing plugin version, configuration access, interface RTTI names, installation order and disabled frame updates. Network callbacks, lifecycle methods, protocols and resources retain their existing behavior.

With the [application build dependencies](../networking.md) installed, run:

```sh
cmake -S . -B out/server-construction -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  '-DVCPKG_MANIFEST_FEATURES=application;boost;etcd-client' \
  -DCMAKE_BUILD_TYPE=Debug -DASIO_STANDALONE=OFF \
  -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_PROTO=ON \
  -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=OFF
cmake --build out/server-construction --target check-server-plugins --parallel 2
```

`check-server-plugins` exercises the real factory and assembly of each of the seven enabled server libraries, checks the retained source include paths, verifies disabled update admission, and loads each actual shared library to inspect both exported entry symbols. Each role runs a minimal core-only export host before its separate real-factory host. This process separation prevents the host and shared library from registering the same statically linked AFProto descriptors twice. A counted substitute through the real Login assembly checks destruction of rejected candidates, preservation of the original registration and a successful retry. `check-core` includes these focused checks in application builds. A separate shared-library fixture runs the real Login entry and exit through the manager loader with a controlled module. The seven business libraries receive export inspection and direct construction checks, without business lifecycle calls. These checks do not start network services or establish deployment readiness.

`node --test tests/ci/server-construction.test.mjs` checks configure-only compatibility forwarding and construction policy without compiling native code. Simulate has source-policy coverage only; its unsupported PhysX 3/MessagePack integration is outside these native targets.

## Verify a local launcher composition

On macOS and Linux, `check-server-launcher` starts the real `app` executable with the actual Utility, Bus, Net and Login libraries. It creates a private runtime, supplies a loopback-only ephemeral endpoint for Bus identity `1.1.32.1`, waits for the Login listener, connects one TCP peer, sends the owned process `SIGTERM`, and checks server EOF, a refused fresh connection and exit code 0. The peer replies to the server's FIN by closing its own send side before waiting for process exit. A raw peer that stays open can make the transport wait for its existing graceful-disconnect timeout; that wait is distinct from the helper's five-second cooperative shutdown bound. The helper has bounded startup/shutdown waits and reaps only its own child. Runtime configuration and logs remain under the build tree; a new directory is used for each run.

```sh
cmake --build out/server-construction --target check-server-launcher --parallel 1
```

Enable `ARK_BUILD_PLUGIN_UTILITY`, `ARK_BUILD_PLUGIN_BUS` and `ARK_BUILD_PLUGIN_NET` when configuring the application build. The test writes its own minimal manifest instead of modifying the shipped role presets. The Bus schema still requires a `registry` table; this local composition does not load Consul, register a service or perform discovery. Windows retains the construction tests above; this process/signal controller is enabled only on Unix.

This is launcher and transport acceptance. Login currently starts a listener without registering account-login handlers. Proxy's server-selection and message-forwarding callbacks are incomplete. The shipped Router manifest does not select the Router role library, and the World preset leaves its plugin paths/configuration empty; use an application-owned, reviewed manifest before treating those presets as runnable deployments. No authentication, guest/session flow, cluster routing, reconnect recovery or persistence is established by a successful connection.

For a bounded existing business operation, enable examples and Kernel/Utility and run `check-typed-examples`. Its real EntityOperations library uses the checked-in manifest and metadata to create and inspect player data/tables/custom values, then destroys entities during shutdown. That in-process business demonstration and the separate Login transport composition verify different boundaries; they do not form an implemented network game-login flow.
