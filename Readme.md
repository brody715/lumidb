# Lumi DB Project

本项目为米哈游 Pipeline TD-鹿鸣 笔试测试题目，要求实现一个支持增删改查的简易学生成绩管理系统，并支持插件形式拓展系统功能。

考虑到功能的复用性，我们可以将学生成绩管理系统抽象为一个内存型数据库，这样也使得代码逻辑更加清晰，用户只需要在输入时自己
建表，就可以变成学生成绩管理系统。我们把该数据库称为 LumiDB，即鹿鸣数据库。

LumiDB 是一个简单的内存型数据库，支持特殊的查询 DSL 操作数据库，提供交互式命令行工具，支持动态链接库的插件拓展系统功能以完成需求。

## 编译与运行

本项目采用 CMake 构建系统，使用 Acutest 测试框架进行单元测试。

## Third-Party

使用的第三方库的相关文件已被包含在项目源代码文件中，位于 third-party/，不需要额外进行安装。

第三方库不包含关键业务逻辑的实现，只是一些辅助代码方便使用，比如命令行参数的解析，字符串的格式化，异常栈的打印。

- [acutest](https://github.com/mity/acutest): 单元测试
- [fmtlib](https://github.com/fmtlib/fmt): 字符串格式化
- [argumentum](https://github.com/mmahnic/argumentum): 命令行参数解析
- [backward-cpp](https://github.com/bombela/backward-cpp): 异常栈帧打印
- [tabulate](https://github.com/p-ranav/tabulate): 表格格式化输出

## 功能完成情况

## 设计文档

本系统的设计架构，实现细节，查询语句详细语法请参考 [DESIGN.md](DESIGN.md)。
