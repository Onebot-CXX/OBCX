# TODO

## Telegram long-poll 阻塞插件响应

- **问题**：Telegram HTTP long polling（`getUpdates`，`timeout=30`）在 bot 的 `io_context` 线程里通过同步阻塞请求（`post_sync`）执行。
- **影响**：插件逻辑若依赖 `co_await bot.run_heavy_task(...)`（拿到结果后）再 `co_await bot.send_group_message(...)` 发送，可能会被当前这次 `getUpdates` 的 long-poll 阻塞，直到 `getUpdates` 返回后协程才能继续执行，导致 LLM 已完成但消息迟迟不发（最坏可达 ~30 秒）。
- **证据**：加入 timing log 后观察到：`send_response START` 总是在 `Telegram getUpdates END` 之后立刻出现；一次样例中 `getUpdates` 耗时 `31245ms`，与“空白期”一致。
- **备注**：转发类插件看起来更“即时”，通常是因为它在 `getUpdates` 刚返回“有更新”时立刻发送；而 `/chat` 的回复是异步完成的，容易“完成在 long-poll 等待窗口中间”，从而被卡住。

## Core 变更回退（asio 别名）

- **问题**：为修复编译测试临时在 `include/core/task_scheduler.hpp` 增加了 `namespace asio = boost::asio;`。
- **风险**：容易在 core 中引入非预期的命名别名依赖，后续 refactor 时会误用 `asio::`。
- **TODO**：把 `TaskScheduler` 内部的 `asio::` 全部改回 `boost::asio::`（或在文件内局部 using），然后移除该别名。
