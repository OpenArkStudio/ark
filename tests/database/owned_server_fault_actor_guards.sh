#!/usr/bin/env bash
# Non-signaling path-boundary regressions: no server is started or inspected.
set -euo pipefail
umask 077
actor=${1:-"$(cd "$(dirname "$0")" && pwd -P)/owned_server_fault_actor.sh"}
fixture_root= fixture_outside=
cleanup() {
    [[ -z "$fixture_root" ]] || rm -r -- "$fixture_root"
    [[ -z "$fixture_outside" ]] || rm -r -- "$fixture_outside"
}
trap cleanup EXIT

check_case() {
    local scenario=$1 actor_root datadir config pidfile token canonical_executable status=0
    fixture_root=$(mktemp -d /tmp/ark-dso-termination-owned.guard.XXXXXX)
    fixture_outside=$(mktemp -d /tmp/ark-dso-termination-guard-outside.XXXXXX)
    actor_root=$fixture_root
    datadir=$fixture_root/data
    config=$fixture_root/config
    pidfile=$fixture_root/pid
    token=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
    mkdir "$datadir" "$fixture_outside/data" "$fixture_outside/inner"
    canonical_executable=$(cd /bin && printf '%s/sleep' "$(pwd -P)")
    printf '%s\n' "$token" > "$fixture_root/ownership"
    printf '%s\n' "$token" > "$fixture_outside/ownership"
    # The fake PID is never used to inspect or signal a real process. BASH_ENV
    # intercepts process/signal/wait commands; every unexpected call fails fast.
    printf '2147483647\n' > "$pidfile"
    printf '2147483647\n' > "$fixture_outside/pid"
    ln -s "$fixture_outside" "$fixture_root/escape"
    ln -s "$fixture_outside/data" "$fixture_root/escape_data"
    case "$scenario" in
        config_traversal) config=$fixture_root/../$(basename "$fixture_outside")/config ;;
        datadir_traversal) datadir=$fixture_root/../$(basename "$fixture_outside")/data ;;
        pidfile_traversal) pidfile=$fixture_root/../$(basename "$fixture_outside")/pid ;;
        config_parent_symlink) config=$fixture_root/escape/config ;;
        datadir_parent_symlink) datadir=$fixture_root/escape/data ;;
        pidfile_parent_symlink) pidfile=$fixture_root/escape/pid ;;
        config_symlink_dotdot) config=$fixture_root/escape_data/../config ;;
        datadir_symlink_dotdot) datadir=$fixture_root/escape_data/../data ;;
        pidfile_symlink_dotdot) pidfile=$fixture_root/escape_data/../pid ;;
        root_parent_symlink)
            actor_root=$fixture_root/escape/inner
            datadir=$actor_root/data; config=$actor_root/config; pidfile=$actor_root/pid
            mkdir "$datadir"
            printf '%s\n' "$token" > "$actor_root/ownership"
            printf '2147483647\n' > "$pidfile"
            ;;
    esac
    printf 'dir %s\npidfile %s\n' "$datadir" "$pidfile" > "$config"
    printf '%s\n2147483647\n%s\n%s\nfake-start-time\n' "$token" "$canonical_executable" "$datadir" \
        > "$actor_root/redis.identity"
    case "$scenario" in
        config_leaf_symlink)
            cp "$config" "$fixture_outside/config"; rm "$config"; ln -s "$fixture_outside/config" "$config" ;;
        datadir_leaf_symlink)
            rmdir "$datadir"; ln -s "$fixture_outside/data" "$datadir" ;;
        pidfile_leaf_symlink)
            rm "$pidfile"; ln -s "$fixture_outside/pid" "$pidfile" ;;
        ownership_symlink)
            rm "$fixture_root/ownership"; ln -s "$fixture_outside/ownership" "$fixture_root/ownership" ;;
        identity_symlink)
            mv "$fixture_root/redis.identity" "$fixture_outside/identity"
            ln -s "$fixture_outside/identity" "$fixture_root/redis.identity" ;;
        ready_symlink) ln -s "$fixture_outside/missing" "$fixture_root/redis.ready" ;;
        ready_tmp_symlink) ln -s "$fixture_outside/missing" "$fixture_root/redis.ready.tmp" ;;
        fired_symlink) ln -s "$fixture_outside/missing" "$fixture_root/redis.fired" ;;
        fired_tmp_symlink) ln -s "$fixture_outside/missing" "$fixture_root/redis.fired.tmp" ;;
        stale_fired) printf 'stale\n' > "$fixture_root/redis.fired" ;;
    esac
    cat > "$fixture_outside/intercept.sh" <<'INTERCEPT'
kill() { printf 'kill\n' >> "$ARK_ACTOR_GUARD_CALLS"; return 99; }
ps() { printf 'ps\n' >> "$ARK_ACTOR_GUARD_CALLS"; return 99; }
lsof() { printf 'lsof\n' >> "$ARK_ACTOR_GUARD_CALLS"; return 99; }
sleep() { printf 'sleep\n' >> "$ARK_ACTOR_GUARD_CALLS"; return 99; }
INTERCEPT
    if ARK_DATABASE_FAULT_DIR=$actor_root ARK_DATABASE_FAULT_TOKEN=$token \
        ARK_ACTOR_GUARD_CALLS=$fixture_outside/calls BASH_ENV=$fixture_outside/intercept.sh \
        bash "$actor" redis 2147483647 "$canonical_executable" "$config" "$datadir" "$pidfile" 49999 \
        > "$fixture_outside/actor.log" 2>&1; then :; else status=$?; fi
    if [[ "$scenario" == control ]]; then
        [[ $status != 0 && "$(cat "$fixture_outside/calls")" == kill ]] || return 1
        printf 'valid private paths reach the intercepted process check; no process inspected or signaled\n'
    else
        if [[ $status == 0 || -e "$fixture_outside/calls" ]]; then
            printf '%s escaped the pre-inspection path guard (actor exit %s)\n' "$scenario" "$status" >&2
            [[ ! -f "$fixture_outside/calls" ]] || cat "$fixture_outside/calls" >&2
            return 1
        fi
        [[ ! -f "$fixture_root/redis.fired" || "$scenario" == stale_fired ]] || return 1
        printf '%s rejected before process inspection/signaling\n' "$scenario"
    fi
    cleanup
    fixture_root= fixture_outside=
}

check_case control
for scenario in config_traversal datadir_traversal pidfile_traversal \
    config_parent_symlink datadir_parent_symlink pidfile_parent_symlink root_parent_symlink \
    config_symlink_dotdot datadir_symlink_dotdot pidfile_symlink_dotdot \
    config_leaf_symlink datadir_leaf_symlink pidfile_leaf_symlink ownership_symlink identity_symlink \
    ready_symlink ready_tmp_symlink fired_symlink fired_tmp_symlink stale_fired; do
    check_case "$scenario"
done
printf '20 invalid-path/marker guards and one non-signaling control passed\n'
