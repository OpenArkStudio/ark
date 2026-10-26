# 通过公开插件 DSO 验证自有服务异常退出

[English](server_termination.md)

`ark_database_public_plugin_server_termination_tests redis|mysql` 仅使用公开 API
target，加载实际的隐藏符号插件 DSO。`check-database` 只编译该程序，不执行它，也不向
数据库进程发送信号。MySQL 需要 `ARKNET_ENABLE_SSL=ON` 和受信任 TLS。
独立 Bash 故障执行脚本需要 `lsof`、`ps`、`rg` 及下述私有服务目录；
它面向本地自有 Unix 进程，不提供 Windows 故障注入。

按[数据库测试配置](README_CN.md#构建)构建：

```sh
cmake --build database-build \
  --target ark_database_public_plugin_server_termination_tests --parallel 1
```

## 请求与断言

Redis 发送单条只读命令 `XREAD BLOCK 60000 STREAMS <exclusive-key> $`。
预检确认独占流键不存在；测试不创建流或数据。独立认证观察连接读取 `CLIENT LIST`，
必须找到名称完全匹配的主连接，并确认 `cmd=xread`、阻塞标志和服务端 client ID。
MySQL 在私有 schema 中发送 `SELECT SLEEP(60) /*<unique-tag>*/`。
同一自有服务上的独立受信任 TLS 连接，必须在 `information_schema.processlist`
中观察到该标签及 connection ID，且状态为 `User sleep`。
只有完成真实服务观察后，C++ 程序才发布私有故障屏障。
轮询间隔仅用于节流，不能作为请求正在执行的证据。

独立 shell 脚本再次核对归属令牌、捕获的 PID 和启动时间、规范化可执行文件映射、
私有数据工作目录、PID 文件及该 PID 的 IPv4 回环监听端口，才向这个 PID 发送
`SIGKILL`。MySQL 还核对 defaults-file 参数，以及私有配置中的数据目录映射。
父 shell 必须收集服务子进程的预期信号退出状态、故障脚本成功凭据及测试程序退出状态；
单个标记文件不能证明进程已退出。

待完成请求的操作超时为 20 秒，服务端阻塞为 60 秒。预期 Redis 返回
`disconnected`、`command_may_have_run=true`；MySQL 返回 `transport`、
`outcome_unknown=true`，两者均保留非零原生传输错误。
取消、操作超时及 MySQL SQL 服务错误均不符合该场景。
这些失败通过服务结果返回，协程异常会使测试失败。请求和观察根任务必须在拥有线程的
共享执行器上各完成一次，在 stop 前自然退出；拥有型原生结果在模块、manager 销毁及
DSO 卸载后仍可访问。协作式 stop 有界且幂等。
测试不通过取消待完成任务注入故障；已退出的任务 ID 必须拒绝 cancel。

## 私有故障屏障

仅使用新建的一次性服务、私有凭据和 IPv4 回环监听。MySQL 使用新建的 schema、
仅具 SELECT 权限的账号，以及证书包含回环 IP 的受信任 TLS。
不得使用共享服务或已安装的服务。本测试不重启服务，不检查持久化、事务回滚或写入效果。

启动服务前，新建私有目录和令牌：

```sh
umask 077
private_root=$(mktemp -d /tmp/ark-dso-termination-owned.XXXXXX)
export ARK_DATABASE_FAULT_DIR=$private_root
export ARK_DATABASE_FAULT_TOKEN=$(openssl rand -hex 32)
printf '%s\n' "$ARK_DATABASE_FAULT_TOKEN" > "$private_root/ownership"
```

配置、数据和 PID 文件路径都必须位于该目录下。Redis 配置需包含完全匹配的
`dir <datadir>` 和 `pidfile <pidfile>` 行；MySQL 需包含 `datadir=<datadir>`、
`pid-file=<pidfile>`，并通过 `--defaults-file=<config>` 启动。
以前台模式作为编排 shell 的子进程运行，捕获 `$!`，通过认证请求和该 PID 的监听端口
检查就绪，不能用等待时间代替。两种服务均须以其私有数据目录作为工作目录。

就绪后，记录这个自有子进程的身份。可执行文件路径必须规范化（目录通过 `pwd -P`
解析）；`datadir`、`config`、`pidfile` 保留私有根目录原始写法：

```sh
printf '%s\n%s\n%s\n%s\n%s\n' "$ARK_DATABASE_FAULT_TOKEN" "$service_pid" \
  "$canonical_executable" "$datadir" "$(ps -p "$service_pid" -o lstart=)" \
  > "$private_root/$mode.identity"
# mode 为 redis 或 mysql；port 仅属于这个自有子进程。
bash tests/database/owned_server_fault_actor.sh "$mode" "$service_pid" \
  "$canonical_executable" "$config" "$datadir" "$pidfile" "$port" > "$actor_log" 2>&1 &
actor_pid=$!
```

与故障脚本并行运行 C++ 程序，提供与[公开 DSO 服务验收](README_CN.md#真实插件动态库与真实服务)
相同的端点变量：Redis host/port/password；
MySQL host/port/user/password/database/CA。凭据仅放入私有文件或环境变量，
不进入命令行参数或提交的日志。`mode` 选择协议：

```sh
./database-build/tests/database/ark_database_public_plugin_server_termination_tests "$mode"
host_status=$?
wait "$actor_pid"; actor_status=$?
wait "$service_pid"; service_status=$?
# 要求 host_status=0、actor_status=0、service_status=137（Bash 中的 SIGKILL）。
```

编排启用 `set -e` 时，需要显式捕获预期的非零信号退出。为整个流程增加有界 shell
监督。缺失、不匹配或旧标记均会失败；故障脚本在 15 秒内未得到有效活动屏障时拒绝发送信号。
保留清理 trap：对尚未注入故障的、身份核验通过的子进程执行认证正常停机；
无论成功还是失败，均回收全部自有子进程、核实其确切 PID 已消失，再按令牌核验删除私有目录，
包括凭据、数据和 TLS 私钥。不得复用故障标记。

该场景仅覆盖观察到的对端丢失请求及本地退出，不代表重启恢复、持久化、自动重放行为、
所有故障竞态、已部署服务、跨主机或容量验收。原生传输错误由平台决定，不要求固定的
EOF/reset 数值。身份、结果和清理原始证据保留在 Git 之外。
