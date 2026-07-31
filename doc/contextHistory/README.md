# contextHistory — 会话历史目录

Agent 对话会话的自动保存文件存放于此（崩溃恢复 + `/history` 列表）。

- 内容已被 `.gitignore` 忽略，不入库
- 文件名：`YYYY-MM-DD_HH-MM-SS.json`（同秒追加 `-2`/`-3`），`_incomplete.json` 为未正常结束的会话
- 超过 30 天自动清理（启动时）
