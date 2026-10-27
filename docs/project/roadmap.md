# Roadmap

This page distinguishes supported source behavior from remaining design and deployment acceptance. C++20 is the build baseline; it does not mean every modernization or optional integration is complete. The [historical roadmap](roadmap-history.md) preserves the previous planning list and its checkbox states.

## Supported behavior

- Core code lives in `src/core/`, the launcher in `src/application/`, shared manifest/configuration reading in `src/config/`, and official plugin headers and implementation together in `src/plugins/<name>/`. Explicit CMake public-header mappings preserve `<ark/...>` include paths. See [project layout and public interfaces](../reference/directory-structure.md) for targets, rebuild requirements and optional plugin boundaries.
- Public [MySQL](../reference/mysql.md) and [Redis](../reference/redis.md) modules use arknet native clients through Asio coroutine entry points. Inputs and results own their storage; manager-owned task groups resume on the module owner thread. MySQL includes prepared statements and transactions; Redis includes commands and pipelines. Legacy database drivers are no longer the public plugin interface.
- The native C++20 [Entity configuration exporter](../guides/tools.md) reads CSV, XLSX or canonical JSON and exports validated Entity definitions, static business profiles and C++/C# field-index constants. [Kernel JSON loading](../reference/kernel.md#json-entity-schema-and-business-data) uses the selected schema and profile snapshot at startup; malformed selected JSON fails startup, while absence preserves the legacy XML path. Export does not reload running servers, migrate databases or filter separate client/server distributions.
- The [guest-room example](../guides/examples.md) uses JSON-defined Kernel Entities and the real Net transport for server-authoritative movement and HP/MP actions over ARK2. The matching official [C# Godot demo](https://github.com/OpenArkStudio/ark-client-godot) has passed local manual review. Restart/rejoin creates fresh state; the example provides no account service or persistence. Its bounded native checks and local manual review do not establish production capacity, comprehensive hostile-client hardening or public deployment acceptance.
- Startup callbacks propagate the first failure. The launcher reports the failed phase and exits with code `1` before the update loop. Shutdown stops managed task admission, drains accepted tasks and waits for module readiness. A configured cooperative wait timeout retains the process/resources and disables unloading. These behaviors do not provide partial-startup rollback or a hard process-stop deadline. See [application startup and shutdown](../guides/applications.md) and [managed tasks](../guides/tasks.md).

Public database service/failure checks, [real-plugin DSO positive workflows](../guides/database-validation.md#real-plugin-dsos-against-real-services), [bounded request cancellation](../guides/database-validation.md#real-plugin-request-cancellation), [observed read-only server termination](../guides/database-validation.md#real-plugin-server-termination) and [Redis TLS command/pipeline and certificate rejection](../guides/database-validation.md#real-plugin-redis-tls) passed against isolated Redis and MySQL servers on macOS. The [database service acceptance record](../guides/database-validation.md) keeps their distinct tested revisions, environments and scope; those results do not cover every deployment or source revision.

## Remaining acceptance and design

| Area | Remaining work and acceptance boundary |
| --- | --- |
| Database deployment | Restart/recovery, persistence and write effects, transaction cancellation, mutual TLS/certificate rotation, Redis TLS cancellation/fault recovery, other cancellation/fault races, other platforms and OS library image removal still need acceptance. Add target-environment and cross-machine capacity evidence before making deployment or capacity claims. Local protocol endpoints and construction checks do not replace these runs. |
| Startup and shutdown | Partial-startup rollback is not provided. A common dependency-aware shutdown protocol remains design work; managed task drain alone does not prove that arbitrary callbacks, native workers or background cleanup have ended. Define ownership and admission phases before extending unload guarantees. |
| Optional plugins | Kafka construction/export checks do not test broker delivery, flush behavior or service interoperability; its driver build needs independent platform acceptance. OSS SDK remains a scaffold without client/request functionality. Physic retains the explicit unsupported legacy PhysX 3 configuration failure. See [optional plugin boundaries](../reference/directory-structure.md). |
| C++20 modernization | Review each remaining module and optional integration against its own API, build and runtime requirements. The language baseline, header layout and focused construction checks do not establish completion of the whole modernization. |

## Deferred directions

Registry design and business-plugin hot update remain paused. In-flight work, state ownership, dependency order and compatibility need an agreed design before implementation or broader unload claims.

Historical ideas in the archive are not automatically scheduled work. Future scope should name the affected API or deployment, its prerequisites and concrete acceptance evidence before it is promoted into this roadmap.
