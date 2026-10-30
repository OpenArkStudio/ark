#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "Usage: $0 <Release|Debug> <name> <busid> [runtime-directory]" >&2
    exit 2
fi
case "$1" in
    Release) binary=./bin/app ;;
    Debug) binary=./bin/app_d ;;
    *) echo "Configuration must be Release or Debug" >&2; exit 2 ;;
esac
case "$2" in
    ''|*[!a-zA-Z0-9_-]*) echo "Name must contain only letters, digits, _ or -" >&2; exit 2 ;;
esac
if [ "$#" -eq 4 ]; then
    cd -- "$4"
else
    CDPATH='' cd -- "$(dirname -- "$0")"
fi
[ -x "$binary" ] || { echo "Missing executable: $binary" >&2; exit 1; }
config="conf/$2.app.toml"
[ -f "$config" ] || { echo "Missing configuration: $config" >&2; exit 1; }
export LC_ALL=C
exec "$binary" "--busid=$3" "--name=$2" "--app_conf=conf/$2.app.toml"
