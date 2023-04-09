## Intro

考虑到功能的复用性，我们可以将学生成绩管理系统抽象为一个内存型数据库，这样也使得
代码逻辑更加清晰，用户只需要在输入时自己建表，就可以变成学生成绩管理系统。我们把
该数据库称为 LumiDB，即鹿鸣数据库。

LumiDB 是一个内存型数据库，即所有的状态和数据都保存在内存中，不支持持久化。

用户可通过 Query DSL 语言进行数据的查询、插入、修改、删除等操作。

LumiDB 也提供了插件机制，用户可以通过编写插件，实现自定义的功能，如添加新的函数定义和实现。

LumiDB 的查询语言是一种函数式 DSL，用户可以通过函数的组合来实现自定义查询操作。

```py
query("students") | where("姓名", "=", "张三") | order_by("语文", "desc") | limit(10)
```

## 关键模块和数据结构

### Database

数据库对象，全局唯一，管理了所有的表，所有的函数实现以及所有的插件，并负责执行查询语句。

数据库对象是线程安全的，可以在多个线程中同时访问

目前出于简单考虑，插件能够直接访问和操作 Db 对象，添加新的函数定义和实现，访问元信息等。

### Table

表对象，LumiDB 中存储数据状态或中间结果的一种数据结构，类似于关系型数据库中的表。

表对象由 `TableSchema` 和 `TableData` 两部分组成，分别表示表的元信息和数据。

`TableSchema` 中包含了表的列名，列类型，等信息。实现了类型检查，列名检查等功能。

`TableData` 中包含了表的数据，为简单的 `std::vector`。

表可以分为 SourceTable 以及 ResultTable 两种类型

SourceTable 用于存储源表状态，ResultTable 用于存储中间结果以及结果。

### Function

函数对象，函数实现了具体的增删改查逻辑，需要支持以组合的形式链式调用。

函数分为 RootFunction 以及 ChildFunction 两种类型

RootFunction 位于函数链的头部，定义了当前命令的执行逻辑以及上下文，如 `query`，`insert`，`update`，`delete` 等。

ChildFunction 位于函数链的中间，对 RootFunction 的返回结果执行修饰，处理，如 `where`，`order_by`，`limit` 等。

LumiDB 将遍历函数链，将上一函数的返回值作为下一函数的输入。最后，执行 RootFunction 的 finalize 方法，返回最终结果。

### Plugins

插件机制，用户可以通过编写插件，实现自定义的功能，如添加新的函数定义和实现，访问元信息等。插件通过动态链接库的形式加载到 LumiDB 中。

### CLI

交互式命令行工具，提供一个 REPL 环境，用户可以在该环境中执行查询语句，查看元信息，加载插件等。

命令行工具读取用户工作流，调用 Database 执行查询语句，将结果输出到标准输出。

## 实现一个插件

插件需要实现一个 C 函数 `LumiDBPluginDef lumi_db_get_plugin_def()`，返回一个 `LumiDBPluginDef` 结构体，该结构体包含了插件的名称，版本，描述，以及插件需要实现的生命周期函数。

(与 LLVM 的插件机制类似)

在 `include/plugin_def.hh` 中定义了相关的结构

```cpp

// 符号定义为弱引用，避免不同目标文件中的符号冲突
#define LUMI_DB_ATTRIBUTE_WEAK __attribute__((weak))

namespace lumidb {
extern "C" {

struct LumiDBPluginContext {
  // user_data is used to store plugin's private data
  void *user_data;

  // db is used to access lumidb's database, it should be dynamic cast to
  // lumidb::Database
  void *db;

  // error is used to store error message (when on_load or on_unload returns non-zero)
  const char *error;
};

struct LumiDBPluginDef {
  const char* name;
  const char* version;
  const char* description;
  int (*on_load)(LumiDBPluginContext *ctx);
  int (*on_unload)(LumiDBPluginContext *ctx);
};

}
}

extern "C" ::lumidb::LumiDBPluginDef LUMI_DB_ATTRIBUTE_WEAK lumi_db_get_plugin_def();
```

在 `my-plugin.cc` 中实现该函数，并编译为 DLL，即可在 LumiDB 中加载

在插件类中，将 ctx 中的 db 通过 dynamic_cast 转换为 lumidb::Database 对象，即可访问 LumiDB 的数据库对象

需要注意的是，插件的生命周期应该小于 Database 的生命周期，否则可能导致访问非法内存。

在插件类中，可以调用 LumiDB C++ 接口，注册自定义函数

```cpp
#include <iostream>

#include "lumidb/db.hh"
#include "lumidb/plugin_def.hh"

using namespace lumidb;

class MyPlugin {
public:
  MyPlugin(db *lumidb::Database): db(db) {}
  ~MyPlugin() {}

  int on_load() {
    auto res = db.registerFunction({
      "show_timers",
      ...
    });

    if (res.has_error()) {
      db.report_error({
        .source = "plugin",
        .name = "my-plugin",
        .error = res.error(),
      });
      return 1;
    }

    return 0;
  }

private:
  lumidb::Database *db;
};

extern "C" {
LuaDBPluginDef LUMI_DB_ATTRIBUTE_WEAK lumi_db_get_plugin_def() {
  static LumiDBPluginDef def = {
    "my-plugin",
    "0.0.1",
    "my plugin",
    [](LumiDBPluginContext *ctx) {
      // cast to lumidb::Database
      auto db = dynamic_cast<lumidb::Database *>(ctx.db);
      if (db == nullptr) {
        ctx.error = "failed to cast to lumidb::Database";
        return 1;
      }

      auto plugin = new MyPlugin(db);
      ctx->user_data = plugin;
      int ret = plugin->on_load();

      if (ret != 0) {
        delete plugin;
        ctx->user_data = nullptr;
      }

      return ret;
    },
    [](LumiDBPluginContext *ctx) {
      if (ctx->user_data != nullptr) {
        delete reinterpret_cast<MyPlugin *>(ctx->user_data);
        ctx->user_data = nullptr;
      }

      return 0;
    }
  };
  return def;
}
}

```

## Query DSL 语法

### 值类型

值包括三种类型

- float
- string
- null

string 类型的字面值可以用单引号或双引号包裹

```py
"hello"
'hello'
```

### 命令

命令以函数的形式表示，每个函数支持多个参数，参数只能为原子类型，不能嵌套函数

参数类型为

- int
- string
- null

```py
<func-name>(<arg1>, <arg2>, ...)
```

函数之间可以通过管道运算符进行组合

```py
<func1>(<arg1>, <arg2>, ...) | <func2>(<arg1>, <arg2>, ...)
```

## Query DSL Examples

1. 系统信息查询

   ```py
   # 查询所有的表
   show_tables()

   # 查询所有的函数
   show_functions()

   # 查询所有注册插件
   show_plugins()
   ```

2. 创建表

   **Syntax**

   ```py
   create_table(<string:table-name>) | add_field(<string:field-name>, <string:field-type>)
   ```

   **Examples**

   ```py
   create_table("students") | add_field("姓名", "string") | add_field("语文", "float?") | add_field("数学", "float?") | add_field("英语", "float?")
   ```

3. 删除表

   **Syntax**

   ```py
   drop_table(<string:table-name>)
   ```

   **Examples**

   ```py
   drop_table("students")
   ```

4. 向表中插入数据

   **Syntax**

   ```py
   insert(<string:table-name>) | add_row(<field1>, <field2>, ...)
   ```

   **Examples**

   ```py
   insert("students") | add_row("张三", "90", "80", "70") | add_row("李四", "80", "70", "60")
   ```

5. 从 CSV 文件中读取数据

   **Syntax**

   ```py
   insert(<string:table-name>) | load_csv(<string:file-path>)
   ```

   **Examples**

   ```py
   insert("students") | load_csv("./data/students.csv")
   ```

6. 修改表中数据

   **Syntax**

   ```py
   update(<string:table-name>) | where(<string:field>, <string:op>, <string:value>) | set_value(<string:field>, <string:value>)
   ```

   **Examples**

   ```py
   update("students") | where("姓名", "=", "张三") | set_value("语文", 100) | set_value("数学", 100)
   ```

7. 删除表中数据

   **Syntax**

   ```py
   delete(<string:table-name>) | where(<string:field>, <string:op>, <string:value>)
   ```

   **Examples**

   ```py
   delete("students") | where("姓名", "=", "张三")
   ```

8. 查询表中所有数据

   **Syntax**

   ```py
   query(<string:table-name>)
   ```

   **Examples**

   ```py
   query("students")
   ```

9. 查询表中所有数据，限制返回条数

   **Syntax**

   ```py
   limit(<int:num_items>)
   ```

   **Examples**

   ```py
   query("students") | limit(10)
   ```

10. 查询表中所有数据，只显示某些字段

    **Syntax**

    ```py
    select(<string:field1>, <string:field2>, ...)
    ```

    **Examples**

    ```py
    query("students") | select("姓名", "语文")
    ```

11. 过滤数据

    **Syntax**

    ```py
    where(<string:field>, <string:op>, <string:value>)
    ```

    **Examples**

    ```py
    query("students") | where("语文", "=", null)
    ```

12. 排序

    **Syntax**

    ```py
    order_by(<string:field>, <string:asc|desc>)
    ```

    **Examples**

    ```py
    query("students") | order_by("语文", "desc") | limit(1)

    query("students") | max("语文")
    ```

13. 聚合函数

    **Syntax**

    平均值，最大值，最小值

    ```py
    max(<string:field>)
    min(<string:field>)
    avg(<string:field>)
    ```

    **Examples**

    ```py
    query("students") | max("语文")
    query("students") | min("语文")
    query("students") | avg("语文")
    ```

14. 加载/卸载插件

    **Syntax**

    ```py
    load_plugin(<string:plugin-path>)
    unload_plugin(<string:plugin-id>)
    ```

    **Examples**

    ```py
    load_plugin("./plugins/ext.dll")
    unload_plugin(100)
    ```

15. 定时器 (载入拓展功能插件后支持)

    **Syntax**

    ```py
    # 添加定时器，打印定时器 ID
    add_timer(<string:duration>, <string:command>)

    # 删除定时器
    del_timer(<string:timer-id>)

    # 列出所有定时器
    show_timers()
    ```

    **Duration string format**

    ```py
    "10s"
    "20s"
    "30s"
    ```

    **Examples**

    ```py
    add_timer("10s", "query('students')")
    add_timer("5s", "query('students') | limit(10)")

    del_timer(2)
    ```
