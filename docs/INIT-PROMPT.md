# 初始化 prompt（新会话用）

新建 DSH 会话，**workspace 设为 `/home/zmq/projects/minarch-gl`**，然后把下面整段发过去。

---

你在 `/home/zmq/projects/minarch-gl` 工作。这是 **minarch-gl**：NextUI（Anbernic H700）
的下游前端覆盖层，补 libretro hw-render（GLES），把 flycast / mupen64plus-next /
yabasanshiro 接进 NextUI 的菜单、着色器和存档体系。

先读（按顺序）：
1. `AGENTS.md` —— 硬规则 + 现状 + `.docs/` 索引（**先读这个**）
2. 需要背景时按索引读 `.docs/`（device / measurement / frontend / pacing-audio /
   cores / pak-store / history / sources）
3. `docs/UPSTREAM-SYNC.md`（上游同步）、`docs/SWITCHER.md`（切换器与手工回滚）

当前状态（2026-10-27）：
- 发行形态：**一个 Tools pak** `Minarch GL`（切换器 + 前端 + 三核心载荷）。安装不改系统；
  用户按 Enable 才写 `.system/bin/minarch.elf` 并把核心 pak 装进 `Emus/h700/`；Restore 回滚。
- 产物：`dist/minarch-gl.elf`（当前基线与 md5 见 `AGENTS.md`）；
  `dist/Minarch.GL.pak.zip` md5 `7b97aa8e23ddf4efa1b8c0f663367869`（4,851,933 B）。
- 最近改动：见 `AGENTS.md` 的现状。
- 设备：RGSP，**冻结中**（yabause 重构），解冻以用户指令为准；上述前端改动从未上机验证。
- 仓库：`zhangmq/minarch-gl`（private）；未发 release。

本轮任务，按顺序做（除非我另说）：
1. `make build` 核对 md5；`make test`（应全过）；`make check`（应为 no changes）。
2. 需要时 `make cores` 打 pak，记录产物尺寸/md5。
3. 设备解冻后：按 `.docs/device.md` 的冒烟流程装 pak → Enable → 三系统各启动一次 → Restore，
   记录 fps/日志/异常。
4. 上游前进时：`tools/sync.py check` → `merge --to <sha>` → 构建/测试 → 冒烟 → `pin` → commit；
   冲突由你依证据解决并写进 commit message。
5. 首发 release 准备（`Minarch.GL.pak.zip` + `minarch-gl.elf` + `SHA256SUMS`）；
   **推送/发布前问我**。

纪律：见 `AGENTS.md` 的"硬规则"。特别是：
- 不要 `git merge` 上游；同步只走 `tools/sync.py`。
- `.system` 只允许切换器在用户按 Enable/Restore 时写。
- 设备上结束进程只允许 `kill -9`；自动化前先确认关机守卫。
- 提交、推送、发布都要我明确说。
- 遇到与本文件描述不符的地方，先查证据（日志/构建产物/digest），别先改代码。
