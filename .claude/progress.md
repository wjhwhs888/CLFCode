# CLFCode 任务进度

## 进行中

### ▶ 中断修复与平台收敛阶段性任务 ⏳ 审查完成、排期四波定稿（2026-09-23，待开工）
- **背景**：用户与 flash 2026-09-22~23 头脑风暴产出四份设计文档，pro 按 `agent协作规范.md` v1.0（审查门开工前必填、证据高于结论）完成全量审查
- **指导总纲**：`设计/设计-中断修复与平台收敛总排期.md`（本任务的唯一权威：审查门结论 + 排期四波 + 裁剪决策）
- **四文档**：`设计/设计-中断时效性.md`（ESC/Ctrl+C 无法立即中断的根因修复，P0）/ `设计/设计-命令执行层.md`（执行器内部实现唯一权威）/ `设计/设计-平台层收敛.md`（Windows 平台耦合治理，第一期 = Windows 11）/ `设计/设计-启动横幅窄窗口渲染异常.md`（观察任务，用户定"先不动"——不入队列）
- **取证**：42 条断言**零证伪**——pro 亲验核心根因链（56 秒日志铁证 / `CLFCommandExec.cpp:102-129` 零检查点 / `:125` 超时只杀父 / `CLFToolApi.hpp:13` 无取消通道 / `CLFAgentLoop.cpp:366-369` 悬空产生点 / jsonl 16 声明 15 结果 1 悬空实证）+ 2 个 Explore agent 清单核实（6 处行号细节偏移，无方向性错误）
- **排期四波**：① 中断根因闭环（A 批 10 步 + 协议闭合 + A2 三慢点 + B2/W6/B4 上提 + W1 世代号 + repair-on-load + 命令层 G1/超时 120s·600 配置——验收门：node test_ai.js 中途 ESC ≤1s 停 + 无残留 + 继续对话不 400）→ ② 命令层收尾（G2 限额头尾截断 / G3 错误归一化 / argv 化消 2>nul）→ ③ 平台层收敛（步骤 0-7 零行为变更基建）→ ④ P1 收尾（B3 Ctrl+C 收尾统一 / W-usage / quiesce 回归）
- **pro 裁剪**（已入总纲）：批C-2 砍（httplib open_stream 改造风险高收益不明）、批C-5 砍（三权威收敛重构）、B2/W6/B4 上提第一波（不随 A 同批会引入连按 ESC 误退新烦恼）
- **用户拍板项全部保留**：D1 立即强杀 / D2 协议闭合 / D3 全域可中断 / D5 用量计入 / D6 半截不入库 / D7 选区 ESC / D8 待办面板 / D10 术语 / 超时 120s+600 配置
- **审查门回填 ✅**：三份文档审查门表已填（方向成立、裁剪、疑问、接口风险——详见总排期 §二）
- **▶ 第一波实施完成 ✅（2026-09-23，9 个提交，ctest 37/37 全绿 + --version 冒烟）**：
  - A 批 10 步全落地：ABI v2 取消通道（bef5c81）→ 宿主接线 interruptFlag + CLFToolCallCtx 化（89b70b1）→ CLFProcessRunner 落地 CLFTypes + 取消检查 + Job Object 杀树（超时同改，W-2 孤儿缺陷同修）+ handler/插件壳下传（685a239）→ 协议闭合（CLFToolExecutor 内产出"未执行"结果，声明数==结果数恒成立，AgentLoop 零差集——总排期细化 5 裁剪落地）（a477f6a）→ qa_CLFProcessRunner C1-C5（C3 心跳探针实证杀树）+ I1a/I1b（a477f6a）
  - 步骤 8 日志 + 批C-1（W1 exchange 最小版）/批C-3（W3 注释 + W2 emitInterrupted 日志）/批C-4（W5 mock abort 做实 + 两时序用例）（654cac7）；批C-2（open_stream）砍、批C-5 砍
  - B 批子集：B2 双击退出 (a)+(b)、W6 状态行单写（setStatusHold 高优先级方案，不加接口）、B4 去 clear（81d9673）；B3/W-usage 留第四波
  - repair-on-load 内存闭合（b54e8ca，J13/J14 用例）
  - 命令层步骤 6 超时配置 120s/600（1c80dce，用户拍板项；handler 层 min clamp + 执行器硬顶 3600）
  - A2 三慢点：search_content（每目录项+每行检查点）、web_fetch（content receiver 逐块取消）、自动摘要（入口/返回后检查）（0d082a7，D7 真插件取消全链路用例）
  - 实施记录已回填两份文档（中断 §十四 / 命令层 §十六，含裁剪落地说明与实抓问题）
- **▶ 用户实机验收 ✅（2026-09-23 上午，抓出 1 个真 bug 已修）**：
  - **验收期实抓**：中断后新回合模型零输出直接中断（第三次提交才恢复）——W1 残留误伤：命令执行期按 ESC → onInterrupt 无条件 abort() → 无在途请求时 m_aborted 残留 → 批C-1 入口 exchange 误判"待决中断"拒发新回合首个请求
  - **修根（b9777e3，已推送）**：abort() 只在有在途请求时置位/stop（同锁防竞态）——无在途 no-op（m_interrupted 通道兜底）；入口恢复无条件重置；W5a 重写为回归钉（abort 先于请求 → 请求必须正常发出）
  - **复验通过（用户）**：① 中断后继续提问正常响应 ✓ ② tasklist 实证：被中断的 node 进程消失（4→3，剩下为用户常驻）✓ ③ 中断命令即时生效"命令被用户中断（已终止进程树）"✓ ④ 状态行"⏹ 已中断"正常显示 ✓
  - 观察项：模型被中断后 29s 又重试同一命令（用户连按 ESC 拦下）——文案强化（"先询问用户"）已提议待用户定
- **▶ 第二波实施完成 ✅（2026-09-23，3 个提交，ctest 37/37 全绿）**：
  - G2 限额头尾截断（2e49619）：OutputLimiter 头+尾各半行粒度环形（\n 是 ASCII 切点零劈半 GBK）+ 入库 truncateToolResult 改头尾 8000×2（所有工具统一头尾口径，长命令尾部结论可见——W-6 缺陷修根）；qa C4/T3
  - G3 错误归一化（41b0cf5）：m_errorKind（interrupted/timeout/not_found/permission/launch_failed/non_zero_exit）+ JSON errorKind 字段（双语文案识别，原始 stderr 保留）；qa C6/D8
  - 步骤 5 argv 化（b24dd01）：CLFExecSpec.m_argv 双模（qargs 转义）+ CLFSubprocessRunner argv 门面 + git 三条 argv 化 + ver 定案 RtlGetVersion 原语（nullDevice 需求消解——冲突 A 销号）；qa C7/C8/C9 + S1/S2
  - 实施记录已回填命令执行层 §十六（第二波列）
- **待办**：① 第三波（平台层收敛步骤 0-7——注：步骤 5 缩减为销号、E 类 SIGKILL 已由第一波补）② 第四波（B3 / W-usage / quiesce 回归）③ 第二波实机验收（长输出尾部结论可见 / not_found 归一化 / 系统提示 OS·git 信息正常）④ 文案强化（待用户拍板）

### ▶ 启动横幅 Logo 改版 ✅✅ 全闭环（2026-09-22 用户已发布 v0.8.3）
- **背景**：用户与 flash 完成前期调研，产出三份文档（决策简报 / 设计稿讨论稿 / 审查报告——5 致命 + 11 中等 + 8 轻微）；pro 任主审 + 执行官
- **主审亲验**：审查报告全部关键断言逐条实读证实——F0 折行口径（charWidth 多字节恒 2 → 块字 58 字符算 110 列，80 列终端必被劈）+ F2（bold(cyan(x)) 不丢 bold，flush 先于 applyCode；真失效形态 = 外层包装内拼接多段，根因在生成侧）+ F0b（行首 `|` 被表格检测吞噬）+ F3（版本读取两份三处差异）+ qa 钉子（charWidth(0xE2)==2 刻意保留）全部属实
- **主审增量判断（审查报告高估影响面的修正）**：`CLFSelectionModel::displayWidth/substrByWidth` 是纯转发（CLFSelectionModel.cpp:16-22）→ 路线 (b) 可**零钉子破坏**实施——不动 charWidth/displayWidth/substrByWidth 本体，新增 renderDisplayWidth/renderSubstrByWidth/wrapLines，只切 CLFReplView 折行调用点；中文零影响（宽表仍计 2）；折行点只会右移
- **拍板记录（已落入决策简报 §八 + 设计稿定稿）**：路线 **(b) 修折行口径**（前置 = 步骤 0 块字字体实机验证，M11 歧义宽度若命中则降级 a）；Q3 立书写约定不修 parser；Q4 保留 skills；Q5 版本访问器归 CLFConfigLoader 三态契约（不存在→"unknown"/打不开→""/成功→首行，两调用点输出逐字保持，is_open 缺陷自然消失）；Q7 保留 ⎿；M8 路径行保持"进程启动目录"语义；M9 probe 链接生产 parser + wrapLines 三用；顺手批修 hpp:43 错误注释
- **文档状态**：决策简报（拍板记录 §八）✅ / 设计稿已重写为**定稿**（§4.5 完整行序规格 10 行、§7 步骤 0-6、§8 验收 8 项，含步骤 0 回填：块字阈值 62 + M11 未命中结论）✅ / 审查报告保持为历史依据
- **步骤 0 实机验证 ✅（2026-09-22 用户 PowerShell 实跑）**：probe 全部断言通过——四变体无缺字、块字 1 格渲染（M11 未命中）、双序等价实证、前导空格保真、ASCII 兜底无 `|` 开头、最宽 58 ≤ 80；M5 防御实锤（重定向环境 exit=2 正确拒绝）
- **步骤 1 ✅**：CLFTextUtil 新增 renderDisplayWidth/renderSubstrByWidth/wrapLines（渲染口径，与 renderCharWidth 同表）；charWidth/displayWidth/substrByWidth 本体不动（qa 钉子全保）；顺手批修 hpp:43 错误注释（"maxW<=0 返回原串"→"首字符即返回空串"）
- **步骤 2 ✅**：CLFReplView 三处折行全部切渲染口径（主内容/pendingLine/折叠块展开，wrapLines 收敛——原三处 while 循环各重复实现收敛为单点）；Sel::displayWidth/substrByWidth 生产调用清零（转发保留，qa_CLFSelectionModel:77 断言两口径一致零破坏）
- **步骤 3 ✅**：printBanner 重构（块字 6 行 cyanLight(bold) 逐行独立着色 + tagline/版本右对齐同行（填充 clamp ≥1）+ 空行分隔 + 环境层 gray 2 行（路径=进程启动目录语义 + skills 保留）；kBlockLogoMinWidth=62 窄终端降级跳过块字；诊断/配置/模型行删除；m_output 早退守卫）
- **步骤 4 ✅**：CLFConfigLoader::readVersionFile 三态契约（不存在→"unknown"/打不开→""/成功→首行）+ main.cpp printVersion/CLFCommands cmdVersion 两调用点改造（输出逐字保持；is_open 缺陷随契约自然消失）
- **步骤 5 ✅**：qa_CLFTextUtil +3 用例（12 tests/57 asserts）+ qa_CLFAnsiParser +2 用例（双序等价 + 跨段拼接语义——Q3 约定固化）；**ctest 36/36 全绿** + --version（v0.8.2）/--help 冒烟逐字一致
- **实施期实抓（qa 静态期陷阱变体）**：qa_CLFAnsiParser 新用例最初调生产 CLFAnsi 包装 → 静态初始化期 s_enabled 恒 false → 包装退化为裸串 → 跨段用例 segs[1] 越界 Debug 弹窗 exit=3（用户实抓）。修法沿用项目惯例：qa 写**字面转义序列**（包装展开形态，既有"两段"用例注释背书）；probe 运行时验证已走真机生产链路
- **收尾 ✅（2026-09-22）**：用户实机验收通过（banner 显示正常 + 验收修订：版本号右对齐→紧随 tagline）→ probe 清理零残留 → 三份设计文档归档（`归档-启动横幅Logo-*`，设计稿补 §10 实施记录）→ CHANGELOG v0.8.3 + VERSION bump → 最终回归（ctest 36/36 + 冒烟）→ 提交 32663fe + tag v0.8.3 推送 → **用户已发布 v0.8.3（2026-09-22）**，全闭环
- **遗留说明**：v0.8.2 的 irm|iex 安装链路实机复验由用户随 v0.8.3 发布合并覆盖（修复已含于 v0.8.3）

### ▶ QA 空壳残留与安装脚本加固 ✅ 实施完成（2026-09-22，待提交推送与用户实机验收）
- **背景**：2026-09-21 用户发布自测事故——install.ps1 删安装目录时 clf_agent.log 被 14 个孤儿进程（findstr/cmd，父进程全死）锁住 → Stop 中止 → 半删残骸（config/会话历史靠 2.49MB 备份救回，哈希一致无损失）；善后发现 TEMP 43 个 clf_* 空壳目录（0 文件，跨 8/26–9/15）+ 10 个备份残留
- **设计文档**：`设计/归档/归档-空壳残留与安装脚本加固.md`（flash 出稿 → pro 复核拍板 §六 → 实施记录 §六.5；2026-09-23 归档）；判定门 A1：5 轮 ctest 36/36 全绿 + TEMP 计数 0→0（当前未复现，防御性实施）
- **核实（2 个 Explore agent + 亲验）**：qa 侧 9 条 + 脚本侧 14 条断言全部属实；**双根因机制**——① 删除失败被吞（ec 忽略 5 处）② **早退跳过清理**（PluginDomains `if (!p) return;` 早退在目录创建后清理前）；表外创建点 5 类（restore/domains/rules/git/plugin CWD 文件 + PluginManager kLogPath 从不清理）
- **问题一（F1-F5）**：新建 `src/test/CLFTestTempDir.hpp`（CLFTestTempDir/File RAII + RemoveWithRetry 1/5/20ms + 登记制哨兵 `static` 每 TU 一份 + 析构 std::exit(1)——与 boost::ut ~runner 同款机制）；CMake clf_add_test 加 include 路径；8 套件改造（AgentLoop/SessionManager/SessionFileCtx/SystemComponents/SearchContent/PluginDomains/PluginFileOps/PluginManager——工厂按值返回 + 清理行删除 + 早退零改动 RAII 自动安全 + F8/G2 CWD 文件显式 parent）
- **编译实抓**：隐式转换边界——MSVC `operator+`/`fs::u8path` 是函数模板，模板推导不做隐式转换 → `dir + "/x"`/`u8path(dir)` 报 C2676/C2672 → 模板实参位置改显式 `.string()`/`.path()`（11 处）；非模板参数隐式转换全部正常
- **哨兵自证**：TextUtil 临时注入泄漏用例 → stderr 残留 + exit 1 → ctest 红 → 还原
- **问题二（S1-S6）**：install.ps1 = 唯一权威实现（-Upgrade 开关 + 3 测试钩子 + S1 独占探测精确报告 + S2 `[System.IO.Directory]::Move` 原子改名 + S3 目标断言 + S4 备份清理收敛 + catch 回滚 + S6 uninstall 模板同名加固）；upgrade.ps1 = 薄壳（版本比较 → 拉 install → & -Upgrade）
- **实施期实抓 3 处**：① **UTF-8 BOM 教训**——PS5.1 -File 执行无 BOM UTF-8 中文脚本按 GBK 误读破坏解析（V1 全败实证），两脚本加 BOM（irm | iex 无此问题）② **S2 假设证伪**——PowerShell Move-Item 目录 = 逐项移动（锁文件时半移+抛异常）；Directory.Move 真 rename 对含锁文件目录被 Windows 拒绝 → 两层设计：S1 精确报告 + S2 改名即探测（失败即退出零破坏）③ **S6 半删实抓**——初版只报错仍半删 → uninstall 同用改名后删
- **验证**：V1-V4 + S6 全场景 **31/31 通过**（正常安装/占用三脚本零破坏/无嵌套/零残留，真实 Gitee 下载链路）+ ctest 36/36 ×2 轮 + 冒烟 exit 0 + TEMP 零残留
- **收尾 ✅（2026-09-22）**：已提交推送（8311d0c）+ 版本定稿（7203aac，CHANGELOG v0.8.2 正式段 + VERSION v0.8.2）+ **tag v0.8.2 已打推送**（用户授权）→ **用户已接手发布**（重点实测安装脚本）
- **实机验收期修复 ✅（2026-09-22 下午，用户 irm|iex 实测抓出，详见设计文档 §六.6）**：
  - **用户实抓**：`irm ... | iex` 报两条解析错误（# 行 + param 行"无法识别为 cmdlet"），脚本继续执行且 S1 占用探测**实机命中**（开着 CLFCode → ✗ CLFCode.exe 被占用 + exit 1 + 目录无损——两层防护实证）
  - **根因链**：① 我加的 UTF-8 BOM 破坏 irm|iex（iex 把 U+FEFF 顶在首行注释前 → 注释变命令）② `param()` 块在 iex 中非法（param 仅脚本文件开头合法）③ 薄壳 `& $tmp` 走 -File 语义又需要 BOM——**编码镜像约束**：irm|iex = 无 BOM + 无 param；`&` 文件执行 = 带 BOM
  - **修复**：install.ps1 去 BOM + 删 param 块（-Upgrade 开关装饰性无用）；upgrade.ps1 去 BOM + 薄壳把拉取内容**转写为带 BOM 临时文件**再执行（两条来源同处理）；uninstall.ps1 生成物走 -File 保持带 BOM
  - **复验**：V1-V4 + S6 全场景 **30/30 通过**（新增"无解析噪声"断言 + 占用零破坏断言全过）
- **tag 重指 ✅（2026-09-22，用户拍板"重指 v0.8.2"）**：修复提交 519bdc9 推送后 tag v0.8.2 重指该 commit 并强推（此前 tag 已被用户实际下载引用，重指经 AskUserQuestion 明确授权后执行）
- **收尾 ✅（2026-09-22）**：用户发布 v0.8.3（含本批修复）——irm|iex 安装链路实机复验随新版发布合并覆盖


### ▶ 命令候选面板与唯一维护 ✅（2026-09-21 全闭环，随 v0.8.1 发布）
- **用户需求**：输入 `/` 时输入框上方（确认区）列出支持命令，前缀实时过滤（`/c` → `/clear`）；用户定案：纯展示（不劫持按键）+ 空格后收起面板
- **用户核心约束**：命令列表唯一维护——触发调用与用户展示同一来源，加命令只改一处
- **设计文档**：`设计/归档/归档-命令候选面板与唯一维护.md`（用户拍板"比我考虑的要充分"）
- **取证发现**：命令清单原两处维护——注册表（CLFCommands.cpp:537-560，分发用）+ /help 硬编码第二份（:81-94），描述已漂移 3 处（/clear、/plugin、/skill）——正是用户担忧的实证
- **方案**：唯一权威源 = CLFCommandDispatcher 注册表——分发（已走 ✅）/help（改遍历注册表动态生成）/候选面板（新增 matchingCommands 前缀查询）三消费方同源；TipsBar/modeLine 引导文案属装饰非列表，有意保留
- **实施**：matchingCommands（Dispatcher）✅ / cmdHelp 动态生成 + lambda 捕获注册表（/plugin 同款注入）✅ / buildCommandHintPanel（ReplView，6 行折叠 + "… 还有 N 个"）✅ / qa_CLFCommandDispatcher 新套件 M1-M8 ✅ / CMake 注册 ✅
- **构建实抓修复 2 处**：① C2589——std::min 被 windows.h min 宏破坏 → NOMINMAX（全文件无裸宏依赖零影响）② LNK2019 缺 main → 套件末尾补 `int main() {}`（boost::ut 静态期执行惯例）
- **验证基线**：ctest 36/36 全绿 + 冒烟 exit=0；**用户实机验收通过**（面板全量/过滤/空格收起恢复/无匹配/help 同源全过）
- **收尾 ✅**：CHANGELOG v0.8.1 段 + VERSION v0.8.1 + 设计文档归档 + commit d73fb2c/tag v0.8.1 推送 → **用户已发布（2026-09-21）**，全闭环

### ▶ 阶段 2 出口 ✅✅（2026-09-21，用户实机验收通过，v0.8.0）
- **阶段 2 全部完成**：2.1 管理器骨架（v0.7.6）→ 2.2a/b/c 试点全闭环 → 2.3 三域铺开 → /plugin 状态表 UX 增强（用户提议：序号索引/三态显示/幂等精确提示）→ 插件 CMake 自管理重构（用户定调）
- **出口标准逐项达成**（分册 §六）：5 域插件全齐（fileops/command/search/web DLL + misc 并入 core）✅ / 原功能等价（ctest 35/35 + 多轮实机）✅ / 管理器验证（21 用例）✅ / main 极薄 ✅ / 阶段 3 挂载点就绪（服务表通用扩展）✅ / core/basic/UI 内建 ✅
- **5 份设计文档全部归档**（2.1/2.2a/2.2b/2.2c + 分册——归档-阶段2-*）；架构文档/README/CHANGELOG v0.8.0 段同步
- **发布 ✅（2026-09-21 用户执行）**：tag v0.8.0 重指 36be959（release.ps1 插件打包修复，三步一致）→ release.ps1 全流程（构建含 4 插件 + 打包 + Gitee/GitHub 上传）→ zip 4MB 已上传；安装测试待用户抽查（zip 解压含 plugins/ 4 DLL + /plugin list 4 插件）
- **脚本检查结论**：release.ps1 修 2 处（构建 target 加插件 + plugins 打包校验）；install/upgrade 整目录解压无需改；uninstall 整目录卸载无需改
- **新规则（memory tag-requires-user-authorization）**：打标签必须用户明确授权——用户不说打标签就只推送；补 tag 用进度文件标点微调触发新 commit（不重指已推送 tag）
- **下一步**：2.4 core 收尾（C2 对象化消费/AgentLoop 纯编排）+ 2.5 main 极薄复核 → 阶段 3（第三方集成，dsh 用例——决策门仍挂起，激活 = 用户排期）

### 【2.4/2.5 收尾 ✅ 已提交 b28b310（2026-09-21）】
- **取证结论**：2.4/2.5 的实际剩余在 2.2b/2.3 实施中已被提前消化——AgentLoop/ToolExecutor 零 manager 直接依赖（工具经装配 handler 调用时查询 + file 服务经 proxy 注入 = 纯编排达成）；main 插件装配 5 行；C2 对象阶段 1 已完成
- **本批清理**：listPlugins 死代码（2.2c UX 增强后零生产调用，语义并入 listPluginEntries）——hpp/cpp 删除 + qa P2 断言改 entries；ctest 35/35 全绿
- **阶段 2 全步骤达成**（2.1 → 2.2a/b/c → 2.3 → 2.4 → 2.5）→ 分册 §4.2 全部完成态；出口标准 §六 逐项达成
- **阶段 3 待激活**：第三方集成（dsh 用例）——决策门仍挂起，激活 = 用户排期；协议适配器多协议预留（§九）同期待排期
- **两条线排期定调（2026-09-21 用户）**：① **thinking 治理批**（设计已定案）——等 v4.1 pro 出来再定（当前问题不大）② **阶段 3/dsh**——继续挂起：dsh 当天出问题靠 CLFCode 审查改源码才解决，成熟度不足（memory dsh-decision-lean 已更新实证）

### 【2.3 铺开三域 ✅ 已提交 f35039d（2026-09-21，随阶段 2 出口发布）】
- **产出**：tools.command.dll（execute_command）/ tools.search.dll（search_content）/ tools.web.dll（web_fetch）三个生产插件；get_current_time/echo 并入 core 内建（分册允许选项——零依赖小工具迁 DLL 无收益）
- **共享化**：CLFHandlerScaffold 独立（withHandlerScaffold 单点——2.3 实抓：整文件编入插件致 FileOps 未解析符号 LNK2019）；3 个域 handler 共享文件；exitCodeMeansSuccess 迁共享（detail 转调钉子 qa 零破坏）；isWithinWorkspaceOf 归位 CLFTextUtil
- **插件壳**：CLFPlugins/Command|Search|Web 三壳（照 FileOps 模式）；command 的 cwd 校验经 host->config 取根（2.2b 校验归属模式复用）
- **CLFBuiltinTools 缩至 4 个 core 工具**（get_current_time/echo/todo_write/compress_context）
- **测试**：qa_CLFPluginDomains 6 用例（D1-D6）——**ctest 35/35 全绿** + 冒烟 exit=0；插件目录 4 个生产 DLL 全产出
- **后续插入批**（已提交）：/plugin 状态表 UX（6fcda30）+ 测试插件 POST_BUILD 残留防御（8e0b6a4）+ Release 残留清理

### 【2.2c /plugin 命令 ✅ 落码完成（2026-09-21，已提交推送 1ccb928，待用户实机验收）】
- **设计**：`设计/设计-阶段2-2.2c-plugin命令.md`（取证 + 清单 + 验收）
- **实现**：① CLFPluginManager 加 listPlugins()（名字+版本对）② CLFCommandDispatcher 构造注入 pluginManager + setBusyChecker（同 onExit 延迟绑定——asyncSubmit 是 run() 局部对象）③ cmdPlugin（list 展示 / load·unload·reload 对话中拒绝 quiesce / ✓✗ 文案）④ Repl 构造注入 manager → Dispatcher；main 传参 ⑤ /help 加条目
- **qa**：P2 扩展 listPlugins 断言；ctest 34/34 + 冒烟 exit=0
- **实机实抓修复 2 处（用户验收期）**：① 注册 lambda 参数错位（args 传 cmdName 位置 → unload 误走 list 分支）② **命令输入走异步提交致空闲自拒**——InputHandler 回车无条件 launch → m_submitting=true → 异步线程内 dispatcher 处理命令时 /plugin 的 quiesce 判定读到"自己这个提交"自拒（日志铁证 [Submit] entry → rejected）；修根：命令输入 UI 线程同步处理不 launch
- **实机验收 ✅（2026-09-21 用户全链路演示）**：/plugin list → unload ✓ → 无插件 → 模型调 list_directory/read_file 得"工具提供者不可用（插件已停用）"→ **自兜底换 execute_command + powershell 分段读取完成任务**（验证点 4 完美实证；模型还从上下文学会了加 chcp 65001 前缀）
- **收尾 ✅**：**2.2 试点出口达成**（分册验证点 1-6 全部通过）；三份 2.2 设计文档归档（归档-阶段2-2.2a/b/c）；分册 §4.1 步骤与验证点更新完成态
- **下一步**：2.3 铺开其余 4 域（command/search/web/misc 逐一迁 DLL——execute_command 迁出后 GBK 包装/exitCodeMeansSuccess 随域走；search/web 零 core 依赖 §3.7 取证；misc 或并入 core 内建）

### 【插入批：execute_command GBK 输出炸 JSON ✅ 双层修根（2026-09-21，已提交推送 33c19f5，待用户实机复验）】
- **用户实抓**（五子棋项目实机会话）：`execute_command(dir /b & ... & git status ...)` 报 `[json.exception.type_error.316] invalid UTF-8 byte at index 2: 0xB2`
- **根因链**：中文 Windows 下 `dir` 输出 GBK、`git status` 输出 UTF-8——**混合字节流**；捕获层已有 CLFEncoding::toUtf8（CP_ACP 单次转换）但混合流 MB_ERR_INVALID_CHARS 整体失败 → 原样返回 GBK → nlohmann json 赋值非法 UTF-8 抛 316；且 toUtf8 对纯 UTF-8 输入可能误转（UTF-8 中文字节在 CP936 下部分可解析 → 乱码，历史合并遗留）
- **修根（两层）**：① **源头 UTF-8 化**——executeCommand 命令包装 `chcp 65001 >nul & `（子进程输出即 UTF-8；输出不留痕、& 保证原命令照常执行）② **toUtf8 预检**——合法 UTF-8 原样返回（防 CP_ACP 误转），非 UTF-8 才走 CP936 转换
- **测试**：qa_CLFWebFetch W4a-c（UTF-8 原样/GBK 转 UTF-8/空串 ASCII）；ctest 34/34 + 冒烟 exit=0
- **待办**：用户实机复验（dir + 混合命令）→ 继续 2.2c

### 【2.2b 注册表装配与主程序切换 ✅ 落码完成（2026-09-21，用户拍板裁决①-⑤全采纳，未提交）】
- **设计**：`设计/设计-阶段2-2.2b-注册表装配与主程序切换.md`（§八 步骤 1-8 + 裁决 5 项 + §十 实施记录）
- **产出**：主程序已切换插件路径——① registerPluginTools（元数据装配 + handler 捕获 manager 调用时查询——不缓存服务指针 §1.2 落地，卸载后调用走兜底错误文本 = 验证点 4 自然实现）② CLFFileServiceProxy（§1.6 转发代理落地，CLFCore——proxy 依赖 manager 分层定案）③ main 装配链（manager 先行 → loadAll → proxy 注入 → 双注册）④ CLFBuiltinTools 删 4 注册段（缩为 5 工具 + 2 core）
- **read_file 校验归属定案（2.2a 硬约束兑现）**：保持 handler 层校验（S2-1）——CLFConfigLoader 加 s_allowAbsoluteRead 静态缓存 + CLFHostApiImpl::config 真实现（宿主级键 workspace_root/allow_absolute_read + 插件配置文件 config/plugins/<pluginId>.json 复合键缓存）；插件 init 经 config 取根，校验语义与静态路径一致（qa F8 补"工作区外路径拒绝"断言实证）
- **降级语义**：插件不可用 → fileops 4 工具不注册（模型不可见，无静态 fallback 双注册）；卸载后调用 → 兜底错误 → 模型自兜底
- **编译实抓修正 5 处**：ICLFFileService 非 const / hpp 前向声明命名空间 / CLFLogger using / AgentLoop 构造第 4 参 / qa OpenSSL 传递依赖
- **测试**：qa_CLFPluginFileOps 17 用例（F1-F12 + G1 装配注册/G2 装配 handler 往返/G3 卸载兜底/G4 proxy 转发/G5 proxy 停用兜底）；**ctest 34/34** + 冒烟 exit=0
- **待办**：提交推送 → **用户实机验收**（文件操作全族 + diff 预览 + TOCTOU + 确认流 + 卸载兜底）→ 2.2c /plugin 命令

### 【2.2a FileOps 迁 DLL 试点 ✅ 落码完成（2026-09-21，用户拍板裁决①-⑤全采纳，未提交）】
- **设计**：`设计/设计-阶段2-2.2a-FileOps迁DLL试点.md`（§七 步骤 1-6 + 裁决 5 项 + §九 实施记录）
- **产出**：tools.fileops.dll（`bin/Debug/plugins/`）——FileOps/Diff 能力 + file 服务（ICLFFileService 回调推送）+ tool.provider（4 工具元数据与 handler 随域打包）；插件壳 `src/CLFPlugins/FileOps/CLFFileOpsPlugin.cpp`（CLFPlugin + CLFFileServiceImpl + ICLFToolProvider 多继承三合一，CLFFileServiceImpl 零重写直接复用——C1 铺路兑现）
- **共享实现**：`CLFCapabilities/FileOps/CLFFileOpsHandlers.hpp/.cpp`（4 handler 双消费者）；isWithinWorkspace 两参版保留为转发钉子（qa 零破坏）+ 参数化版 isWithinWorkspaceOf 新增；sliceLines 归位 CLFTextUtil；withHandlerScaffold 迁能力域（CLFBuiltinTools using 引入）；read_file workspaceRoot 参数化（CLFBuiltinTools 传 ConfigLoader 值零行为变化；插件暂传空串跳过——**2.2b 切换前必须定案校验归属**，倾向宿主侧与 SecurityPolicy 同层）
- **构建期实抓修复**：clf_add_plugin 模板 /MD 硬设缺陷（LNK2038 运行时库不匹配——2.2a 首个链静态库插件暴露）→ 删硬设、跟随全局配置（2.1 §4.1 差异回写）
- **测试**：qa_CLFPluginFileOps 12 用例（F1-F12：加载/路由/readFile 回调/previewEdit/computeDiff/getFileInfo/元数据同值/4 工具 callTool 往返/卸载）全绿；**全量 ctest 34/34** + 主程序 --version 冒烟 exit=0（宿主行为零变化——静态注册未动）
- **qa 实抓修正 2 处**：F4 ctx 类型错配（ContentCollector 当 ErrorCollector）；cb.onError = cb.onResult 不可行（两参/三参签名不同）
- **待办**：提交推送 → 2.2b 注册表装配（管理器收集元数据 → 装配 CLFTool → AgentLoop 注册；转发代理切换 §1.6；read_file 校验归属定案）

### 【插入批：任务清单进行中标识 agent 层兜底 ✅（2026-09-21，用户实机验收 2.1 时发现，未提交）】
- **用户报告**：清单 1-5 按序执行，1 2 完成变绿，但 3 在执行中无"进行中"标识，执行完直接变绿
- **取证链（全链路实证）**：渲染层支持 in_progress（⏳ 青色，面板每帧常驻渲染 ✓）→ update 分支支持三态（校验/落库/快照/面板重现 ✓）→ 工具描述只列枚举值无行为指令 → 系统提示词零 todo 指导 → **历史会话 jsonl 中 in_progress 从未出现**——模型从没发过该状态
- **根因**：模型自然行为"默默执行、完成才写 completed"——数据里没有 in_progress，渲染无从显示
- **用户定调**：**不教模型、不干扰模型（"不要主动教大模型你要干嘛干嘛"），agent 层面兜底**
- **修复（显示层推断，不伪造数据）**：`buildTodoPanelLines`——模型报告了 in_progress → 以模型为准；未报告 → **第一个 pending 项以 ⏳ 进行中样式显示**（未知状态不被推断）。数据保持模型原样（jsonl/上下文零影响）
- **测试**：qa_CLFTodoPanel P6 更新（正常 pending 被推断断言）+ P7（模型未报告 → 首个 pending ⏳、其余 ○）/ P8（模型报告 → 以模型为准不推断）新增；6 tests / 17 asserts 全绿；ctest 33/33 全绿
- **收尾 ✅（2026-09-21）**：用户实机复验通过（待办显示明显）；随 v0.7.6 发布

### 【插入批：拖选偏一行根因修复 ✅（2026-09-20，全链路取证闭环 + 双根因修复 + 用户双终端验收，未提交）】
- **用户报告**：外部终端（WT 中的 PowerShell 5.1）拖选复制时，实际响应行在鼠标点击起始位置的**下一行**（偏 +1 行）
- **取证闭环**（CLF_DEBUG_EVENTS 事件日志 + 临时取证小程序，均已清理）：
  - CLFCode 日志铁证：`calib in=(76,12) curoff=(1,1) lfc=(2,27) csbi=(2,27) origin=(0,0) → out=(77,13)` → hit row=16，而用户点击屏幕第 13 行实际显示 row 15——**偏 +1 实证**
  - `curoff=(1,1)` = CPR 从未响应（取证小程序同证：CPR 查询两次超时）→ 组件层 y = raw − 1 恰好正确；自校准又 +1 破坏
  - 用户环境 bufRows=30=viewRows=30（ConPTY 特征）、stdin/stdout=CHAR（ConDrv 字符设备，WT 下同样成立）
- **根因 1（行偏移）**：v0.7.4 自校准公式基准混用——origin = csbi(0基) − LastFrameCursor(0基) 是 **0 基**，SGR raw 是 **1 基**，raw − origin 恒偏 +1（行、列都偏）。v0.7.2 及以前 CPR 相消（1基−1基）掩盖；FTXUI v7 迁移（3e5c158）后鼠标路径从 MOUSE_EVENT（0 基）改 SGR 解析（1 基）从未重新验证
- **修复 1**：origin 计算 +1 统一为 1 基（与 SGR raw 同基准），公式幂等性保持（CPR 有无响应都正确）；**JediTerm 补偿 5→4**（origin +1 已相当于多减 1 行，补偿减 1 保持净效果 = v0.7.4 实测对齐——首版我加成 6 算错方向致 JediTerm 选区不响应，用户当场抓出，改 4 后用户验收通过）
- **根因 2（列偏移，修复 1 后暴露）**：含 ⎿(U+23BF)/●/❯ 等"非宽多字节符号"的行点击列偏移 1——`charWidth` 对多字节恒计 2，而 FTXUI 布局按 wcwidth 区间表（g_full_width_characters）渲染 ⎿ 为 **1 宽**。修复 1 前 out_x 偏右 +1 与宽度差 −1 碰巧抵消（用户未察觉）；修复 1 后暴露（用户实测：'⎿ 配置: …' 行点击 k 命中左侧 e）
- **修复 2**：`CLFTextUtil::renderCharWidth/utf8CharLen` 新增（FTXUI 同款 116 区间 wcwidth 表 + UTF-8 解码 + 二分）；`CLFSelectionModel::colToByte/colToByteEnd` 改用渲染宽度（列→字节与布局同表）；**displayWidth/substrByWidth 保持项目规则口径不动**（❯ 计 2 的 qa 钉子语义，渲染视觉零影响——FTXUI 布局一直按宽表）；charWidth 单字节转发删除
- **测试**：qa_CLFTextUtil +3 用例（renderCharWidth 宽表/utf8CharLen/charWidth 口径不变钉子）+ qa_CLFSelectionModel S1d（⎿ 行列换算）；ctest 31/31 全绿
- **用户验收 ✅（2026-09-20）**：外部终端（行对齐 + ⎿ 行正反向列对齐）+ JediTerm（拖选对齐）双通过
- **教训**：① 取证闭环三步（事件日志 → 校准中间值 → 根因锁定）避免纸面猜 ② 基准混用（0 基/1 基）是坐标类 bug 的头号来源 ③ 补偿换算要算净效果方向，不能想当然 ±1
- **待办**：CHANGELOG 未发布段 + 提交（用户定：不着急，等滚轮跨视口选区需求一起）

### 【插入批：跨视口拖选复制（拖选期间滚轮滚动扩展选区）✅ 全闭环（2026-09-20，用户双终端验收通过，未提交）】
- **用户需求**：滚动显示区复制——当前视口内容可拖选复制，但视口外内容无法一边拖选一边滚轮滚动来连续选中（终端原生拖选同样受限——终端缓冲=视口，程序侧无法扩展，应用内拖选+滚轮是唯一路径）
- **实现**：
  - `CLFScrollView`：抽 `recalcWindow()`（clamp + 可见区间重算，update 与 handleEvent 共用）——滚动事件后可见区间**立即生效**（原实现等下一帧 update，拖选期间随即 hitTest 需要新值）
  - `CLFInputHandler` 选区态滚轮分支：原"放行到滚动段"改为**滚动 + 用滚轮事件坐标 hitTest + extendTo**——滚轮事件带鼠标坐标，滚动后鼠标下方内容即新游标，选区跟随滚动连续扩展（跨视口）；松手复制路径不变（Range 行号为全局行号，天然跨视口）
- **顺带根因修复（qa 取证发现）**：`CLFScrollView::handleEvent` 的 Home/End 分支**从未被触发过**——qa 环境（boost::ut 静态初始化期执行测试）里 `Event::Home` 等静态常量尚未初始化（input 为空），空==空致 Home 误匹配 PageUp 分支（±15 行而非到顶）；实机主程序初始化正常不受影响。修法：键盘事件比较改 **input 字符串比较**（`"\x1B[5~"`/`"\x1B[6~"`/`"\x1B[H"`/`"\x1B[F"`，与 FTXUI event.cpp 定义一致），无静态对象依赖——生产与测试同码更可靠；qa 键盘事件同步改 `Event::Special` 显式构造（qa_CLFSelectionModel S1a/S1b 两处同步）
- **测试**：新建 qa_CLFScrollView 套件（W1 滚轮后可见区间立即更新 / W2 clamp 与提示行 / W3 键盘翻页 Home/End）；ctest 32/32 全绿
- **用户验收 ✅（2026-09-20）**：外部终端 + CLion 双环境通过（跨视口拖选 + 滚轮连续扩展 + 松手复制 + Home/End 无回归）
- **收尾 ✅**：已提交推送（a6a0527，14 文件，不打标签——版本号暂定 v0.7.5 待后续批次定）

### 【插入批：拖选边缘自动滚动（记事本效果）✅ 代码完成（2026-09-20，待用户实机验收，未提交）】
- **用户需求**：拖选时鼠标拖出显示区边缘自动滚动内容、选区随之扩展——不借助滚轮（记事本式）；上边缘 = 拖出终端窗口（frame 填满视口，出窗后事件停更、最后位置保持贴顶值）
- **实现**：
  - `CLFScrollView`：`stepScroll(bool up)`（±3 行与滚轮同语义 + recalc，handleEvent 滚轮分支收敛复用）+ `bottomHintCount()/contentHeight()`（内容区总高口径 = 可见行 + 上下提示行）
  - `CLFReplView`：`aboveContent(y<=0)/belowContent(y>=contentHeight)/autoScrollStep` 边缘判定与步进转发
  - `CLFInputHandler`：选区态跟踪最后鼠标位置（m_dragLastX/Y，出窗后事件停更）+ `dragTickEvent()`（Special "\x1B[DT"）tick 分支——贴顶且**有拖动动作**（m_dragMoved，防单击首行误滚）→ 上滚+扩展；越内容区底 → 下滚+扩展；滚轮/Pressed/Moved 更新位置；startAt 启动 / clear·Esc·松手 停止
  - `CLFRepl`：`std::atomic<bool> m_dragAutoScroll` + `setDragAutoScroll` 开关；50ms CLFPeriodicTimer 常开、仅选区激活期 PostEvent(dragTickEvent)（空闲零渲染开销）
- **测试**：qa_CLFScrollView W4（stepScroll 步进 + 内容区高度/上下提示行口径 + 顶部 clamp）；ctest 32/32 全绿
- **首轮验收发现 + 修复 ✅（2026-09-20）**：CLion 自上往下翻动正常、**自下而上不触发**——根因：边缘判定未过校准，JediTerm 组件层 y 含行号偏移（≈4），贴顶时 m_dragLastY≈4>0 使 aboveContent(y<=0) 永不成立（向下判定因偏移提前触发反而"有翻动"）。修法：hitTest 校准段抽为 `CLFReplView::calibratePoint`（Windows 自校准 + JediTerm 补偿，2026-09-09 引入 2026-09-20 复用），aboveContent/belowContent 判定前先校准——边缘判定与 hitTest 同坐标系
- **用户复验 ✅（2026-09-20）**：双终端通过（自下而上/自上而下边缘滚动 + 外部终端无回归）
- **收尾 ✅**：已提交推送（095a80b，11 文件）

### 【插入批：回合 token 用量双数显示 ✅ 代码完成（2026-09-20，待用户实机验收，未提交）】
- **背景**：用户观察 summary 行 "· 3.5k tok" 快速增长至几十 k，质疑"一个 read 花了 3.5k"是否显示错误——取证确认：该数 = **会话累计**（m_totalTokensUsed，P2-4 既定口径），delta 才是单轮消耗（DeepSeek 每轮携带全量上下文的 prompt_tokens，几十 k 累计是真实账单）
- **用户定案（方案 A）**：summary 行双数——"本轮 X tok"（该回合实际消耗）+ "累计 Y tok"（会话总计保留），一行内更直观
- **实现**：
  - `ToolStats` 加 `turnTokens`（本轮累计；CLFTypes.hpp）
  - `CLFAgentLoop`：`m_turnStartTokens` 回合开头快照（runTurn，m_lastToolStats 重置处）；R3 累计点同步填 `turnTokens = 累计 − 快照`（回合内多次 API 实时增长）；`getLastTurnTokens()` 测试 getter
  - `CLFToolExecutor` progressSummary：`· 本轮 X tok · 累计 Y tok`（totalTokens==0 时两数同规则省略）
  - 边界：触顶 wrapUp 在 summary 显示之后累加 → 计入下一轮（与累计生命周期一致，注释钉死）
- **测试**：qa_CLFAgentLoop T10d 跨回合用例补断言（一轮两次 API Σ=220=累计/本轮；第二轮无 usage → 本轮 0 快照清零语义）；ctest 32/32 全绿
- **用户实机验证 ✅（2026-09-20）**：多工具回合数据自洽（本轮实时增长 = 每批工具循环的 API 增量之和，最后一行 = 回合总消耗；累计 = 回合前 + 本轮）——四行同回合的误读澄清，逻辑无误

### 【插入批：工具摘要直显执行详情 ✅ 全闭环（2026-09-20，用户验收通过"舒服、直观"，随 v0.7.5 发布）】
- **用户诉求**：summary 行 "(ctrl+t to expand)" 是失效文案（Ctrl+T 已改作切换思考过程，InputHandler:249——展开功能早被覆盖）；行间两个空行浪费——不如直接显示该轮读了哪些文件
- **实现**：
  - `CLFToolExecutor::execute`：循环内收集详情行（与 summary 同条件——渐进模式才有 summary；`↳` 弱化前缀，成功行仅文件名、失败行带 ✗ 原因——rd 在 try 块内不可见，用 toolOk/toolResultText）；summary 拼接时追加（单批上限 3 行 + "… 还有 N 个工具"折叠防刷屏）
  - 删 "(ctrl+t to expand)" 失效文案；块间空行 "\n \n"×2 → "\n "×1（视觉收敛）
- **测试**：既有 32/32 全绿（E 系列未开渐进模式无 summary 断言点；实机验收为主）
- **用户验收 ✅（2026-09-20）**："很好，舒服，直观，虽然信息比前面多了，但是看得到干嘛了"
- **收尾**：随 v0.7.5 发布（token 双数批同批）

### ▶ v0.7.5 发布批次 ✅✅（2026-09-20，tag 已打推送，**用户已发布**，全闭环）
- **批次内容**（v0.7.4 之后全部工作）：① 拖选复制行/列偏移根因修复（origin 1 基统一 + JediTerm 补偿 5→4）② 符号行列定位（renderCharWidth 与 FTXUI 布局同表）③ 跨视口拖选（滚轮连续选择）④ 拖选边缘自动滚动（记事本式）⑤ 回合 token 双数显示（本轮/累计）⑥ 工具摘要直显执行详情 ⑦ 测试构建体系模块化（v0.7.4 后首批）⑧ CLFScrollView Home/End 潜伏修复 + boost::ut 静态初始化 qa 教训
- **验证基线**：ctest 32/32 全绿；用户实机验收——外部终端 + CLion 双环境（拖选对齐/跨视口/边缘滚动/双数显示/详情行）全通过
- **发布状态 ✅**：CHANGELOG v0.7.5 正式段 + VERSION v0.7.5 → commit 1999039 + tag v0.7.5 推送 → **用户已发布（Release 编译 + release.ps1 打包 + Gitee release 上传）**

### 【阶段 2 / 2.1 插件 ABI 与管理器骨架设计 ✅ 已验收定案（2026-09-10 flash 编写 / 09-11 用户拍板），待开工落码】
- **背景**：DeepSeek 官方公告（2026-09-10）——V4.1 Flash 发布、**V4 Pro 9/14 12:00 下线**（请求全部路由到 V4.1 Flash）。阶段 2 文档 §3.1②/§4.1/§八 三处排期写的是"由 pro 细化签名 / pro 执行 / 谷价时段路由 pro"——**排期落空**，改由 4.1 Flash 承接（用户 2026-09-10 拍板"可以，标明是给阶段2使用的"）
- **产出**：`.claude/plans/设计/设计-阶段2-2.1-插件ABI与管理器骨架.md`（8 节：ABI 头完整代码草案 / 回流修正 4 项 / 管理器骨架 / CMake 插件模板 / 实施步骤 8 步 / 测试计划 12 用例 / 待裁决 4 项 / 上游差异回写清单）
- **关键取证（决定设计难度的发现）**：9 个待迁工具 handler 依赖面极干净——`CLFBuiltinTools.cpp:439-441`（按值捕获单 bool，注释"无生命周期约束"）、`:458/:506/:582/:593` 等零捕获静态函数；唯一捕获 `agent` 的 todo_write/compress_context 本就留 core（§3.6 定案）→ **跨边界 CLFToolCallbacks 可极简（onResult + onError）**；宿主侧唯一调用点 `CLFToolExecutor.cpp:593` 一句同步赋值 → 适配成本近零
- **补齐上游留白（直接进入实施）**：CLFService 基类 / CLFServiceEntry 服务表 / CLFToolCallbacks / CLFToolFlags 位集 / CLFLogLevelCode（static_assert 对齐 CLFDiffOpCode 先例）/ CLF_PLUGIN_API_VERSION 精确匹配 / **禁 RTTI**（services() 显式服务表替代 dynamic_cast——跨编译器可靠性）/ 插件完整实现样板 / 加载状态机 / 卸载顺序陷阱（Destroy 必先于 FreeLibrary）
- **裁决 4 项 ✅ 已定案（2026-09-11 用户拍板全采纳）**：① `ICLFFileService` 加 CLFService 基类（仅 CLFFileService.hpp 1 文件 ≤3 行）② 插件配置 TOML→JSON（零新依赖）③ `requires()` 填服务名（SDP）④ 插件目录 `bin/$<CONFIG>/plugins/`
- **评审补强 ✅（2026-09-11，pro 评审 + 已写入正文）**：4 项裁决均建议采纳（§七 评审注记）；6 个补充设计点入正文——导出宏 `CLF_PLUGIN_EXPORT`（双编译链对称，§1.2/§1.5）、`clf_plugin_api` INTERFACE target（§4.0）、多提供方歧义规则（扫描序首个，§3.2）、reload 失败语义（§3.3）、config() 按 key 缓存 map（§3.4）、变体插件定名 badinit/badabi（§4.2）+ typo 修复（bin/Day→bin/Debug）
- **二轮现状全量核实 ✅（2026-09-11）**：9 项代码断言逐条实证通过（ToolExecutor:593 / BuiltinTools:439-441 / CLFTypes:124 / 枚举序 0-3 / GetModuleFileNameW:141 等行号分毫不差）；实质修正 1 处——修正①影响面精确化为"仅 CLFFileService.hpp 1 文件"（Impl 单继承链间接继承基类，声明无需改；qa 零影响）；§4.0 CMake 变量对齐项目惯例
- **三轮 flash 审查 + 四轮 pro 拍板 ✅（2026-09-11）**：flash 7 项修订全部采纳（getService 去 pluginId 单参服务名 / 多继承 CLFService 子对象约束 / 文件名字典序排序 / 插件禁文件级非平凡静态对象 / 服务指针禁跨 unload 缓存 / SEH 加固可选 / P13-P16 测试 + 变体插件清单）；pro 补齐 3 处一致性（§3.3 查询多提供方语义、§3.4 m_configCache 改 map、§1.2 example 宏）+ 宿主长期持有指针与热卸载交互列入 2.2a 评估
- **后续**：按 §五 步骤 1-8 落码 → 2.2a FileOps DLL 试点（上游 §4.1 步骤 3）
- **五轮用户评审修订 ✅（2026-09-21，四点落地，仍待开工落码）**：用户提两问——"ABI 还需再分析防设计出错" + "UI 是否插件化"，pro 全文档复查（2.1/分册/总纲/阶段3 四文档 + C1 头实读）后四点落地：
  - ① **同步契约显式化**（2.1 §1.2/§1.3 头注释 + CLFFileService.hpp 注释）：回调必须在调用返回前完成、ctx 调用栈持有；异步能力以新接口表达（**新增服务接口 ≠ ABI 版本变更**）——防阶段 3 集成插件异步补调回调致悬空 ctx（IPC 往返本质异步，插件作者自然写法即踩坑）
  - ② **宿主长期持有服务指针定案倾向 = 进程内转发代理**（2.1 新增 §1.6）——CLFFileServiceImpl 改造为转发代理、每次调用经 getService 查询；否决"unload 前通知宿主刷新"（与插件自治 D5 矛盾）；分册 §4.1 "core 零改动"修正为"core 仅装配点小改"（ToolExecutor/AgentLoop 构造注入形状不变、类零改动）
  - ③ **管理器析构语义显式化**（2.1 §3.1）——先卸载全部插件再释放 host；成员默认析构逆序会致插件 shutdown 时 host 已死 + DLL 句柄泄漏
  - ④ **阶段 3 分册 §七补两条待细化**——IPC 客户端超时责任（工具 ABI 同步契约下外部进程挂死不得拖死宿主）/ UI 扩展点缺口（ask_user 类交互经新接口或版本 +1 演进）
  - **UI 插件化分析结论（用户问题 2）**：维持不迁定案（阶段 2 §七③）成立，补全两层论证——含义①（可替换 UI 实现）无需求且 A1 后渲染/输入状态（friend 访问 Repl、拖选校准、IME 光标）不可 ABI 化，C3 的 ICLFOutput 四窄接口已留"进程内接口化"路线；含义②（UI 扩展点）当前零需求（确认流按 risk 宿主侧触发、进度走状态行、dsh 审批在 IPC 侧），唯一缺口 ask_user 类交互留版本演进路径
  - 附带：CLFFileService.hpp:55 旧双参 getService 注释同步修正（§八 差异清单已记）；2.1 §七 加五轮评审修订块
- **六轮测试计划审查 ✅（2026-09-21，用户定调"保护设计兜底"，阶段 2 开工前）**：P1-P16 对照 ABI/管理器功能面逐项核对（覆盖矩阵：核心机制大部分已覆盖、层次结构完整）。落地补丁：
  - **2 个必修盲区**：P17 析构自动卸载（钉五轮修订③析构语义——teststub shutdown 调 host->apiVersion() 作析构顺序检测锚点，顺序错当场崩）+ P18 依赖满足正向拓扑（P11 缺失/P15 环反向已有、正向缺失；新增 depA/depB 变体——阶段 3 集成插件大概率声明依赖，此机制不能只有反向测试）
  - **5 个便宜补充**：P1 扩 getService 未命中 nullptr / P5 扩回调判空义务（cb=nullptr + 全 null 函数指针）/ P10 拆 P10a 缺符号 + P10b 坏 PE（LoadLibraryW 失败是独立代码路径；新增 nosym 变体 target）/ P12 扩 config() nullptr 骨架断言 / qa_CLFCapabilities 加 static_assert(is_base_of_v<CLFService, ICLFFileService>) 编译期钉子（随步骤 3 实施）
  - **1 个设计语义补定义**：§3.1 重复加载（load 已加载 → false + warn 幂等拒绝；loadAll 跳过已加载；unload 未加载 → false + warn），P19 钉之
  - 变体插件清单合计 11 个 target（主 teststub + 10 变体）；§五 步骤 3 验证列加静态断言、步骤 7 套件数表述修正；2.1 §七 加六轮审查块
- **七轮实施落码 ✅（2026-09-21，§五 步骤 1-8 全部完成，未提交）**：ctest 33/33 全绿（qa_CLFPluginManager 20 tests / 97 asserts）+ 主程序 --version 冒烟 exit=0。实施中发现并修正（2.1 §七 七轮已入档）：
  - **① requires 改名 requiresServices**——`requires` 是 C++20 关键字，ABI 头被 C++20 消费者（qa 即 C++20）编译直接失败，**构建期抓出**（测试插件比生产插件先编译 = 测试保护设计的直接收益）
  - **② 两个管理器实现 bug（qa 实抓根因修复）**：a) init 成功分支漏 m_plugins.push_back（记录随 pending 析构 → P2 names[0] 越界 abort）b) init 循环 move 后未从 pending erase（下一轮 find_if 空 unique_ptr 解引用 → P14 双插件段错误——此前用例均单插件从未暴露）
  - **③ 服务注册时机前移**：批量注册（init 循环后）→ init 成功即注册（P18 实抓：depA 的 init 里 getService 查不到提供方服务，拓扑序语义要求依赖方 init 时可用）
  - **④ P7/P5 断言修订**：删除"实例地址变化"断言（Windows 同路径 DLL 重载常映射相同地址，实抓）；P5 判空跳过回调时 content 保持旧值
  - **⑤ 系统错误弹窗抑制**：loadDll 加 SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)——用户实测 P10b 垃圾文件触发 Windows"损坏的映像"弹窗（临时目录路径实抓），CLI 程序不能被系统弹窗卡住；加载后立即恢复
  - **⑥ 文档回写完成**：2.1（requires 改名全同步 + §五 机制表述 + §六 P5/P7 + §七 七轮块 + §八 差异行）+ 分册（旧草案签名回写：getService 单参、依赖服务名注释）；CLF_TEST_TARGETS 过时表述修正为 clf_add_test
  - **调试插曲**：exit=3 零输出 + 段错误两连击（静态期 boost::ut + Debug 越界断言 + 空指针），fprintf 探针逐层收敛定位（P1→P2→析构→expect→listPluginNames size=0→init 循环）——sed 跨行探针清理留残片两次，教训：探针清理用 Edit 逐段删除
  - **收尾 ✅（2026-09-21）**：用户实机验收通过（deepseek 模型无问题、进度显示完整、待办显示明显——todo 兜底同批验收）；CHANGELOG v0.7.6 段 + VERSION bump + 架构文档/README 增量同步 + TEMP 测试残留清理（36 项）；**已提交推送 + tag v0.7.6**
  - **发布策略定调（2026-09-21，用户）**：阶段 2 期间**暂停打包发布**（不打 zip/不做 Gitee release 上传），等阶段 2 全部完成再统一打包——tag 照常打（memory release-workflow 已更新）
  - **并发提醒**：CLion 构建与命令行 ninja 构建撞车致 obj Permission denied（瞬时冲突非代码问题）——命令行构建期间不要在 IDE 里同时 Build
  - **下一步**：2.2a FileOps 迁 DLL 试点（分册 §4.1 验证点 1-6，§1.6 转发代理定案倾向落地）

### 【模型名同步小批 ⏳ 待用户定夺（2026-09-10 发现，未开工）】
- **背景**：V4.1 Flash 发布后新调用名 `deepseek-flash`（用户已手动改 `config/agent_settings.json`，未提交）
- **待改（纯文档，零代码风险）**：`config/README.md:41-42/98/111-112`（默认值、"Pro 正式版发布后建议切回"已失效、示例）、`doc/api_interface.md:25-26`
- **附带发现（待定夺 a/b/c）**：`thinking_level` 是**死配置**——`CLFTypes.hpp:93` 定义 + `CLFConfigLoader.cpp:66` 映射，但 `CLFProtocolAdapter::buildChatRequest`（:37-89）从未发送它。选项：(a) 接线实现 (b) 标注预留未实现 (c) 删除
  - **设计文档已出 + 评审补强 ✅（2026-09-11）**：`设计/设计-thinking配置接线与死配置治理.md`（方案 A/B/C/D 分步可独立排期）。补充取证实锤：① buildChatRequest 生产调用点仅 2 处（主循环 :162 + wrapUp :419），摘要独立构造 body（CLFSessionSummarizer.cpp:97-101，其 temperature=0.0 同样落在官方"thinking 模式无效"规则内——确定性意图落空）② **dsh 官方实现把 reasoning_content 回传当规则实现**（serialize.ts:233 + spec 名 "official passback rule"）→ 方案 C 从"待验证"升级为"直接实现"③ 默认值 "max" 接线后全员成本上升 → 待定案（建议空串/`high`）④ V4 Pro 9/14 下线 → sub_model 原意图失去承担者，b) 删除权重上升
  - **二轮现状全量核实 ✅（2026-09-11）**：全部行号断言实证通过（m_thinkingLevel 三处、§2.1 表 11 行、字段全集、serializeMessage :184-218、CLFMessage 无 reasoning 等）。新发现入正文：① **README:61 对 thinking_level 的描述本身就是错误文档**（值域含不存在的 `off` 档 + "仅 pro 支持"——双重误导，用户最初追问的源头）② **README:42/:73 写明 sub_model 意图 = 轻量任务 + 摘要压缩**，而 S3-1 摘要已用主模型实现（Summarizer.cpp:98）——意图从未被采用，删除论据进一步补强
  - **三轮 flash 审查 + 四轮 pro 拍板 ✅（2026-09-11）**：flash 5 项修改全部采纳（top_p clamp WARN / 空占位规则 / 实施顺序 A→B→C→D / V4 Pro 下线新闻核实 / 主模型 flash 生命周期待查）；**默认值定案 = 空串**（不干预服务端默认，跟随官方演进）；pro 补齐 2 处一致性（空占位边界：仅 thinking 开启需要 + serializeMessage 感知开关的实现注意；§七 风险表"先验证"残留修正）
  - **✅ 五轮用户拍板定案（2026-09-11）**：实施顺序 A（含 A3 开关同批）→ B → C（独立批次）→ D 随批；默认值空串；A4 保留 `thinking_level` 主键不加别名；**sub_model 选 c) 保留并标注"预留，未接线"**（用户判断后续可能有用）；剩余 P2-8 项随批顺手处理。附带待查：主模型 `deepseek-v4-flash` 生命周期（是否同批下线 → 配置需改 `v4.1-flash`）。设计文档已定案（§八 定案记录）
- **好消息**：生产代码零硬编码模型名（全仓 `deepseek-v4-*` 仅在文档/配置/测试字面量）

### 【插入批：转圈卡顿 + 拖选校准修复 ✅（2026-09-09，不占正式进度，待收尾提交）】
- **转圈卡顿根因链（完整闭环）**：v0.7.3 卡顿非 alt screen 渲染路径，而是 **CPR 周期查询被 alt screen 附带关闭** → 转圈动画失去驱动源（转圈纯事件驱动，静止期唯一兜底 1Hz Timer → 秒跳一帧）。A/B 实证四组：primary+CPR 开=丝滑 / alt+CPR 关=卡顿 / primary+CPR 开=丝滑 / primary+CPR 关=卡顿
- **转圈修复（D4 定案）**：① 回 primary 屏幕（`FullscreenPrimaryScreen`，恢复滚动历史）② CPR 周期查询精准关闭（`TrackCursorPosition(false)`，3rdparty 新增开关——IME 抖动修复保持）③ **Timer 50ms（20Hz）显式动画驱动**（CLFAgentLoop runTurn 周期 1s→50ms + setStatusTextOnly 按秒去重）——不再依赖 CPR 意外心跳
- **拖选校准（外部终端）**：CPR 响应校准不可靠 → **Windows 自校准**：`GetConsoleScreenBufferInfo` 光标 − LastFrameCursor（3rdparty 新增 getter，Draw 帧尾 CUP 目标捕获）= frame 原点；hitTest 修正公式 `x + CursorOffsetX − origin` 与 CPR 校准幂等。首帧+resize force CPR 保留（app.cpp 条件补 frame_count==0/resized）
- **CLion(JediTerm) 拖选偏移（用户定案"接受局限"）**：取证链——查询发出零响应（CPR 无响应，首帧与稳定后均实测）→ SGR 鼠标 y 含 JediTerm 内部行号计数偏移（4~6+，每次启动 +2 增长；`\033[3J` 清屏不清计数、ConPTY 缓冲=视口、程序侧不可测）→ **默认补偿 5（用户实测定案：CLion 拖选对齐）** + `CLF_MOUSE_OFFSET_Y` 环境变量精确校准 + 仅 `TERMINAL_EMULATOR=JetBrains-JediTerm` 探测命中才补偿（外部终端零影响）。已知局限：滚动视口后需回底部
- **3rdparty patch 清单（随 v0.7.4）**：① app.cpp TrackCursorPosition 开关 + 首帧/resize force CPR 条件 ② app.cpp LastFrameCursor 捕获 + getter ③ app.hpp CursorOffsetX/Y + LastFrameCursorX/Y + RequestCursorPosition public 化 ④（沿用 f49465b 的 CUP 定位/光标稳态/placeholder 三 patch）
- **验证基线**：MSVC 构建全过 + ctest 31/31 + 冒烟 exit=0；用户实测——外部终端转圈丝滑/拖选精确/中文输入正常；CLion 转圈正常/中文输入不抖/拖选错位最小化
- **收尾待办**：用户最终验证 → CHANGELOG v0.7.4 段落 + VERSION bump 已写 → 分析文档补最终结论 → tag + 推送 → 用户发布

### 【插入批：终端光标闪烁 + CLion 中文输入抖动 ✅⏳（2026-09-08，不占正式进度）】
- **用户报告**：① PowerShell 直接运行时光标闪烁频率特别快 ② CLion 内置终端运行时看不出光标闪烁；中文输入法打字每敲一个字母界面抖动 + 输入框下面自动补出一个空行（英文正常）
- **现象 1 根因（VT 转储实证）**：FTXUI `App::Draw` 每帧输出 `?25l`（隐藏）→ 全帧重绘 → 移动真实光标到输入框 → `?25h` + `ESC[5 q`（DECSCUSR 闪烁竖线，Input 组件 insert 默认 true 走 Bar 分支）。conhost 每次 `?25h` 重置光标 blink 相位 → 闪烁节奏被帧率绑架（打字每字符一帧、turnTimer 每秒 PostEvent）→ 异常快闪。JediTerm 对 DECSCUSR 闪烁支持差异 → 看不出闪烁
- **现象 1 修复 ✅**：patch 3rdparty input.cpp——`focusCursorBarBlinking/BlockBlinking` → 稳态 `Bar/Block`（DECSCUSR `[6 q`/`[2 q`），光标表现与帧率解耦；IME 组合窗口定位逻辑（?25h + 光标移动）不变。选 Bar 形状原因：块状光标与 FTXUI 反色块双重反转会互相抵消（光标不可见），竖线叠加在反色块上可见。MSVC 构建 16/16 + ctest 31/31 全绿
- **用户实测反馈 1**：闪烁大幅缓和但仍有抖动——"打第一个字还未确认到输入框时，❯ 前面有 ❯ 残影疯狂抖动"（输入框空 = placeholder 状态时 IME 组合窗口渲染在 ❯ 上）
- **事件日志取证（用户 CLion 终端执行）**：中文上屏正常（`Char '你' '是' '谁'` 逐个到达，onChar 全 0=PassThrough）；**组合期间零事件**（拼音字母不到达程序）；**无 Return 误入** → 候选根因 (a) pasteCoalescer 插入 \n **证伪**——"补空行"= 终端层 IME 组合窗口渲染错位，非程序状态
- **根因 2（placeholder 光标错位）**：input.cpp placeholder 分支 `focused` 装饰整个 placeholder 文本 → 焦点 box 落在 "❯" 处 → 真实光标 + IME 组合窗口定位在 ❯ 上，与每帧重绘冲突（❯ 残影抖动，PowerShell 同样存在）
- **根因 3（相对定位偏差）**：FTXUI 帧间光标往返全用相对移动（\r + ESC[nA/nB/nC/nD），依赖"右下角→home→右下角"状态链与 conhost 的 autowrap 行为一致；JediTerm 执行偏差一行 → 真实光标错位到输入框下一行 → IME 组合窗口显示在下一行（"补空行"）
- **修复 2+3 ✅**：① input.cpp placeholder 分支——focused 移到 placeholder 末尾空格 cell（光标/组合窗口定位到 "❯ " 之后 = 输入首字符位置）② app.cpp Draw()——帧尾光标移动改 **CUP 绝对定位**（`ESC[y;xH`，帧尾→input 焦点、帧头逆序列→右下角 CUP，ResetPosition 的"起点=右下角"假设不变；dimx!=terminal.dimx 的 +1 hack 随相对定位删除）。VT 转储实证：帧尾 `[22;3H ?25h [6 q`（22 行 3 列 = placeholder 末尾）。ctest 31/31
- **用户实机验证 2**：PowerShell 正常 ✅（❯ 残影消失 + 稳态光标）；CLion 仍有空行+抖动 → 加做 **alt-screen 实验版**（FullscreenPrimaryScreen → Fullscreen，一行改动）
- **alt-screen 实验结果（用户定案）✅**：CLion 里**抖动消失**（界面稳定）、空白预留行仍存在（组合期间输入框下空行 + 内容下移、上屏恢复）——用户明确"可以接受"。机理取证：primary 下 FTXUI 每 500ms 发 CPR 光标查询（ThrottledRequest，app.cpp:182-212），响应与 IME 组合渲染在 ConPTY 流内交错 = 抖动源；alt screen 下 Draw 的 `!use_alternative_screen_` 条件跳过 CPR 查询 → 稳定。空白预留行 = conhost ConPTY 对 TUI 的 IME 组合渲染固有行为（所有 TUI 程序受影响，Claude Code 日语输入崩坏同源——Web 取证），程序侧无法控制
- **收尾 ✅（2026-09-08）**：alt screen 转正式（CLFRepl.cpp 注释定案：Fullscreen + 行为变化说明"退出后屏幕恢复，对话不在滚动历史"）；CHANGELOG 未发布段落（修复 2 条 + 优化 1 条含已知残留与行为变化）；ctest 31/31 全绿。**⚠ 待用户最终验证 PowerShell 下 alt screen 表现**（IME 组合、光标、退出恢复、滚动历史缺失是否可接受）；残留：CLion 组合期空白预留行（终端链固有，记录为已知问题）
- **发布 ✅（2026-09-08）**：用户最终验证通过（PowerShell alt screen 表现正常、行为变化接受）；CHANGELOG 转 v0.7.3 正式段落；VERSION bump v0.7.3；commit f49465b（6 文件，不含鸿蒙文档删除——用户独立操作未混入）；tag v0.7.3 已打推送 Gitee；**待用户执行发布**（Release 编译 + release.ps1 打包 + Gitee release 上传 zip）
- 本批 3rdparty patch 清单（随 CHANGELOG 记录）：① input.cpp 光标形状 Blinking→稳态 ② input.cpp placeholder 焦点位置 ③ app.cpp 帧尾光标 CUP 绝对定位
- 验证手段（已备）：stdout 重定向跑 exe 捕获 VT 流（`printf 'h\x1b\x1b' | ./CLFCode.exe > dump`）——已实证每帧 ?25l/?25h/DECSCUSR/CUP 序列

### 【v0.7.2 收尾 ✅（2026-09-08，UI 配色/拖选批次全闭环，待用户发布）】
- **批次内容**（v0.7.1 之后全部工作）：① 输入行视觉强化（❯ 深青锚点 + 内容浅青 + 时间戳灰三级层次 + 多行逐行着色 + resume 回显同步 + ● CLFCode: 整段青色）② **ANSI 颜色三层根因修复**（enable 零调用 → FTXUI 丢控制字符 → emitContent 剥转义；渲染层 CLFAnsiParser 分段着色 + SGR 白名单保留 + isSgrSequence）③ **拖选修复两轮**（v3.1 选区坐标 clean 空间收敛 + 双值语义修复自下而上首字符丢失 + S8 回归钉子）④ 协议多协议预留（阶段 2 §九）
- **用户实机验收**：颜色显示正常（❯ 青/时间戳灰/banner 原设计色首次生效/● CLFCode 全青）、复制粘贴无乱码、自上而下正常；发现并修复：自下而上首字符丢失、多行输入仅首行着色
- **用户定案不动项**：progressSummary 行（● thought for…）着色保持现状（历史无颜色设计，有显示即可）
- **测试基线**：ctest 31/31（新增 qa_CLFAnsiParser 9 用例 + qa_CLFTextUtil 6 用例 + qa_CLFSessionUsage 6 用例）+ 冒烟 exit=0
- **收尾**：CHANGELOG v0.7.2 段落（修复 2 + 优化 1）；VERSION bump v0.7.2；tag v0.7.2 已打待用户发布
- **用户工作流提示**：用户后续用 release 模式测试（Debug exe 被运行占用会 LNK1168 阻断构建）

### 【ANSI 颜色修复 v3：emitContent 剥离根因 ✅（2026-09-08，qa 31/31 过；主程序 exe 待用户关闭占用后链接+冒烟）】
- **用户排除 exe 版本假设**：删除 debug/release + cmake 缓存全量重编译运行——问题依旧；日志确认 18:35 启动过（doc/log/clf_agent.log）
- **第三层根因（决定性）**：`CLFTerminal::emitContent`（CLFTerminal.cpp:108-125）**在内容存储时就剥离一切 ANSI 转义**（m_inAnsiSeq 状态机：\033 起、字母止全丢）——v1 的 enable 让转义生成后**在此被剥**（v1 无效）；v2 的渲染层解析器拿到的行**转义早已被剥**（v2 空转）。转圈/绿● 正常因不经 CLFTerminal 内容流。三层根因链闭合：① enable 零调用 ② FTXUI 丢控制字符 ③ emitContent 剥转义
- **修复 v3**：① CLFTerminal 剥离状态机 → **SGR 白名单保留**（m_ansiBuf 成员缓冲跨调用——流式 chunk 边界安全；`CLFAnsiParser::isSgrSequence` 判定 \033[...m；OSC 标题/光标定位等仍剥，模型输出防御保持）② CLFAnsiParser 加公共 `isSgrSequence` ③ 渲染层 v2 机制不变（buildStyledLine 分段着色）
- **测试**：qa_CLFAnsiParser +isSgrSequence 用例（SGR 正例/OSC/清屏 CSI/截断/空）。qa 31/31 过；**⚠ 主程序 CLFCode.exe 被用户运行占用 LNK1168 未链接——待用户关闭后构建+冒烟再提交**
- 教训（记死）：三处转义相关逻辑（CLFAnsi 门控/FTXUI 丢弃/emitContent 剥离）各挡一层——排查时必须全链路每环节确认数据形态，不能只看一端

### 【ANSI 颜色修复 v2：渲染层解析 ✅（2026-09-08，ctest 31/31 + 冒烟 exit=0，待实机看颜色）】
- **用户实测反馈**：enable 修复后仍白色（❯/● CLFCode: 无色），且转圈动画（蓝）与成功绿 ● 正常——两路颜色机制差异暴露第二层根因
- **第二层根因（FTXUI 源码取证）**：`ftxui::text()` 的 `Utf8ToGlyphs`（3rdparty/ftxui/src/ftxui/screen/string.cpp:1402-1405）**直接丢弃控制字符**（`if (IsControl(codepoint)) continue;`）——字符串内嵌 ANSI 转义在 FTXUI 下是死路（ESC 被吞、参数字节残留乱码）；转圈/绿 ● 正常因走 ftxui::color 装饰器
- **修复 v2（改机制）**：**渲染层解析**——新建 `CLFAnsiParser`（CLFUI：SGR 状态机 parse→样式分段 + strip；支持 0/1/22/30-37/39/90-97，非 SGR 序列按普通字符保留不吞内容）；CLFReplView `buildStyledLine` 分段 → ftxui::color/bold 装饰器（与状态点/modeLine 同机制）；选区行先 strip 再三段高亮（字节偏移与显示宽对齐，拖选临时态无颜色）；enable 调用保留（s_enabled 门控转义生成）
- **测试**：qa_CLFAnsiParser 新套件 8 用例（直通/单段青/两段嵌套/四段实际形态/red/不完整防御/空串/strip）。踩坑：空格在灰转义前累积 → 4 段非 3 段（测试期望修正，渲染无视觉差异）。ctest 31/31
- CHANGELOG 未发布段"修复"条目更新；**待用户实机看颜色（这次与转圈同机制，应生效）**

### 【ANSI 颜色从未生效根因修复 ✅（2026-09-08，ctest 30/30 + 冒烟 exit=0，待实机看颜色）】
- **现象**：用户发现 ❯ 无青色、● CLFCode: 无青色——且问题"很久了"
- **根因链（三层取证）**：① `CLFAnsi::s_enabled` 默认 false，`enable()` 靠 SetConsoleMode(VT_PROCESSING) 置位 ② `CLFTerminal::enableAnsi()`（唯一转发点）**声明后全仓零调用** → s_enabled 恒 false ③ cyan/bold/gray/red 包装恒返回原字符串（空操作）——**从第一天起内置颜色就没生效过**。FTXUI 装饰器颜色正常（状态点四态等）是因为 FTXUI 自己开 VT（WindowsEmulateVT100Terminal），与 CLFAnsi 无关
- **修复**：CLFRepl::run() 开头调 `CLFTerminal::enableAnsi()`（UI 启动单点）；**配套修复**：CLFTextUtil displayWidth/substrByWidth **ANSI 转义感知**（skipAnsiEscape 匿名命名空间 helper）——enable 后转义真实进内容流，宽度计算不跳过会硬换行提前/选区坐标错位（既有潜伏小瑕疵转真问题）
- **踩坑**：① 首版 skipAnsiEscape i 停在终止字节——displayWidth 的 for ++i 越过 OK 但 substrByWidth 的 while 无自增 → 终止字节被多计 1 宽（qa 当场抓住）→ i 统一推进到终止字节之后 + displayWidth 改 while 式 ② 测试期望两次写错（❯ 按项目规则计 2 宽；"\033[36x" 的 x 是合法终止字节，防御用例改无终止字节截断序列）
- **测试**：新建 qa_CLFTextUtil 套件（6 用例：纯文本回归/SGR 跳过/嵌套转义形态/不完整转义防御/substrByWidth 转义含入/CJK 不劈半）——CLFTextUtil 首次独立套件；CMake 注册（clf_types + CLF_TEST_TARGETS）
- CHANGELOG 未发布段已补（修复 + 优化两条）；待用户实机看颜色效果

### 【输入行视觉强化 ✅（2026-09-08，ctest 29/29 + 冒烟 exit=0，待实机看效果）】
- **需求**：用户想更容易区分输入/输出——现状 "> " 前缀不明显，多轮长输出翻页易漏输入内容
- **方案（用户三选一定案）**：青色加粗 `❯` 前缀（与 AI 侧"● CLFCode:"青色标签对称成视觉锚点）+ 时间戳转 gray 低调。备选：整行底色块 / 块状标签行
- **实施**：CLFRepl.cpp 实时回显（cyan(bold("❯ ")) + bold(input) + gray(时间戳)）+ CLFCommands.cpp resume 回显同步同款（补 CLFTerminal.hpp include）。取证：既有"● CLFCode:"青色标签同模式（CLFAnsi 转义直塞 emitContent 由终端解释）→ 零新风险；qa 无 "> " 文案断言
- **已知小瑕疵（既有，不修）**：displayWidth 不跳过 ANSI 转义——含转义行宽度虚高导致硬换行点略提前（"● CLFCode:"行既有同问题）；潜在优化：displayWidth 转义感知
- CHANGELOG 未发布段已补；待用户实机看效果后随下一版发布

### 【缓存命中率显示 v2 需求变更落地 ✅（2026-09-08 下午，ctest 29/29 + 冒烟 exit=0）】
- **用户需求变更**："底部常亮显示，不补在对话后面，统计这一轮会话的值，类似 dsh，作为当前会话的缓存命中率参数"
- **v2 方案**：① 显示 = **modeLine 常亮参数行**（模型名│📁目录│🔒安全模式同列，安全模式后加"⚡缓存命中 X%"dim 段，无数据 emptyElement 零占用；ReplView 每帧渲染读 getSessionUsage()）② 口径 = **会话累计**（与 m_totalTokensUsed 同生命周期——全仓无 reset 点、/clear 不清；R3 gate 累计）
- **v1 回退**：appendCacheHitLine helper + finishTurn/触顶两处调用删除；对话流零缓存文案（不污染上下文问题在 v2 架构下消失）
- **类改造**：CLFTurnUsage → **CLFSessionUsage**（reset/displayLine 删除，percentText 替代；文件/测试/CMake 同步重命名）
- **测试重写**：qa_CLFSessionUsage 新建(6)；qa_CLFAgentLoop T10d 系列改为会话累计断言（流式 100%/floor 98/零命中 gate/无 usage gate/两轮 Σ 75/跨回合不清零+对话流与请求 body 无缓存文案/触顶 wrapUp 不计入 75）；T10b 中断不累计补断言
- **验证**：MSVC 构建 42/42；ctest 29/29 全绿；冒烟 exit=0。设计文档重写为 v2 终版（§九 变更历史）
- **用户实机验收 ✅（2026-09-08）**：第一轮"查看项目进度"显示 60%，连续对话（脚本测试 AI 强度）后升至 92%——"目前看合理，而且效率挺高"
- **v2.1 用户微调 ✅（2026-09-08）**：① 位置移到 📁工作目录后、🔒安全模式前 ② 真实零命中显示 "0%"（无 usage 数据仍不显示——统计未发生不估猜）；gate 改 prompt>0；测试同步（qa_CLFSessionUsage 零命中断言 "0"、T10d-3 改 "0"）
- **v2.2 灵魂拷问修复 ✅（2026-09-08，用户提问："换大模型后缓存命中率还有效吗"）**：发现缺陷——第三方 provider（usage 有、缓存字段无）会误显示 0%（"无法统计"≠"零命中"）。修根：解析层加 `m_hasCacheField` 标志（CLFAssistantResponse + StreamAccumulator 两处解析置位 + reset 归零 + 流式落地拷贝）；AgentLoop 累计 gate 改 `if (parsed.m_hasCacheField)`。测试 +4（qa_CLFProtocolAdapter 第三方标志 false 用例 + 两用例标志断言；qa_CLFStreamAccumulator 标志保持/归零；qa_CLFAgentLoop 第三方不显示用例）。设计文档 §3.5 固化 Provider 兼容矩阵（DeepSeek 全系 ✅ / OpenAI 官方 ✅ / 中转站 ⚠️ 同步有效流式无 usage / Anthropic ❌ 协议层整体不兼容 / OpenAI 兼容第三方 ➖ 不显示）。ctest 29/29 + 冒烟 exit=0
- **协议适配器多协议预留 ✅ 记录（2026-09-08，用户定调："预留设计，适配器插件未设计，后续按需开发对应接口插件协议"）**：写入阶段 2 设计文档 §九（新增节）+ §七 开放项表加 ⑩ 行。要点：现状取证（单协议硬编码/isUsageStreamSupported host 白名单/AgentLoop 持具体类值成员）；未来接入形态（优先阶段 3 插件形态，兜底 core 内 ICLFProtocolAdapter 接口化——C2-3 缓做评估在多协议时代需重估）；协议选择机制预留（host 扩表/provider 配置字段）；缓存字段归一预留（hasCacheField 语义沿用）；**阶段 2 不埋雷硬约束**（clf_plugin_api 头不暴露协议细节/工具 handler 与协议适配器术语分离/接口不按 OpenAI 字段设计/单协议行为不变）
- **收尾 ✅（2026-09-08，用户实机验证通过后）**：设计文档归档（`设计/归档/归档-缓存命中率显示.md`）；CHANGELOG 未发布段 → v0.7.1 正式段落；VERSION bump v0.7.1；tag v0.7.1 已打待用户发布。用户后续自行观察长上下文压缩/截断兜底/clear 等场景命中率（已定：不计入进度）

### 【缓存命中率显示 实施完成 ✅（2026-09-08，ctest 29/29 + 冒烟 exit=0）】
- **生产代码**：CLFStreamAccumulator.hpp（+m_usageCacheHit/getter/feedUsage 双拼写解析/reset）；CLFProtocolAdapter.hpp/.cpp（CLFAssistantResponse 加字段 + 同步双拼写解析）；**CLFTurnUsage.hpp 新建**（CLFTypes/，header-only：reset/accumulate/displayLine，gate + floor 防虚报 + clamp 100%）；CLFAgentLoop.hpp/.cpp（成员 m_turnUsage + runTurn reset + 流式落地 + R3 gate 累计 + 私有 helper `appendCacheHitLine` 双通道发射 + finishTurn 与触顶路径两处调用）
- **测试**：新建 qa_CLFTurnUsage（7 用例：reset/基本/100%/floor 99%/异常 clamp/gate/Σ 累计）+ qa_CLFStreamAccumulator +3（双拼写/缺失保持/reset）+ qa_CLFProtocolAdapter +2（双拼写/缺失）+ qa_CLFAgentLoop +7（T10d 系列：100% 流式/floor/零命中 gate/无 usage gate/回合累计 75%/非流式不污染上下文（mock 加 lastBodies 记录）/触顶路径）+ T10b 中断补断言。CMake：qa_CLFTurnUsage 目标（clf_types 链 + CLF_TEST_TARGETS 列表）
- **验证**：MSVC 构建 47/47（警告均既有）；ctest 29/29 全绿（含新套件）；主程序 --version 冒烟 exit=0
- **待办**：实机观测（真实 DeepSeek 连续同前缀请求第二轮收尾"✳ 缓存命中 X%"，非验收阻塞）→ 设计文档归档（设计/归档/）+ CHANGELOG 未发布段落已补

### 【缓存命中率显示 设计定稿 ✅（2026-09-08，待排期实施）】
- **设计文档**：`设计/设计-缓存命中率显示.md`（写到具体实现步骤级别，5 步 + 测试计划）
- **用户裁决 5 项**：MVP 回合显示（无 /context 累计）、新显示区域、jsonl 不进、中性文案"缓存命中 X%"、分母待 pro 确认
- **pro 取证裁决 2 项**：① 分母口径——dsh 上游取证（`llm-deepseek/types.ts:164`：`prompt_tokens = prompt_cache_hit_tokens + prompt_cache_miss_tokens`，**含命中部分**，分母用 prompt_tokens）② 新显示区域落点——**回合收尾统计行**（finishTurn 内 ✻ worked 行之后；流式 emitContent 直发 / 非流式进返回值尾部且必须在 `addMessage` 之后——不污染模型上下文）
- **回合口径 pro 定案**：回合累计 ΣcacheHit/Σprompt（runTurn 入口 reset，R3 gate 内累计，多工具回合覆盖全部 API 调用）
- **关键取证**：全链路行号实读（StreamAccumulator:133-142 / ProtocolAdapter:146-152 / AgentLoop 落地:254-257 + R3:294-299 + finishTurn:458-501 / Repl 非流式通道:290-293）；qa mock 设施已核（MockHttpClient sync/stream 双队列 + MockOutput contents，T10 系列可扩展）
- **双拼写归一**（dsh translate.ts:56 同构）：`prompt_tokens_details.cached_tokens` 优先，`prompt_cache_hit_tokens` 兜底
- **防虚报**：cacheHit>=prompt → 100%，否则 floor 永不四舍五入；gate prompt>0 且 cacheHit>0 才显示（零命中不宣称 0%）
- **不动项**：ToolExecutor:691 progressSummary 行（tok 是会话累计语义，不动）、ToolStats、jsonl 格式、/context、配置零新增
- **实施范围**：CLFStreamAccumulator.hpp + CLFProtocolAdapter.hpp/.cpp + CLFAgentLoop.hpp/.cpp + 测试三套件扩展（+11 用例）
- **SOLID 复查 ✅（2026-09-08 用户要求，文档 §八 已附复查表）**：① 影响面小（捕获层纯增量）② **修订——累计+文案抽独立类 `CLFTurnUsage`**（初稿放 AgentLoop 与 C2 拆角色方向/P1-1 god 类治理背道而驰；CLFTypes/CLFTurnUsage.hpp 新文件，header-only 零 CMake 源改动；捕获层刻意留原类——CCP 同簇）③ 插件化零冲突（阶段 2 §七定案② core 不迁 DLL；④"适配器并入工具域"指工具 handler 非 CLFProtocolAdapter；CLFAssistantResponse 不跨 clf_plugin_api 边界）④ 新发现边界：触顶收尾请求（wrapUp）不走 R3 累计点（既有行为）→ 回合累计=主循环各轮，不修正。测试计划改四套件：新建 qa_CLFTurnUsage（6 用例，须入 CLF_TEST_TARGETS）+ 扩展三套件
- **flash 设计审查 ✅ 采纳（4 处全合理）**：① 文案 MVP 形态注记（UI 变化时抽离显示层）② **口径注记（实质发现）**——DeepSeek 每轮 prompt_tokens 含共享历史非增量，Σ 求和分母重复计权重叠历史 = 回合平均口径（≠ dsh 单轮命中率）；末轮命中率留扩展路径（CLFTurnUsage 增方法即可）③ 既有行为注记（worked 入上下文，缓存行 addMessage 后追加避让）④ 同步落地补句（pro 复核精确化：:291 直接赋值 → 无需额外行）。另修小节引用笔误 2 处
- **pro 终检 ✅（2026-09-08）**：**发现并修正文档错误断言**——初稿"触顶路径共用 finishTurn 三出口"系错误（:384-440 触顶是独立收尾段，不走 finishTurn）→ 修正：触顶路径单独接线（:437 后），双通道逻辑抽 AgentLoop 私有 helper `appendCacheHitLine`（finishTurn 与触顶两处各一行调用）；触顶回合缓存收益是长回合最有价值观测场景，与 worked 行一致性对齐；触顶 finalContent 本不进上下文（:419 仅 wrapUp 单独 addMessage）零污染。测试 +1 触顶用例（共 +18）
- **进入实施**：设计定稿，按 §四 步骤 1-5 实施

### 【插入批：测试注册 CMake 模块化重构 ✅（2026-09-15，已提交推送 + 已归档，全闭环）】
- **设计文档**：`设计/设计-测试注册CMake模块化重构.md`（用户新增，定稿待实施）。实施前逐条取证通过：31 测试等价表与 `src/CMakeLists.txt:111-272` 行号分毫不差；构建目录 CTestTestfile 无 WORKING_DIRECTORY 实证（默认 = `${CMAKE_BINARY_DIR}/src`）；`CLF_TEST_TARGETS` 全仓仅一处定义使用、无 CI 配置、release.ps1 只建 CLFCode target
- **实施三改**：① 顶层 `CMakeLists.txt`——`option(CLF_BUILD_TESTS ... ON)` + `enable_testing()` 收进条件 ② `src/CMakeLists.txt`——测试段 162 行删除（272→110 行）+ 末尾 `if(CLF_BUILD_TESTS) add_subdirectory(test) endif()` ③ 新建 `src/test/CMakeLists.txt`（`clf_add_test` 函数一行一测 + 31 行注册 + `WORKING_DIRECTORY` 显式固定回 `cmake-build-debug/src`——消除注册位置迁移引起的唯一行为差异点）
- **验证全过**：ctest -N = 31（名字零变化）→ 31/31 全绿 6.68s（基线 6.53s）→ 工作目录抽查 = `cmake-build-debug/src` → **OFF 独立目录构建**（136 target 主程序正常 + `qa_` 零 target + Total Tests: 0）→ `--version` exit=0；临时目录 `cmake-build-off` 已清理
- **新增经验（memory 已更新 msvc-manual-env）**：全新目录 configure 需显式传 4 个工具链路径（cl/ninja/rc/mt），缺一依次报 `CMAKE_CXX_COMPILER not set` → `unable to find Ninja` → `RC Pass 1 no such file`；已有 cache 目录重 configure 只需手动 env 三件套
- **收尾**：~~commit~~ ✅（c48ba5f 已推送，不打标签）→ ~~设计文档归档~~ ✅（2026-09-15 移入 `设计/归档/归档-测试注册CMake模块化重构.md`，头部标完成时间与归档原因）→ **⚠ 版本号 v0.7.5 暂定**（用户 2026-09-15：下个版本更新可能很大，版本号可能调整——CHANGELOG 段已标"未发布（暂定 v0.7.5）"，VERSION 暂留 v0.7.5 供构建）；tag/发布由用户后续执行
- **附注**：OFF 构建与主构建共用 `bin/Debug` 输出目录（项目固有），CLFCode.exe 被同内容覆盖，零影响；阶段 2 插件测试（clf_plugin_teststub 等）届时归 `src/test/CMakeLists.txt` 管理（设计 §九.1）

### ▶ 下次开工指引（2026-09-11 设计定案时更新，从这里接着干）
- **基线**：`v0.7.4`（转圈卡顿根因修复 + 拖选自校准 + CLion 终端适配；tag 已打待用户发布；ctest 31/31 + 冒烟 exit=0）
- **当前状态**：两份设计文档已验收定案——阶段 2 的 2.1（插件 ABI 与管理器骨架）+ thinking 配置接线治理；sub_model 定案保留标注
- **下一步（两线可选，按排期）**：
  - **线 1 = 阶段 2 开工**：2.1 CLFPluginManager 骨架落码（`设计-阶段2-2.1` 已定案，§五 步骤 1-8，含 clf_plugin_api 头扩展 + CMake 模板 + qa 16 用例）；随后 2.2a tools.fileops.dll 试点（C1 接口化已铺路，core 零改动承诺待验证）
  - **线 2 = thinking 治理批**：按 A（接线+值域+开关 A3）→ B（参数条件下发）→ C（reasoning 回传，独立批次）→ D 随批（`设计-thinking配置接线` §八 定案记录）
- **阶段 2 新增约束**：协议适配器多协议**预留不实施**（§九 预留 + §七⑩——不埋雷硬约束：clf_plugin_api 不暴露协议细节、术语分离、接口不按 OpenAI 字段设计）
- **构建环境**（memory：msvc-manual-env）：export INCLUDE/LIB（MSVC 14.51.36231 + D:/Windows Kits/10/Include/10.0.26100.0 系列）；ninja = `D:/Program Files/JetBrains/CLion 2026.1.1/bin/ninja/win/x64/ninja.exe`；构建目录 cmake-build-debug
- **⚠ 遗留**：C2-3 接口化（ProtocolAdapter 等）缓做记录在案（C3 注记；多协议时代需重估——阶段 2 §九）；「首次运行崩溃修复」长期观察未闭环（progress 长期观察区）
- **阶段 2 出口后**：试点 FileOps 迁 DLL（tools.fileops.dll）——C1 接口化后 core 零改动承诺待验证


### 【插件化与集成三阶段】阶段 1 评审·取证·修订 ✅（2026-09-03 晚，谷价时段；方案待排期执行）
- **【文档一致性审计】（2026-09-03 晚，用户定调：防执行期引用混乱）**：全套文档交叉引用逐条核对。修复：总纲章节重编号（两个"五、"→五~八）+ 评审记录 63→69 条 + §一 补边界清单 + §三 阶段 2 出口标准同步（"全部模块插件化"→能力层+工具层）+ §七 索引明细化；分册 16 处（P2-2 denied 4→2、§2.3 遗留 JSON 表述、9→10 handler、时间戳 6→7 处、截断 7+9 精确化、825-845、§七 专项验证与 A2 单安全版对齐、总体方案 §六→§七 引用、B1 遗留项删除、B3 依赖解除、适配器层加阶段 2 联动注）；边界清单 6 处（F1-F5 悬空引用、§五 冒烟清单→T5、A1 行号 3 处、A2-2 补 .cpp、A2 标题补覆盖项）；阶段 2 分册 2 处（"1-2 模块"→FileOps 域试点）；README 重写对齐实际目录（原树列了 5 个不存在的文件、缺 40+ 实际文件）
- **总纲**：`设计/设计-总体方案-插件化与集成.md`（阶段 1/2/3 三分册 + 边界清单配套）。上游决策已拍板 7 条（DLL 热拔插/两个世界/各自适配/三阶段顺序/自治兜底/安装模型/试点 dsh）
- **本轮工作（pro 评审取证）**：分册源码断言逐条对照实读（5 大文件 + hpp/CMake 全读，grep 交叉验证）——边界清单 69 条销号表全部核实：8 条证伪修订、61 条证实/定案
- **证伪修订 8 处**：① denied 回显 4→2 处 ② P1-6 死代码精确化（findIncomplete/removeAllIncomplete/promote 零引用可删；save 测试引用 7 处保留测试设施；migrateLegacyIncomplete main:162 生产调用保留；新增 Repl::saveSession/AgentLoop::saveSession 死壳）③ P1-8 行号补 .cpp ④ A2 UTF-8 截断**单安全版**（取证：库中无精确字节场景，原双函数方案作废）⑤ A3 范围定案 ⑥ B1 **m_risk 复用**（Write/Command 已可替代 2 类名字匹配，仅新增 m_isSearch/m_isRead 统计标签；executor 匹配点全集 7 处，原列 516/542 证伪）⑦ B2 收敛为**单新方法 closeSessionAndReset()**（cmdClear 无确认交互，B2-5 原断言证伪；原 4 方法大多已存在）⑧ B3 **m_todoPanelDone 语义定案**（回合级展示生命周期，接口保留+C2 随 CLFTodoStore 迁移）
- **新发现**：ToolExecutor readCount/progressReads 口径不一致（list_directory 只入后者）；m_escPending/m_escTime/m_escTimer + getThinkingLines/hasThinkingContent 死代码；main.cpp:139-140 重复注释
- **阶段 1 批次**：A3（死代码热身）→ A1（Repl 拆分）→ A2（字符工具）→ A4（handler 脚手架）→ B5 → B1 → B2/B4 → B3 → C1-C4；出口 = P0/P1 清零 + 无 CMake hack + 回归全绿
- **【阶段 2 论证深化】（2026-09-03 晚，用户定调：目标在功能、方案论证详细了再动手）**：调用链空白打通 + ABI 细化 + 开放项 9 项全定案（详 `设计-阶段2-自身插件化设计方案.md` 论证定案注记）。核心定案：basic/core/UI 不迁 DLL；5 能力域 DLL（fileops/command/search/web/misc）；9 工具随域迁 + todo_write/compress_context 自引用留 core；适配器并入工具域；C1 回流补接口化（ICLFFileService，试点 core 零改动）；试点依赖最小集 = C1（含接口化）+ B1
- **【非插件模块全量审查】（2026-09-03 晚，用户定调：功能混杂/大文件/调用执行混杂，要详细 SOLID 检查）**：30 文件全查（core 14 + UI 12 + basic 4）→ 5 需实质重组 / 8 轻度 / 17 干净。新增 P 项 8 条（P0-7 Builder 6 簇混杂+双文件级静态对象+裸 localtime、P0-8 Context sanitizeUtf8 被 UI 反向引用+容器含策略、P1-13 token 估算双实现、P1-14 execCommand 与 CommandExec 重复、P1-15 ConfigLoader 30+ if 样板、P1-16 SecurityPolicy 双簇、P2-7 Logger 窄路径、P2-8 ProtocolAdapter 2 小项）；A2 取证补漏（时间戳 7 处、截断点+2）。批次新增 B6/C5/C6 + 顺手批；执行序 A3→A1→A2→A4→B5→B1→B2/B4→B3→B6→C1-C4→C5→C6。底稿已归档 `设计/归档/归档-代码审查-非插件模块全量审查.md`（结论全回流分册）。副产品：能力层 4 文件仅依赖 basic → 5 域 DLL 拆分零障碍（验证阶段 2 §3.7）
- **【阶段 1 执行开始（2026-09-03 晚）】批次 A3 死代码清理 ✅（ctest 21/21 + 冒烟 exit=0）**：
  - 删：findIncomplete/removeAllIncomplete/promote（零引用，测试无背书注释同步清理）
  - 删：CLFRepl::saveSession + CLFAgentLoop::saveSession 死壳（grep 已核测试零引用）
  - 删：CLFContext::serialize/restore（生产零调用，qa 引用 3 处 → 删 2 个旧语义用例——损坏保护语义已由 SessionManager::load 的 .bak 承担）
  - 留：SessionManager::save 保留为测试设施（hpp/cpp 注释标记"新代码不应使用"，头部"保存模型"注释更新为 jsonl 时代）
  - 改：list 复用 header 解析——新增 readHeaderInfo helper（收敛"读首行+parse"样板，与 loadJsonl 共用 parseHeaderLine 单点）；补 J12 用例（损坏首行 jsonl → 不崩 + stem fallback）
  - 验证：MSVC 增量构建 38/38 + ctest 21/21 + --version exit=0。**环境记录**：Kits 在 D:/Windows Kits/10（非 Program Files (x86)）；ninja 实际路径 bin/ninja/win/x64/ninja.exe
- **【批次 A1 CLFRepl 拆分 ✅（2026-09-03 晚，ctest 21/21 + 冒烟 exit=0）】**：run() 两巨型闭包拆出——Renderer(178-484)+hitTest → **CLFReplView**（render/hitTest/scrollHandleEvent 转发 + scrollView 值成员）、CatchEvent(516-855) → **CLFInputHandler**（handle 全分支搬移，消费返回值逐一保真）；闭包捕获 → 构造注入引用（friend 访问 Repl 状态，成员不搬家=纯搬移纪律）；抽 **stripCprResidual** 消两处 CPR/ANSI 剥离同构；删 m_escPending/m_escTime/m_escTimer 死代码；CLFRepl.cpp 瘦壳 = run 编排+装配+生命周期收尾（A1-5 划界）。踩坑：ftxui ScreenInteractive 前向声明与 include 定义冲突（C2371）→ hpp 直接 include screen_interactive.hpp；View/Handler 缺 CommandDispatcher 头（C2027）。**遗留**：交互路径（T5 冒烟）待用户实机验收；CLFClipboard/CLFScrollView include 残留清理随顺手批
- **【批次 A2 公共字符工具 ✅（2026-09-03 晚，ctest 21/21 + 冒烟 exit=0）】**：新建 **CLFTextUtil**（basic/clf_types）：utf8SafeHead/Tail（16+ 截断点收敛，阈值语义逐处保留）、charWidth/displayWidth/substrByWidth（SelectionModel/Terminal 两套等价合并，SelectionModel 保持 API 转发零调用方改动）、splitLines、localNow/localNowTm（7 处时间戳 ifdef → 5 处收敛 + CLFTypes 2 内联封装保持 + Builder 裸 localtime 消除）、token 估算（Context/Builder 双实现统一，id/name 整数除语义保真）、replaceAll 归位。**sanitizeUtf8 归位 CLFEncoding**（Clipboard 不再依赖 Context 头——P0-8 分层泄漏消除）。**handleHttpError** 收敛 AgentLoop 私有（流式/同步 ~25 行×2 → 单实现 + HttpErrorAction 枚举三态）。删 getThinkingLines/hasThinkingContent 死代码。View pendingLine wrap → substrByWidth（R4 行为变更：CJK 换行点变化）。踩坑：sed 误伤定义行/锚点短路致 include 漏插（4 轮修复）；CLFUI/CLFTools 命名空间需 using。**遗留**：T3 视觉回归（CJK 长输入换行）待实机
- **【批次 A4a handler 脚手架收敛 ✅（2026-09-03 晚，ctest 21/21 + 冒烟 exit=0）】**：detail::withHandlerScaffold 统一 parse/try-catch/dump 骨架（原 8 处同构样板）；7 handler 改造（readFile/webFetch/writeFile/editFile/listDirectory/executeCommand/search lambda，业务与容错保留——url 必填/cwd 边界/行切片语义逐一保真）；todo_write 状态机不碰（A4-2）；search 错误文案统一 "Handler error: "（qa 无文案断言，行为变化仅错误文本）。**A4b 结果结构化推迟至 B1 后**（与 executor 改造联动，分册 A4 已注）+ P2-8 后半（ProtocolAdapter m_error 显式字段）随 A4b
- **【B 批推进（2026-09-03 晚）】**：**B5 ✅（cf4ee97）** ICLFOutput 注释修正（17 方法 10 通道 + 扩展纪律）；**B1 ✅（f6ed67d）** 能力标签（m_risk 复用 + m_isSearch/m_isRead + 口径统一；qa T5 忘打标实证——测试同步打标）；**B2/B4 ✅（d470e30）** 会话收敛（beginTurnSession/closeSessionAndReset——P0-4 关闭）+ 恢复回显外移（CLFSessionEchoLine——P0-6 关闭）+ JsonlType 常量单点（P1-7 关闭）；qa T7 测试更新（折叠块断言 → 结构化行断言）。每批 ctest 21/21 + 冒烟 exit=0
- **【B 批全部完成（2026-09-03 晚）】**：**B3 ✅ + B6 ✅（fdca1b8）** todoPanelDone 语义注释定案（回合级展示生命周期，C2 随 CLFTodoStore 迁移）+ CLFDangerousCommandDetector 拆分（P1-16 双簇分离，SecurityPolicy API 转发测试零改）。**P0 项 8 条全部清零 ✅：P0-1（A1）P0-2（B1）P0-3（A2）P0-4（B2）P0-5（B3）P0-6（B4）P0-7（C5）P0-8（A2+C2b）**
- **【C1 ✅（2026-09-07 谷时段，两提交：5a239e4 + 0707c39）】归属修正 + 接口化**：
  - **C1a 归属修正**：FileOps/Diff 四文件移入 `src/CLFCapabilities/FileOps/`（命名空间 CLF::CLFTools 保留，阶段 2 迁 DLL 再重定）；新增 clf_capabilities 目标（链 clf_types 单向）；clf_core 删 2 个 CLFTools 源直编（CMake hack 消除）+ 链 clf_capabilities；clf_tools 同链；qa_CLFFileOps 改链 capabilities。依赖图：capabilities ← core/tools 单向无环 ✓
  - **C1b 接口化**：`src/CLFPluginApi/CLFFileService.hpp`（唯一跨 DLL 共享头：CLFFileInfo POD + CLFDiffOpCode 编码 + CLFFileCallbacks 回调集 + ICLFFileService 纯虚，禁依赖项目其他头）；CLFFileServiceImpl（进程内适配：POD → 静态函数转调，枚举序 static_assert 双向钉死）；ToolExecutor 构造注入 ICLFFileService*（必需依赖置默认参数组之前——C++ 规则：默认实参后不能跟无默认参数，踩坑已记录）+ prepareWritePreview 回调接收器化（readFile/previewEdit/computeDiff 三调用点 + TOCTOU getFileInfo）+ **读失败静默行为保真**（现状忽略返回值 = 新文件语义，注释钉死）；AgentLoop 构造加 fileService 借用参数（nullptr → 默认实现兜底，试点时管理器注入，core 零改动）
  - **qa_CLFCapabilities 新套件 13 用例**（S1-S8 接口契约 + E1-E5 executor 全链路：write/edit 预览 diff 渲染 + TOCTOU 阻断 + 读失败静默）——**填补 qa_CLFToolExecutor 原 Write 工具零覆盖盲区**（C1 改写 prepareWritePreview 后此覆盖为必需）。踩坑：① 测试拼 JSON 用 `R"({"path":")" + path` 手拼——Windows 路径反斜杠成非法 JSON 转义 → parse 异常 → valid=false（改 nlohmann::json 对象构造）；② CLFSecurityMode 无 Confirm 值（写确认 = Edit 模式 L3）
  - ctest 22/22 + 冒烟 exit=0；P2-6 顺手修（BuiltinTools 过时注释）；CHANGELOG 补 C1 条目
- **【C2 ✅（2026-09-07 谷时段，两提交：5b375d2 + aa4ef3c）】AgentLoop 拆角色 + Context 收窄，P0 项全部清零**：
  - **C2a 三对象抽取（公共 API 零变化）**：**CLFTodoStore**（todos 数据+锁+面板/脏标记，锁随对象；allDoneSnapshot 收敛 finishTurn/restoreSession 两处全完成判定——B3 迁移定案落地）；**CLFSessionFileCtx**（活动文件/resumedFrom/historyDir/轮初消息数 + beginSessionFile/appendTurn 差集/appendSummaryLine/collectEchoLines——restoreSession 回显解析搬入，C2-2 落点；modelName/loadedSkills 参数传入不持 AgentLoop 引用）；**CLFSummaryCache**（生成器+缓存+频控，shouldSummarize 参数化）。AgentLoop 公共 API 全保留转发门面——todo_write/compress_context handler 与 UI 调用面零改动（BuiltinTools 零改）
  - **C2b CLFContext 收窄（P0-8 后半关闭）**：**CLFContextWindow** 策略类（窗口截断自 getMessages 原样搬移）+ **truncateToolResult 归位 CLFTextUtil**（8000 截断移出）+ CLFContext 纯容器（构造删参数、getMessages 全量、仅 sanitize 存储不变量）；AgentLoop 发 API/摘要输入前 apply；差集/轮初计数改全量语义（行为等价——截断从尾部保留，新增必在窗内）
  - **测试**：新增 qa_CLFTodoStore(6) + qa_CLFSessionFileCtx(6) + qa_CLFSummaryCache(4) + qa_CLFContextWindow(6)；qa_CLFContext 改纯容器断言。ctest 26/26 + 冒烟 exit=0。踩坑：① Windows 文件锁——ifstream 未析构就 remove_all（测试读文件作用域化）② shared_ptr 参数构造 nullptr 需显式传参
  - **C2-3 接口化缓做**（评估记录）：ProtocolAdapter/SecurityPolicy/Summarizer 接口化收益低（qa 全真实实现、无 mock 需求），C3 后按需
- **【C3 ✅（2026-09-07 谷时段，27e1fde）】ICLFOutput 拆窄四接口（ISP 落地）**：
  - 归位表（C3-1 落定）：内容 4（emitContent/emitRaw/emitStyledLine/showFoldedBlock）+ 进度状态 8（setStatus/setStatusTextOnly/setStatusKind/showProgress/finishProgress/requestRefresh/notifyActivity/activityCount）+ 交互 2（confirm/onInterrupt）+ 辅助 3（emitError/appendThinking/clearThinking）
  - ICLFOutput 收窄为**聚合接口**（仅继承四窄接口无新方法）——CLFTerminal 类声明零改动（聚合形式 = 接口层多继承，比 Terminal 多继承/组合持有更轻，C3-2 落定）
  - 签名窄化按使用清单：Passerby→Content / ThinkingIndicator→Progress / TipsBar→Progress（1 方法消费者示范）+ ToolExecutor→Content+Progress 两指针（5 方法，含 renderDiff/guard 重定向）。**保持宽（回流记录）**：AgentLoop（9 方法全通道编排）/ Repl（装配点）/ Commands（3 方法跨两通道留维护）
  - mock 分化：qa_CLFPasserby 17→4 方法、qa_CLFTipsBar 17→3 方法。踩坑：复合条件 `if (m_output && ...)` 残留（批量替换只命中 `if (m_output)` 整条件）——4 处逐行按通道重定向。ctest 26/26 + 冒烟 exit=0
- **【C4 ✅（2026-09-07 谷时段，bb01fc4）】CLFTerminal 状态封装（P1-3/P1-4 关闭）**：
  - public 状态块全量收 private（C4-1 直写点清单一处不漏：Repl 2 处 / View 2 处 / InputHandler 9 处 / ConfirmBar 4 处全收敛）
  - 新增窄操作：clearContent（启动重置）/ consumeRefreshPending（刷新消费）/ submitConfirm(accepted)（确认协议收敛，锁序 confirmMutex→mutex 嵌套与原内联一致 + cv 唤醒单点化）/ confirmSelection + cycleConfirmSelection / interruptFromUi（3 处中断收敛）
  - ConfirmBar 渲染改走 ContentSnapshot（零直读）；C4-4 纪律遵守（纯封装零接口化）
  - **⚠ 遗留（C4-3）**：确认交互实机冒烟（确认/取消/Esc/Tab 切换/中断五路径）待用户验收——ctest 无 Terminal 自动化
- **【C5 ✅（2026-09-07 谷时段，d897809）】Builder 拆分重建（P0-7 关闭 → P0 项 8 条全部清零）**：
  - 三组件落定（逻辑原样搬移）：**CLFSubprocessRunner**（popen/_popen 封装静态 run——P1-14 后半：CommandExec 迁插件后 core 的独立子进程通道）/ **CLFProjectRulesLoader**（PROJECTRULES.md→CLAUDE.md 降级 + 5000 字符 UTF-8 安全截断）/ **CLFSystemInfoProvider**（detectOsInfo/detectShellInfo 静态 + captureGitStatus 实例方法，Git TTL 30s 缓存随实例）
  - **静态对象消除**：s_gitCache → InfoProvider 实例成员、s_constitutionCache → Builder 实例成员（mtime 缓存语义不变）
  - Builder 实例化收窄（build 改实例方法，流程保真）；AgentLoop 持成员（2 处调用点）。新增 qa_CLFSystemComponents 8 用例。ctest 27/27 + 冒烟 exit=0
- **【C6 ✅（2026-09-07 谷时段，ddf768c）】ConfigLoader 表驱动（P1-15 关闭）**：
  - 26 字段映射表（constexpr POD：section/key/类型枚举/成员指针槽）替代 30+ if(contains)——新配置项 = 结构加字段 + 表加一行（2 处→1 处）
  - 7 类型枚举语义保真：stop 追加不清空 vs command_allowlist clear 先行、IntMap 逐键过滤、类型过滤保持默认
  - 新增 qa_CLFConfigLoader 5 用例（26 字段全断言 = 映射表形状钉子）。ctest 28/28 + 冒烟 exit=0
  - **阶段 1 全部 P0/P1 清零 → 出口标准达成（§八.1/2/4），§八.3 功能回归待用户实机验收**
- **待办**：阶段 1 出口验收（用户实机冒烟五路径 + C4-3 确认专项）→ 设计文档归档 → 阶段 2 开工（2.1 插件管理器）；阶段 3 分册仍标识性

> **阶段划分（以"是否开始接入 dsh"为界）**：**A 阶段 = 本体自研**（CLFCode 自己的功能）✅ **全部完成（v0.5.0 发布中）** → 🚦**决策门**（唯一问题：subagent 值不值）→ **B 阶段 = dsh 对接**（8.5-12.5 天）。
> A 阶段产出在 B 阶段**不会白做**——双后端并存，直连后端永远是降级兜底路径。

### 【A 阶段】本体自研 — ✅ 全部完成（v0.5.0，2026-09-02 tag 已打，发布由用户执行）
- ~~S1 小修~~ ✅（v0.3.5）｜~~S2 安全+工具~~ ✅（v0.4.0）｜~~【插入批】todo 面板 + jsonl 追加式保存~~ ✅｜~~S3 净增点自研~~ ✅——详见下方已完成区
- **A4**（=S4，按需穿插）：配置校验 / session 版本分流 / 宽字符 / 多会话 / `/reload` / 信号 / 并发锁 / git 工具 / list_directory 增强 / **ask_user（N 选项确认栏 — 若确定走 B 阶段，建议提前到此做，B 阶段的 dsh 确认链可复用同一套 UI）**
- **A5 工具调用循环上限机制改造 — ✅ 全流程闭环（实机验收通过，v0.6.0 tag 已打，发布由用户执行）**：设计文档已归档 `设计/归档/归档-工具调用循环上限机制改造.md`。阶段一（69ea8c7）：concludesTurn 机制 + 触顶收尾请求 + max-tokens Warn + 默认 48。阶段二（b3d8e6a）：CLFTipsBar + 活动计数 + config/tips.txt + 打包。实机验收（用户，12:50-13:13 大任务实测）：Tips 显示（反馈加 "Tips: " 前缀，ab70dc5）+ **触顶真实链路实证**（48 轮触顶 → 「继续」衔接无损 → 未完成清单 #7 原样复现续做完成 → 失败降级不逸出）+ parse_error.101 根因修复（同步传输 vs 流式请求体协议不匹配）+ A5-9 回归用例。**tips.txt 多轮打磨定稿（ab015bd，用户主笔 + pro 审查）**：12 命令全覆盖、7 CLI 参数全覆盖、顺序修正（Esc 中断→「停止过度思考」）、安装卸载 PowerShell 风格、每条一句完整描述。ctest 21/21。遗留：OpenSSL 4.0 环境收尾（见 A5 记录）

### 【B 阶段】dsh 对接 — ⏸ 决策门暂缓（2026-09-02 用户定：慎重、不着急）
- **用户态度（2026-09-02）**：进入 dsh 前先慎重考虑；dsh 当前为 **alpha 版本、不稳定，不着急**——决策门保持挂起，暂不投入 B 阶段
- **A 阶段产出完毕**：S1/S2/S3 + 插入批（todo 面板 + jsonl）全部落地并发布 v0.5.0——dsh 的净增点已收敛为**仅剩 subagent**
- **过渡期选项**（决策门挂起期间的按需工作）：A4 可选批（配置校验 / 多会话 / /reload / 信号 / 并发锁 / git 工具 / list_directory 增强 / ask_user 确认栏）；或**自研轻量 subagent**（进程内嵌套 CLFAgentLoop 2-3 天，不依赖外部后端——若用户想先要 subagent 能力，这是更稳的路线）
- **决策门复盘素材（存档备查，下次评估时直接引用）**：
  - B0 环境重建 0.5 天 → B1(=M1) 传输层 3-4 天 → B2(=M2) 会话层 2-3 天 → B3(=M3) 收尾 3-5 天 = 全程 8.5-12.5 天
  - Spike S0-S5 全过（go 决策），素材齐备：`tools/spike/`（报告 + spike_driver.mjs 五模块 + frames/norm 12 组 fixture）
  - 2026-08-25 上游核实：仓库 `E:\deepseek-harness`；上游 `b150a55`(0.1.1-rc.2)；协议面几乎未动 → fixture 仍有效；platforms.json 仍无 Windows → Node 闭包唯一路径
  - 若走 → 先做 M1（CLFJsonRpcClient 与 MCP 传输同构，价值独立于决策）；若不走 → 自研轻量 subagent

## 已完成

### 2026-09-07 阶段 1 代码审查与模块重构 ✅ 全部完成（v0.7.0 发布中，tag 已打）
- **全部批次落地（ddf768c 基线）**：A3（死代码清理）→ A1（Repl 拆分）→ A2（字符工具）→ A4（handler 脚手架）→ B5/B1/B2/B4/B3/B6（接口纪律/标签/会话收敛/回显外移/语义定案/危险命令拆分）→ **C1（能力层独立+接口化）→ C2（AgentLoop 拆角色+Context 纯容器）→ C3（ICLFOutput 四窄接口）→ C4（Terminal 封装）→ C5（Builder 拆分重建）→ C6（ConfigLoader 表驱动）**
- **P0 项 8 条 + P1 项 16 条全部清零**；ctest 21 → 28 套件全绿（新增 7 套件 49 用例）；零 CMake hack、依赖图单向无环
- **用户实机验收通过（2026-09-07）**：按常用测试路径实测——确认交互（C4 专项）、正常对话（C2/C5 路径）全部正常
- **收尾**：设计文档归档（阶段 1 分册 + 边界清单 + 功能修复批 → `设计/归档/`，总纲/README 引用同步）；CHANGELOG v0.7.0 段落；VERSION bump v0.7.0；tag 已打
- **阶段 2 前置已铺路**：C1 接口化（ICLFFileService 落 clf_plugin_api 头 + ToolExecutor 经接口调用）——试点 FileOps 迁 DLL 时 core 零改动

### 更早完成区（A 阶段本体自研 / dsh Spike / 各版本发布）——见下

### 2026-09-02 A5 工具调用循环上限机制改造 ✅（两阶段全落地，待人工验收 Tips 行；未发布——攒入下一版本）
- **设计**：flash 草案 → pro 三审定稿（`设计/设计-工具调用循环上限机制改造.md`）——三审修 9 处：伪代码结构（finalResponse 机制照草案实现会 fall 触顶路径）、T2 构造复制 vs 成功复制冲突、悬空引用×2、静默计时挂载点（流式回调→输出活动计数）、阈值可注入、触顶文案补"继续"引导、收尾 user 消息 jsonl 语义、max-tokens+tool_calls 边界、concluded 空文本边界
- **阶段一（69ea8c7）机制 A/B/C**：`CLFToolResult`/`CLFTool` 加 `m_concludesTurn`（仅 handler 成功路径复制）；`CLFAgentLoop` concluded-break（丢弃多余工具调用、协议安全）+ `finishTurn()` 收尾汇聚点（自然停/concluded 共用，max-tokens → Warn）+ `appendWorked()` helper；触顶收尾请求（同步 + 独立 try/catch 不逸出 + 失败降级保留 user 消息）+ 中性文案 `(已达工具调用上限，任务可能未完成——输入「继续」可接着做)`；默认 16→48（用户确认）
- **阶段二（b3d8e6a）机制 D Tips 行**：新类 `CLFUI/CLFTipsBar`（config/tips.txt 每行一条 + constexpr char* 内置兜底 + 5s 轮播 + 300s 静默阈值 + busy 显隐 + startTimer 参数供 qa）；`ICLFOutput` ⑨ `notifyActivity()` 活动计数（基类实现零破坏；CLFTerminal 7 个内容类 emit 入口接线——状态行/刷新刻意不计，turnTimer 每秒驱动会永不清零）；`CLFRepl` vbox statusLine 与 thinSep 之间插入；release.ps1 打包清单加 tips.txt
- **测试**：qa_CLFAgentLoop A5-1~8（concludesTurn 最终响应/丢弃收尾/触顶收尾请求 syncCallCount+1/收尾失败降级/max-tokens Warn/空文本仅 worked/length+tool_calls 继续/并行任一）+ qa_CLFToolExecutor A5-1~3（成功复制/未声明 false/失败不复制）+ qa_CLFTipsBar P1-P6 新套件——**ctest 21/21 全绿**
- **验证**：MSVC（cmake-build-debug 日常目录）构建全过 + ctest 21/21 + 主程序 --version 冒烟 exit=0（A2 教训遵守）
- **踩坑**：① ftxui::Element 是 shared_ptr<Node> 的 using 别名——前向声明 class Element 与已有定义冲突（C2371），须 include dom/node.hpp ② CLFTipsBar.cpp 漏加 clf_ui 源列表 → LNK2019 ③ 命名空间（CLFUI 内调 CLFCore 类需 using，CLFRepl.cpp 同模式）
- **实机验收反馈修复（ab70dc5，2026-09-02 下午）**：① Tips 加 "Tips: " 前缀标识（异常态 ⚠ 自带标识不加）② **触顶收尾请求 parse_error.101 根因修复**——用户实测大任务触顶时报 `[Error] JSON parse failed ... last read: 'd'`。根因链：收尾请求复用 `buildChatRequest(m_config)`，用户 `m_stream=true` → 请求体带 `"stream":true` → DeepSeek 返回 SSE 文本（首字符 'd'）→ postJson 同步读回整串解析失败。**修根**：收尾请求用 `m_stream=false` 副本配置（同步传输须配非流式请求体）。补 A5-9 回归用例（流式配置下触顶，ctest 21/21）。教训：测试盲区——原触顶用例全在 stream=false 下跑；凡涉及请求体的路径测试必须覆盖流式/同步双配置
- **实机验收全链路实证（用户 12:50-13:13 大任务实测，日志 doc/log/clf_agent.log）**：触顶（48 轮，13:07:12）→ 收尾降级正常（回合不崩溃正常结束）→ 用户"继续"（13:08:02）→ 模型从上下文直接续做 **todo #7 in_progress → completed（13:08:20）**——"触顶 = 阶段完成点"、清单未完成态复现续做、"继续"衔接无损 三项设计目标实证通过。用户定调：API 昂贵，不再安排大任务验证，本批闭环
- **tips.txt 多轮打磨（ab015bd，用户主笔 + pro 逐条核验）**：审查修 5 处——卸载路径 PowerShell 风格（%USERPROFILE% → $env:）；补 /exit；系统提示模板补位置；「停止过度思考」顺序修正（须先 Esc 中断，busy 时输入不提交）；CLI 参数 2→8 条全覆盖（--allow-write/--config/--project-root/--help 拆分一句一义）。核验通过：12 命令全真实（/skill [list|<name>]、/history 均存在）、7 参数全真实、快捷键全真实、安装升级 URL 与 README 一致。发布策略：v0.6.0 包用旧版 tips（发布时点），新版 tips 留待下版发布随包走
- **⚠ OpenSSL 4.0 环境问题（顺手修复 + 收尾待办）**：系统 OpenSSL 升至 4.0.1（`C:/Program Files/OpenSSL-Win64`），旧 httplib 弃用 API + 全局 -Werror 阻断 MinGW build/ 构建 → CMakeLists 3rdparty/OpenSSL 头改 SYSTEM 语义隔离（自身 -Werror 不动，第三方警告不归我们管辖）。**发现**：build/（MinGW）从未成功构建过（无产物），日常构建 = cmake-build-debug（MSVC）。**待办**：3rdparty/openssl/lib 的 libssl.a 版本未确认（头 4.0.1 + 旧导入库 ABI 风险）；长期应升级 3rdparty/httplib + 统一 OpenSSL 版本

### 2026-09-02 S3 净增点自研 ✅（未发布——攒入下一版本）

- **S3-1 上下文摘要**：`shouldSummarize` 阈值判定（剩余窗口 < `m_autoSummaryThreshold` 默认 4000）+ 频控（每 10 轮最多一次）+ runTurn 起始自动触发（先于任何 API 调用）：生成（同步 LLM，失败规则降级）→ **追加 jsonl summary 行落盘** → rebuildSystemMessage（摘要经 Builder 段落拼入 `{{project_context}}`——system 永不截断 + 老模板天然兼容）；`compress_context` 工具（Read，模型主动调用同路径 `compressContextNow`）；注入去重 = m_cachedSummary 单值覆盖语义
- **S3-2 /model 切换 + 多模型自适应**：`/model <name>` 运行时切换（不落盘，提示"新会话生效"）；max_tokens **按模型名查表覆盖**（配置 `agent.model_max_tokens` 对象，用户显式声明、程序零猜值，未命中保持全局值）；`include_usage` 按 base_url host 判定（deepseek.com 后缀才发送，其他 provider 防误发——usage 缺失由 R3 保持 0 预期降级）
- 测试：qa_CLFAgentLoop W1-W3 + qa_CLFProtocolAdapter S3-2（include_usage 三态）；ctest 20/20
- 验证：干净重建 161/161 + 双重冒烟（--version + dummy 端点 --prompt 标准输出吻合）
- **A 阶段全部完成** → 🚦 **进入 dsh 决策门**（见下）

### 2026-09-02 todo 面板 + jsonl 追加式保存 ✅（全流程闭环，未发布——攒入下一版本）

- **三批实施（D1-D3）**：T1/T2 m_todos 加锁；J1 codec 行编解码（header/turn/todo_snapshot/complete/summary）；J2 SessionManager 追加/逐行解析/list [当前] 重定义/cleanupOld 适配；J3 AgentLoop 会话上下文（四状态 + 9 接口 + beginSessionFile 懒创建/续写复制 + T6 完成分支收尾 + restoreSession 分流）；J4/J5 轮末追加与命令层（/exit 纯退出、/clear 摘要落盘）；T3 刷新链；T4/T5 常驻面板渲染；J6 resume 行级回显（每轮清单状态行 + complete 收尾行）
- **人工验收 43 项全过**（用户实机两轮：场景 A-F + 补测 exit/clear/强杀/中断；强杀 resume 显示未完成清单 = F20 预期行为实证——07:47 强杀时快照 1✓2✓/3-7 pending，resume 原样重现）
- **验收期修复 4 个 bug**：BUG-1 update 后面板消失（跨轮场景 update 未清面板隐藏标志，dsh projection 语义）；BUG-2 turn 行序列化失败丢失（非法 UTF-8 零容错 → dumpLine 降级 replace + loadJsonl 判损放宽——无 turn 但有快照的崩溃残留不再误判损坏）；收尾清单格式改多行（标识行 + 每任务一行，实机调整）；QA 中途修 3 个测试编码/设计问题
- **实施期真 bug 7 个**：up() 无限递归 SegFault、rename 共享冲突、中文窄路径构造 CP936 陷阱×3、静态非平凡对象（第三次）、sed 误删测试声明
- **设计修订**：审查补丁 §八 6 条（线程模型/四状态归属/API 契约/T6 判定去 m_todoDirty/lifecycle/list 语义）；实施期补丁（header 补 skills 字段、§3.9 契约收行文本）
- 测试增量：qa_CLFMessageCodec L1-L9、qa_CLFSessionManager J1-J11（重写+扩展）、qa_CLFAgentLoop U1/V1-V5、qa_CLFTodoPanel（新套件 P1-P6）、qa_CLFBuiltinTools B4、qa_CLFToolExecutor T12——**ctest 20/20 全绿（历史首次，旧 qa_CLFSessionManager 3 个过时用例已清理）**
- 设计文档已归档：`设计/归档/归档-任务清单UI显示.md`、`设计/归档/归档-会话追加式保存.jsonl.md`；测试记录：`测试/测试-todo面板与jsonl保存-人工验收.md` + `补测二.md`
- **与 S3 衔接**：summary 行类型已预留（S3 摘要自动触发复用同格式）；jsonl 先行反为 S3 铺路
- **待办**：下一版本发布时并入（CHANGELOG 未发布段落 + 版本号）

### 2026-08-31 中文路径全链路编码修复 ✅（两批，已提交推送，**未发布**——攒入下一个版本）
- **第一批（36a33cc）**：状态栏目录显示乱码（"椤圭洰"应为"项目"）→ `CLFRepl.cpp` modeLine 改 `u8path`；`CLFCommands.cpp` /init 项目根、`CLFAgentLoop.cpp` workspaceRoot（模型提示词中的项目路径）改 `.u8string()`。**用户已实测 modeLine 修复生效**
- **第二批（b3a1244，系统性排查）**——`path::string()`/窄字符 path 构造/A 系列 WinAPI 三类编码陷阱全扫：
  - `/init` 项目名 `path(projectRoot)` 窄构造双重乱码 → `u8path(...).filename().u8string()`
  - `CLFSearchContent` 结果相对路径与超限文件路径输出 → `.u8string()`（模型看到的搜索结果路径此前中文乱码）
  - `CLFConfigLoader` `GetModuleFileNameA` → **W 版本**（exe 位于中文目录时配置加载乱码）+ `path(exeDir)` → `u8path(exeDir)` + `dir.string()` → `.u8string()`
  - `CLFSessionManager` 会话列表中文标题 fallback → `.u8string()`；窄字符 `ifstream file(info.m_path)` → `u8path`（中文路径打开失败）
  - `CLFSkillLoader` skills 文件枚举/打开 → `.u8string()` + `u8path`
  - `CLFFileOps::toNativePath` fallback → `u8path`
  - **保留不动**：ASCII 名单比较类 `filename().string()`（后缀匹配乱码无害）、旧版兼容区（findIncomplete/promote/migrate，注释明确"新代码不应使用"，jsonl 方案将重写）
- 根因族：MSVC 窄字符文件系统 API 按 `GetACP()`（CP936）解释 UTF-8 字节——注意 `main.cpp` 的 `SetConsoleCP(CP_UTF8)` **不改 GetACP()**，只影响控制台 I/O
- 验证：MSVC Debug 构建 28/28；ctest 18/19（唯一失败 qa_CLFSessionManager 既有环境问题不变）
- 发布策略：**攒入下一个版本**（CHANGELOG"未发布"段落已更新；VERSION 保持 v0.4.2 不打 tag）

### 2026-08-31 自问自答 P0 Bug 修复 ✅（v0.4.2 已发布，全流程闭环）
- **现象**：用户 16:26 提交后零输入零按键，16:31:49 自动提交（会话 JSON [51] "你猜我咋想的…"），16:02:46 同类（[36]）；长回复时概率高（触发轮 21474 字符）
- **根因**（三重证据 + 用户实证，推翻初稿"上膛残留 5 分钟"推断）：终端注入 → Char 突发进 inputText → 末尾 Return → 40ms 窗满自动提交。核心缺陷 = 40ms 窗只检测"Return 后"不检测"**Return 前字符突发**"→ 单行注入末尾 Return 与手打回车在事件层同构，机制无法区分；「一次 Return+40ms 静默=提交意图」是脆弱假设
- **注入源**：右键粘贴用户同一终端实测排除（两次）；**Shift+Insert 与 Ctrl+V 实测注入生效** → 16:31 最可能为滚轮翻看时误触 Ctrl+V、剪贴板残留草稿（含行尾换行）。精确方式未做事件级确认，但不影响修复（机制级防住所有注入）
- **修复**（用户定案 2026-08-31：粘贴后二次 Enter）：`CLFPasteCoalescer` 前置突发检测——`onCharacter` 刷新 `m_lastCharTime`；`onReturn(Idle)` 判定 `now-lastChar ≤ 40ms` → 置 `m_pendingFromPaste`；定时线程窗满时粘贴上下文 → **不置 confirmed、复位 Idle**（文本留输入框、零自动请求、wakeCb 零触发）；手打回车（间隔>40ms）窗满提交不变。构造加 `pasteBurstMs` 参数（默认 40，测试注入）
- 测试：qa_CLFPasteCoalescer P1-P10 全绿 + 新增 N1-N6（粘贴末尾不提交+wakeCb 零触发 / 手打不回归 / 二次 Enter 提交 / 多行粘贴 PasteMode / 阈值边界）；P3 注入 burst=5ms；P7 重写为新语义
- 验证：MSVC Debug 全量重建 **18/19**（唯一失败 qa_CLFSessionManager 既有环境问题不变）；主程序 --version 冒烟 exit=0
- **实机验收（用户执行，全过）**：busy 期间 Ctrl+V 多次注入 → 零自动提交（agent 日志无自动 [Submit]）；注入文本停输入框（含末尾换行）；二次 Enter 提交残留；5.4 万字符超长回复期间注入无干扰；多行粘贴全链路正确（PENDING→PasteMode→InsertNewline 换行全保留）
- 排除项：右键粘贴（终端层不注入）；CLFPasserby 与提交无关
- 设计文档已归档：`.claude/plans/设计/归档/归档-自问自答严重Bug分析与修复.md`；原 F0-F5 不实施（F0 与 P8 时序矛盾且对注入场景无效，F1/F2 现状已防，F4 留作可选增强）
- 踩坑：vcvars64.bat 在 VS 18 环境 call 失败 → 手动组装 INCLUDE/LIB/LIBPATH（MSVC 14.51.36231 + Windows Kits 10.0.26100.0 均在 D 盘）绕过，方法已存 memory
- 顺带发现：`m_maxToolCallIterations` 实测为 48（progress 旧记 16 已过时）

### 2026-08-25 定时器退出机制优化 ✅（v0.4.1 已提交推送 + tag 已打，发布由用户执行）
- **背景**：`qa_CLFAgentLoop` 28.6s → **1.38s**（20.7×），全量 ctest 30s → **1.26s**
- **CLFThinkingIndicator 线程删除**：查证为纯空转——循环体算的 `elapsed` 从未使用（StatusLine 已由 turnTimer 统一管理）、`m_http` 成员从未被引用；唯一实效是退出时 `setStatus("")`，同步做即可。`stop()` 现在立即返回
- **新增 `CLFPeriodicTimer`**（`clf_types`）：条件变量唤醒，`stop()` 不等剩余间隔；回调执行期放锁防拖住 stop；回调异常在定时器内兜住（线程逸出异常 = std::terminate，v0.3.3 事故根因之一）。放 clf_types 是因 AgentLoop(core) 与 ThinkingIndicator(network) 都依赖
- **turnTimer / thinkingTimer 换用新定时器**：thinkingSec 曾被怀疑可删线程，但 `CLFToolExecutor.cpp:594` 确实读值，保留计数
- 新增 `qa_CLFPeriodicTimer`（8 用例）：核心断言 stop() 在 30s 间隔下 200ms 内返回；回调抛异常后线程不死（stderr 见 "boom" 兜底）
- ⚠ **用户验收**：状态行 `Working for Ns…` 每秒递增 / 回合结束状态清理 / 工具执行期刷新 / Esc 中断 + 安装目录 exe 替换测试，全部通过
- ⚠ **验证盲区事故**：A2 全程只跑 ctest 没启动主程序 → `main.cpp.obj` 陈旧（A2 改了 `CLFAgentConfig` 布局但 main.cpp 未重编）→ 主程序启动段错误（139 零输出）。用户当场抓出。教训已写设计文档：改公共结构必须干净重建 + 验证必须含主程序启动冒烟

### 2026-08-25 A1（=S1）小修批 ✅（v0.3.5）
- **S1-1 edit_file 空 old_string 校验**：`CLFFileOps::editFile` 入口提前返回。原行为不是死循环而是**误导性错误**——`find("")` 每个位置都算命中，会遍历全文后报 "matches N times"（N = 文件长度+1）。校验刻意置于 `readFile` 之前，qa 用 F2 用例钉死该顺序
- **S1-2 force 文案**：去掉 `CLFToolExecutor` 中对不存在参数的引用（两处：`m_content` 与 `emitContent`）
- **S1-3 重试策略分级**：`CLFRetryPolicy` 新增 `extractHttpStatus`（**前缀匹配**，原 `find` 会把响应体里的 "HTTP 400" 误读为状态码）+ `maxAttemptsForError`（三档：致命=1 / 其他4xx=2 / 429·5xx·网络=3）；致命集合由 400-403 扩至含 404/405/409/413/422。`CLFAgentLoop` 流式(:224)与同步(:279)两处判定改用分类上限；**catch 异常分支(:367)保持 kMaxRetries**（无状态码可分级，刻意不改）
- **S1-4 m_wasAborted 接线**：⚠️ 设计文档描述有误——`m_wasAborted` 是 `CLFHttpResponse` 的**响应字段**（`ICLFHttpClient.hpp:15`），非客户端成员，`abort()` 无法直接赋值。实际修法：4 个返回点均由 `m_aborted` 写入响应；并补 `postJson` 起始的标志复位（原先只有 `postJsonStream` 有，同步路径不对称）。根因链：中断→`stop()`→httplib 报连接失败→**被上层当网络故障重试**
- 新增测试：`qa_CLFRetryPolicy`（16 用例 45 断言）+ `qa_CLFFileOps`（5 用例 12 断言），均已加入 `CLF_TEST_TARGETS` 列表（该列表统一设 `CXX_STANDARD 20`，boost::ut 必需——注册新测试时**必须同时加入此列表**，否则 C++17 下 ut.hpp 编译失败）
- 构建：MSVC Debug 25/25 通过。**命令行构建需先导入 vcvars64**（CLion 内部自带环境，裸 bash 调用 cl.exe 会找不到 `<atomic>` 等标准库头）
- ⚠️ **基线记录更正**：progress 原记"ctest 12/13"已过时，实测基线为 **14/15**（唯一失败为 `qa_CLFSessionManager` 既有环境失败）

### 2026-08-25 A2（=S2）安全 + 工具批 ✅（v0.4.0，6 项全完成）
- **S2-1 read_file 边界+50MB+行范围**：三项均在 **handler 层**实施——`CLFFileOps::readFile` 还被 previewEdit / SystemPromptBuilder 等内部路径调用，在底层加限制会误伤配置读取。边界用 `weakly_canonical` 跟随 symlink 防逃逸；**逐段比较而非字符串前缀**（否则 `<cwd>-evil` 会被误判在 `<cwd>` 内，qa B1e 专门钉死）；逃生口 `agent.allow_absolute_read`
- **S2-2 危险命令检测**：⚠ **架构修正**——设计文档原写"放 CLFCommandExec"，但它在 clf_tools 而触发确认的 CLFToolExecutor 在 clf_core，依赖方向 tools→core，**core 调不到 tools**。改放 `CLFSecurityPolicy`，allowlist 也挂其上（避免给已有 8 参数的 ToolExecutor 构造再加参数）。命中强制确认，**不受安全模式影响**；定位为提示层，模型可绕过
- **S2-3 退出码白名单 + cwd**：grep/rg/findstr/diff/fc 退出码 1 判成功；首 token 归一化（去引号/路径/扩展名/大小写）；仅退出码 1 参与白名单（grep 的 2 仍判失败）。cwd 走 `lpCurrentDirectory`(Win)/`chdir`(POSIX)。**env 移入 S4**：两平台合计 60-80 行，而 `set VAR=x && cmd` 可变通
- **S2-4 search 增强**：默认文本扩展名白名单 / 忽略目录补 bin·lib·out·cmake-build-*·.idea·.vscode / 命中行非法 UTF-8 则跳过（只校验命中行，GBK 文件里的 ASCII 行仍可匹配）。`isValidUtf8` 提取到 `CLFEncoding` 供 fileops 与 search 共用
- **S2-5 web_fetch**（新模块）：⚠ **刻意不复用 CLFHttpClient**——后者恒带 `Authorization: Bearer`，抓第三方 URL 会泄漏 API key。1MB 上限 + head8K/tail2K **字节级**截断（`headTailCapWithMarker` 是 vector 模板按元素数，语义不同不可复用）+ NUL 二进制探测。风险级取 Read，**POST 由执行器动态升级为强制确认**（`m_risk` 是单值不能随参数变）
- **S2-6 todo_write**：并入会话状态、不独立落盘（对标 dsh/Claude Code 实证）。`CLFTodoItem` 放 CLFTypes（分层约束：codec 在 core，不能依赖 tools）；codec/SessionManager 加**带默认值**的 todos 参数，`version` 维持 1 双向兼容；**首个捕获 agent 引用的 handler**——已给 `CLFAgentLoop` 显式 `= delete` 拷贝/移动钉死自引用约束
- 新增测试 4 套：`qa_CLFBuiltinTools`(16/33) `qa_CLFWebFetch`(11/37) `qa_CLFMessageCodec`(7/21) + `qa_CLFSecurityPolicy` 扩展(+6 用例)
- 🚨 **踩坑：静态非平凡对象（本项目第二次）**——S2-4 首版三张查表用文件级 `std::set/std::vector`，`qa_CLFSearchContent` 立即段错误（零输出）。根因：boost::ut 在**静态析构阶段**运行测试，跨 TU 析构顺序未定义。改 `constexpr const char* const[]` + 线性查找解决。与 A1 的 magic static 死锁同源，已写入设计文档为项目级教训
- 验证：MSVC Debug 构建通过；**ctest 17/18**（唯一失败 `qa_CLFSessionManager` 为既有环境问题）

### 2026-08-25 qa_CLFAgentLoop 超时根因排查 ✅（两个独立问题，非"单一既有问题"）
- **排查手段**：ctest 输出为空曾被误判为"早期挂起"——实为 stdout 重定向到管道是全缓冲、进程被 kill 时缓冲区丢失。改用 **stderr 逐用例插桩**（无缓冲）定位到 case 5 = T6b，再逐行插桩收窄到 `S4b → S9` 之间
- **问题①（本次引入，已修）**：`CLFRetryPolicy::extractHttpStatus` 中的函数局部 `static const std::string kPrefix` —— MSVC 的 magic static 走 `_Init_thread_header` 全局锁，在该多线程路径上**死锁**，表现为进程永久挂起、零输出。改为 `constexpr const char*` 后消失
  - 判定证据：禁用 `fireInterrupt` 后**仍然挂起**（排除中断竞态）；改 constexpr 后立即全绿
- **问题②（既有，未修）**：即便无死锁，该测试仍需 **28-29 秒**。根因是 `turnTimer` / `thinkingTimer` / `CLFThinkingIndicator` 三个后台线程都用 `sleep_for(1s)` 轮询退出标志，每次 join 平均等 ~0.8s，12 个用例累计约 2.4s×12。已另立待办（条件变量改造）
- **顺带修复**：`MockHttpClient` 队列耗尽时只 `expect` 后继续对空 deque 调 `front()/pop_front()` 是 **UB**（boost::ut 的 expect 不终止执行），改为抛异常由 `runTurn` 的 catch 兜住
- ⚠️ **更正 A1 提交中的错误结论**：当时记"基线也超时，与本批改动无关"——**只对了一半**。基线超时确实存在（原因是②的慢，29s > 我设的 25s timeout），但我**另外引入了①这个真死锁**，当时未能区分。教训：`git stash` 验证基线时只看了退出码，没有区分"慢"与"挂死"
- **真实基线：14/15**（唯一失败 `qa_CLFSessionManager` 环境问题）；跑 ctest 需 `--timeout 90` 以上，否则 AgentLoop 会被误判超时

### 2026-08-25 设计文档核实与合并 ✅
- 对两份新增设计文档（08-19 产出）的 **23 条代码断言逐条对照源码验证**，修正 7 处后合并为 `设计/设计-功能修复与工具补充.md`，原两份删除
- **关键修正**（原文档错误）：① `generateAndCacheSummary` 实由 `/exit`+`/clear` 调用而非压缩路径；~~`shouldSummarize` 已实现~~ **08-25 二次核实为臆造——`CLFSessionSummarizer` 无此方法（全库零命中），仅 `isEnabled()` 判开关**；S3-1 阈值判定需新写，成本回到原估 ② CLFMessageCodec 已有 `version:1` 字段（P2-4 降级为只补分流）③ `m_wasAborted` 检查在 `:212`/`:266`（非 203-207）④ CLFToolExecutor 在 `CLFCore/` 非 `CLFTools/` ⑤ **web_fetch 不可复用 CLFHttpClient——恒带 `Authorization: Bearer` 会向第三方泄漏 API key**（安全缺陷）⑥ ask_user 的"CLFRepl 无需改"不成立（`:524` 硬编码 0↔1 取反、Enter 二分支、返回 bool）⑦ `.clf/` 目录约定不存在（全库零命中）
- 其余 16 条断言属实，可照做
- 路线决策（用户定）：走 S1→S2→S3，dsh 挂决策门；**多模型（S3-2）用户定为必做**

### 2026-08-19 v0.3.4 小功能更新 ✅
- 新增一项小功能性更新（不影响既有功能与使用，公开口径见 CHANGELOG v0.3.4）；tag v0.3.4 已推，`release/CLFCode-v0.3.4-win64.zip` 已出包（3.6M）
- 🔒 **隐私备忘（维护者/pro 必读，勿删除）**：v0.3.4 该项更新含用户隐私内容。**其触发条件、特殊日期、名称/编码方式、实现细节一律不得在本仓库任何文档（本文件 / CHANGELOG / 设计文档 / README）中体现**。维护时仅按公开口径"小功能性更新"提及，不展开、不说明。此前版本曾写入具体触发细节，已按用户要求移除——后续涉及该功能时请保持"隐形"。

### 2026-08-18 笔记本输入必崩根因修复 ✅（FTXUI 事件队列竞态，v0.3.3 候选）
- **现象**：安装版交互输入必崩——流式回合静默退出，无报错/WER/转储，日志截断于 `[API] streaming request`；退出码 3（terminate/abort）；非交互模式同提示完整跑通；桌面同包正常
- **四轮取证**：17:47/17:50/18:07 三次真实复现 + 监控时间线（iter=2 工具回合流式 → 进程消失）→ `EXIT=3` + stderr 空 + 零 WER → 排除 [Fatal]/硬崩/包问题 → **退出码 3 = std::terminate（未处理异常在非 UI 线程）**
- **根因**：FTXUI v7 `MultiReceiverBuffer`（event_buffer）**无锁**——CLFCode turnTimer/提交/思考指示器/粘贴定时器多线程 `PostEvent`→`Push`，UI 主循环每帧 `Pop`/`Prune` 并发访问同一 deque → 数据竞争（UB）→ 内存破坏 → turnTimer 线程 `push_back` 抛异常 → 无兜底 → terminate → abort（退出码 3）；破坏若打在 UI 线程则为访问违例（0xc0000005——08-12 那 36 条 WER 硬崩疑似同一竞态的另一形态）
- **修复（A→B→C 三层）**：A 根治——`multi_receiver_buffer.hpp` 全方法加 `std::recursive_mutex`（12 处锁）；A+ `previous_animation_time_` 原子化（app.cpp）；B 防御——turnTimer/思考指示器/粘贴定时器线程体 try/catch（异常记 `[TurnTimer]`/`[ThinkingIndicator]`/`[PasteTimer]`，分层约束下 clf_network 用 cerr）；C 可观测——`set_terminate` 全局兜底留痕
- **验证**：Debug/Release（MSVC）构建通过；ctest 11/12（SessionManager 既有失败不变）；**E:\deepseek-harness 现场 11 轮工具迭代 + 40 消息上下文完整跑通不再崩**；B/C 防御零触发
- 设计文档：`.claude/plans/设计/设计-FTXUI事件队列竞态修复.md`（含实施记录）
- 收尾：bump v0.3.3 + CHANGELOG 条目；设计文档归档至 `设计/归档/归档-FTXUI事件队列竞态修复.md`；install.ps1/upgrade.ps1 修复——升级不再丢会话历史/日志/崩溃转储（GUID 唯一备份目录 + 四目录备份恢复，模拟测试通过）；用户手动替换安装版 exe 验证通过
- 遗留：08-12 AV 家族同根因假设待观察
- ~~发布 v0.3.3 未执行~~ → **更正（08-25 核实）**：v0.3.3 已发布，`release/CLFCode-v0.3.3-win64.zip` 08-18 出包，tag 已推。包体从 12M 降至 3.6M 系 v0.3.2 起 DLL 只带 OpenSSL 对、不再携带历史 MinGW 运行库所致（预期变化）

### 2026-08-18 发布版必崩溃根因修复 ✅（v0.3.2，取证闭环）
- **现象**：v0.3.1 打包版在台式机+笔记本必崩溃（输入"帮我查看当前项目信息"），源码 Debug 运行正常；同一 exe 时崩时不崩（时序相关假象）
- **四轮取证定位**（诊断 exe + 异常陷阱，`CLF_DEBUG_EVENTS` 日志）：`[Fatal] No mapping for the Unicode character exists in the target multi-byte code page` → `[HandlerExc]`（Esc 退出路径，回合完成后 51s 触发）→ `[EscExitExc]`（/exit 分发）→ `[ExitSaveExc]`（**saveSession finalize 归档**）
- **根因**：MSVC 窄字符文件系统 API 按 ANSI 代码页（CP936）解释 UTF-8 路径字节——`/exit` 归档中文标题会话（`时间戳_帮我查看一下项目信息.json`）时转换失败抛异常 → run() 兜底退出。诊断版 6-7 轮循环全绿验证修复
- **修复**：CLFSessionManager/CLFConfigLoader/CLFFileOps 全链路 `u8path`/`u8string`；标题截断 UTF-8 边界安全；readFile UTF-8 内容探测（顺带修乱码隐患）；listDirectory 宽路径直读
- **连带修正**：release.ps1 构建目录（旧脚本静默打包陈旧 exe 的间接成因）+ 构建失败硬退出 + exe 新旧自检 + vcvars 环境导入 + DLL 只带 OpenSSL 对
- 取证设施清理完毕（复现钩子/陷阱移除；增强异常捕获与事件日志设施保留）

### 2026-08-17 复制粘贴功能修改 ✅（验收通过，关闭，已清理归档）
- 分析：`.claude/plans/分析/分析-复制粘贴功能修改.md`；设计已归档：`.claude/plans/设计/归档/归档-复制粘贴功能修改.md`（Flash 四轮 12 条意见 + 终审 6 缺口全消化）
- 实现：CLFPasteCoalescer（粘贴事件突发合并，P1-P10）+ CLFSelectionModel（选区状态机/提取，S1-S7）+ 渲染器 RowMap 并行构建与高亮 + qa_CLFInputRender（Ref 光标同步回归）；ctest 基线 11/12（SessionManager 既有环境失败不变）
- **验收收敛定稿（用户决策）**：选区 = 纯鼠标左键拖选 + 松手自动复制（copy-on-select）；移除 Ctrl+S 键盘选区与 Ctrl+C/Enter 复制；Ctrl+C 空闲忽略（原误触即退出）、busy 中断保留
- **验收期根因修复 7 项**（事件日志取证实证）：① cv 谓词缺陷（wait→wait_until）② 鼠标坐标 0 基（WT/ConPTY 投递 0 基，FTXUI Box 对照）③ **ENABLE_PROCESSED_INPUT 未清**（FTXUI 不清理，Ctrl+C 被系统转信号、SIGINT 处理器直接退主循环——事件永远到不了应用层）④ 拖选末字符丢失（colToByteEnd 含入 + 松手补位）⑤ hitTest 下方 clamp 缺陷（输入框点击误判为选区）⑥ **Ref<int> 拥有型构造致光标不同步**（粘贴首两行合并，\n 被推到末尾——改引用型 Ref，字节级定位）⑦ 防重复守卫误吞粘贴 Return（500ms→100ms+无字符间隔，后随 Ctrl+C 复制移除一并删除）
- 顺带根因修复 qa_CLFSecurityPolicy 测试缺陷（const char* 指针比较 → std::string）
- 已知边界：粘贴源若为终端原生复制（Shift+拖选）会带渲染网格填充空格——用应用内拖选复制作源；超大粘贴分批（>40ms 批间隔）可能整段自动提交（设计 §2.1）
- 取证模式保留：`CLF_DEBUG_EVENTS=1` → `doc/log/clf_events.log`（独立追加）；取证屏幕转储、键盘选区 API（moveCursor）等临时设施已清理
- 文档同步：/help 与 README 快捷键表、CHANGELOG v0.3.1 条目

### 2026-08-16 dsh 后端接入 Spike S1-S5 全部完成 ✅（go，M1 立项）
- 产出：`tools/spike/`（Spike报告.md + spike_driver.mjs 五模块 + cordis-smoke/final.yml + frames raw/norm 8 组，自包含）
- P1-P4 全过：全链路 8 轮跑通（流式/reasoning/usage/shutdown/exit 0）；工具面五类实测可用（fs 写不受沙箱约束、pwsh 写被拒+升级无审批服务）；subagent 四断言全过（父子会话隔离 731/644）；frames 双轨就绪
- 关键协议事实（M1/M2 必读）：事件先于响应（receipt 门控须缓冲回溯）/ sessionId 复用碰撞 / assistant/message 在 data.message.content / 双 finish 枚举 / tool-call 参数为 JSON 字符串 / spliced 带 removedCount
- 回填分析文档 #1/#4/#5（确认链 UX：审批请求不进 JSON-RPC 协议）
- 决策点 3 实测输入齐备：read-only 只约束 pwsh 通道、升级需装配审批服务

### 2026-08-16 dsh 后端接入 Spike S0 启动冒烟 ✅（四项全过）
- 产出：`tools/spike/cordis-smoke.yml`（零 !!js + 修正装配 + junction node_modules；后已移至 tools/）
- 四项清单全过：缺配置 exit(1) / 畸形行静默跳过 / 20 插件全树加载 / 持开 stdin 存活 10s stderr 零行
- 执行中抓出草案缺陷 3+4：pwsh-local 与 pwsh-sandbox 的 ctx.shell 服务冲突（只挂 sandbox 版即可，三件套实为两件）；缺 dsh-shell-env（tool-pwsh 挂起不激活）
- 部署事实实测：bare 包名自配置文件目录向上解析（"configuration project" 语义）→ M2/M3 部署时 cordis.yml 须与 runtime 闭包同目录；npm 发布版 0.0.1-rc.5 ≠ 钉住 0.1.0-rc.5（M3 核实项）
- 插曲：首轮 loader 失败一度怀疑 dsh web（:3080）并发干扰，隔离测试排除——根因是配置目录解析（bare 包名），与并发无关

### 2026-08-14 P2-UI M2+M3（审批卡强化 + usage 打通） ✅
- P2-2 审批卡：splitPrompt 纯函数 + headline 琥珀加粗/参数 dim 分层 + 确认结束清 prompt 防残影
- P2-4 usage 打通：同步解析 + 流式 `stream_options.include_usage` + **feedUsage 独立投喂**（usage chunk 的 choices 为空数组，须在 lambda 的 choices 过滤前提取——首版把提取放 feedDelta 被过滤挡死，用户验收当场发现，T10c 集成测试钉死）
- summary 行追加 `· X.Xk tok`（缺失省略）+ /context "本次会话累计"（条形仪表早已存在）
- 落定规则 R3：仅正常解析路径累计，中断/错误不累计（T10b）
- 测试：T10×5 + T10a/b/c + T11；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：summary tok 计数 / context 累计 全部通过
- ⚠ 随机崩溃观察：19:16 交互式流式中崩溃一次（M3 代码在流式中无执行路径，疑似 08-11 长期观察 bug 自然复现）；已开启 HKCU LocalDumps 全量转储 + debug 日志待复现

### 2026-08-14 P2-UI M1（恢复回显折叠 + 时间戳） ✅
- 依据：《设计-P2-UI展示完善.md》（两轮外部审查 R1-R5 定稿）
- P2-1 恢复回显折叠：ICLFOutput 增 showFoldedBlock（默认空实现）/ CLFTerminal 折叠态 / Ctrl+R 切换 / restoreSession 改走折叠路径
- R5 视口保持：CLFScrollView::keepLineVisible（offset 计算），切换后折叠行不被顶出
- P2-3 时间戳：localDateStamp/localTimeStamp 双平台 helper + 用户消息行尾 HH:mm（跨日带日期）
- 顺带修复整体审查 P2-8：get_current_time POSIX 分支（localtime_r）
- 测试：T7（折叠回显不进滚动区）+ T9a/T9b（时间戳格式）；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：折叠行/展开/时间戳/get_current_time 全部通过

### 2026-08-14 UI 信息展示借鉴 M2 ✅
- P1-1 四态状态点：接线全表（Running/Done/Warn/Error×7 + Repl catch 兜底 + /resume /clear 的 None）+ 渲染（running=蓝动画帧、done=绿●、warn=琥珀●、error=红✕）+ 计时 ≥15s 才显示
- D1 色语义落地：analyze 模式改紫，蓝让给 running
- P1-2 summary 增强："N 工具 (read A · search B · edited C)"——顺手修 search 双计数（原逻辑 search 同时计入 read 桶，T5 测试当场抓住）
- P1-3 思考折叠摘要：执行中=实时尾行、完成=首行（UTF-8 安全截断）+ Ctrl+O 注释修正
- 清理 useProgressive 同作用域遮蔽警告（C4456）
- 测试：T4a/T4b（含 F20 不被覆盖断言）+ qa_CLFToolExecutor 新套件（T5/F10/降噪保持）；ctest 8/9（SessionManager 既有环境失败不变）
- 人工验收：状态点四态 / 动画 / 15s 计时 / 折叠摘要 / analyze 紫 全部通过

### 2026-08-14 UI 信息展示借鉴 M1 ✅
- 依据：`设计/设计-UI信息展示借鉴.md`（四轮审查定稿 F1-F21）+ `分析/分析-UI信息展示借鉴.md`（dsh 展示设计对照）
- P0-1 错误首行摘要（emitError 单点收敛 + UTF-8 安全截断）
- P0-2 head/tail 截断：search_content 环形缓冲（head 240 + tail 240）+ renderDiff 16+16（公共 headTailCapWithMarker）
- P0-4 工具执行中单行 + 旋转动画（事件驱动无新线程）+ 读工具失败可见性（F10）
- P0-5 中断消息收敛 9 处→1 helper（文案统一 + clearThinking + Warn）
- F13 潜伏缺陷修复：ICLFOutput::requestRefresh + turnTimer 1Hz 驱动（工具执行期界面冻结）
- 测试：qa_CLFHeadTail（8 用例 59 断言）+ qa_CLFSearchContent（3 用例 311 断言）+ T6 三时点中断；ctest 7/8（SessionManager 既有环境失败，HEAD 对照实验确认与改动无关）
- 人工验收：执行中单行+动画 / 中断后显示 / 搜索截断标记触发（agent 自主 PowerShell 补全验证截断可感知）

### 2026-08-12 System Prompt 优化 ✅
- 设计文档：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)
- CLFSystemPromptBuilder：模板加载（降级默认）/ L1 宪法 mtime 缓存 / Git TTL 30s 惰性刷新 / 项目规则加载 / token 预算
- CLFContext::setSystemPrompt()（去重）+ removeSystemMessages()
- CLFAgentLoop::injectSystemPrompt() → Builder，injectSkillToContext() → 重建模式
- config/system_prompt_template.md 可编辑模板

### 2026-08-12 /init 项目初始化命令 ✅
- `/init` → 在工作目录创建 PROJECTRULES.md 模板（已有则不覆盖）
- 模板含 6 个区块，128 行限制提示

### 2026-08-12 P0 第一批 CLI 参数 + 非交互模式 + search_content ✅
- CLI 参数解析：`--help`/`--version`/`--prompt`/`--prompt-file`/`--allow-write`/`--config`/`--project-root`
- 非交互模式：`--prompt` 直接执行后退出，安全策略 Analyze（block 写），`--allow-write` 提升为 Auto
- `search_content` 工具：纯文本匹配/跳过 ignore 目录/1MB 上限/扩展名过滤/500 行截断
- bugfix: `ProgressGuard` 析构 null 检查 + `config.m_stream=false` 非交互模式

### 2026-08-12 v0.1.6 发布 ✅
- `/version` 命令 + `/help` 字母序排列
- install.ps1 本地版本检测（已是最新则跳过）
- 发布包新增 VERSION 文件，使用说明重写
- tag: v0.1.6，release: CLFCode-v0.1.6-win64.zip (12M)

### 2026-08-12 v0.1.5 发布 ✅
- 归档方案：[归档-SystemPrompt优化](../../.claude/plans/设计/归档/归档-SystemPrompt优化.md)
- tag: v0.1.5，release: CLFCode-v0.1.5-win64.zip (12M)

### 2026-08-12 渐进式工具显示细化 ✅
- 执行中：只显示当前工具，空行隔离；完成后折叠 summary，Ctrl+T 展开
- 状态栏：Working → Cooked 切换

### 2026-08-11 Resume 会话恢复完善 & 上下文智能压缩 ✅
- latest.json 原子写入 + /exit 归档 + CLFSessionSummarizer + 灾难保护

### 2026-08-11 UI 体验优化 ✅
- 输入框灰色背景移除 / 状态栏着色 / 分割线细化 / Markdown 表格列对齐

### 2026-08-11 首次运行崩溃修复（长期观察） 🔍
- 三层防御（L1 线程兜底 / L2 子进程 stdin 隔离 / L3 诊断日志），当前无法复现
- 诊断入口：`doc/log/clf_agent.log`，搜索 `[AsyncSubmit]` 和 `[ToolExec]`

### 更早版本
- 01-10: diff 着色 / 渐进式工具显示+双计时器 / 文件修改 diff 渲染
- 01-07: 显示区信息降噪 / 快捷键系统 / 推理过程显示
- 01-06: 代码清理+OCP重构+组件提取+CJK
- 01-05: UI 全面重构 (FTXUI)
- 01-04: Harness 架构重构
- 01-03: FTXUI 终端 UI 重构 / 全量优化 P0-P3

## 长期观察

- **首次运行崩溃修复（2026-08-11）**：等待自然复现后根据日志定位根因
  - 2026-08-14 19:16 交互式流式中崩溃一次（iter=1 流式期间，无工具运行；非交互模式同提示完整通过；M3 代码在流式中无执行路径，疑似本 bug 自然复现）
  - 取证已武装：HKCU LocalDumps 全量转储（`doc/debug/`）+ 日志 debug 级（`agent_settings.local.json`），复现后分析 dump 定位根因

## 已知问题

- **install.ps1 版本检测闪退** ✅ 已修复（`exit 0` → `return`）
- Ctrl+C 确认栏退出（低优先，暂缓）
- emitRaw 钩子（设计预留）
