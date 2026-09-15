# 测试目录

`tests/` 只保存 OBCX 根仓库拥有的可重复自动化测试。QQ、LLOneBot、Docker
Compose 和包含凭据的本地配置位于 `dev/onebot/`，不属于测试门禁。

## 所有权边界

根仓库测试可以覆盖：

- actor runtime、scheduler、Asio/BlockingExecutor、reload 与通用 ABI 2；
- `BotInstallation`/`BotComponent`/capability registry、固定 recipe、严格 bot
  configuration、OneBot/Telegram protocol/transport/ingress/operation 组件；
- CLI、数据库、metadata、registry、packaging、安装后 SDK 与通用 fixture actor。

根测试不得包含生产 actor 的私有头文件、实现源码或业务断言，也不得遍历、
构建或校验 `local_actor/` 下的独立仓库。Bot component DAG、recipe、严格配置、
ingress、operation endpoint、通用 ABI、same-SONAME staging、dependency
isolation 和 generation cutover 使用根仓库自有源码与通用 fixture 验证。每个
独立 actor 仓库自行拥有并执行其 standalone build、安装、业务测试和跨 actor
集成测试。

## 目录职责

- `cpp/`：GoogleTest 单元与小型集成测试。
- `python/`：Python `unittest` metadata、packaging 与根仓库模块化约束。
- `cmake/`：由 CTest 调用的 SDK、CLI 与根仓库集成脚本。
- `compile/`：C++ 正向与负向反射编译契约。
- `fixtures/`：根 runtime 专用的通用 actor DSO 与 standalone SDK consumer。
- `support/`：多个根测试共享的辅助代码。

`CMakeLists.txt` 只负责引入注册模块：

- `cmake/reflection_compile_tests.cmake`
- `cmake/actor_fixtures.cmake`
- `cmake/unit_tests.cmake`
- `cmake/python_tests.cmake`
- `cmake/integration_tests.cmake`

## 测试层级

快速根测试，不执行 compile/package 门禁：

```bash
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel
ctest --preset actor-fast
```

完整根测试，包括反射编译、Python package、CLI 与 installed-SDK：

```bash
ctest --preset actor-full
```

标签仍可用于进一步缩小范围：

```bash
ctest --preset actor-dev -L actor-runtime
ctest --preset actor-dev -L network
```

## 确定性 WebSocket 测试

WebSocket FIFO、bounded backpressure、write failure、shutdown、OneBot echo
response/timeout race 使用手动 write gate 与 deadline 驱动，并默认进入 fast/full
门禁。测试不得用固定 `sleep_for` 或真实响应时长证明正确性；`wait_for` 只可作为
发现 deadlock 的有界 watchdog。Beast loopback smoke 使用 listening、connected、message
completion signal，不使用 startup sleep。

Python 测试也可以直接运行，例如：

```bash
python3 -m unittest -v tests/python/actor_metadata_test.py
```

新增 C++ 测试时，使用 `cmake/unit_tests.cmake` 中的 `obcx_add_gtest`
注册 target 与职责标签。Python 产生的 `__pycache__`、本地 bot 环境和 build
outputs 必须保持 ignored，不属于测试源码。
