# CLFCode 模块接口定义

> 更新日期：2026-07-31 | 与当前代码一致

## 1. 命名空间

所有接口位于 `CLF` 命名空间下，按模块分层：
- `CLF::CLFCore` — Agent 核心
- `CLF::CLFTools` — 工具层
- `CLF::CLFNetwork` — 通信层

## 2. CLFCore 模块

### 2.1 CLFAgentLoop — Agent 主循环调度器

```cpp
namespace CLF::CLFCore {

struct CLFAgentConfig {
    // —— connection（连接认证）——
    std::string m_apiBaseUrl = "https://api.deepseek.com";
    std::string m_apiKey;

    // —— chat_completions（对齐 DeepSeek API 参数）——
    std::string m_modelName    = "deepseek-v4-flash";  // 主模型
    std::string m_subModel     = "deepseek-v4-pro";    // 副模型（轻量任务）
    int         m_maxTokens    = 8192;
    float       m_temperature  = 0.0f;
    float       m_topP         = 1.0f;
    float       m_frequencyPenalty = 0.0f;   // -2.0~2.0
    float       m_presencePenalty  = 0.0f;   // -2.0~2.0
    std::string m_responseFormat   = "text"; // "text"|"json_object"
    std::vector<std::string> m_stop;         // 停止序列（最多16个）
    bool        m_stream       = false;      // 流式输出
    std::string m_thinkingLevel = "max";     // off|low|medium|high|max

    // —— agent（Agent 行为参数）——
    int         m_maxContextWindow      = 1048576; // 1M tokens
    int         m_maxToolCallIterations = 16;
    bool        m_contextCompression    = false;
    int         m_maxResponseDelaySec   = 300;    // 秒
    std::string m_interactionLanguage   = "zh-CN";
    std::string m_securityMode          = "edit"; // auto|analyze|edit|manual

    // —— logging（日志配置）——
    std::string m_logLevel   = "info";              // debug|info|warn|error
    std::string m_logFile    = "doc/log/clf_agent.log";
    bool        m_logConsole = false;
};

struct CLFTool {
    std::string m_name;
    std::string m_description;
    std::string m_parametersSchema; // JSON Schema 字符串
    CLFToolRisk m_risk = CLFToolRisk::Read; // 风险等级
    std::function<std::string(const std::string&)> m_handler; // JSON 参数 → 结果串
};

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config);

    // 执行一轮对话（含 tool-calling 循环，流式/同步自动切换）
    // 流式模式：内容已实时输出，返回空串
    std::string runTurn(const std::string& userInput);

    // 注册工具
    void registerTool(const CLFTool& tool);

    // 清空对话上下文（保留 system 身份提示词）
    void clearContext();

    // 注入知识库内容（系统消息级别）
    void injectSkillToContext(const std::string& skillName, const std::string& content);

    // 安全模式切换/查询
    void setSecurityMode(CLFSecurityMode mode);
    CLFSecurityMode getSecurityMode() const;
    const char* getSecurityModeName() const;

    // 高风险工具确认回调（返回 true 表示用户允许）
    void setConfirmCallback(std::function<bool(const std::string&)> callback);
};

} // namespace CLF::CLFCore
```

### 2.2 CLFContext — 对话上下文管理器

```cpp
namespace CLF::CLFCore {

struct CLFMessage {
    std::string m_role;     // "system"|"user"|"assistant"|"tool"
    std::string m_content;  // 文本内容（assistant 发 tool_calls 时可为空）
    std::vector<CLFToolCall> m_toolCalls; // assistant role
    std::string m_toolCallId;             // tool role
    std::string m_name;                   // tool role
};

struct CLFToolCall {
    std::string m_id;
    std::string m_name;
    std::string m_arguments; // JSON string
};

struct CLFToolResult {
    std::string m_toolCallId;
    std::string m_name;
    std::string m_content;
};

class CLFContext {
public:
    explicit CLFContext(int maxContextWindow = 65536);

    void addMessage(const std::string& role, const std::string& content);

    // assistant 含 tool_calls（content 可为空）
    void addAssistantToolCalls(const std::vector<CLFToolCall>& toolCalls,
                               const std::string& content = "");

    // tool 角色结果消息（超 8000 字符自动截断 + 标记）
    void addToolResult(const std::string& toolCallId,
                       const std::string& name,
                       const std::string& content);

    // 获取消息列表（system 永不截断，其余从新到旧截断）
    std::vector<CLFMessage> getMessages() const;

    void clear();

    // token 估算：ASCII 0.25/字，CJK 1.5/字
    int estimateTokens() const;
};

} // namespace CLF::CLFCore
```

### 2.3 CLFProtocolAdapter — 协议适配器

```cpp
namespace CLF::CLFCore {

struct CLFAssistantResponse {
    std::string              m_content;
    std::vector<CLFToolCall> m_toolCalls;
    std::string              m_finishReason; // "stop"|"tool_calls"|...
};

class CLFProtocolAdapter {
public:
    // 构建 /v1/chat/completions 请求体（参数全从 CLFAgentConfig 读取）
    std::string buildChatRequest(
        const std::vector<CLFMessage>& messages,
        const std::vector<CLFTool>&    tools,
        const CLFAgentConfig&          config) const;

    // 解析非流式响应
    CLFAssistantResponse parseAssistantResponse(const std::string& responseBody) const;

    static bool hasToolCalls(const CLFAssistantResponse& resp);
    static bool isValidFinish(const CLFAssistantResponse& resp);
};

} // namespace CLF::CLFCore
```

### 2.4 CLFConfigLoader — 配置加载器

```cpp
namespace CLF::CLFCore {

class CLFConfigLoader {
public:
    static bool loadFromFile(const std::string& configPath, CLFAgentConfig& outConfig);

    // 文件加载 + 环境变量覆盖（CLF_API_KEY / CLF_API_BASE_URL / CLF_MODEL）
    static bool loadFromFileWithEnv(const std::string& configPath, CLFAgentConfig& outConfig);

    // 项目根目录：从 exe 向上找 CMakeLists.txt（结果缓存）
    static std::string findProjectRoot();
    static const std::string& getProjectRoot();

    // 基于项目根的相对路径解析
    static std::string resolvePath(const std::string& relativePath);
};

} // namespace CLF::CLFCore
```

### 2.5 CLFSecurityPolicy — 安全策略

```cpp
namespace CLF::CLFCore {

enum class CLFSecurityMode { Auto = 0, Analyze = 1, Edit = 2, Manual = 3 };
enum class CLFToolRisk { Read = 0, Write = 1, Command = 2 };

class CLFSecurityPolicy {
public:
    explicit CLFSecurityPolicy(CLFSecurityMode mode = CLFSecurityMode::Edit);

    void setMode(CLFSecurityMode mode);
    CLFSecurityMode getMode() const;
    const char* getModeName() const; // "auto"|"analyze"|"edit"|"manual"

    // risk 工具在当前模式下是否允许；needConfirm = 允许但需确认
    bool isAllowed(CLFToolRisk risk, bool& needConfirm) const;

    static CLFSecurityMode modeFromString(const std::string& s);
};

} // namespace CLF::CLFCore
```

### 2.6 CLFLogger — 日志系统（单例）

```cpp
namespace CLF::CLFCore {

enum class CLFLogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class CLFLogger {
public:
    static CLFLogger& instance();

    void init(CLFLogLevel level, const std::string& filePath, bool consoleOutput);

    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    static CLFLogLevel levelFromString(const std::string& levelStr);
};

} // namespace CLF::CLFCore
```

### 2.7 CLFSkillLoader — 知识库加载

```cpp
namespace CLF::CLFCore {

class CLFSkillLoader {
public:
    static int loadFromDir(const std::string& dirPath);   // 返回加载数量
    static std::string getContent(const std::string& name);
    static std::vector<std::string> listNames();
    static void clear();
};

} // namespace CLF::CLFCore
```

### 2.8 CLFStreamAccumulator — 流式 delta 累积器（header-only）

```cpp
namespace CLF::CLFCore {

class CLFStreamAccumulator {
public:
    // 输入 delta JSON，返回 content 增量（实时显示用）
    std::string feedDelta(const nlohmann::json& delta);

    void markDone(); // 收到 [DONE] 时调用

    bool isFinished() const;
    const std::string& getContent() const;
    const std::vector<CLFToolCall>& getToolCalls() const;
    const std::string& getFinishReason() const;

    void reset();
};

} // namespace CLF::CLFCore
```

## 3. CLFTools 模块

### 3.1 CLFFileOps — 文件操作

```cpp
namespace CLF::CLFTools {

struct CLFFileResult {
    bool        m_success = false;
    std::string m_content;
    std::string m_error;
};

CLFFileResult readFile(const std::string& path);
CLFFileResult writeFile(const std::string& path, const std::string& content); // 覆盖模式
CLFFileResult listDirectory(const std::string& path);

} // namespace CLF::CLFTools
```

### 3.2 CLFCommandExec — 命令执行

```cpp
namespace CLF::CLFTools {

struct CLFCommandResult {
    int         m_exitCode = -1;
    std::string m_stdout;
    std::string m_stderr;
    bool        m_timedOut = false;
};

CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds = 30);

} // namespace CLF::CLFTools
```

### 3.3 CLFBuiltinTools — 内置工具注册

```cpp
namespace CLF::CLFTools {

// 注册全部内置工具（6 个）：
//   read_file(Read) / write_file(Write) / list_directory(Read)
//   execute_command(Command) / get_current_time(Read) / echo(Read)
void registerBuiltinTools(CLF::CLFCore::CLFAgentLoop& agent);

} // namespace CLF::CLFTools
```

## 4. CLFNetwork 模块

### 4.1 CLFHttpClient — HTTP 客户端

```cpp
namespace CLF::CLFNetwork {

struct CLFHttpResponse {
    int         m_statusCode = 0;
    std::string m_body;
    std::string m_error;
};

class CLFHttpClient {
public:
    CLFHttpClient(const std::string& baseUrl, const std::string& apiKey);

    // 同步 POST JSON 请求
    CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody);

    // 流式 POST（SSE 逐行回调，跨 chunk 行缓冲）
    CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine);

    void setTimeout(int seconds);
};

} // namespace CLF::CLFNetwork
```

## 5. 错误处理约定

- **工具执行**：handler 内部 try-catch，异常转错误描述字符串返回，不向外抛
- **JSON 解析**：防御性解析（`json::parse` 失败返回错误响应而非崩溃）
- **网络失败**：返回 `CLFHttpResponse::m_error` 非空，调用方决定重试/降级
- **日志**：诊断信息统一走 `CLFLogger`；用户交互输出保留 `std::cout`
- **崩溃保护**：main 的 REPL 循环外层 try-catch，异常时清空上下文不退出

## 6. 配置结构（config/agent_settings.json）

```
connection      — base_url / api_key
chat_completions— model / sub_model / max_tokens / temperature / top_p /
                  frequency_penalty / presence_penalty / stop / response_format /
                  stream / thinking_level
agent           — max_context_window / max_tool_call_iterations / context_compression /
                  max_response_delay_sec / interaction_language / security_mode
logging         — level / file / console
```

详细字段说明见 `config/README.md`。
