# Jemalloc 内存诊断构建

`OBCX_ENABLE_JEMALLOC_PROFILING` 是默认 **OFF** 的编译开关（已获显式批准）。
开启时只给应用程序链接 jemalloc，并给应用/core 的实现编译保留 frame pointer；
不向 actor SDK 的链接接口导出 jemalloc，不改变 actor 工厂 ABI。
关闭时不查找或链接 jemalloc，继续使用原来的分配器。

这不是向运行中的进程注入 profiler。必须使用**独立构建目录**，经操作者批准后
用新的应用和匹配 core/SDK 启动诊断进程；不能通过 actor reload 更新 core。
诊断构建替换了进程分配器，会改变碎片、缓存和 RSS，并增加采样开销。
不要据此断言原 glibc 分配器会有相同占用。

## 依赖与构建

需要提供 `jemalloc.pc` 的 jemalloc，且构建了 `--enable-prof` 和统计支持。
原生 CMake configure 会检查这两项；交叉编译无法运行该检查，须由操作者保证。
分析工具为同版本的 `jeprof`，另需 Perl、`nm`、`addr2line`、`objdump`；图形输出
还需要 Graphviz。普通构建不需要安装这些可选依赖。

Linux Nix 开发 shell 已提供 jemalloc 与 `jeprof`，但仍只在开启编译开关时链接。
修改 `flake.nix` 后需要退出旧 shell 再运行 `nix develop`，或使用 direnv 的
`direnv reload` 重新加载；用 `pkg-config --modversion jemalloc` 确认依赖可见。
若出现 `No package 'jemalloc' found`，说明当前环境尚未包含 `jemalloc.pc`。

不重进当前 shell 时，也可显式获取锁定 nixpkgs 中的 jemalloc，并将 prefix 传给 CMake：

```bash
JEMALLOC_PREFIX=$(nix build --inputs-from . nixpkgs#jemalloc --no-link --print-out-paths)
# 非 Nix 环境：将上面的变量设为 profiling-capable jemalloc 的安装 prefix。
PROFILE_BUILD="$PWD/build/jemalloc-profile"
cmake -S . -B "$PROFILE_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER="$(command -v g++)" \
  -DCMAKE_PREFIX_PATH="$JEMALLOC_PREFIX" \
  -DENABLE_DEBUG_TRACE=OFF \
  -DOBCX_ENABLE_IO_URING=ON \
  -DOBCX_BUILD_BENCHMARKS=OFF \
  -DOBCX_ENABLE_JEMALLOC_PROFILING=ON \
  -DOBCX_PACKAGES_WORKSPACE="$PWD/packages.toml" \
  -DOBCX_PACKAGES_CACHE="$PROFILE_BUILD/package-cache" \
  -DOBCX_PACKAGES_STATE_DIR="$PROFILE_BUILD/package-state" \
  -DOBCX_PACKAGES_MODE=development \
  -DOBCX_PACKAGES_NETWORK=allow
JOBS=$(nproc)
[ "$JOBS" -ge 6 ] || JOBS=6
cmake --build "$PROFILE_BUILD" --target obcx --parallel "$JOBS"
```

这里只构建应用和 core，不构建 actor。若需要匹配的 actor，按
[冻结 SDK 的 actor 构建流程](actors/README.md) 准备；不要调用在线 core 工作区的
actor target，不要混用不匹配的 core/SDK/actor。保留未剥离的二进制和调试符号。

## 启动与命令

仅在批准启动诊断进程后执行下面示例。自行指定独立的配置、数据库和连接环境，
不要直接拿生产配置启动第二个 bot。`MALLOC_CONF` 必须在进程启动前设置：

```bash
PROFILE_DIR="$PWD/build/jemalloc-profile/captures"
install -d -m 700 "$PROFILE_DIR"
MALLOC_CONF='abort_conf:true,prof:true,prof_active:true,lg_prof_sample:19,prof_accum:false,prof_final:false,prof_gdump:false,lg_prof_interval:-1' \
  "$PROFILE_BUILD/src/app/obcx" --no-tui /absolute/path/to/diagnostic-config.toml
```

`lg_prof_sample:19` 是本示例**显式选择**的平均约 512 KiB 分配采样间隔，
不是框架新增的配置默认值；减小它会提高采样密度和开销。
`prof_accum:false` 不累计已释放对象的历史分配栈；本流程关注存活堆而非完整时间线。
示例关闭自动快照，只通过下面的本地命令导出。

TUI 命令输入和 `--no-tui` 标准输入均支持：

```text
memory_stats
memory_dump=/absolute/private/directory/obcx-before
# 执行待观察的诊断负载后：
memory_stats
memory_dump=/absolute/private/directory/obcx-after
```

- `memory_stats` 刷新 jemalloc epoch 并报告版本、采样状态及字节统计。
- `memory_dump=...` 的参数是文件名前缀，支持绝对路径和相对路径，例如
  `memory_dump=./captures/obcx`；相对路径基于进程当前工作目录解析，
  不存在的父目录会递归创建（遵循进程 umask，不修改已有目录权限）。
  命令产生唯一的 `prefix.XXXXXX.heap`，权限为 `0600`，并返回完整路径；不覆盖旧快照。
- 开关关闭、采样未启用、路径错误或导出失败均给出明确消息，不退出进程。
  未设置 `prof:true` 时仍可查看统计，但无法事后只靠命令开启启动时未启用的采样。
- 检查 `sampling_active=true`；环境中关闭 `prof_active` 会使新分配停止采样。
  暂停采样时导出只能包含已有样本，不能代表完整的当前负载。
- 启动时和每次统计/导出时检查 malloc 与 mallctl 的符号来自同一个 DSO；
  冲突的分配器 preload 会被报告。不要同时使用其他 malloc 替换器或 sanitizer。
- 导出会同步占用命令处理线程并消耗 CPU/磁盘；避免在延迟敏感时频繁操作。
  使用自己独占、不可被其他用户写入的目录，且不要设置自动 profile 输出到公共目录。

## 数字怎么读

| 字段 | 含义 |
|---|---|
| allocated | 应用当前分配、尚未释放的 jemalloc 字节数；不是采样估算 |
| active | 容纳应用分配的活动页，包含页内空隙 |
| resident | jemalloc 认为驻留的内存，含其元数据和未回收脏页等；不等于内核 RSS |
| mapped | jemalloc 当前映射的内存范围；虚拟空间不等于物理内存 |
| retained | 保留待复用的虚拟映射；不是额外驻留内存，不要与 resident 相加 |
| metadata | 分配器元数据占用，不要重复加到 resident |

统计覆盖**全进程的 jemalloc 分配**，包括 core、actor、第三方库及 profiler 自身，
线程缓存可能使统计滞后。直接 `mmap`、线程栈、文件映射和其他自带分配器不都计入；
同时保留 `/proc/PID/smaps` 与 `smaps_rollup` 解释进程 RSS。
不要把“总 RSS 减去 actor 的 `.so` 映射”视为 core 内存。

## 分析 core / actor 分配栈

```bash
BIN="$PROFILE_BUILD/src/app/obcx"
BEFORE=/absolute/private/directory/obcx-before.ACTUAL.heap
AFTER=/absolute/private/directory/obcx-after.ACTUAL.heap
"$JEMALLOC_PREFIX/bin/jeprof" --text --show_bytes --cum "$BIN" "$AFTER"
"$JEMALLOC_PREFIX/bin/jeprof" --text --show_bytes --cum --base="$BEFORE" "$BIN" "$AFTER"
# 按实际符号或源文件名筛选，而非凭共享 malloc 帧判断归属：
"$JEMALLOC_PREFIX/bin/jeprof" --text --lines --focus='obcx::core::' "$BIN" "$AFTER"
"$JEMALLOC_PREFIX/bin/jeprof" --collapsed "$BIN" "$AFTER" > "$PROFILE_DIR/after.stacks"
# 在 actor reload / 进程退出前保存已解析符号的文本报告：
"$JEMALLOC_PREFIX/bin/jeprof" --text --lines --cum "$BIN" "$AFTER" > "$PROFILE_DIR/after.txt"
```

profile 包含映射地址信息，但 actor generation 的临时 DSO 会在 retirement 后删除。
务必在 reload/退出之前保存文本报告和 collapsed stacks，并保留 core、应用、actor
及私有依赖的准确副本；仅保留 `.heap` 无法保证未来还能解析全部符号。
当前验证的 jemalloc 5.4.0 `jeprof --raw` 输出不能被自身完整读回，故不推荐该归档方式。
缺少符号会影响分类。
调用栈中的模板/标准库共享帧也可能无法唯一归属。

先观察分配叶节点和完整调用链：actor 调用 core 分配内存的同一个样本，同时包含两者。
可约定“调用链包含 actor 的算 actor 驱动，剩余包含 core 的算 core 驱动”，但不能直接
把多个 `--focus` 报告相加，它们可能重叠。累计列同样不能跨帧求和。
快照调用栈为采样估算，不能证明严格的模块所有权，也不是泄漏判定本身。
诊断资料包含路径、符号、地址和负载规模，应按敏感运行资料处理。

## 本次离线验证

在 Linux x86_64 / GCC 16.2.0 / jemalloc 5.4.0 下，使用 20 个编译 worker：

- ON、默认 OFF 两种隔离应用构建成功；OFF 应用无 jemalloc 动态依赖，
  ON 构建的 core DSO 也不直接链接 jemalloc。
- 通过只初始化本地 CLI 的手动诊断程序，持有 32 MiB 缓冲区：allocated 从
  309,696 升至 33,893,248 字节，释放后回落到 338,368 字节；
  `jeprof --base` 解析出 33,554,432 字节增量及 `hold_diagnostic_buffer` 调用栈。
- 多次相同前缀导出得到不同文件，均为 `0600`；关闭采样、关闭构建开关以及
  冲突 allocator preload 均得到明确错误/状态消息。
  后续已支持相对路径并自动递归创建父目录；路径按进程工作目录解析并返回完整路径，
  无法创建目录或文件时返回错误。
- 应用 `--help` 和缺失配置的启动诊断通过，无 bot 启动；未运行生产负载、
  未交互验证 TUI、未新增单元测试，也未由本次操作发起在线进程重启。
- 原始日志与手动诊断程序位于本地 `build/jemalloc-profile/diagnostics/`；
  上述数字只验证诊断链路，不是当前生产 core 的内存测量结果。
