#!/usr/bin/env bash
# Explicitly opt-in actor for the public-plugin server-termination program.
# Requires bash, lsof, ps, rg and the fresh private service layout described in the guide.
set -euo pipefail
umask 077
[[ $# == 7 ]] || { echo 'Usage: owned_server_fault_actor.sh redis|mysql PID executable config datadir pidfile port' >&2; exit 2; }
mode=$1 process_id=$2 executable=$3 config=$4 datadir=$5 pidfile=$6 service_port=$7
[[ "$mode" == redis || "$mode" == mysql ]] || exit 1
[[ "$process_id" =~ ^[0-9]+$ && "$process_id" -gt 1 && "$service_port" =~ ^[0-9]+$ ]] || exit 1
: "${ARK_DATABASE_FAULT_DIR:?}" "${ARK_DATABASE_FAULT_TOKEN:?}"
private_root=$ARK_DATABASE_FAULT_DIR
token=$ARK_DATABASE_FAULT_TOKEN
# Resolve directory aliases and every existing path's parent components before
# inspecting any process. Leaf symlinks are refused rather than followed.
canonical_path() {
    local candidate=$1
    [[ -e "$candidate" && ! -L "$candidate" ]] || return 1
    if [[ -d "$candidate" ]]; then
        (cd -P -- "$candidate" && pwd -P)
    else
        (cd -P -- "$(dirname "$candidate")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$candidate")")
    fi
}
[[ "$private_root" == /tmp/ark-dso-termination-owned.* && -d "$private_root" && ! -L "$private_root" ]] || exit 1
canonical_tmp=$(cd -P /tmp && pwd -P)
canonical_root=$(canonical_path "$private_root") || exit 1
[[ "$(dirname "$canonical_root")" == "$canonical_tmp" && "$(basename "$canonical_root")" == ark-dso-termination-owned.* ]] || exit 1
canonical_config=$(canonical_path "$config") || exit 1
canonical_datadir=$(canonical_path "$datadir") || exit 1
canonical_pidfile=$(canonical_path "$pidfile") || exit 1
verify_owned_paths() {
    [[ "$(canonical_path "$private_root")" == "$canonical_root" ]] || return 1
    local candidate resolved
    for candidate in "$config" "$datadir" "$pidfile"; do
        resolved=$(canonical_path "$candidate") || return 1
        [[ "$resolved" == "$canonical_root/"* ]] || return 1
    done
    [[ "$(canonical_path "$config")" == "$canonical_config" &&
       "$(canonical_path "$datadir")" == "$canonical_datadir" &&
       "$(canonical_path "$pidfile")" == "$canonical_pidfile" ]] || return 1
    [[ -f "$config" && -d "$datadir" && -f "$pidfile" ]] || return 1
    # These files are direct children of the canonical root. Reject even a
    # dangling link, including stale output markers that redirection could follow.
    for candidate in ownership "$mode.identity" "$mode.ready" "$mode.ready.tmp" "$mode.fired" "$mode.fired.tmp"; do
        [[ ! -L "$canonical_root/$candidate" ]] || return 1
        if [[ -e "$canonical_root/$candidate" ]]; then
            [[ -f "$canonical_root/$candidate" ]] || return 1
        fi
    done
    [[ -f "$canonical_root/ownership" && -f "$canonical_root/$mode.identity" &&
       ! -e "$canonical_root/$mode.fired" && ! -e "$canonical_root/$mode.fired.tmp" ]] || return 1
}
verify_owned_paths || exit 1
[[ "$token" =~ ^[0-9a-f]{64}$ && "$(cat "$canonical_root/ownership")" == "$token" ]] || exit 1
canonical_executable=$(cd -P -- "$(dirname "$executable")" && printf '%s/%s' "$(pwd -P)" "$(basename "$executable")")
identity=$canonical_root/$mode.identity
[[ -f "$identity" && ! -L "$identity" ]] || exit 1
[[ "$(sed -n '1p' "$identity")" == "$token" && "$(sed -n '2p' "$identity")" == "$process_id" ]] || exit 1
[[ "$(sed -n '3p' "$identity")" == "$canonical_executable" && "$(sed -n '4p' "$identity")" == "$datadir" ]] || exit 1
expected_start=$(sed -n '5p' "$identity")
[[ -n "$expected_start" ]] || exit 1
if [[ "$mode" == redis ]]; then
    rg -Fx "dir $datadir" "$config" >/dev/null
    rg -Fx "pidfile $pidfile" "$config" >/dev/null
else
    rg -Fx "datadir=$datadir" "$config" >/dev/null
    rg -Fx "pid-file=$pidfile" "$config" >/dev/null
fi
verify_identity() {
    verify_owned_paths || return 1
    [[ "$(cat "$canonical_root/ownership")" == "$token" && "$(cat "$pidfile")" == "$process_id" ]] || return 1
    kill -0 "$process_id" 2>/dev/null || return 1
    [[ "$(ps -p "$process_id" -o lstart=)" == "$expected_start" ]] || return 1
    lsof -nP -a -p "$process_id" -d txt -Fn 2>/dev/null | rg -Fx "n$canonical_executable" >/dev/null || return 1
    # Both owned servers chdir to their exclusive data directory. This also
    # validates Redis after it replaces its original argv with a process title.
    lsof -nP -a -p "$process_id" -d cwd -Fn 2>/dev/null | rg -Fx "n$canonical_datadir" >/dev/null || return 1
    lsof -nP -a -p "$process_id" -iTCP:"$service_port" -sTCP:LISTEN 2>/dev/null \
        | rg -F "127.0.0.1:$service_port" >/dev/null || return 1
    if [[ "$mode" == mysql ]]; then
        ps -p "$process_id" -o command= | rg -F -- "--defaults-file=$config" >/dev/null || return 1
    fi
}
verify_identity || exit 1
ready=$canonical_root/$mode.ready
for ((step=0; step<150; ++step)); do
    verify_owned_paths || exit 1
    if [[ -f "$ready" ]]; then
        [[ "$(sed -n '1p' "$ready")" == "$token" && "$(sed -n '2p' "$ready")" =~ ^[0-9]+$ ]] || exit 1
        verify_identity || exit 1
        printf '%s server-active id=%s; verified owned pid=%s; injecting SIGKILL\n' \
            "$mode" "$(sed -n '2p' "$ready")" "$process_id"
        kill -KILL "$process_id"
        (set -o noclobber; printf '%s\n%s\nSIGKILL\n' "$token" "$process_id" > "$canonical_root/$mode.fired.tmp")
        mv "$canonical_root/$mode.fired.tmp" "$canonical_root/$mode.fired"
        exit 0
    fi
    # Poll throttling, never evidence that server work is active.
    sleep 0.1
done
echo 'No authenticated server-active barrier; refusing to signal any process.' >&2
exit 1
