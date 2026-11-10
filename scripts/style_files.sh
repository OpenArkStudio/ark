#!/usr/bin/env bash
# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0

# Shared file scope for formatting and naming checks. Generated build outputs and
# vendored implementations retain their upstream conventions.
ark_style_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)

ark_style_files() {
    git -C "$ark_style_root" ls-files -z --cached --others --exclude-standard -- \
        include src examples tests benchmarks tools resources/photon/proto |
        while IFS= read -r -d '' style_file; do
            case "$style_file" in
                tests/catch.hpp|tests/network/doctest.h|\
                src/plugins/physic/wave_front.hpp|\
                src/plugins/physic/wave_front.cpp|\
                src/plugins/utility/ctpl.h|\
                src/plugins/utility/ctpl_stl.h) continue ;;
            esac
            case "$style_file" in
                *.hpp|*.h|*.cpp|*.cc|*.c|*.proto)
                    if [[ -f "$ark_style_root/$style_file" ]]; then
                        printf '%s\0' "$style_file"
                    fi
                    ;;
            esac
        done
}
