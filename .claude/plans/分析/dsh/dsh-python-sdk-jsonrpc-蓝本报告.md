# dsh Python SDK 作为 JSON-RPC 客户端的完整接入蓝本（供 C++ 传输层实现参考）

> 调研对象：`F:\wjh_work\deepseek-harness`（deepseek-harness 仓库）。
> 目的：为 CLFCode 用 C++ 实现同样的 dsh runtime 接入做直接蓝本。
> 关键源码：
> - Python 客户端：`python/sdk/src/deepseek_harness/{api.py,client.py,models.py,errors.py}`
> - 运行时定位包：`python/sdk-runtime/src/deepseek_harness_runtime/__init__.py`
> - 线协议定义（TS）：`packages/sdk/protocol/src/{transport.ts,types.ts}`
> - 服务端（runtime 侧，即"对端"）：`packages/sdk/server/src/{server.ts,index.ts}`
> - 进程入口：`packages/examples/jsonrpc-demo/src/{runner.ts,packaged-bin.ts,bin.ts}`
> - TS 客户端（Python 的姊妹实现，可对照）：`packages/sdk/client/src/{client.ts,dispose.ts}`
> - 协议行为测试（最权威的线格式文档）：`python/sdk/tests/test_client.py`、`examples/jsonrpc-agent/tests/snapshots/text-turn/notifications.expected.jsonl`

---

## 0. 协议总览（先建立全局图）

dsh 的 SDK 接入是一条 **newline-delimited JSON-RPC 2.0 over stdio** 管道：

```
C++/Python 客户端进程  ──stdin 写──►   dsh-jsonrpc-agent 子进程（Node 单文件 exe / node 运行 packaged-bin）
      ◄──stdout 读──   （stdout 只保留协议帧，禁止任何日志）
      ◄──stderr 读──   （仅用于诊断：exit code + stderr tail）
```

- 客户端 → 服务端 **请求**（3 个，均有响应）：`initialize`、`session/prompt`、`shutdown`
- 服务端 → 客户端 **通知**（4 个，无响应）：`session.event`、`session.status`、`subagent.started`、`subagent.finished`
- 服务端还可能向客户端发 **请求**（桥接模式，客户端可用 `respond()/respond_error()` 应答），见 `test_client_routes_bridge_requests_and_sends_responses`

帧分类规则（`packages/sdk/protocol/src/transport.ts` 的 `handleLine` + Python `_handle_message`）：
| 帧内容 | 含义 |
|---|---|
| 有 `id` 且 `id` 是 string/number，且有 `method`（string） | **incoming request**（服务端→客户端，需应答） |
| 有 `id`（string/number），无 `method` | **response**（按 id 匹配挂起请求；`error` 成员存在则为错误响应） |
| 无 `id`，有 `method`（string） | **notification**（fan-out 给订阅者） |

服务端/客户端双方对 stdout 行的处理一致：**逐行读、跳过空行、跳过非 JSON 行（静默）**，因为 Node 单文件 exe 启动时可能往 stdout 打警告行（测试 `test_client_ignores_non_json_stdout_lines` 明确覆盖）。

---

## 1. 进程启动

### 1.1 定位可执行文件

入口链：`HarnessClient.start()` → `_default_launch_args()` → `deepseek_harness_runtime.resolve_bundled_launch_args()`。

`resolve_bundled_launch_args(mode=None)`（`python/sdk-runtime/src/deepseek_harness_runtime/__init__.py`）返回 argv 元组：
- **exe 模式（生产，自动选择）**：`(exe_path,)`，exe 路径 = `<package_data>/runtime/dsh-jsonrpc-agent-pkg-<platform>-<arch>`，平台映射：`linux-x64`、`linux-arm64`、`macos-arm64`（见 `platforms.json`；`sys.platform`/`platform.machine()` → tag，不支持则 `FileNotFoundError`）。**macOS 还要求同目录存在 `-spawn-helper` 侧车文件**（node-pty 用），缺失视为启动失败。
- **node 模式（仅开发，绝不自动选择）**：`(node_path, <package_data>/runtime/node/node_modules/@deepseek-ai/dsh-sdk-jsonrpc-demo/lib/packaged-bin.js)`，需要系统 Node ≥ 22.19。
- 模式选择优先级：显式参数 > 环境变量 `DSH_RUNTIME_MODE`（`exe`|`node`）> 自动（只找 exe）。自动解析找不到 exe 时报 `FileNotFoundError`，消息里给出两条获取途径。

客户端侧覆盖项（`HarnessConfig`，`python/sdk/src/deepseek_harness/client.py`）：
- `runtime_bin: str | None` → argv 就是 `(runtime_bin,)`
- `bridge_bin: str | None` → argv 是 `(bridge_bin,)`
- `launch_args_override: tuple[str, ...] | None` → 直接作为完整 argv（测试里用它注入 `(python, fake_runtime.py)`，见 `tests/test_client.py` 大量用例）
- 三个都为空时才走 bundled 解析；解析包未安装抛 `FileNotFoundError`（"Install deepseek-harness-runtime-bin ..."）

### 1.2 传参与进程 spawn

`HarnessClient.start()`（`client.py` L63-85）：
```python
args = list(self.config.launch_args_override or self._default_launch_args())
env = os.environ.copy()
if self.config.env: env.update(self.config.env)
self._inject_bundled_default_config(env)   # 零配置注入，见下
self._proc = subprocess.Popen(
    args,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    text=True, encoding="utf-8",
    cwd=None if self.config.cwd is None else str(Path(self.config.cwd).resolve()),
    env=env, bufsize=1,          # bufsize=1 → 行缓冲
)
self._start_reader_thread()      # 读 stdout 的线程
self._start_stderr_thread()      # 读 stderr 的线程
```

要点：
- 三条管道全部接管（stdin/stdout/stderr），`text=True + utf-8`（C++ 里等价于 UTF-8 文本管道）。
- `cwd`（`runtime_cwd`）在 spawn 前解析为**绝对路径**（`Path(...).resolve()`）。
- `start()` 幂等（`self._proc is not None` 直接 return）；`close()` 后再 `start()` 语义上是新建。
- **零配置注入**（`_inject_bundled_default_config`）：当 launch 解析到 bundled runtime 且环境里 `DSH_CORDIS_CONFIG` 为空/缺失时，注入 `env["DSH_CORDIS_CONFIG"] = bundled_default_config_path()`（即 `runtime/cordis.yml`，仓库内已签入的默认配置）。runtime 二进制**本身没有内置 fallback，没有显式 config 会 stderr 打 usage 并 exit(1)**。

### 1.3 环境变量（C++ 侧要完整复刻的清单）

`DeepSeekHarness.__init__`（`api.py` L56-83）在 `env` 上追加：
| 变量 | 值 | 用途 |
|---|---|---|
| `DSH_CWD` | 绝对 cwd | agent 工作区（bash/fs 工具用；cordis 里 `process.env.DSH_CWD ?? process.cwd()`） |
| `DSH_SESSION_ROOT` | 可选 | JSONL 会话持久化根目录 |
| `DSH_CORDIS_CONFIG` | 显式 `cordis` 时设置 | 覆盖 bundled 注入 |
| `DEEPSEEK_BASE_URL` / `DEEPSEEK_API_KEY` | 可选 | 模型端点凭据（适配器读取） |
| `DSH_RUNTIME_MODE` | 可选 `exe`/`node` | 载具选择 |

其余继承调用方环境。组合相关的可选变量：`DSH_SYSTEM_PROMPT`、`DSH_MODEL`、`DSH_MAX_TOKENS_AS_SUCCESS`、`DSH_CONTEXT_WINDOW`、`DSH_SNAPSHOT`。

### 1.4 runtime 侧（对端）入口行为

`packages/examples/jsonrpc-demo/src/runner.ts` 的 `runJsonrpcAgent`：
- config 解析：`DSH_CORDIS_CONFIG`（env，**空串视为缺失**）优先于 `process.argv[2]`；两者都没有 → stderr 打 usage、`process.exit(1)`。
- `boot(NAME, configPath, ...)` 后：`process.stdin.on('end')` → 优雅 dispose → `exit(0)`；`SIGTERM` → dispose → `exit(0)`；`SIGINT` → dispose → `exit(130)`。
- 服务插件（`packages/sdk/server/src/index.ts` 的 `apply`）：transport 接到 `shutdown` 请求后，**先写出响应帧，再** `transport.flush()` → `rootFiber.dispose()` → `exit(0)`。stdout 被协议独占，config 里不允许有 console logger。

---

## 2. stdio 帧协议

### 2.1 写请求（stdin 方向）

`client.py` `_write_message`（L298-308）：
```python
payload = json.dumps(message, separators=(",", ":")) + "\n"   # 紧凑 JSON + 单个换行
with self._write_lock:
    proc.stdin.write(payload)
    proc.stdin.flush()
```
- 请求帧形状：`{"jsonrpc":"2.0","id":"<uuid-str>","method":"...","params":{...}}`；`params` 可省略。
- `notify()`（L180-184）：同形状但**无 id**（发送通知，不等响应；测试里用于桥接模式的 `emit-first` 等）。
- `respond(request_id, result)` / `respond_error(request_id, *, code, message, data)`：应答服务端发来的请求，`{"jsonrpc":"2.0","id":...,"result":...}` 或 `{...,"error":{"code":...,"message":...,"data":...}}`。
- **并发写串行化**：`_write_lock`（`threading.Lock`）包裹整次 write+flush，保证 50 线程并发 `notify()` 时每行完整（`test_client_serializes_concurrent_writes`）。C++ 必须用互斥锁包住一次"序列化+写+flush"。
- 写失败（EPIPE / 进程已死）→ 抛 `TransportClosedError`（带诊断），且 `request_id` 从 `_responses` 摘除。

### 2.2 读响应（stdout 方向）

单条 **reader 线程**（`_reader_loop`，L318-334）：
```python
for line in proc.stdout:          # 文本模式逐行迭代，阻塞直到一行或 EOF
    if not line.strip(): continue          # 跳过空行
    try:
        message = json.loads(line)          # 非 JSON 行 → 静默跳过
    except json.JSONDecodeError:
        continue
    self._handle_message(message)
# 循环退出（EOF）→ _fail_waiters(TransportClosedError("...stdout closed"))
```
- **帧=一行**：双方都以 `\n` 分割，不做 length-prefix、不做 Content-Length。
- 对端服务端实现（`transport.ts` `JsonRpcLineTransport.onData/drainLines`）：字节流缓冲 + `StringDecoder('utf8')`，按 `\n` 切行并 `trim()`，空行跳过；`end` 事件先处理残余缓冲再 `failPending`。

### 2.3 消息路由（`_handle_message`，L343-384）——C++ 复刻的核心状态机

按 `id`/`method` 字段三分类：
1. **有 id（str/int）且有 method（str）→ 服务端发来的请求**：`IncomingRequest(id, method, params)` 放入 `self._requests` 队列；上层可用 `next_request()` 取、`respond()`/`respond_error()` 答。
2. **有 id（str/int）→ 响应**：
   - `self._responses.pop(str(msg_id))` 取 waiter（`queue.Queue(maxsize=1)`）；没有对应 waiter 直接丢弃（迟到响应）。
   - `message["error"]` 是 dict → waiter 收到 `JsonRpcError(code, message, data)`；否则 waiter 收到 `message.get("result")`。
   - **request id 用 `str(uuid.uuid4())`**（Python；TS 用 `req_<hex>`）。客户端 id 只需唯一，格式任意。
3. **只有 method（str）→ 通知**：`Notification(method, payload)`，先更新 session 亲缘关系（见 §5），再按订阅者 fan-out：
   - 遍历 `_notification_subscribers`（`subscription_id -> (queue, filter)`）；filter 通过才入队；**filter 抛异常只废掉该订阅**（把异常放入该订阅队列并注销），不影响别的订阅和读循环。
   - 没有任何订阅匹配时，放入全局 `self._notifications` 队列（`next_notification()` 取）。

**多帧交错**：响应与通知可任意穿插；每个请求有独立 waiter 队列，互不阻塞；`session/prompt` 的响应往往在若干通知之后到达（快照里 inbox receipt 事件先于响应帧）。reader 线程是唯一消费 stdout 的地方，路由全是内存队列操作——C++ 里等价于：reader 线程 + `std::mutex` 保护的 `map<id, waiter>` + 条件变量/队列。

---

## 3. initialize 握手

`HarnessClient.initialize(*, cwd, provider, model, max_tokens=None)`（L117-136）：
```python
payload = {"cwd": str(Path(cwd).resolve()), "provider": provider, "model": model}
if max_tokens is not None:
    payload["maxTokens"] = max_tokens
return self.request("initialize", payload, response_model=InitializeResponse)
```
- 参数键名：`cwd`、`provider`、`model`、`maxTokens`（camelCase！）。
- 响应模型 `InitializeResponse(serverInfo: ServerInfo | None = None)`，`ServerInfo(name, version)`。Python 侧校验很轻：`request()` 只要求 result 是 dict，pydantic 允许 `serverInfo` 缺省。TS 客户端更严：`serverInfo.name/version` 必须是字符串，否则 `SdkProtocolError`（C++ 建议按 TS 的严格校验做）。
- 真实服务端返回（`server.ts` L124）：`{"serverInfo":{"name":"deepseek-harness-sdk-runtime","version":"0.0.1"}}`。
- 服务端校验：`maxTokens` 必须是正整数 safe integer（否则 TypeError → error 响应）；`provider` 无注册适配器且非 `deepseek-official` → error；`deepseek-official` 时懒挂载 DeepSeek 适配器。
- **失败处理**：`initialize` 抛任何异常（含超时、错误响应）→ `self.close()` 收割子进程 → 重新抛出（`test_initialize_failure_reaps_started_runtime`）。即握手失败 = 进程作废。

---

## 4. session/prompt 调用

### 4.1 contentBlocks 构造

`api.py` `normalize_input(input)`（L199-202）：
```python
if isinstance(input, str):
    return [{"type": "text", "text": input}]
return input
```
字符串 → 单块 `{"type":"text","text":...}`；已是 `list[JsonObject]` 则原样透传（服务端 `createUserMessage({content: contentBlocks, source:{kind:'user'}})` 逐块组装用户消息）。

### 4.2 session_id 的生成与复用

- `DeepSeekHarness.start_session(session_id=None)`：缺省生成 `f"session-{uuid.uuid4().hex}"`（L115）。
- 显式传 id 则复用：服务端 `getOrCreateSession(sessionId)`（`server.ts` L203-216）按 id 缓存 `AgentHandle`，**未知 id 懒创建 agent+session**（`ctx.agents.create({sessionId, meta:{cwd}, agentOptions:{provider, model, maxTokens?}})`），已知 id 直接复用——复用 id 意味着延续同一持久会话（包括 bash 进程的工作目录/导出变量/shell 函数）。
- 高并发去重：`sessionCreations` Map 缓存创建中的 Promise，防止同 id 并发创建。
- `shutdown` 期间再 prompt → error（"SDK server is shutting down"）。

### 4.3 低层调用

`HarnessClient.session_prompt(session_id, content_blocks, *, on_notification=None, notification_subscription=None)`（L138-155）：
```python
payload = {"sessionId": session_id, "contentBlocks": content_blocks}
response = self.request("session/prompt", payload,
    response_model=_SessionPromptResponse,          # 校验 messageId: str
    notification_filter=self._notification_belongs_to_session_tree(session_id),
    notification_subscription=notification_subscription)
return response.messageId
```
- 响应 `{"messageId": "<uuid>"}`（服务端用 `createUserMessage` 生成的消息 id）；pydantic 校验失败抛 `ValueError`（`test_client_rejects_unaccepted_session_prompt_response`：result 里没有 `messageId` 字段 → TypeError/ValueError）。
- **注意**：低层接口只保证 prompt 已入队并返回 messageId，不负责等待活动结束——"活动边界"由高层 `Session.run()` 负责。

---

## 5. 事件消费（高层 `Session.run()` 的完整状态机）

### 5.1 订阅机制

- `subscribe_notifications(filter)` → `NotificationSubscription`（内部队列 + filter）；`subscribe_session_notifications(session_id)` = 带 session 树 filter 的订阅。
- **session 树过滤**（`_notification_belongs_to_session_tree`，L474-491）：
  - 对 `subagent.started`/`subagent.finished`：`parentSessionId` 是 root 的后代（含 root 自身）→ 匹配；或 `childSessionId == root` → 匹配。
  - 对其它通知：`payload.sessionId` 是 root 的后代 → 匹配。
  - 后代判定 `_session_is_descendant_of`：沿 `_session_parents`（`child -> parent`）向上走链，带 visited 集合防环；链在客户端进程生命周期内持续累积（只在 `subagent.started` 时记录，L460-472）。**服务端对每个 session（不只 SDK 创建的）都发通知，作用域完全靠客户端过滤。**
- 订阅是**每轮 run 新建**的（`with ... as subscription:`），所以旧轮的残留通知不会串进新轮；但同一订阅内仍用"inbox receipt"门控确定本轮的起始点（见下）。

### 5.2 run() 循环（`Session.run`，api.py L132-183）

```python
with self.harness.client.subscribe_session_notifications(self.id) as subscription:
    message_id = self.harness.client.session_prompt(self.id, content_blocks,
                                                    notification_subscription=subscription)
    received = False
    while True:
        notification = subscription.next()          # 阻塞取下一个通知
        if not received:
            if not _is_inbox_receipt(notification, self.id, message_id):
                continue                            # 未到本轮 receipt 前一律丢弃
            received = True
        collect(notification)                       # 记录 + on_notification 回调
        if (notification.method == "session.status"
                and notification.payload.get("sessionId") == self.id
                and notification.payload.get("status") == "idle"):
            break                                   # ← run() 返回条件
```

**关键：inbox receipt 门控**（`_is_inbox_receipt`，L186-196）：本轮真正的第一帧必须是
```json
{"method":"session.event","params":{"sessionId":"<id>","event":{
   "type":"agent/inbox/spliced","data":{"inserted":[{"id":"<message_id>"}, ...]}}}}
```
即 runtime 把该 prompt 的消息拼进 inbox 的事件，`data.inserted` 里出现与响应 `messageId` 相同的消息 id。**在此之前的通知（上一轮残留、无关事件）全部忽略**。这与 TS 客户端一致（TS 用同样的 receipt 门控，见 `packages/sdk/client/src/client.ts` 的 `run` 相关逻辑——注意 TS 客户端 `Session.run` 在 `client.ts` 之外的 api 层）。

`collect(notification)`：
- 追加到 `notifications` 列表；有 `on_notification` 回调则同步调用（保证回调先于 `run()` 返回，`test_session_run_invokes_notification_callback_before_returning`）。
- 仅当 `method == "session.event"` **且 `payload.sessionId == self.id`（只收根会话事件）** 时，把 `payload.event`（dict）追加进 `events`。子代理事件只进 notifications、不进 events（`test_session_run_collects_nested_subagent_tree_without_polluting_root_events`）。

### 5.3 run() 返回条件与结果组装

- **一轮结束 = 收到本会话 `session.status` 且 `status == "idle"`**。整轮从 receipt 起、到 idle 止；期间所有事件按线上顺序收集。
- `RunResult(session_id, final_response, finish_reason, events, notifications, session_root)`：
  - `final_response`（`final_response(events)`，L205-222）：**倒序**找最后一个 `type == "assistant/message"` 的根会话事件，拼其 `data.message.content`（或 `data.content`）里所有 `type == "text"` 块的 `text`。
  - `finish_reason`（`finish_reason(events)`，L225-241）：**倒序**找最后一个 `type == "turn/end"`，取 `data.reason.kind`（如 `completed`/`max-tokens`/`error`）；`reason.kind` 不是字符串 → 抛 `SdkProtocolError`（`test_high_level_sdk_rejects_turn_end_without_reason_kind`）；没有 turn/end → `None`。
- 真实事件流参考快照 `examples/jsonrpc-agent/tests/snapshots/text-turn/notifications.expected.jsonl`（42 帧）：`agent/inbox/spliced` → `session.status: running` → `turn/start` → `user/message` → `request/header` → `assistant/chunk`×N（`block-start`/`reasoning-delta`/`text-delta`/`block-end`/`usage`/`finish`）→ `assistant/message` → `step/end` → `turn/end` → `session.status: idle`。

### 5.4 超时与中断（Python 现状 + C++ 建议）

- **请求级超时**（`_request_raw`，L228-296）：deadline = `time.monotonic() + timeout`（默认 `config.request_timeout_seconds`，可为 None=永不）。等待用 `waiter.get(timeout=wait_timeout)` 轮询（有 `on_notification` 时每 50ms 醒来 drain 订阅，把等待期间的通知喂给回调）。**超时后把 `request_id` 从 `_responses` 弹出**（迟到响应直接丢弃，不留状态——与 TS AbortController 语义一致），抛 `TimeoutError`，消息附诊断（exit code + stderr tail）。
- **无线上取消**：超时只是客户端放弃等待，服务端工作继续跑直到 runtime 关闭（TS 客户端注释明确："There is no wire-level cancel"）。
- **Python 的空白点**：拿到 `messageId` 后的 idle 等待**没有超时**（`subscription.next()` 无限阻塞；runtime 卡死/agent 永不 idle 就永久挂起）。**C++ 实现必须补一个"整轮超时"**（例如配置 `run_timeout_seconds`，超过则中止订阅并报错/返回已收集事件）。
- 中断：`with` 块退出 → `close()`（见 §6）。Python 没有 SIGINT 期间的半途恢复逻辑。

---

## 6. 生命周期管理

### 6.1 完整时序

`DeepSeekHarness`（api.py）：
- `__enter__` → `start()`：`_client.start()`（spawn + 两个线程）→ `_client.initialize(cwd, provider, model, max_tokens)` → `_initialized = True`。**惰性启动**：只有首次 `start()`/`run()`/`start_session()` 才 spawn，跨多次 `run()` 复用同一子进程。
- `__exit__` → `close()` → `_client.close()`；`_initialized = False`。

`HarnessClient.close()`（client.py L87-115）——C++ 要逐级复刻的 shutdown 阶梯：
1. **协议 shutdown**：`self.request("shutdown", None, response_model=_ShutdownResponse, timeout_seconds=self.config.shutdown_timeout_seconds)`（默认 1.0s）。失败仅记入 stderr 诊断，不阻断。
2. **关 stdin**（`proc.stdin.close()`）——EOF 触发 runtime 的 `stdin 'end' → dispose → exit(0)` 路径（runner.ts L51）。
3. 若 `proc.poll() is None`（还活着）→ `proc.terminate()`（POSIX = SIGTERM，Windows = TerminateProcess）。
4. `proc.wait(timeout=shutdown_timeout_seconds)`；`TimeoutExpired` → `proc.kill()`（SIGKILL）→ `proc.wait()`。
5. `self._proc = None`；`_fail_waiters(TransportClosedError("...runtime closed"))`（所有挂起请求、订阅、全局通知队列、请求队列全部收到异常，阻塞中的 `next()`/`request()` 立刻醒）。
6. join reader/stderr 线程各 0.5s。

对端（`server.ts` `shutdown` → `performShutdown`，L150-181）：等所有进行中的 session 创建结束 → 摘除全部事件订阅 → `handle.dispose()`（每个 session 的 agent）+ `llmFiber.dispose()` → 返回 `{}`；`index.ts` 的 `apply` 在**响应帧已写之后**再 `transport.flush()` → `rootFiber.dispose()` → `exit(0)`。

TS 客户端的对照阶梯（`dispose.ts` `disposeRuntimeProcess`，更精细，可作 C++ 参考）：stdin EOF → 等 EOF 宽限（默认 6s，合作式落盘）→ SIGTERM → 等 3s → SIGKILL → 等有界退出边。**Windows 上 TS 直接跳 SIGKILL 等价物**（Node 把两个信号都映射成 `TerminateProcess`）。

其他要点：
- `close()` 幂等（前后都可调，`HarnessClient().close()` 空转，`test_client_close_is_idempotent...`）。
- `initialize` 失败 → `close()` 收割（见 §3）。
- 进程 crash 的感知：reader 线程 EOF（stdout 关闭）→ `_fail_waiters`，所有等待者收到 `TransportClosedError`；`_runtime_diagnostics()` 拼 exit code + stderr tail（`deque(maxlen=400)`）进错误消息（`test_runtime_closed_error_includes_stderr_tail` 断言 stderr 内容出现在异常里）。

---

## 7. 错误处理汇总

| 场景 | Python 行为 | 异常/错误类 |
|---|---|---|
| 错误响应帧（有 `error`） | `JsonRpcError(code, message, data)` 投给该请求的 waiter | `JsonRpcError`（`errors.py`） |
| 子进程退出 / stdout 关闭 | 所有等待者收到带诊断（exit code + stderr tail）的错误 | `TransportClosedError` |
| 请求超时 | 弹出 waiter、迟到响应丢弃、抛超时（带诊断） | 内建 `TimeoutError` |
| 协议违规（turn/end 无 reason.kind 等） | 抛协议错误 | `SdkProtocolError` |
| 写失败（EPIPE） | 转成带诊断的传输错误 | `TransportClosedError` |
| stdout 非 JSON 行 / 空行 | 静默忽略 | — |
| 响应缺少预期字段 | pydantic 校验失败（`TypeError`/`ValueError`） | — |
| 通知 filter 抛异常 | 仅废掉该订阅，注入异常给该订阅 | — |
| 找不到 runtime | `FileNotFoundError`（带安装指引） | — |
| `initialize` 失败 | 关闭并收割子进程后重抛 | — |

错误类层级：`HarnessError` ← {`TransportClosedError`, `SdkProtocolError`, `JsonRpcError`}。

---

## 8. 对 C++ 实现的启示

### 8.1 Python 直接可复用的模式（C++ 等价实现）

1. **帧协议**：一行一个紧凑 JSON（`{"jsonrpc":"2.0",...}\n`）+ 三分类路由（id+method / id / method）+ 静默跳过空行与非 JSON 行。C++ 用 nlohmann::json 直接照搬 `_handle_message` 的状态机。
2. **reader 线程 + 按 id 的 waiter 表**：Python `_responses: dict[str, queue]` + `_write_lock`；C++ 等价于 `std::jthread reader`（`std::getline` 逐行） + `std::mutex` 保护的 `std::unordered_map<std::string, waiter>`（waiter = promise/queue + condvar）。响应按 `id` 弹表；迟到响应丢弃。
3. **请求 id**：`uuid4().hex`；C++ 用随机 hex 或递增 id 均可（服务端不校验格式）。
4. **通知 fan-out + 订阅过滤**：`subscribe_session_notifications` 的 session 树过滤必须原样复刻（`subagent.started` 维护 `child→parent` 链 + visited 集合的向上追溯）；否则子代理事件会串台。
5. **inbox receipt 门控 + idle 判定**：`agent/inbox/spliced` 中匹配 `messageId` 才开始收集；`session.status: idle` 结束。这是 run() 语义的核心，两个客户端一致。
6. **结果组装**：倒序找最后一个 `assistant/message`（拼 text 块）+ 最后一个 `turn/end`（取 `data.reason.kind`）。
7. **shutdown 阶梯**：shutdown 请求（有界等待）→ 关 stdin（EOF）→ SIGTERM → SIGKILL → 收割；错误消息附 exit code + stderr tail（环形缓冲 400 行）。
8. **并发写串行化**：一次"序列化 + 写 + flush"持锁，防半行。
9. **进程启动参数/环境**：argv = exe 路径（或 override），`cwd` 绝对化，环境 = 父进程环境 + `DSH_CORDIS_CONFIG`（必需！runtime 无内置 fallback）+ `DSH_CWD` + 可选 `DSH_SESSION_ROOT`/`DEEPSEEK_*`/`DSH_RUNTIME_MODE`。

### 8.2 Python 有、C++ 必须自己造的

1. **并发读 stdout + 写 stdin 的线程模型**：Python 靠 GIL 让 `queue.Queue`/dict 免锁，C++ 必须显式 mutex/condvar；stdout reader 线程与调用线程通过条件变量通信，stderr 必须**单独线程**排空（否则子进程写满 stderr 管道缓冲会死锁——Python 有 `_stderr_thread`，C++ 不能省）。
2. **进程 spawn/收割**：`subprocess.Popen` 的完整等价物（Windows `CreateProcess` + 管道句柄；POSIX `fork`+`exec`/`posix_spawn`）；`terminate()`/`kill()` 的平台映射（Windows 用 `TerminateProcess`，注意"kill 进程树"要用 Job Object）；`wait(timeout)` 等价物（`WaitForSingleObject` / `waitpid` + 轮询）。
3. **文本管道解码**：Python `text=True, encoding="utf-8"` 负责 UTF-8 解码；C++ 需按 UTF-8 处理（nlohmann::json 原生支持），跨平台换行（`\r\n` 与 `\n`）要在行切分时容错。
4. **行读取缓冲**：`for line in proc.stdout` 是带缓冲的整行迭代；C++ 用 `std::getline`（阻塞、自动处理半行）或自建缓冲区分片（pipe read 可能任意切分）。
5. **超时语义**：Python `queue.get(timeout)` + deadline 轮询；C++ 用 `condition_variable::wait_for`。**必须额外补 Python 缺的两个超时**：a) 拿到 messageId 后整轮 idle 等待超时；b) shutdown 后进程未退出的强制 kill 等待。
6. **JSON 校验层**：pydantic → 手写字段校验（建议按 TS 客户端的严格度：`initialize` 必须返回字符串 `serverInfo.name/version`，`session/prompt` 必须返回字符串 `messageId`）。
7. **服务端→客户端请求的应答**：桥接模式（`next_request()`/`respond()`/`respond_error()`）Python 已实现；C++ 若只做标准 SDK 流程可先不做，但路由状态机里要预留（否则遇到带 id+method 的帧会丢）。
8. **平台可用性**：bundled exe 只发行 linux-x64/arm64、macos-arm64（macOS 还需 `-spawn-helper`），**没有 Windows 载具**。C++ 客户端在 Windows 上要么依赖用户提供 `runtime_bin`/`launch_args_override`（例如 node 载具），要么把 `DSH_RUNTIME_MODE=node` + 系统 Node 作为开发路径。
9. **诊断收集**：stderr 环形缓冲 + exit code 拼进所有传输错误消息（C++ 报错信息要带这两样）。
10. **id 唯一性**：服务端同 id 并发去重靠 `sessionCreations`（客户端无关）；客户端只需保证请求 id 唯一。

### 8.3 建议的 C++ 类骨架（对应 Python 类）

```
CLFJsonRpcClient            ← HarnessClient：spawn、reader/stderr 线程、_responses、订阅表、
                               request()/notify()/respond()、close() 阶梯
CLFJsonRpcNotification      ← Notification {method, payload}
CLFJsonRpcSubscription      ← NotificationSubscription {队列, filter, next()/drain()/close()}
CLFHarnessSession           ← Session {id, run(input, on_notification) -> CLFRunResult}
CLFRunResult                ← RunResult {sessionId, finalResponse, finishReason, events, notifications}
CLFHarnessConfig            ← DeepSeekHarnessConfig（provider/model/maxTokens/cwd/sessionRoot/cordis/env/runtimeBin/timeouts）
```

---

## 9. 附：最小编码清单（供 C++ 传输层直接实现）

1. 请求帧：`{"jsonrpc":"2.0","id":"<uid>","method":"<m>","params":{...}}\n`
2. 响应帧：`{"jsonrpc":"2.0","id":"<uid>","result":...}\n` 或 `{"jsonrpc":"2.0","id":"<uid>","error":{"code":<int>,"message":"<str>","data":...}}\n`
3. 通知帧：`{"jsonrpc":"2.0","method":"<m>","params":{...}}\n`（params 可省）
4. 方法：`initialize`(cwd, provider, model, maxTokens?) → `{serverInfo:{name,version}}`；`session/prompt`(sessionId, contentBlocks) → `{messageId}`；`shutdown`() → `{}`
5. 通知：`session.event`(sessionId, event)、`session.status`(sessionId, status∈{idle,running})、`subagent.started`(parentSessionId, childSessionId)、`subagent.finished`(provider, agentId, parentSessionId, childSessionId, status∈{ok,error}, stopReason, lastAssistantMessage?)
6. run() 完成判定：收到本会话 `session.status:{status:"idle"}`；起始判定：`session.event` 的 `event.type=="agent/inbox/spliced"` 且 `event.data.inserted[].id` 含 `messageId`。
7. 环境：至少注入 `DSH_CORDIS_CONFIG`（config 文件绝对路径）；runtime 无 config 直接 exit(1)。
