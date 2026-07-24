# CLFCode 模块接口定义

## 1. 命名空间

所有接口位于 `CLF` 命名空间下。

## 2. Core 模块

### CLFAgentLoop

```cpp
namespace CLF::CLFCore {

struct CLFAgentConfig {
    std::string m_apiBaseUrl;
    std::string m_apiKey;
    std::string m_modelName;
    int         m_maxTokens;
    float       m_temperature;
    bool        m_enableThinking;
};

class CLFAgentLoop {
public:
    explicit CLFAgentLoop(const CLFAgentConfig& config);

    // 执行一轮 Agent 循环：发送 prompt，处理 tool_calls，返回最终响应
    std::string runTurn(const std::string& userInput);

    // 注册工具
    void registerTool(const std::string& name, std::function<std::string(const std::string&)> handler);

private:
    CLFContext           m_context;
    CLFHttpClient        m_httpClient;
    std::vector<CLFTool> m_tools;
};

} // namespace CLF::CLFCore
```

### CLFContext

```cpp
namespace CLF::CLFCore {

class CLFContext {
public:
    // 添加消息到上下文
    void addMessage(const std::string& role, const std::string& content);

    // 获取当前对话历史（自动截断到 maxTokens）
    std::vector<CLFMessage> getMessages(int maxTokens) const;

    // 清空上下文
    void clear();

    // 估算当前 token 用量
    int estimateTokens() const;

private:
    std::vector<CLFMessage> m_messages;
    int                     m_maxContextWindow;
};

} // namespace CLF::CLFCore
```

## 3. Tools 模块

```cpp
namespace CLF::CLFTools {

// 文件操作结果
struct CLFFileResult {
    bool        m_success;
    std::string m_content;
    std::string m_error;
};

// 读取文件
CLFFileResult readFile(const std::string& path);

// 写入文件
CLFFileResult writeFile(const std::string& path, const std::string& content);

// 列出目录
CLFFileResult listDirectory(const std::string& path);

// 命令执行结果
struct CLFCommandResult {
    int         m_exitCode;
    std::string m_stdout;
    std::string m_stderr;
    bool        m_timedOut;
};

// 执行命令（带超时）
CLFCommandResult executeCommand(const std::string& command, int timeoutSeconds = 30);

} // namespace CLF::CLFTools
```

## 4. Network 模块

```cpp
namespace CLF::CLFNetwork {

struct CLFHttpResponse {
    int         m_statusCode;
    std::string m_body;
    std::string m_error;
};

class CLFHttpClient {
public:
    explicit CLFHttpClient(const std::string& baseUrl, const std::string& apiKey);

    // 同步 POST JSON 请求
    CLFHttpResponse postJson(const std::string& path, const std::string& jsonBody);

    // 流式 POST 请求（回调处理每行 SSE）
    CLFHttpResponse postJsonStream(
        const std::string& path,
        const std::string& jsonBody,
        std::function<void(const std::string& line)> onLine
    );

    // 设置超时（秒）
    void setTimeout(int seconds);

private:
    std::string m_baseUrl;
    std::string m_apiKey;
    int         m_timeoutSeconds;
};

} // namespace CLF::CLFNetwork
```

## 5. 错误处理约定

- 所有可能失败的操作返回 `std::expected<T, std::string>` 或带状态码的结构体
- 网络请求失败自动重试（最多 3 次），每次间隔递增
- 工具执行异常时返回错误描述而非抛异常
- 日志通过统一的 `DEBUG_LOG` / `CLF_LOG_INFO` 宏输出
