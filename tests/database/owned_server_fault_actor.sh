#!/usr/bin/env bash
# Explicitly opt-in actor for the public-plugin server-termination program.
# Requires bash, lsof, ps and the fresh private service layout described in the guide.
set -euo pipefail
umask 077
[[ $# == 7 ]] || { echo 'Usage: owned_server_fault_actor.sh redis|mysql PID executable config datadir pidfile port' >&2; exit 2; }
mode=$1 process_id=$2 executable=$3 config=$4 datadir=$5 pidfile=$6 service_port=$7
[[ "$mode" == redis || "$mode" == mysql ]]
[[ "$process_id" =~ ^[0-9]+$ && "$process_id" -gt 1 && "$service_port" =~ ^[0-9]+$ ]]
: "${ARK_DATABASE_FAULT_DIR:?}" "${ARK_DATABASE_FAULT_TOKEN:?}"
private_root=$ARK_DATABASE_FAULT_DIR
token=$ARK_DATABASE_FAULT_TOKEN
[[ "$private_root" == /tmp/ark-dso-termination-owned.* && -d "$private_root" && ! -L "$private_root" ]]
[[ "$token" =~ ^[0-9a-f]{64}$ && "$(cat "$private_root/ownership")" == "$token" ]]
for file in "$config" "$datadir" "$pidfile"; do
    [[ "$file" == "$private_root/"* && -e "$file" && ! -L "$file" ]]
done
canonical_executable=$(cd "$(dirname "$executable")" && printf '%s/%s' "$(pwd -P)" "$(basename "$executable")")
identity=$private_root/$mode.identity
[[ -f "$identity" && ! -L "$identity" ]]
[[ "$(sed -n '1p' "$identity")" == "$token" && "$(sed -n '2p' "$identity")" == "$process_id" ]]
[[ "$(sed -n '3p' "$identity")" == "$canonical_executable" && "$(sed -n '4p' "$identity")" == "$datadir" ]]
expected_start=$(sed -n '5p' "$identity")
[[ -n "$expected_start" ]]
if [[ "$mode" == redis ]]; then
    rg -Fx "dir $datadir" "$config" >/dev/null
    rg -Fx "pidfile $pidfile" "$config" >/dev/null
else
    rg -Fx "datadir=$datadir" "$config" >/dev/null
    rg -Fx "pid-file=$pidfile" "$config" >/dev/null
fi
verify_identity() {
    [[ "$(cat "$private_root/ownership")" == "$token" && "$(cat "$pidfile")" == "$process_id" ]] || return 1
    kill -0 "$process_id" 2>/dev/null || return 1
    [[ "$(ps -p "$process_id" -o lstart=)" == "$expected_start" ]] || return 1
    lsof -nP -a -p "$process_id" -d txt -Fn 2>/dev/null | rg -Fx "n$canonical_executable" >/dev/null || return 1
    # Both owned servers chdir to their exclusive data directory. This also
    # validates Redis after it replaces its original argv with a process title.
    local canonical_datadir
    canonical_datadir=$(cd "$datadir" && pwd -P) || return 1
    lsof -nP -a -p "$process_id" -d cwd -Fn 2>/dev/null | rg -Fx "n$canonical_datadir" >/dev/null || return 1
    lsof -nP -a -p "$process_id" -iTCP:"$service_port" -sTCP:LISTEN 2>/dev/null \
        | rg -F "127.0.0.1:$service_port" >/dev/null || return 1
    if [[ "$mode" == mysql ]]; then
        ps -p "$process_id" -o command= | rg -F -- "--defaults-file=$config" >/dev/null || return 1
    fi
}
verify_identity
ready=$private_root/$mode.ready
for ((step=0; step<150; ++step)); do
    if [[ -f "$ready" && ! -L "$ready" ]]; then
        [[ "$(sed -n '1p' "$ready")" == "$token" && "$(sed -n '2p' "$ready")" =~ ^[0-9]+$ ]]
        [[ ! -e "$private_root/$mode.fired" ]]
        verify_identity
        printf '%s server-active id=%s; verified owned pid=%s; injecting SIGKILL\n' \
            "$mode" "$(sed -n '2p' "$ready")" "$process_id"
        kill -KILL "$process_id"
        printf '%s\n%s\nSIGKILL\n' "$token" "$process_id" > "$private_root/$mode.fired.tmp"
        mv "$private_root/$mode.fired.tmp" "$private_root/$mode.fired"
        exit 0
    fi
    # Poll throttling, never evidence that server work is active.
    sleep 0.1
done
echo 'No authenticated server-active barrier; refusing to signal any process.' >&2
exit 1
