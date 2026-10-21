# 接手文档 — minarch-gl

> 给"新开一个 DSH 会话、workspace 指向本仓库"的人（或 agent）。
> 一次性初始化 prompt 见 `docs/INIT-PROMPT.md`，两者配合使用。

## 一句话

minarch-gl 是 **NextUI（Anbernic H700）的下游前端覆盖层**：补上 libretro
hw-render（GLES），让 flycast（DC/NAOMI）、mupen64plus-next（N64）、
yabasanshiro（Saturn）能在 NextUI 的菜单 / 着色器 / 存档体系里跑。
上游明确不做这件事（issue #627 open、PAKS.md 写死不实现 OpenGL libretro API）。

构建模型：把本仓库的 `workspace/`（**原仓库的切面**，路径与 NextUI 完全一致）
覆盖到 `upstream.lock` 固定 sha 的 NextUI 树上 → 用钉死 digest 的 docker 工具链
编译 → 产出 `dist/minarch-gl.elf`；再连同核心打成**自带前端的 pak**（不碰
`.system/bin/minarch.elf`）。

## 当前状态（2026-10-21 bootstrap）

- ✅ 仓库就绪：2 个提交，53 个跟踪文件，2.2 MB（不含 .cache/build）
- ✅ 前端在 base `5291344b`（NextUI main, 2026-10-20）上构建通过：
  `dist/minarch-gl.elf`，md5 `51d0fc1799b595b166c69b1eda8f6e32`，667 KB，aarch64
- ✅ 覆盖层已和 base 做过一次真实三方合并（8 个文件，含 `api.h` 的
  `ASSET_TROPHY` 等上游新增；`generic_video.c` 唯一冲突=上游新增的
  `PLAT_captureScreenshot`，无调用者，已丢弃）
- ⬜ 3 个核心 + 4 个 pak 还没实际构建（只验证过 `make -n core`）
- ⬜ 没上过设备；没建 GitHub 远程；没发 release
- ⬜ NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目还在（见"待决"）

## 目录

```
workspace/                     # 原仓库切面（只有这里会被覆盖进 base 树）
├── all/minarch-gl/            # 前端覆盖层（27 文件：12 改写上游 + 13 新增 + makefile/AUDIO-STATUS）
└── h700/cores/makefile        # 我们自己的核心清单（flycast/mupen64plus_next/yabasanshiro）
paks/{DC,NAOMI,N64,SATURN}/    # pak 模板：launch.sh + pak.json + default.cfg
tools/base.sh                  # 取 base（tarball，缓存 .cache/base/<sha>）
tools/build.sh                 # 组装 + 构建前端 / 核心 / pak
tools/diff.sh                  # 生成 12 个改写文件的 unified diff（评审用）
tools/sync.py                  # check / merge / pin（上游三方合并）
overlay.map                    # 12 个"改写上游"文件 → 上游路径（合并的唯一依据）
upstream.lock                  # base sha + 工具链 digest + yabause pin
docs/UPSTREAM-SYNC.md          # 上游同步策略与坑
AGENTS.md                      # 维护记忆（gitignore，不追踪）
```

## 命令

```sh
make build        # 只编前端            -> dist/minarch-gl.elf
make cores        # 前端 + 3 核心 + 4 pak zip -> dist/*.pak.zip
make check        # 上游漂移报告（只读，应显示 no changes）
make diff         # dist/patches/*.patch（评审/发布用，不手工编辑）
make clean        # 删 build/ dist/
make distclean    # 再删 .cache/（下次重新下载 base）
```

依赖：`docker`、`git`、`curl`、`zip`。工具链镜像已按 digest 钉死；
第一次 `make build` 会下载 ~40 MB base tarball。

## 第一次会话建议顺序（SOP）

1. 读 `README.md`、`AGENTS.md`、`docs/UPSTREAM-SYNC.md`（10 分钟）。
2. `make build`，核对 md5 是否为 `51d0fc…`。不一致先查 base sha 与工具链 digest
   （`upstream.lock`），不要先怀疑代码。
3. `make check`，期望 "no changes to overridden files"。
4. `make cores`（耗时：克隆 flycast/mupen/yabause 源码 + 编译；需要网络和 ~3 GB 空间），
   记录三个 `.so` 与四个 pak zip 的尺寸/md5。
5. 设备冒烟（见下节），四个 pak 各启动一次，对照基线：Daytona ~35 fps、
   SM64 ~40 fps、SATURN 启动 ~57 fps；无崩溃、无花屏、MENU 可回。
6. 把结果写回本文件 / commit message；然后建 GitHub 仓库、打 tag、发 release
   （**推送前先问用户**）。

## 设备冒烟

设备 `root@192.168.50.233`，helper 在当前 NextUI 工作区（指向 yaba 项目）：

```sh
H=/home/zmq/projects/NextUI/workspace/tmp
# 0) 关机守卫必须存在，否则任何自动化都可能把设备关掉
python3 $H/devssh.py 'grep -q NEXTUI-AUTOMATION-GUARD /mnt/sdcard/.system/h700/bin/poweroff_next && echo GUARD-OK'
# 1) 上传 pak（示例：SATURN）
python3 $H/devcp.py dist/SATURN.pak.zip /mnt/sdcard/Emus/h700/SATURN.pak.zip
python3 $H/devssh.py 'cd /mnt/sdcard/Emus/h700 && rm -rf SATURN.pak.bak && mv SATURN.pak SATURN.pak.bak 2>/dev/null; unzip -qo SATURN.pak.zip && rm SATURN.pak.zip && ls SATURN.pak'
# 2) 菜单里启动游戏；日志：/mnt/sdcard/.userdata/h700/logs/SATURN.txt
# 3) 结束进程只允许 kill -9（SIGTERM/正常退出会触发关机）
```

SD 卡是 vfat（大小写不敏感）：目录改名要两步（`mv X __t && mv __t Y`），
`rm -rf` 后同名的不同大小写目录可能"仍存在"。

## 纪律（硬规则）

- **永远不要 `git merge` 上游**：我们的文件和上游路径不同，merge 永远无冲突，
  上游修复会静默丢失。同步只走 `tools/sync.py check/merge`（三方合并）。
- **不要动 `.system`**：pak 自带前端与核心；发行物只进 `Emus/h700/<TAG>.pak`。
- 新增"改写上游"的文件必须登记进 `overlay.map`；新增普通文件不需要。
- 上游增删 `.c` 时同步更新 `workspace/all/minarch-gl/makefile` 的 `UPSTREAM_SOURCE`
  （它硬编码了 SOURCE 列表，这是最容易被忽略的断点）。
- `upstream.lock` 的 `YABAUSE_SHA` 与 `workspace/h700/cores/makefile` 的
  `yabasanshiro_HASH` 必须一致（`sync.py pin` 会告警）。
- 提交/推送要用户明示；ST-V 话题不主动提（用户 2026-09-19 定下的规则）。

## 已知坑（都已踩过）

1. **容器挂载点**：NextUI 的工具链习惯把 `$(pwd)/workspace` 挂到 `/root/workspace`；
   本仓库的 `build.sh` 挂到 `/workspace`（因为以 root 运行时 `/root` 可写、但
   non-root 不可进入）。改挂载点时要同步改 `cd` 路径。
2. **容器以 root 写产物**：`build/tree` 里会有 root 所有的文件，宿主机删不掉；
   `build.sh` 在组装前用容器兜底 `rm -rf`。不要用 `sudo`，也不要手工 chown。
3. **hash.txt**：`workspace/hash.txt` 由 `build.sh` 写入（minarch 的 makefile 读它
   做版本字符串），不进 git。
4. **mupen64plus-next**：上游树有 gitlink 但没有完整 `.gitmodules`，模板的
   recursive submodule 会失败 ⇒ 必须预克隆（`build.sh` 已做）。
5. **yabasanshiro**：必须 `LDFLAGS=-ldl`（共享上下文用 dlopen/dlsym，glibc 2.28
   sysroot 里 libdl 是独立库）；平台档 `platform=arm64`；异步宏
   `-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT`。
6. **上游大改的风险**：C++20 迁移 PR #793（未合并，落地后覆盖层要移植）；
   shader sets #796、frame-time callback #848（与我们的呈现/pacing 重叠）；
   若上游实现了 #627，`ma_gl.c`/`ma_core.c` 的一部分会变冗余，届时重新评估拆分。

## 基线

| 项目 | 值 |
|---|---|
| base | `LoveRetro/NextUI@5291344b`（2026-10-20） |
| 工具链 | `ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f…` |
| 前端 elf | `51d0fc1799b595b166c69b1eda8f6e32`（2026-10-21 bootstrap 构建，未上机） |
| 设备 | RGSP（720×480，Mali-G31），旧树在役前端的行为基线：Daytona ~35 fps / SM64 ~40 fps |

## 待决

1. NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目是否删除
   （建议先打 tag 存档，再删，避免双事实源）。
2. 是否/何时向上游提 issue #627 的最小实现（先讨论、小步、带真机数据）。
3. 是否把 pak 提交到 Pak Store（`DC`/`N64` 与现有 standalone 包同名，二选一；
   `SATURN` 已避开 `SS`）。
