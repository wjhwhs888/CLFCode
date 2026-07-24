# L3 单元测试脚手架生成规则

## 测试框架
使用 [Boost.UT](https://github.com/boost-ext/ut) (`boost::ut`)。

## 文件命名
测试文件命名：`qa_<TypeName>.cpp`，放置于 `src/<module>/` 目录。

## 模板

```cpp
#include <boost/ut.hpp>
using namespace boost::ut;

const boost::ut::suite<"CLFClassName"> tests = [] {
    "descriptive scenario name"_test = [] {
        // arrange
        // act
        // assert
        expect(actual == expected);
    };

    "edge case description"_test = [] {
        // arrange
        // act
        // assert
        expect(throws<std::runtime_error>([] { /* ... */ }));
    };
};
```

## 覆盖要求
- 每个公开方法至少一个测试场景
- 覆盖正常路径 + 异常路径 + 边界条件
- 测试名称是描述性句子，不是函数名

## 构建配置
在 `src/CMakeLists.txt` 中添加：

```cmake
add_executable(qa_CLFClassName qa_CLFClassName.cpp)
target_link_libraries(qa_CLFClassName PRIVATE clf_core)
add_test(NAME qa_CLFClassName COMMAND qa_CLFClassName)
```
