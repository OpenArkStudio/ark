# MySQL 一致性读隔离

公共 `begin()` 使用服务端默认隔离级别。验收程序通过公共插件协程接口，验证两种显式配置的私有 MySQL 服务。

## 验证结果

| 读事务观察时机 | REPEATABLE READ | READ COMMITTED |
| --- | --- | --- |
| 写事务尚未提交 | 原始行；新增行不可见 | 原始行；新增行不可见 |
| 写事务已提交；读事务未结束 | 原始行；新增行不可见 | 更新行与新增行均可见 |
| 读事务结束后再次查询 | 两项已提交变更均可见 | 两项已提交变更均可见 |

两个事务分别固定使用独立连接。自有查询结果在删表和宿主销毁后仍有效；已接收任务在所属线程的共享执行器上完成一次。

## 运行

```sh
cmake --build database-build --target ark_database_public_plugin_isolation_tests --parallel 1
# 服务端默认值：--transaction-isolation=REPEATABLE-READ
./database-build/tests/database/ark_database_public_plugin_isolation_tests repeatable-read
# 另一个私有服务的默认值：--transaction-isolation=READ-COMMITTED
./database-build/tests/database/ark_database_public_plugin_isolation_tests read-committed
```

参数只声明预期值，不设置隔离级别。使用启用认证并校验可信 TLS 的私有服务；配置、权限、清理和完整验证步骤见[操作说明](../../tests/database/isolation_CN.md)。`check-database` 只编译程序，不启动服务。

查询租约归还时执行 `COM_RESET_CONNECTION`。单独调用 `query()` 设置会话级别，不能配置后续 `begin()`；程序同时验证这一重置边界。事务配置规则见[MySQL 文档](https://dev.mysql.com/doc/refman/8.4/en/set-transaction.html)。

## 本地验收

| 项目 | 配置／结果 |
| --- | --- |
| 日期与机器 | 2026-10-29；Mac mini，Apple M4 Pro，14 核，48 GiB 内存 |
| 系统／构建 | macOS 27.0.1 arm64；Apple Clang 21.0.0；C++20 Debug |
| 依赖 | MySQL 8.4.13；Boost.Asio 1.90.0；OpenSSL 3.6.4 |
| 源码 | ark `093dfcb9a430beedcd86de19eb0d70c8aae9160e`；arknet `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| 正向用例 | 两种级别均通过；分别使用 `127.0.0.1:29879` 上的全新私有服务 |
| 反向对照 | 两个服务均拒绝相反的预期级别；五种无效配置均被拒绝 |
| 清理 | 无剩余测试表、自有进程、监听端口、私有数据或凭据 |

验收程序与所测版本一致，原始记录仅保留在本地。普通非锁定读取不证明锁定读取、`SERIALIZABLE`、取消、持久性、高可用、其他平台或容量。[重启恢复](database-restart_CN.md)另有验收记录。
