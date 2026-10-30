#!/usr/bin/env sh
# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
set -eu
sample_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
bin_dir=''
runtime_dir=''
host=127.0.0.1
port=19100
program=app
while [ "$#" -gt 0 ]; do
    case "$1" in
        --bin-dir) bin_dir=$2; shift 2 ;;
        --runtime-dir) runtime_dir=$2; shift 2 ;;
        --host) host=$2; shift 2 ;;
        --port) port=$2; shift 2 ;;
        --debug) program=app_d; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$bin_dir" ] || [ -z "$runtime_dir" ]; then
    echo "Usage: run.sh --bin-dir BUILD/bin --runtime-dir NEW_RUN_DIR [--host 127.0.0.1] [--port 19100] [--debug]" >&2
    exit 2
fi
cmake "-DBIN_DIR=$bin_dir" "-DRUNTIME_DIR=$runtime_dir" "-DHOST=$host" "-DPORT=$port" -P "$sample_dir/stage.cmake"
cd -- "$runtime_dir"
exec "./bin/$program" --busid=1.1.31.1 --name=guest_room --app_conf=conf/guest_room.app.toml --shutdown_timeout_ms=5000
