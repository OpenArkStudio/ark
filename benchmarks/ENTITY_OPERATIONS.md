# Entity access benchmark

This optional C++ executable retains the former entity example's twelve hot loops: string reads, integer reads and writes, each directly by index/name and after a Kernel lookup. It loads real Kernel/Utility plugins but never loads `entity_operations_plugin` or runs the example's startup. Measurement mode uses monotonic `steady_clock` intervals in milliseconds; it does not measure server capacity or production workload behavior.

Enable applications, configuration, Protobuf and the Kernel/Utility plugins alongside `ARK_BUILD_BENCHMARKS=ON`, with the dependencies in the installation guide. The executable is excluded from the default build and is not a dependency of application startup or `check-core`.

```sh
cmake -S . -B app-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARK_BUILD_BENCHMARKS=ON
cmake --build app-build --target ark_entity_operations_benchmark -j2
mkdir -p run/entity-benchmark/conf
cp config/kernel.plugin.conf run/entity-benchmark/conf/
cp -R examples/entity_operations/resource run/entity-benchmark/
cd run/entity-benchmark
../../app-build/benchmarks/ark_entity_operations_benchmark --plugin-dir ../../app-build/bin --iterations 5000000
```

The iteration count is explicit and positive; there is no default measured run. Five million iterations reproduce the old 60-million-operation loop count. Do not use that number for correctness CI. On Windows add `.exe` to the executable and use the matching configuration output directory.

For a small correctness smoke, use `--smoke` instead of `--iterations N`. It runs eight iterations per loop with timing disabled and verifies the final name and gender values. The optional target stages the same metadata and runs only this smoke:

```sh
cmake --build app-build --target check-entity-benchmark-smoke -j2
```

After either mode finishes, the CLI cooperatively drains accepted plugin work before
exiting, with a fixed five-second shutdown deadline. Owner updates keep the synthetic
clock used at startup. A terminal failure or expired deadline exits with status `1`
and a shutdown diagnostic; it is not reported as a completed workload. Deadline
checks cannot interrupt a synchronous hook. The smoke's outer 20-second process
observation bound remains unchanged.

No benchmark measurement or speedup result is published with this extraction. Compiling and passing the smoke do not establish performance. Any later report must identify machine, OS, toolchain, dependency and source/binary revisions, explicit iteration count and units; retain raw measurements locally until separately authorized for publication.

The existing Applications CI job explicitly enables benchmark builds and runs this smoke as a separate step. It does not execute measurement mode; `check-core` remains independent of the optional benchmark.
