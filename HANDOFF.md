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

## 当前状态（2026-10-21 bootstrap 构建日）

- ✅ 仓库就绪：2 个提交，53 个跟踪文件，2.2 MB（不含 .cache/build）
- ✅ 前端在 base `5291344b`（NextUI main, 2026-10-20）上构建通过：
  `dist/minarch-gl.elf`，md5 `51d0fc1799b595b166c69b1eda8f6e32`，667248 B，aarch64
- ✅ **md5 可复现**：2026-10-21 在无 `.cache/`、`build/` 的干净状态下从零
  `make build`（重新下载 base tarball）得到同一 md5；`make check` → "no changes to overridden files"
- ✅ 覆盖层已和 base 做过一次真实三方合并（8 个文件，含 `api.h` 的
  `ASSET_TROPHY` 等上游新增；`generic_video.c` 唯一冲突=上游新增的
  `PLAT_captureScreenshot`，无调用者，已丢弃）
- ✅ 3 个核心 + 4 个 pak 已**首次实际构建**（2026-10-21 12:51，`make cores` 全绿）：
  产物尺寸/md5 见"基线"表；构建期踩到 3 个坑，已修并记入"已知坑" 7–9
- ⬜ **设备冒烟未做**：2026-10-21 12:40 用户要求暂停设备操作（RGSP 上另有开发任务）。
  冒烟前必须先恢复关机守卫（见"设备冒烟"第 0 步，当时 `poweroff_next` 是真实 ELF）
- ⬜ 没建 GitHub 远程；没发 release（产物与 `SHA256SUMS` 在 `dist/`，说明草稿
  `docs/RELEASE-NOTES-v0.1.0.md`，见"发布清单"）
- ✅ **`LICENSE` 已换成上游 PolyForm Noncommercial 1.0.0**（2026-10-21 用户拍板；
  与 base 树逐字节一致），4 个 pak 现在也带 `LICENSE`/`NOTICE`/`LICENSE-<core>`
- ⬜ NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目还在（见"待决" 1）

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
docs/RELEASE-NOTES-v0.1.0.md   # 首发发布说明草稿
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

依赖：`docker`、`git`、`curl`，以及 `zip` **或** `bsdtar`（打包 pak 用，宿主机没 zip
时自动用 bsdtar，权限位保留）。工具链镜像已按 digest 钉死；
第一次 `make build` 会下载 ~40 MB base tarball。

## 第一次会话建议顺序（SOP）

1. 读 `README.md`、`AGENTS.md`、`docs/UPSTREAM-SYNC.md`（10 分钟）。
2. `make build`，核对 md5 是否为 `51d0fc…`。不一致先查 base sha 与工具链 digest
   （`upstream.lock`），不要先怀疑代码。
3. `make check`，期望 "no changes to overridden files"。
4. `make cores`（耗时：克隆 flycast/mupen/yabause 源码 + 编译；需要网络和 ~3 GB 空间），
   记录三个 `.so` 与四个 pak zip 的尺寸/md5（首次实测约 5 分钟，见"基线"）。
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
7. **核心源码的 git 所有权**（2026-10-21 首次 `make cores` 踩到）：源码在宿主机
   克隆（uid=zmq）、在容器里以 root 构建，核心驱动的 `git submodule update` 报
   `fatal: detected dubious ownership`。修法：`build.sh` 的 `run()` 给容器加
   `GIT_CONFIG_*`（`safe.directory=*`），不改宿主 git config、不 chown。
8. **`*_BUILD_PATH` 是相对 `src/` 的**：模板里固定写成
   `cd src/$$($1_BUILD_PATH)`，默认值是核心名。yabause 的 libretro 端口实际在
   `src/yabasanshiro/yabause/src/libretro`，所以必须写
   `yabasanshiro/yabause/src/libretro`（原来少了核心目录那层，`cd` 失败）。
   只 `make -n` 验证看不出来，必须真编。
9. **宿主机没有 `zip`**：README 把它列为依赖，但这台机器只有 `bsdtar`/`7z`。
   `build.sh` 现在优先 `zip`，缺失时用 `bsdtar -a -cf`（保留 unix 权限位，
   `launch.sh` 解压后仍是 0755——已解开四个 pak 核对过）。

## 基线

**构建输入**

| 项目 | 值 |
|---|---|
| base | `LoveRetro/NextUI@5291344b`（2026-10-20） |
| 工具链 | `ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f…`（git 2.43.0，无 `zip`） |
| 核心 pin | flycast `aa97a6d6` / mupen64plus_next `6752836d` / yabasanshiro `9b36fd6e`（`zhangmq/yabause#h700`） |

**构建产物（2026-10-21 12:51，`make cores` 全绿；md5 由宿主机 `md5sum` 记录）**

| 产物 | 尺寸 (B) | md5 | sha256（前 16） |
|---|---:|---|---|
| `dist/minarch-gl.elf` | 667248 | `51d0fc1799b595b166c69b1eda8f6e32` | `056fdc29b22b0d3b` |
| `flycast_libretro.so` | 26372000 | `cce42969c347c045f8fc8dae91b1b5db` | — |
| `mupen64plus_next_libretro.so` | 4508304 | `19ef50e5325eb390ada04d6fab941b39` | — |
| `yabasanshiro_libretro.so` | 2777280 | `92cb837ae912ae59a3320144ca82e1ed` | — |
| `dist/DC.pak.zip` | 2258640 | `cbfb3c1cb166c95a72d2b43da4f13094` | `7f427926cdfa80dd` |
| `dist/NAOMI.pak.zip` | 2258700 | `562dee32c56a616b5e763e382632f856` | `587fcb510c4b5852` |
| `dist/N64.pak.zip` | 2008615 | `8c0d90ef0344f524634f43da16a7e8bc` | `3e8b7608beef03a5` |
| `dist/SATURN.pak.zip` | 1159295 | `e711bbcc33efb4f16b737fa9778adcf7` | `87c05a673bec10b5` |

四个 pak 解包后都含 `launch.sh`(0755) + `pak.json` + `default.cfg` +
`minarch-gl.elf`（md5 与 dist 一致）+ 对应核心 `.so` + `LICENSE`（PolyForm）+
`NOTICE` + `LICENSE-<core>`（该核心的 GPLv2 全文）；核心均为 aarch64，
依赖 `libGLESv2/libEGL`（设备自带）。完整 sha256 在 `dist/SHA256SUMS`。

**行为基线（未上机，勿引用为已验证）**

| 项目 | 值 |
|---|---|
| 设备 | RGSP（720×480，Mali-G31） |
| 旧树在役前端 | Daytona ~35 fps / SM64 ~40 fps（2026-09 实测，**不是**本仓库产物） |
| 本仓库 4 个 pak | **未冒烟**（2026-10-21 设备被其他任务占用）；首次冒烟要同时记 fps、`logs/<TAG>.txt` 与异常 |

## 待决

1. NextUI fork 里旧的 `workspace/all/minarch-gl/` 和自建核心条目是否删除
   （建议先打 tag 存档，再删，避免双事实源）。
2. 是否/何时向上游提 issue #627 的最小实现（先讨论、小步、带真机数据）。
3. 是否把 pak 提交到 Pak Store（`DC`/`N64` 与现有 standalone 包同名，二选一；
   `SATURN` 已避开 `SS`）。
4. ~~`LICENSE` 与上游不一致~~ **已解决（2026-10-21）**：`LICENSE` 换成 base 树那份
   PolyForm Noncommercial 1.0.0（逐字节相同）。三个核心（flycast /
   mupen64plus-next / yabasanshiro）都是 GPLv2，以独立 `.so` 打包，`NOTICE` 与
   发布说明都附了 pin 对应的源码链接。
5. ~~pak 内是否附 `LICENSE`/`NOTICE`~~ **已解决（2026-10-21）**：`build.sh` 组装时
   拷入 `LICENSE`、`NOTICE`、`LICENSE-<core>`（核心的 GPLv2 全文）；四个 zip 的
   md5 已重记入"基线"。

## 发布清单（首发，2026-10-21 准备，**未发布**）

- 远程仓库：`zhangmq/minarch-gl`（已创建，**private 空仓库**，`origin` 指向
  `git@github.com:zhangmq/minarch-gl.git`；用户定：发布时转 public）
- tag / 版本：`v0.1.0`（四个 `pak.json` 的 `version` 已是 `0.1.0`）
- 发布物（`dist/`，校验和 `dist/SHA256SUMS`）：

  | 文件 | 大小 (B) | sha256 |
  |---|---:|---|
  | `minarch-gl.elf` | 667248 | `056fdc29b22b0d3b4947d294896e9baf583e87fbe9774d659e3979cd78f8a66e` |
  | `DC.pak.zip` | 2258640 | `7f427926cdfa80dd8d5f64fa14b80c3f5b58b7a7f926158ad897b6f89ebacc9f` |
  | `NAOMI.pak.zip` | 2258700 | `587fcb510c4b5852ce408be87636d9f62296041b7b8ccb28d86184a9b43157da` |
  | `N64.pak.zip` | 2008615 | `3e8b7608beef03a51f080b6d0aeaf642e61bc4a11ad35ddca3710fea45abec1c` |
  | `SATURN.pak.zip` | 1159295 | `87c05a673bec10b501b7a04f21216c0ec18909a638650500142bcb24f16d7988` |

- 发布说明草稿：`docs/RELEASE-NOTES-v0.1.0.md`（含产物表、构建来源、许可证与源码链接）
- 发布前还差：① 设备冒烟（4 个 pak）② 用户对推送/发布的明确指令
