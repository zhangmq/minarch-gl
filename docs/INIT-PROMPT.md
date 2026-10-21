# 初始化 prompt（新会话用）

新建 DSH 会话，**workspace 设为 `/home/zmq/projects/minarch-gl`**，然后把下面整段发过去。

---

你在 `/home/zmq/projects/minarch-gl` 工作。这是 **minarch-gl**：NextUI（Anbernic H700）
的下游前端覆盖层，补 libretro hw-render（GLES），把 flycast / mupen64plus-next /
yabasanshiro 接进 NextUI 的菜单、着色器和存档体系。

先读这几个文件（按顺序）：
1. `README.md` —— 项目定位、构建、安装
2. `AGENTS.md` —— 维护记忆与硬规则
3. `docs/UPSTREAM-SYNC.md` —— 上游同步策略
4. `docs/SWITCHER.md` —— 前端切换器机制与手工回滚
5. `HANDOFF.md` —— 当前状态、SOP、设备冒烟流程、已知坑

当前形态（2026-10-21）：
- 只发一个 **Tools pak**：`Minarch GL.pak`（切换器 UI + 前端 + 三个核心载荷）。
  安装不改任何东西；用户在 UI 里按 Enable 才写 `.system/bin/minarch.elf`，
  同时把核心 pak（`LIBRETRO_FLYCAST` / `LIBRETRO_MUPEN64PLUS` /
  `LIBRETRO_YABASANSHIRO`）装进 `Emus/h700/`；Restore 一键回滚。
- 构建：`make build`（前端）、`make cores`（+ 切换器 + 三核心 → `dist/Minarch.GL.pak.zip`）、
  `make test`（切换器文件逻辑的主机测试）、`make check`（上游漂移）。
- 前端 md5 基线 `51d0fc1799b595b166c69b1eda8f6e32`；产物表见 `HANDOFF.md`「基线」。
- **从未上过设备**（设备当时被其他任务占用并冻结）；`zhangmq/minarch-gl` 远程已建但为空。

本轮任务，按顺序做（除非我另说）：
1. `make build` 复现 md5，`make check`（应 no changes），`make test`（应全过）。
2. `make cores`，记录 pak zip 与三个核心的尺寸/md5。
3. 按 `HANDOFF.md` 的"设备冒烟"：装工具 pak → Enable → 三个系统各启动一次
   （DC/Naomi、N64、Saturn）→ Restore 一次；记录 fps/日志/异常。
4. 把结果写回 `HANDOFF.md` 的基线表与 commit message。
5. 准备首发 release（`Minarch.GL.pak.zip` + `minarch-gl.elf` + 校验和）；
   **推送/发布前问我**。

纪律：
- 不要 `git merge` 上游；同步只走 `tools/sync.py`。
- `.system` 只允许切换器在用户点 Enable/Restore 时写；pak 安装本身不得改动它。
- 设备上结束进程只允许 `kill -9`；自动化前先确认关机守卫。
- 提交、推送、发布都要我明确说。
- 遇到与本文件描述不符的地方，先查证据（日志/构建产物/digest），别先改代码。
