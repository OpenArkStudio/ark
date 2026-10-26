# 历史路线图

本页保留原路线图的文字与勾选状态，仅供追溯，不作为支持的 API、已验收能力或当前优先级清单。中英文快照保留原有差异。支持范围与剩余验收见[当前路线图](roadmap_CN.md)。

---

## 当前改造

- [x] 完成 `include/ark/`、`src/` 与 `src/plugins/<名称>/` 的目录整理和 snake_case 接口迁移，包含 Protobuf 源码命名。
- [x] 随 arknet 0.2.0 发布原生 MySQL、Redis 单节点客户端。
- [ ] 替换 ark 数据库插件的旧驱动，提供 Asio 协程入口，直接使用 arknet 拥有型结果，并在模块线程恢复。
- [ ] 整合其余独立官方插件，按需构建并补齐测试。
- [ ] 在本地审阅 Nimbus 文档，保留原有文章和入口。

业务热更新的讨论与实现暂缓。下方保留原有规划的历史勾选状态，不作为当前 API 或能力清单；当前实现以以上条目、源码和验收记录为准。

## High priority

- [x] excel格式修改为一张excel里放所有的schema,其他配置表分开来写,兼容现在的格式
- [x] 调整代码风格统一
- [x] spdlog升级
- [x] 增加dirty data layer, 当一个客户端请求处理完成后统一下发变化数据，不能有变有发，会导致小包太碎，并且浪费协议的head(改为非实时数据下一帧统一发送，实时数据有变有发)
- [ ] 文档逐步补齐，概念介绍、设计理念、服务器架构、使用例子
- [x] 所有的基础数据结构向C++容器看齐
- [x] 服务中心注册(考虑etcd/consul来做), 已经使用consul来处理了
- [x] 定时log和心跳改为timer方式，不要再是用update和HeartBeat了
- [x] 修改配置打包工具(用Excel编辑，生成`xml`来给工程读取。为解决多人协作冲突, excel可以用名字后缀区分，类如Item.A.xlsx Item.B.xlsx)
- [x] 修改xml不再直接用rapidxml，用封装后的AFXml
- [x] 接口类重新设计纯粹化
- [x] 增加生成器来生成重复代码(_插件类和模块类_)
- [x] 使用`std::chrono`改造`AFDateTime`
- [x] session id 使用GUID类型
- [x] 增加murmur hash函数用来做consistent hash map
- [x] 增加xoshiro/xoroshiro随机算法
- [x] 增加xxtea, crc16, crc32, crc64加密算法
- [ ] 增加codec类，用来剥离框架对特定协议的依赖，方便外部替换协议类型
- [ ] 增加插件动态热更新
- [ ] 增加工具获取插件动态链接库版本号
- [x] Linux下强杀进程处理(kill -9 xxx)
- [ ] 配置meta和entity meta的区分
- [ ] 网络修改为asio系
- [ ] web serever/client修改为asio/beast系
- [ ] Redis 插件接入 arknet 自主实现的 Redis 客户端

## Low priority

- [x] Map, Layer,对应修改协议和Client的处理(**概念已经修改, 还未对接客户端**)
- [x] 关于背包类的table，整理或者交换，需要改成别的方式
- [x] 优化数据同步的流量,减小属性同步的体积(去掉`NodeName`和`TableName`类似的string同步, 上线先同步index和name的映射，后续全部用index下发，入库用name)
- [x] 拓展DataTable的组成，例如道具包含了宝石(带经验，附魔等)、武器(带随机属性)等东西，DataTable现有的二维结构是特定的，不方便做扩展
- [ ] 增加网络库和性能的基准测试用例，放到目录benchmark/unittest
- [ ] 增加kcp
- [ ] 脚本模块使用SWIG
- [x] 考虑使用ECS，全部对象Entity化，~~通过GUID来确定属于哪个分组的哪个位置，能快速定位和查找(通过守望先锋、ET、[ECS-unity](https://github.com/sschmid/Entitas-CSharp)，具体还要再调研)~~
- [ ] 增加coroutine，参考libgo
- [ ] 引入tcmalloc
- [ ] 增加监控web页面(ant-design-for-vue)
- [ ] 增加Deployment web工具
- [ ] ~~增加消息队列处理~~
- [ ] 增加运维工具(tcm & deploy)
- [ ] 部分计算密集的地方使用多线程
- [ ] 增加协议wrapper层，这样框架层包装后，可以甩出来给业务用，无需知道业务层用的协议类型
- [ ] ~~升级至C++17~~
- [ ] ~~`std::string_veiw`/`discard`/...~~
- [ ] ~~插件加载顺序(通过配置中的字段, 或者通过配置的顺序)~~
- [ ] ~~修改进程启动优先链接logserver，如果logserver连不上，则直接不能启动，这样就可以从一开始就能打log了~~

## Part finished

- [x] spdlog增加按照文件夹/大小/小时滚动的sink(*已经修改了一部分，还需要进一步修改成tlog的格式，/data/binlog/1001/20180627/game1_hour.log*)
- [x] 增加consistent_hash_map

## Finished

- [x] 新架构修改
- [x] 增加连接关系的配置，不再通过程序中手动设置关系(bus_relation, server_settting, IP和port不用再指定了, 通过获取IP和动态生成port), 网络地址修改为tcp://0.0.0.0:9001 wss://0.0.0.0:9001这样的URL
- [x] 修改bin文件夹debug和release在一起，debug和release分开配置(方便配置是一套)
- [x] guid改为内部指针类型(降低其他非guid类型的内存占用)***修改为int64_t了,不需要再考虑内存占用了***
- [x] 增加网络插件,这样可以将网络模块和业务逻辑模块拆分开(现在NetClient和NetServer还是跟业务关联太紧密了)
- [x] net plugin增加endpoint
- [x] kernel和config插件还是得合并为一个，因为kernel非常依赖config插件

## Bugs

- [x] ClearRow的时候后面的行号会变化，所以要考虑要么行号不变，只是置空，要么就得客户端来根据DeleteRow消息同步修改后续的row变化
- [x] 现在的ConfigPlugin数据加载有问题
- [x] AFMapEx的First Next返回值有问题
- [ ] 单个进程开多个监听服务，busid会被覆盖和连接管理有问题
