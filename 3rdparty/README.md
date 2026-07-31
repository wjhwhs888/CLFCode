# 第三方依赖库

本项目仅使用 Header-Only 库，直接包含头文件即可，无需编译。

| 库 | 最低版本 | 头文件路径 | 来源 |
|----|----------|-----------|------|
| cpp-httplib | v0.18.4 | `httplib/httplib.h` | https://github.com/yhirose/cpp-httplib |
| nlohmann/json | v3.11.3 | `nlohmann/json.hpp` | https://github.com/nlohmann/json |
| Boost.UT | 2.x | `boost-ut/ut.hpp` | https://github.com/boost-ext/ut |

## 下载

```bash
# cpp-httplib
curl -L -o 3rdparty/httplib/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/main/httplib.h

# nlohmann/json
curl -L -o 3rdparty/nlohmann/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp

# Boost.UT
curl -L -o 3rdparty/boost-ut/ut.hpp https://raw.githubusercontent.com/boost-ext/ut/master/include/boost/ut.hpp
```
