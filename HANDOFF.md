# 接手文档 — minarch-gl

> 给"新开一个 DSH 会话、workspace 指向本仓库"的人（或 agent）。
> 一次性初始化 prompt 见 `docs/INIT-PROMPT.md`，两者配合使用。

## 一句话

minarch-gl 是 **NextUI（Anbernic H700）的下游前端覆盖层**：补上 libretro
hw-render（GLES），让 flycast（DC/Naomi）、mupen64plus-next（N64）、
yabasanshiro（Saturn/ST-V）能在 NextUI 的菜单 / 着色器 / 存档体系里跑。
上游明确不做这件事（issue #627 open、PAKS.md 写死不实现 OpenGL libretro API）。

构建模型：把本仓库的 `workspace/`（**原仓库的切面**，路径与 NextUI 完全一致）
覆盖到 `upstream.lock` 固定 sha 的 NextUI 树上 → 用钉死 digest 的 docker 工具链
编译 → 产出 `dist/minarch-gl.elf`；再连同三个核心和切换器打成一个 **Tools pak**
`dist/Minarch.GL.pak.zip`。

发行形态（2026-10-21 重构）：**只发一个 pak**。它是 TOOL pak，安装本身不改任何东西；
用户在 UI 里按 **Enable minarch-gl** 才会写 `.system/bin/minarch.elf`（换成转发脚本）
并把三个核心 pak 装进 `Emus/h700/`。核心 pak 不再自带前端，全系统共用当前前端。

## 当前状态（2026-10-21）

- ✅ 仓库就绪，前端在 base `5291344b`（NextUI main, 2026-10-20）上构建通过：
  `dist/minarch-gl.elf`，md5 `51d0fc1799b595b166c69b1eda8f6e32`，667248 B，aarch64
- ✅ **md5 可复现**：2026-10-21 在无 `.cache/`、`build/` 的干净状态下从零
  `make build`（重新下载 base tarball）得到同一 md5；`make check` → "no changes to overridden files"
- ✅ 覆盖层已和 base 做过一次真实三方合并（8 个文件，含 `api.h` 的 `ASSET_TROPHY`；
  `generic_video.c` 唯一冲突=上游新增的 `PLAT_captureScreenshot`，无调用者，已丢弃）
- ✅ 三个核心首次实际构建并稳定复现（md5 见"基线"）
- ✅ **发行形态改为单 Tools pak**（2026-10-21 下午，用户定）：
  `Minarch GL.pak` = 切换器 UI + `minarch-gl.elf` + 三个核心载荷；
  核心 tag 用核心名避开商店同名包（`LIBRETRO_FLYCAST` / `LIBRETRO_MUPEN64PLUS` /
  `LIBRETRO_YABASANSHIRO`），DC/Naomi 共用 FLYCAST tag，Saturn/ST-V 共用 YABASANSHIRO tag
- ✅ 切换器文件逻辑有主机测试（`make test`，`switch-core.hpp`，无需设备）：
  覆盖 安装 / 幂等重装 / 恢复 / 再启用 / NextUI 更新换代 / 外来同名 pak 不删
- ⬜ **设备冒烟未做**：2026-10-21 12:40 起设备被其他任务占用，14:00 起**设备冻结**
  （yabause 大规模重构）。解冻等用户发话；冒烟前先恢复关机守卫（见"设备冒烟"第 0 步，
  当时 `poweroff_next` 是真实 ELF）
- ⬜ 没发 release（远程 `zhangmq/minarch-gl` 已建，private 空仓库；清单见文末）
- ✅ `LICENSE` 已换成上游 PolyForm Noncommercial 1.0.0（与 base 逐字节一致）
- ⬜ NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目还在（见"待决" 1）

## 目录

```
workspace/                     # 原仓库切面（只有这里会被覆盖进 base 树）
├── all/minarch-gl/            # 前端覆盖层（27 文件：12 改写上游 + 13 新增 + makefile/AUDIO-STATUS）
├── all/minarch-switch/        # 切换器工具（UI + switch-core.hpp + tests/）
└── h700/cores/makefile        # 核心清单（flycast/mupen64plus_next/yabasanshiro）
paks/MinarchGL/                # 唯一发行的 pak 模板（launch.sh + pak.json）
paks/LIBRETRO_*/               # 三个核心 pak 模板（launch.sh + default.cfg + 标记）
tools/base.sh                  # 取 base（tarball，缓存 .cache/base/<sha>）
tools/build.sh                 # 组装 + 构建前端/切换器/核心 + 打 pak
tools/test.sh                  # 切换器文件逻辑的主机测试
tools/diff.sh                  # 生成 12 个改写文件的 unified diff（评审用）
tools/sync.py                  # check / merge / pin（上游三方合并）
overlay.map                    # 12 个"改写上游"文件 → 上游路径（合并的唯一依据）
upstream.lock                  # base sha + 工具链 digest + yabause pin
docs/UPSTREAM-SYNC.md          # 上游同步策略与坑
docs/SWITCHER.md               # 切换器机制与手工回滚
docs/RELEASE-NOTES-v0.1.0.md   # 首发发布说明草稿
AGENTS.md                      # 维护记忆（gitignore，不追踪）
```

## 命令

```sh
make build        # 只编前端                  -> dist/minarch-gl.elf
make cores        # 前端 + 切换器 + 3 核心     -> dist/Minarch.GL.pak.zip
make test         # 切换器文件逻辑的主机测试（host g++，不需要 docker/设备）
make check        # 上游漂移报告（只读，应显示 no changes）
make diff         # dist/patches/*.patch（评审/发布用，不手工编辑）
make clean        # 删 build/ dist/
make distclean    # 再删 .cache/（下次重新下载 base）
```

依赖：`docker`、`git`、`curl`、`g++`（仅 `make test`），以及 `zip` **或** `bsdtar`
（打包 pak 用，宿主机没 zip 时自动用 bsdtar；脚本权限位由 build.sh 强制 0755）。
第一次 `make build` 会下载 ~40 MB base tarball。

## 第一次会话建议顺序（SOP）

1. 读 `README.md`、`AGENTS.md`、`docs/UPSTREAM-SYNC.md`、`docs/SWITCHER.md`。
2. `make build`，核对 md5 是否为 `51d0fc…`。不一致先查 base sha 与工具链 digest
   （`upstream.lock`），不要先怀疑代码。
3. `make check`，期望 "no changes to overridden files"。
4. `make test`，期望 `switch-core: all checks passed`。
5. `make cores`（首次要克隆 flycast/mupen/yabause 源码；需要网络和 ~3 GB 空间；
   实测约 5 分钟），记录 pak zip 与三个 `.so` 的尺寸/md5（见"基线"）。
6. 设备冒烟（见下节）：装 pak → Tools → Minarch GL → Enable → 三个系统各启动一次，
   对照基线 Daytona ~35 fps、SM64 ~40 fps、SATURN 启动 ~57 fps；再 Restore 一次确认回滚。
7. 把结果写回本文件 / commit message；推送与发布要用户明确指令（**先问**）。

## 切换器（发行核心）

细节见 `docs/SWITCHER.md`，这里只记要点：

- Enable 后：`minarch.elf` = 转发脚本（标记 `MINARCH-GL-DISPATCH`）→ `minarch-gl.elf`，
  原版存为 `minarch-stock.elf`；脚本在 GL 二进制缺失时回退到原版（不会把设备搞成没前端）。
- 核心 pak 在 `Emus/h700/`，带 `minarch-gl-only` 标记；恢复原版时改名
  `<TAG>.pak.disabled`（NextUI 的 `hide()` 认这个后缀），系统项从菜单消失。
- NextUI 更新会重建 `.system`：切换器检测到"状态 gl 但没有转发脚本"→ 显示
  Reinstall；同时 `boot.d/minarch-gl-guard.sync.sh` 会在启动早期把 GL-only pak 重新藏起来。
- 状态文件：`.userdata/h700/minarch-gl/state`（`gl` / `stock`）。
- CLI（调试/自动化）：`./minarch-switch.elf --status|--install|--restore`。

## 设备冒烟

设备 `root@192.168.50.233`，helper 在当前 NextUI 工作区（指向 yaba 项目）。
**当前设备冻结，以下流程解冻后才可执行。**

```sh
H=/home/zmq/projects/NextUI/workspace/tmp
# 0) 关机守卫必须存在，否则任何自动化都可能把设备关掉
python3 $H/devssh.py 'grep -q NEXTUI-AUTOMATION-GUARD /mnt/sdcard/.system/h700/bin/poweroff_next && echo GUARD-OK'
# 1) 装工具 pak（Tools 目录）
python3 $H/devcp.py dist/Minarch.GL.pak.zip /mnt/sdcard/Tools/h700/minarch-gl.pak.zip
python3 $H/devssh.py 'cd /mnt/sdcard/Tools/h700 && unzip -qo minarch-gl.pak.zip && rm minarch-gl.pak.zip && ls "Minarch GL.pak"'
# 2) 菜单里 Tools → Minarch GL → Enable；观察生成的核心 pak
python3 $H/devssh.py 'ls -d /mnt/sdcard/Emus/h700/LIBRETRO_*.pak*; head -3 /mnt/sdcard/.system/h700/bin/minarch.elf'
# 3) 各启动一次；日志：/mnt/sdcard/.userdata/h700/logs/LIBRETRO_<TAG>.txt
# 4) 结束进程只允许 kill -9（SIGTERM/正常退出会触发关机）
```

**2026-10-21 实测：守卫未生效。** `$SYSTEM_PATH/bin/poweroff_next` 是真实 ELF
（211592 B，md5 `5aab1804…`，与 `poweroff_next.real-bak` 相同，Oct 20 21:20 系统更新
恢复的），`poweroff_next.guard-stub` / `poweroff.guard-stub` 还在目录里。自动化前必须恢复：

```sh
python3 $H/devssh.py 'B=/mnt/sdcard/.system/h700/bin; cp $B/poweroff_next.guard-stub $B/poweroff_next; cp $B/poweroff.guard-stub $B/poweroff; grep -c NEXTUI-AUTOMATION-GUARD $B/poweroff_next'
```

SD 卡是 vfat（大小写不敏感）：目录改名要两步（`mv X __t && mv __t Y`），
`rm -rf` 后同名的不同大小写目录可能"仍存在"。

## 纪律（硬规则）

- **永远不要 `git merge` 上游**：我们的文件和上游路径不同，merge 永远无冲突，
  上游修复会静默丢失。同步只走 `tools/sync.py check/merge`（三方合并）。
- **`.system` 只允许切换器在用户点 Enable/Restore 时写**；pak 安装本身不得改动它。
  手工调试后要恢复（见 `docs/SWITCHER.md` 的"Undo by hand"）。
- 新增"改写上游"的文件必须登记进 `overlay.map`；新增普通文件不需要。
- 上游增删 `.c` 时同步更新 `workspace/all/minarch-gl/makefile` 的 `UPSTREAM_SOURCE`
  （它硬编码了 SOURCE 列表，这是最容易被忽略的断点）。
- 切换器依赖上游 `workspace/all/settings/menu.cpp`（菜单框架）；上游若移动它，
  构建会直接失败而不是悄悄变丑，按 `docs/SWITCHER.md` 更新 `MENU_DIR`。
- `upstream.lock` 的 `YABAUSE_SHA` 与 `workspace/h700/cores/makefile` 的
  `yabasanshiro_HASH` 必须一致（`sync.py pin` 会告警）。
- 提交/推送要用户明示。
- ST-V 已由用户确认纳入（2026-10-21），此前"不主动提"的规则作废；
  但我们 pin 的核心（`9b36fd6e` = `stv-fix-13` 合并后）是否覆盖全部 ST-V 行为，
  要按实测说话，不要替核心打包票。

## 已知坑（都已踩过）

1. **容器挂载点**：NextUI 的工具链习惯把 `$(pwd)/workspace` 挂到 `/root/workspace`；
   本仓库的 `build.sh` 挂到 `/workspace`。改挂载点时要同步改 `cd` 路径。
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
7. **核心源码的 git 所有权**（2026-10-21 首次 `make cores` 踩到）：源码在宿主机
   克隆（uid=zmq）、在容器里以 root 构建，核心驱动的 `git submodule update` 报
   `fatal: detected dubious ownership`。修法：`build.sh` 的 `run()` 给容器加
   `GIT_CONFIG_*`（`safe.directory=*`），不改宿主 git config、不 chown。
8. **`*_BUILD_PATH` 是相对 `src/` 的**：模板里固定写成
   `cd src/$$($1_BUILD_PATH)`，默认值是核心名。yabause 的 libretro 端口实际在
   `src/yabasanshiro/yabause/src/libretro`，所以必须写
   `yabasanshiro/yabause/src/libretro`。只 `make -n` 验证看不出来，必须真编。
9. **宿主机没有 `zip`**：README 把它列为依赖，但这台机器只有 `bsdtar`/`7z`。
   `build.sh` 优先 `zip`，缺失时用 `bsdtar -a -cf`（保留 unix 权限位）。
10. **新建文件的执行位**（2026-10-21 踩到）：用编辑工具新建的 `launch.sh` 是 0600，
    打进 zip 后设备上无法启动 pak。`build.sh` 现在对 pak 的 `launch.sh` 与
    `minarch-switch.elf` 显式 `chmod 0755`；新增可执行文件记得进 git 前 `chmod +x`。
11. **vfat 上 `rename()` 是原子的，`mkdir` 不递归**：切换器的嵌套目录
    （`.userdata/<plat>/minarch-gl`、`.hooks/boot.d`）必须 `mkdir -p` 式创建——
    主机测试 `make test` 就是抓这个 bug 用的。

## 基线

**构建输入**

| 项目 | 值 |
|---|---|
| base | `LoveRetro/NextUI@5291344b`（2026-10-20） |
| 工具链 | `ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f…`（git 2.43.0，无 `zip`） |
| 核心 pin | flycast `aa97a6d6` / mupen64plus_next `6752836d` / yabasanshiro `9b36fd6e`（`zhangmq/yabause#h700`，含 ST-V） |

**构建产物（2026-10-21 14:17，`make cores` 全绿）**

| 产物 | 尺寸 (B) | md5 | sha256（前 16） |
|---|---:|---|---|
| `dist/minarch-gl.elf` | 667248 | `51d0fc1799b595b166c69b1eda8f6e32` | `056fdc29b22b0d3b` |
| `dist/Minarch.GL.pak.zip` | 4850962 | `cf905fb96555c8aebd1b06d594940e16` | `a12d843b0b5093ba` |
| `flycast_libretro.so` | 26372000 | `cce42969c347c045f8fc8dae91b1b5db` | — |
| `mupen64plus_next_libretro.so` | 4508304 | `19ef50e5325eb390ada04d6fab941b39` | — |
| `yabasanshiro_libretro.so` | 2777280 | `92cb837ae912ae59a3320144ca82e1ed` | — |
| `minarch-switch.elf` | 190912 | `4f1bc91d5a0cdfd16e3339dfcebb4545` | — |

pak 内部结构：`Minarch GL.pak/{launch.sh(0755), pak.json, minarch-switch.elf, LICENSE,
NOTICE, payload/minarch-gl.elf, payload/LIBRETRO_*.pak/}`；每个核心 pak 含
`launch.sh`(0755) + `default.cfg` + `minarch-gl-only` + 核心 `.so` + `LICENSE` +
`NOTICE` + `LICENSE-<core>`。核心均为 aarch64，依赖 `libGLESv2/libEGL`（设备自带）。
完整 sha256 在 `dist/SHA256SUMS`。

**行为基线（未上机，勿引用为已验证）**

| 项目 | 值 |
|---|---|
| 设备 | RGSP（720×480，Mali-G31） |
| 旧树在役前端 | Daytona ~35 fps / SM64 ~40 fps（2026-09 实测，**不是**本仓库产物） |
| 本仓库 pak | **未冒烟**（设备冻结）；首次冒烟要记 fps、`logs/LIBRETRO_<TAG>.txt`、Enable/Restore 往返 |

## 待决

1. NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目是否删除
   （建议先打 tag 存档，再删，避免双事实源）。
2. 是否/何时向上游提 issue #627 的最小实现（先讨论、小步、带真机数据）。
3. 是否把 pak 提交到 Pak Store。现状：单 TOOL pak（`Minarch GL`），
   自己装核心——需要确认 store 对"工具 pak 写 `.system`"的政策态度。
   另外发行 zip 目前是**手动安装形态**（包内含 `Minarch GL.pak/`，解压到 `Tools/h700/`）；
   store 要求 **zip 内容 = pak 目录内容**（解压根），文件名 = `release_filename`，
   且 `pak.json.version` 与 git tag 逐字一致。上架前要另出一份 store 形态的 zip。
4. ~~`LICENSE` 与上游不一致~~ **已解决（2026-10-21）**：换成 base 树那份 PolyForm。
5. ~~pak 内是否附 `LICENSE`/`NOTICE`~~ **已解决**：单 pak 与三个核心 pak 都带。
6. ~~核心 pak 是否与商店同名~~ **已解决**：三个 tag 全部改为核心名
   （`DC`/`SS`/`N64` 分别被商店 standalone 包占用）。

## 发布清单（首发，2026-10-21 准备，**未发布**）

- 远程仓库：`zhangmq/minarch-gl`（已创建，**private 空仓库**，`origin` =
  `git@github.com:zhangmq/minarch-gl.git`；用户定：发布时转 public）
- tag / 版本：`v0.1.0`（`paks/MinarchGL/pak.json` 的 `version` 为 `0.1.0`）
- 发布物（`dist/`，校验和 `dist/SHA256SUMS`）：

  | 文件 | 大小 (B) | sha256 |
  |---|---:|---|
  | `minarch-gl.elf` | 667248 | `056fdc29b22b0d3b4947d294896e9baf583e87fbe9774d659e3979cd78f8a66e` |
  | `Minarch.GL.pak.zip` | 4850962 | `a12d843b0b5093ba53007054972d5c1f1b4ddc407a7fd158da57ca28532814ba` |

- 发布说明草稿：`docs/RELEASE-NOTES-v0.1.0.md`
- 发布前还差：① 设备冒烟（Enable → 三个系统各一次 → Restore）② 用户对推送/发布的明确指令
