#!/usr/bin/env node
// CLFCode spike S1 帧驱动脚本 — 直写 stdio JSON-RPC wire protocol
//
// M1 对译映射（"脚本即对译模板"，逐函数对译）：
//   spawnAndEnv()      -> CLFJsonRpcClient::spawn
//   lineReader()       -> CLFJsonRpcClient reader 线程
//   routeFrame()       -> CLFJsonRpcClient::handleLine
//   runTurn()          -> CLFHarnessSession::run
//   normalizeFrames()  -> M1 测试工具（fixture 生成）
//
// 语义照抄双客户端（Python SDK client.py / TS SDK client.ts）：
//   - 帧三分类：id+method=桥接请求(respond 空结果) / 仅 id=响应(按 id 弹 waiter, 迟到丢弃) / 仅 method=通知(fan-out)
//   - receipt 门控：session.event 且 type=="agent/inbox/spliced" 且 inserted[].id 含 messageId 才起收
//   - 结束判定：本会话 session.status == "idle"
//   - 两个超时（Python 缺失，必须自补）：整轮 idle 等待 / shutdown 后强杀

import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// ============================================================================
// 命令行参数（默认值即 S1 判据执行参数）
// ============================================================================
function parseArgs(argv) {
  const get = (name, def) => {
    const i = argv.indexOf(`--${name}`);
    return i >= 0 && argv[i + 1] !== undefined ? argv[i + 1] : def;
  };
  return {
    prompt: get('prompt', '你好，请简短回复确认联通'),
    model: get('model', 'deepseek-v4-pro'),
    provider: get('provider', 'deepseek-official'),
    maxTokens: get('max-tokens', null),
    // 默认唯一 id：sessionId 复用会与已落盘日志碰撞（实测：id collision 错误，turn 直接 error）
    sessionId: get('session-id', `spike-s1-${Date.now().toString(36)}`),
    bin: get('bin', path.join(__dirname, '../../../../deepseek-harness/packages/examples/jsonrpc-demo/lib/bin.js')),
    config: get('config', path.join(__dirname, 'cordis-smoke.yml')),
    settings: get('settings', path.join(__dirname, '../../config/agent_settings.local.json')),
    idleTimeoutMs: Number(get('idle-timeout-ms', '600000')),
    shutdownGraceMs: Number(get('shutdown-grace-ms', '5000')),
    framesDir: get('frames-dir', path.join(__dirname, 'frames')),
    runName: get('run-name', 's1-hello'),
    help: argv.includes('--help'),
  };
}

const USAGE = `usage: node spike_driver.mjs [--prompt <text>] [--model <m>] [--provider <p>]
       [--max-tokens <n>] [--session-id <id>] [--bin <path>] [--config <path>]
       [--settings <path>] [--idle-timeout-ms <n>] [--shutdown-grace-ms <n>]
       [--frames-dir <dir>] [--run-name <name>]`;

// ============================================================================
// spawnAndEnv() — M1: CLFJsonRpcClient::spawn
// ============================================================================
function loadCreds(settingsPath) {
  const parsed = JSON.parse(readFileSync(settingsPath, 'utf8'));
  const conn = parsed.connection ?? {};
  if (!conn.api_key || !conn.base_url) throw new Error(`凭据缺失: ${settingsPath} (connection.api_key/base_url)`);
  return { apiKey: conn.api_key, baseUrl: conn.base_url }; // 永不打印
}

function spawnAndEnv(binPath, configPath, creds) {
  const env = {
    ...process.env,
    DEEPSEEK_API_KEY: creds.apiKey,
    DEEPSEEK_BASE_URL: creds.baseUrl,
    DSH_CWD: path.join(__dirname, 'workspace'),
    DSH_SESSION_ROOT: path.join(__dirname, 'sessions'),
  };
  const child = spawn(process.execPath, [binPath, configPath], {
    stdio: ['pipe', 'pipe', 'pipe'],
    env,
    windowsHide: true,
  });
  child.stderr.pipe(process.stderr); // 诊断走 stderr 透传，stdout 只承载协议帧
  return child;
}

// ============================================================================
// lineReader() — M1: CLFJsonRpcClient reader 线程
// ============================================================================
function lineReader(child, onFrame) {
  const rl = createInterface({ input: child.stdout, crlfDelay: Infinity });
  rl.on('line', (line) => {
    const trimmed = line.trim();
    if (!trimmed) return;                              // 跳过空行
    let frame;
    try { frame = JSON.parse(trimmed); } catch { return; } // 跳过非 JSON 行
    onFrame(frame);
  });
  return rl;
}

// ============================================================================
// routeFrame() + 帧 IO — M1: CLFJsonRpcClient::handleLine / 写锁
// ============================================================================
function createClient(child) {
  let nextId = 1;
  const waiters = new Map();
  const frames = [];           // 全量帧（tx+rx），供归一化
  let onNotification = () => {}; // 可变引用：runTurn 注入

  const client = {
    frames,
    setNotificationHandler(fn) { onNotification = fn; },

    send(frame) {
      child.stdin.write(JSON.stringify(frame) + '\n');
      frames.push({ dir: 'tx', frame });
    },

    routeFrame(frame) {
      frames.push({ dir: 'rx', frame });
      if (frame.id !== undefined && frame.method !== undefined) {
        client.send({ jsonrpc: '2.0', id: frame.id, result: {} }); // 桥接请求：respond 空结果
      } else if (frame.id !== undefined) {
        const w = waiters.get(frame.id);                            // 响应：按 id 弹 waiter，迟到丢弃
        if (w) { waiters.delete(frame.id); w.resolve(frame); }
      } else if (frame.method !== undefined) {
        onNotification(frame);                                      // 通知：fan-out
      }
    },

    request(method, params) {
      const id = nextId++;
      const p = new Promise((resolve) => waiters.set(id, { resolve }));
      client.send({ jsonrpc: '2.0', id, method, params });
      return p;
    },
  };
  return client;
}

// ============================================================================
// runTurn() — M1: CLFHarnessSession::run（receipt 门控 + idle 判定 + 超时）
// ============================================================================
async function runTurn(client, args, state) {
  const events = [];          // 根会话事件（含 receipt 事件）
  const notifications = [];   // 全部通知（子会话事件只进这里）
  let started = false;        // receipt 门控
  let gateLive = false;       // 响应前只缓冲；响应到达后回溯过滤再直通
  let finishReason = null;
  let finalResponse = '';
  let timedOut = false;

  let settle;
  const done = new Promise((r) => { settle = r; });
  const timer = setTimeout(() => { timedOut = true; settle(); }, args.idleTimeoutMs);

  // 实测（s1-hello 帧序）：runtime 先推事件（spliced 已带最终 messageId），
  // session/prompt 响应后到——receipt 门控必须"缓冲 + 响应到达后回溯过滤"，
  // 不能到达时即判（messageId 尚不存在）
  const ingest = (n) => {
    if (n.method !== 'session.event') return;
    const { sessionId, event } = n.params ?? {};
    if (sessionId !== args.sessionId || !event || typeof event !== 'object') return;
    if (!started && event.type === 'agent/inbox/spliced') {
      const ids = event.data?.inserted?.map((m) => m.id) ?? [];
      if (ids.includes(state.receiptMessageId)) started = true;
    }
    if (started) {
      events.push(event);
      if (event.type === 'turn/end') finishReason = event.data?.reason?.kind ?? finishReason;
      if (event.type === 'assistant/message') {
        // 实测形态：内容块在 data.message.content（含 reasoning 块与 text 块）
        const texts = (event.data?.message?.content ?? []).filter((b) => b.type === 'text').map((b) => b.text);
        if (texts.length) finalResponse = texts.join('');
      }
    }
  };

  client.setNotificationHandler((n) => {
    notifications.push(n);
    if (n.method === 'session.status' && n.params?.sessionId === args.sessionId && n.params?.status === 'idle') {
      settle();
    }
    if (gateLive) ingest(n);
  });

  // 1. initialize — 首条，验证握手 + serverInfo
  const init = await client.request('initialize', {
    cwd: path.resolve(path.join(__dirname, 'workspace')),
    provider: args.provider,
    model: args.model,
    ...(args.maxTokens !== null ? { maxTokens: Number(args.maxTokens) } : {}),
  });
  console.log(`[initialize] serverInfo=${JSON.stringify(init.result?.serverInfo ?? {})}`);

  // 2. session/prompt（响应到达前事件已开始流动）
  const pr = await client.request('session/prompt', {
    sessionId: args.sessionId,
    contentBlocks: [{ type: 'text', text: args.prompt }],
  });
  state.receiptMessageId = pr.result?.messageId ?? '';
  console.log(`[session/prompt] messageId=${state.receiptMessageId}`);

  // 3. 响应到达：回溯过滤缓冲（receipt 门控），之后直通
  gateLive = true;
  for (const n of notifications) ingest(n);

  await done;
  clearTimeout(timer);
  return { events, notifications, finalResponse, finishReason, timedOut };
}

// ============================================================================
// shutdown 阶梯 — M1: CLFJsonRpcClient::close 阶梯
// ============================================================================
async function shutdownLadder(client, child, args) {
  if (child.exitCode !== null || child.stdin.destroyed) return;
  try {
    await Promise.race([
      client.request('shutdown', {}),
      new Promise((r) => setTimeout(r, args.shutdownGraceMs)),
    ]);
  } catch { /* 超时或通道已断，进入强杀 */ }
  child.stdin.end(); // EOF
  const exited = new Promise((r) => child.once('exit', r));
  const force = setTimeout(() => child.kill('SIGKILL'), args.shutdownGraceMs);
  await exited;
  clearTimeout(force);
}

// ============================================================================
// normalizeFrames() — M1 测试工具（fixture 生成）
// 占位符：{{rootSessionId}} / {{childSessionId-N}}（按 subagent.started 出现序）
//         / {{messageId-N}}（root 消息 id，先见序）/ {{childMessageId-N}}
//         / {{cwd}} / {{system}}
// 归零：time / seq / createdAt
// 恒等保持：同一原始 id 映射同一占位符（map 去重），M1 断言可依赖相等性
// ============================================================================
function normalizeFrames(frames, rootSessionId) {
  const sessionMap = new Map([[rootSessionId, '{{rootSessionId}}']]);
  const messageMap = new Map(); // 原始 id -> 占位符
  let childSeq = 0;
  let msgSeq = 0;

  // pass 1: 建映射（子会话按 subagent.started 出现序）
  for (const { frame } of frames) {
    if (frame.method === 'subagent.started') {
      const child = frame.params?.childSessionId;
      if (child && !sessionMap.has(child)) sessionMap.set(child, `{{childSessionId-${++childSeq}}}`);
    }
  }
  // pass 1b: 消息 id 归属（按帧的 sessionId 定 root/child）
  for (const { frame } of frames) {
    if (frame.method !== 'session.event') continue;
    const sid = frame.params?.sessionId;
    if (sid === undefined) continue;
    const owner = sessionMap.has(sid) ? sid : null;
    const walkIds = (node) => {
      if (Array.isArray(node)) return node.forEach(walkIds);
      if (!node || typeof node !== 'object') return;
      if (typeof node.id === 'string' && node.id.length > 0 && !messageMap.has(node.id)) {
        const isRoot = owner === rootSessionId;
        messageMap.set(node.id, isRoot ? `{{messageId-${++msgSeq}}}` : `{{childMessageId-${++msgSeq}}}`);
      }
      for (const v of Object.values(node)) walkIds(v);
    };
    walkIds(frame.params?.event?.data ?? frame.params);
  }

  // pass 2: 重写
  return frames.map(({ dir, frame }) => ({ dir, frame: rewrite(JSON.parse(JSON.stringify(frame))) }));

  function rewrite(node) {
    if (Array.isArray(node)) return node.map(rewrite);
    if (!node || typeof node !== 'object') return node;
    const out = {};
    for (const [k, v] of Object.entries(node)) {
      if (k === 'sessionId' && typeof v === 'string' && sessionMap.has(v)) out[k] = sessionMap.get(v);
      else if (k === 'id' && typeof v === 'string' && messageMap.has(v)) out[k] = messageMap.get(v);
      else if (k === 'cwd') out[k] = '{{cwd}}';
      else if (k === 'system' && typeof v === 'string') out[k] = '{{system}}';
      else if ((k === 'time' || k === 'seq' || k === 'createdAt') && typeof v === 'number') out[k] = 0;
      else out[k] = rewrite(v);
    }
    return out;
  }
}

// ============================================================================
// 统计（S1 判据：reasoning 流实测 + chunk 形态）
// ============================================================================
function statsOf(events) {
  const s = { reasoningDelta: 0, textDelta: 0, toolCalls: 0, toolNames: [], blockStarts: [], usage: null, finish: null };
  for (const ev of events) {
    if (ev.type !== 'assistant/chunk') continue;
    const c = ev.data?.chunk ?? {};
    const t = c.type ?? ev.data?.type;
    if (t === 'reasoning-delta') s.reasoningDelta++;
    else if (t === 'text-delta') s.textDelta++;
    else if (t === 'block-start') s.blockStarts.push(c.blockType ?? '?');
    else if (t === 'usage') s.usage = c;
    else if (t === 'finish') s.finish = c;
  }
  for (const ev of events) {
    if (ev.type === 'tool/call') { s.toolCalls++; s.toolNames.push(ev.data?.name ?? '?'); }
  }
  return s;
}

// ============================================================================
// main
// ============================================================================
async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { console.log(USAGE); return; }

  const creds = loadCreds(args.settings);
  const child = spawnAndEnv(args.bin, args.config, creds);
  console.log(`[spawn] pid=${child.pid}`);

  const client = createClient(child);
  lineReader(child, (frame) => client.routeFrame(frame));

  const state = { receiptMessageId: '' };
  const result = await runTurn(client, args, state);
  console.log(`[runTurn] events=${result.events.length} notifications=${result.notifications.length} timedOut=${result.timedOut}`);

  const stats = statsOf(result.events);
  console.log(`[stats] reasoning-delta=${stats.reasoningDelta} text-delta=${stats.textDelta} tool-calls=${stats.toolCalls}`);
  if (stats.toolNames.length) console.log(`[stats] toolNames=${stats.toolNames.join(',')}`);
  if (stats.blockStarts.length) console.log(`[stats] blockStarts=${[...new Set(stats.blockStarts)].join(',')}`);
  if (stats.usage) console.log(`[stats] usage=${JSON.stringify(stats.usage)}`);
  if (stats.finish) console.log(`[stats] finish=${JSON.stringify(stats.finish)}`);
  console.log(`[finalResponse] ${result.finalResponse.slice(0, 300)}`);
  console.log(`[finishReason] ${result.finishReason ?? '(none)'}`);

  // frames 双轨落盘
  mkdirSync(path.join(args.framesDir, 'raw'), { recursive: true });
  mkdirSync(path.join(args.framesDir, 'norm'), { recursive: true });
  const rawPath = path.join(args.framesDir, 'raw', `${args.runName}.jsonl`);
  writeFileSync(rawPath, client.frames.map(({ dir, frame }) => JSON.stringify({ dir, frame })).join('\n') + '\n');
  const norm = normalizeFrames(client.frames, args.sessionId);
  const normPath = path.join(args.framesDir, 'norm', `${args.runName}.norm.jsonl`);
  writeFileSync(normPath, norm.map(({ dir, frame }) => JSON.stringify({ dir, frame })).join('\n') + '\n');
  console.log(`[frames] raw=${rawPath}`);
  console.log(`[frames] norm=${normPath}`);

  await shutdownLadder(client, child, args);
  console.log(`[exit] code=${child.exitCode}`);
}

main().catch((e) => { console.error('[fatal]', e); process.exit(1); });
