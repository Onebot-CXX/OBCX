# 测试目录

`tests/` 只保存 OBCX 根仓库拥有的可重复自动化测试。QQ、LLOneBot、Docker
Compose 和包含凭据的本地配置位于 `dev/onebot/`，不属于测试门禁。

## 测试保留标准

根仓库及 `local_actor/`、`local_library/` 的自有测试只保留边界与高风险场景：

- 空值、缺项、非法输入、上下限及边界上的成功输入。
- 超时、取消、并发竞态、重复调用、销毁与动态库卸载安全。
- 权限与 installation/conversation/topic 隔离、敏感信息脱敏。
- 数据完整性、迁移回滚、重复投递、失败后的恢复与原子性。
- 历史缺陷回归，以及防止修复误伤所需的成功对照。
- 真正执行的编译拒绝、SDK 隔离及跨动态库生命周期边界。

不保留独立的普通成功流程、纯赋值/往返序列化、夹具自测或重复冒烟。
边界用例需要的成功准备步骤不应删除；不要把普通用例藏进循环或大测试。
删除用例时同时移除无人使用的辅助代码、独立夹具及 CMake 注册。

## 所有权边界

根仓库测试可以覆盖：

- actor runtime、scheduler、Asio/BlockingExecutor、reload 与通用 ABI 2；
- `BotInstallation`/`BotComponent`/capability registry、固定 recipe、严格 bot
  configuration、OneBot/Telegram protocol/transport/ingress/operation 组件；
- CLI、数据库、metadata、registry、packaging、安装后 SDK 与通用 fixture actor。

根仓库自有测试不得包含生产 actor 的私有头文件、实现源码或业务断言，也不得
遍历全部 `local_actor/`。工作区 tests profile 可加载明确选中的包，并注册由各包
自己拥有的业务测试；这不改变测试源码的所有权。Bot component DAG、recipe、严格配置、
ingress、operation endpoint、通用 ABI、same-SONAME staging、dependency
isolation 和 generation cutover 使用根仓库自有源码与通用 fixture 验证。每个
独立 actor 仓库自行拥有并执行其 standalone build、安装、业务测试和跨 actor
集成测试。

## 目录职责

测试源码按功能归类，不按 C++、Python 或测试执行方式拆分：

- `actor/`：actor 配置、协程、调度、加载、staging、热重载与反射编译契约。
- `bot/`：bot SDK、组件、平台协议、操作与消息入口。
- `command/`：命令协调与平台适配。
- `network/`：HTTP、curl、WebSocket、超时与取消。
- `package/`：包契约、解析、来源、provider、registry、CMake 集成与发布工具。
- `cli/`：命令行处理。
- `database/`：数据库。
- `tui/`：终端界面布局。

共享测试基础设施单独保留：

- `cmake/`：测试注册模块，以及由 CTest 调用的 SDK、CLI 与根仓库集成脚本。
- `fixtures/`：通用 actor DSO、standalone SDK consumer 与静态测试数据。
- `support/`：多个根测试共享的辅助代码。

`CMakeLists.txt` 只负责引入注册模块：

- `cmake/reflection_compile_tests.cmake`
- `cmake/actor_fixtures.cmake`
- `cmake/unit_tests.cmake`
- `cmake/python_tests.cmake`
- `cmake/integration_tests.cmake`

## 测试层级

先按根 README 准备 v2 workspace。验证使用至少 6 个并行 worker；低负载时使用
全部可用 CPU 核心。快速根测试，不执行 compile/package 门禁：

```bash
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel "$(nproc)"
ctest --preset actor-fast --parallel "$(nproc)"
```

完整根测试，包括反射编译、Python package、CLI 与 installed-SDK：

```bash
ctest --preset actor-full --parallel "$(nproc)"
```

标签仍可用于进一步缩小范围：

```bash
ctest --preset actor-dev --parallel "$(nproc)" -L actor-runtime
ctest --preset actor-dev --parallel "$(nproc)" -L network
```

## 确定性 WebSocket 测试

WebSocket FIFO、bounded backpressure、write failure、shutdown、OneBot echo
response/timeout race 使用手动 write gate 与 deadline 驱动，并默认进入 fast/full
门禁。测试不得用固定 `sleep_for` 或真实响应时长证明正确性；`wait_for` 只可作为
发现 deadlock 的有界 watchdog。

Python 测试也可以直接运行，例如：

```bash
ctest --test-dir build --parallel 20 --output-on-failure -R '^package_.*_test$'
```

新增测试时，将源码放入对应功能目录。C++ 测试在 `cmake/unit_tests.cmake`
中使用 `obcx_add_gtest(功能目录/名称.cpp "标签")` 注册；Python 测试在
`cmake/python_tests.cmake` 中使用
`obcx_add_python_unittest(功能目录/名称.py "标签")` 注册。target/CTest 名称
由文件名推导，不包含目录；迁移目录不改变测试名称与标签。

Python 产生的 `__pycache__`、本地 bot 环境和 build outputs 必须保持 ignored，
不属于测试源码。
