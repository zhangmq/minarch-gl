# 初始化 prompt（新会话用）

新建 DSH 会话，**workspace 设为 `/home/zmq/projects/minarch-gl`**，然后把下面整段发过去。

---

你在 `/home/zmq/projects/minarch-gl` 工作。这是 **minarch-gl**：NextUI（Anbernic H700）
的下游前端覆盖层，补 libretro hw-render（GLES），把 flycast / mupen64plus-next /
yabasanshiro 接进 NextUI 的菜单、着色器和存档体系，以"自带前端的 pak"分发。

先读这几个文件（按顺序）：
1. `README.md` —— 项目定位、构建、安装
2. `AGENTS.md` —— 维护记忆与硬规则
3. `docs/UPSTREAM-SYNC.md` —— 上游同步策略
4. `HANDOFF.md` —— 当前状态、SOP、设备冒烟流程、已知坑

当前状态（2026-10-21 bootstrap）：
- 前端已构建通过：`dist/minarch-gl.elf`，md5 `51d0fc1799b595b166c69b1eda8f6e32`
- 3 个核心和 4 个 pak **还没实际构建**（只做过 `make -n` 验证）
- 没上机、没建 GitHub 远程、没发 release

本轮任务，按顺序做（除非我另说）：
1. `make build` 复现 md5；再 `make check`（应报 no changes）。
2. `make cores`：构建 flycast / mupen64plus_next / yabasanshiro 与 DC、NAOMI、N64、
   SATURN 四个 pak，记录每个产物的尺寸和 md5。
3. 按 `HANDOFF.md` 的"设备冒烟"流程在 RGSP 上各启动一次，记录 fps/日志/异常。
4. 把结果写回 `HANDOFF.md` 的基线表与 commit message。
5. 建 `zhangmq/minarch-gl` 远程仓库，准备好首发 release 的清单
   （elf + 4 个 pak zip + 校验和）；**推送/发布前问我**。

纪律：
- 不要 `git merge` 上游；同步只走 `tools/sync.py`。
- 不要动设备 `.system`；pak 只进 `Emus/h700/`。
- 设备上结束进程只允许 `kill -9`；自动化前先确认关机守卫。
- 提交、推送、发布都要我明确说；ST-V 不主动提。
- 遇到与本文件描述不符的地方，先查证据（日志/构建产物/digest），别先改代码。
