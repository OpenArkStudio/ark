# Benchmarks and capacity acceptance

Correctness, component cost and deployment capacity answer different questions.

| Measurement | Scope |
| --- | --- |
| C++ regression tests | Message correctness, reconnect, shutdown and callback behavior |
| Local component benchmarks | Receive-buffer allocation, framing cost and private database delivery |
| Cross-machine load and endurance | Throughput, latency, CPU, memory and failure recovery at the target load |

The deployment target is 100,000–200,000 simultaneous connections across processes, without a single-process capacity claim. Before measuring, define message frequency, body sizes, process count, connection distribution and failures.

Run [network regressions](../networking.md) and consult [arknet's protocol reports](https://openarkstudio.github.io/arknet/performance/) for transport measurements. TODO: publish a complete cross-machine deployment acceptance report.

## Application receive-buffer measurements

The C++ [receive-buffer benchmark](receive-buffers.md) compares 4, 64 and 512 KiB initial capacities, recording the machine, medians/ranges and throughput regression. It does not replace cross-machine load acceptance.

## Database callbacks and coroutines

The [database report](database-coroutines.md) compares callbacks/Asio coroutines through ark’s private service, manual executor and task group: 56 configurations, 336 fresh processes, two payloads, connection/in-flight limits and continuous/1 ms updates. SVGs show throughput/CPU; response time, process memory and complete tables expand below. Short local runs do not establish deployment capacity.
