# QinDB 用户手册

## 目录
1. [简介](#简介)
2. [安装与配置](#安装与配置)
3. [启动和连接](#启动和连接)
4. [数据库操作](#数据库操作)
5. [表操作](#表操作)
6. [数据操作](#数据操作)
7. [索引操作](#索引操作)
8. [事务管理](#事务管理)
9. [用户和权限管理](#用户和权限管理)
10. [查询优化](#查询优化)
11. [数据导出](#数据导出)
12. [系统维护](#系统维护)
13. [故障排查](#故障排查)

---

## 简介

QinDB 是一个轻量级的关系型数据库管理系统，支持标准 SQL 语法，提供完整的事务支持、多种索引类型和高性能的查询处理能力。

### 主要特性

- **标准 SQL 支持**：兼容主流 SQL 语法
- **ACID 事务**：完整的事务保证
- **多种索引**：B+ 树、哈希、复合索引、全文索引
- **高并发**：支持多客户端并发访问
- **查询缓存**：提升重复查询性能
- **数据导出**：支持 CSV、JSON、XML、HTML 等格式

---

## 安装与配置

### 系统要求

- 操作系统：Windows/Linux/macOS
- 内存：至少 512MB
- 磁盘空间：至少 100MB

### 安装步骤

1. 下载 QinDB 可执行文件
2. 解压到目标目录
3. 配置环境变量（可选）

### 配置文件

QinDB 使用 `qindb.ini` 配置文件，默认配置如下：

```ini
[Server]
# 服务器端口
port=5432

# 最大连接数
max_connections=100

# 监听地址（0.0.0.0 表示监听所有网卡）
bind_address=0.0.0.0

[Storage]
# 数据目录
data_dir=./data

# 缓冲池大小（页数）
buffer_pool_size=1024

# 页大小（字节）
page_size=4096

# 是否启用 WAL
enable_wal=true

[Cache]
# 查询缓存大小（查询数量）
query_cache_size=100

# 查询缓存是否启用
enable_query_cache=true

[Log]
# 日志级别：DEBUG, INFO, WARNING, ERROR
log_level=INFO

# 日志文件路径
log_file=qindb.log

# 是否输出到控制台
log_to_console=true

[Transaction]
# 事务隔离级别：READ_UNCOMMITTED, READ_COMMITTED, REPEATABLE_READ, SERIALIZABLE
isolation_level=READ_COMMITTED

# 事务超时时间（秒）
timeout=30
```

---

## 启动和连接

### 启动服务器模式

```bash
# 使用默认配置启动
./qindb --server

# 指定端口启动
./qindb --server --port 5432

# 指定配置文件
./qindb --server --config /path/to/qindb.ini
```

### 客户端连接

```bash
# 连接到本地服务器
./qindb --host localhost --port 5432

# 使用用户名和密码连接
./qindb --host localhost --port 5432 --user admin --password admin

# 直接连接到指定数据库
./qindb --host localhost --port 5432 --database mydb
```

### 交互式命令行

连接成功后，进入交互式命令行界面：

```
QinDB version 1.0.0
Connected to localhost:5432
Type 'help' for help, 'quit' to exit.

qindb>
```

---

## 数据库操作

### 创建数据库

```sql
CREATE DATABASE database_name;
```

示例：
```sql
CREATE DATABASE mydb;
```

### 删除数据库

```sql
DROP DATABASE database_name;
```

示例：
```sql
DROP DATABASE mydb;
```

### 切换数据库

```sql
USE database_name;
```

示例：
```sql
USE mydb;
```

### 列出所有数据库

```sql
SHOW DATABASES;
```

---

## 表操作

### 创建表

```sql
CREATE TABLE table_name (
    column1 datatype [constraints],
    column2 datatype [constraints],
    ...
    [table_constraints]
);
```

**支持的数据类型：**
- `INT` / `INTEGER`：整数类型
- `BIGINT`：长整数类型
- `DOUBLE` / `FLOAT`：浮点数类型
- `VARCHAR(n)`：变长字符串，最大长度 n
- `TEXT`：长文本类型
- `BOOLEAN` / `BOOL`：布尔类型
- `DATE`：日期类型
- `TIMESTAMP`：时间戳类型

**约束类型：**
- `PRIMARY KEY`：主键约束
- `NOT NULL`：非空约束
- `UNIQUE`：唯一性约束
- `DEFAULT value`：默认值
- `AUTO_INCREMENT`：自动递增（仅用于整数类型）

示例：
```sql
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL,
    age INT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 删除表

```sql
DROP TABLE table_name;
```

示例：
```sql
DROP TABLE users;
```

### 修改表结构

#### 添加列

```sql
ALTER TABLE table_name ADD COLUMN column_name datatype [constraints];
```

示例：
```sql
ALTER TABLE users ADD COLUMN phone VARCHAR(20);
```

#### 删除列

```sql
ALTER TABLE table_name DROP COLUMN column_name;
```

示例：
```sql
ALTER TABLE users DROP COLUMN phone;
```

#### 修改列

```sql
ALTER TABLE table_name MODIFY COLUMN column_name new_datatype [constraints];
```

示例：
```sql
ALTER TABLE users MODIFY COLUMN email VARCHAR(200) NOT NULL;
```

### 查看表结构

```sql
DESC table_name;
-- 或
DESCRIBE table_name;
```

示例：
```sql
DESC users;
```

### 列出所有表

```sql
SHOW TABLES;
```

---

## 数据操作

### 插入数据

#### 插入单行

```sql
INSERT INTO table_name (column1, column2, ...) VALUES (value1, value2, ...);
```

示例：
```sql
INSERT INTO users (username, email, age) VALUES ('alice', 'alice@example.com', 25);
```

#### 插入多行

```sql
INSERT INTO table_name (column1, column2, ...) VALUES
    (value1, value2, ...),
    (value1, value2, ...),
    ...;
```

示例：
```sql
INSERT INTO users (username, email, age) VALUES
    ('bob', 'bob@example.com', 30),
    ('charlie', 'charlie@example.com', 28);
```

### 查询数据

#### 基本查询

```sql
SELECT column1, column2, ... FROM table_name;
```

示例：
```sql
SELECT username, email FROM users;
```

#### 查询所有列

```sql
SELECT * FROM table_name;
```

#### WHERE 子句

```sql
SELECT * FROM table_name WHERE condition;
```

示例：
```sql
SELECT * FROM users WHERE age > 25;
SELECT * FROM users WHERE username = 'alice';
SELECT * FROM users WHERE age BETWEEN 20 AND 30;
SELECT * FROM users WHERE username IN ('alice', 'bob');
SELECT * FROM users WHERE email LIKE '%@example.com';
```

#### ORDER BY 子句

```sql
SELECT * FROM table_name ORDER BY column [ASC|DESC];
```

示例：
```sql
SELECT * FROM users ORDER BY age DESC;
SELECT * FROM users ORDER BY username ASC, age DESC;
```

#### LIMIT 子句

```sql
SELECT * FROM table_name LIMIT n;
SELECT * FROM table_name LIMIT offset, n;
```

示例：
```sql
SELECT * FROM users LIMIT 10;           -- 前 10 条
SELECT * FROM users LIMIT 10, 10;        -- 第 11-20 条
```

#### GROUP BY 和 HAVING

```sql
SELECT column, aggregate_function(column)
FROM table_name
GROUP BY column
HAVING condition;
```

示例：
```sql
SELECT age, COUNT(*) as count FROM users GROUP BY age;
SELECT age, AVG(salary) as avg_salary
FROM employees
GROUP BY age
HAVING AVG(salary) > 5000;
```

**支持的聚合函数：**
- `COUNT()`：计数
- `SUM()`：求和
- `AVG()`：平均值
- `MIN()`：最小值
- `MAX()`：最大值

#### JOIN 查询

```sql
-- INNER JOIN
SELECT * FROM table1 INNER JOIN table2 ON table1.column = table2.column;

-- LEFT JOIN
SELECT * FROM table1 LEFT JOIN table2 ON table1.column = table2.column;

-- RIGHT JOIN
SELECT * FROM table1 RIGHT JOIN table2 ON table1.column = table2.column;
```

示例：
```sql
SELECT users.username, orders.order_id, orders.amount
FROM users
INNER JOIN orders ON users.id = orders.user_id;
```

### 更新数据

```sql
UPDATE table_name SET column1 = value1, column2 = value2, ... WHERE condition;
```

示例：
```sql
UPDATE users SET age = 26 WHERE username = 'alice';
UPDATE users SET email = 'newemail@example.com', age = age + 1 WHERE id = 1;
```

### 删除数据

```sql
DELETE FROM table_name WHERE condition;
```

示例：
```sql
DELETE FROM users WHERE id = 1;
DELETE FROM users WHERE age < 18;
```

---

## 索引操作

### 创建索引

#### B+ 树索引（默认）

```sql
CREATE INDEX index_name ON table_name (column);
```

示例：
```sql
CREATE INDEX idx_username ON users (username);
```

#### 哈希索引

```sql
CREATE INDEX index_name ON table_name (column) USING HASH;
```

示例：
```sql
CREATE INDEX idx_email ON users (email) USING HASH;
```

#### 复合索引

```sql
CREATE INDEX index_name ON table_name (column1, column2, ...);
```

示例：
```sql
CREATE INDEX idx_name_age ON users (username, age);
```

#### 全文索引

```sql
CREATE FULLTEXT INDEX index_name ON table_name (column);
```

示例：
```sql
CREATE FULLTEXT INDEX idx_content ON articles (content);
```

### 删除索引

```sql
DROP INDEX index_name ON table_name;
```

示例：
```sql
DROP INDEX idx_username ON users;
```

### 查看索引

```sql
SHOW INDEX FROM table_name;
```

---

## 事务管理

### 开始事务

```sql
BEGIN;
-- 或
START TRANSACTION;
```

### 提交事务

```sql
COMMIT;
```

### 回滚事务

```sql
ROLLBACK;
```

### 事务示例

```sql
BEGIN;

INSERT INTO accounts (user_id, balance) VALUES (1, 1000);
UPDATE accounts SET balance = balance - 100 WHERE user_id = 1;
UPDATE accounts SET balance = balance + 100 WHERE user_id = 2;

-- 如果一切正常
COMMIT;

-- 如果出现问题
-- ROLLBACK;
```

### 设置事务隔离级别

```sql
SET TRANSACTION ISOLATION LEVEL level;
```

支持的隔离级别：
- `READ UNCOMMITTED`
- `READ COMMITTED`（默认）
- `REPEATABLE READ`
- `SERIALIZABLE`

示例：
```sql
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

---

## 用户和权限管理

### 创建用户

```sql
CREATE USER 'username' IDENTIFIED BY 'password';
```

示例：
```sql
CREATE USER 'john' IDENTIFIED BY 'secret123';
```

### 删除用户

```sql
DROP USER 'username';
```

示例：
```sql
DROP USER 'john';
```

### 授予权限

```sql
GRANT privileges ON database.table TO 'username';
```

**权限类型：**
- `SELECT`：查询权限
- `INSERT`：插入权限
- `UPDATE`：更新权限
- `DELETE`：删除权限
- `CREATE`：创建权限
- `DROP`：删除权限
- `ALTER`：修改权限
- `ALL`：所有权限

示例：
```sql
-- 授予单个表的查询权限
GRANT SELECT ON mydb.users TO 'john';

-- 授予数据库的所有权限
GRANT ALL ON mydb.* TO 'john';

-- 授予多个权限
GRANT SELECT, INSERT, UPDATE ON mydb.users TO 'john';
```

### 撤销权限

```sql
REVOKE privileges ON database.table FROM 'username';
```

示例：
```sql
REVOKE INSERT ON mydb.users FROM 'john';
```

### 查看用户权限

```sql
SHOW GRANTS FOR 'username';
```

---

## 查询优化

### 使用 EXPLAIN 分析查询

```sql
EXPLAIN SELECT * FROM users WHERE age > 25;
```

EXPLAIN 输出包括：
- 使用的索引
- 扫描的行数
- 查询代价
- 执行计划

### 优化建议

1. **使用索引**：为经常查询的列创建索引
2. **避免全表扫描**：使用 WHERE 子句过滤数据
3. **使用合适的索引类型**：
   - 等值查询使用哈希索引
   - 范围查询使用 B+ 树索引
   - 全文检索使用全文索引
4. **避免在 WHERE 子句中使用函数**：会导致索引失效
5. **使用 LIMIT 限制结果集大小**
6. **合理使用 JOIN**：避免笛卡尔积

### 查询缓存

QinDB 自动缓存查询结果，相同的查询会直接从缓存返回。可以通过配置文件调整缓存大小：

```ini
[Cache]
query_cache_size=100
enable_query_cache=true
```

---

## 数据导出

QinDB 支持将查询结果导出为多种格式：

### 导出为 CSV

```sql
SELECT * FROM users INTO OUTFILE 'users.csv' FORMAT CSV;
```

### 导出为 JSON

```sql
SELECT * FROM users INTO OUTFILE 'users.json' FORMAT JSON;
```

### 导出为 XML

```sql
SELECT * FROM users INTO OUTFILE 'users.xml' FORMAT XML;
```

### 导出为 HTML

```sql
SELECT * FROM users INTO OUTFILE 'users.html' FORMAT HTML;
```

---

## 系统维护

### VACUUM 清理

VACUUM 命令用于回收已删除数据占用的空间：

```sql
VACUUM table_name;
```

示例：
```sql
VACUUM users;
```

### 更新统计信息

```sql
ANALYZE table_name;
```

示例：
```sql
ANALYZE users;
```

### 检查数据库完整性

```sql
CHECK TABLE table_name;
```

### 查看系统信息

```sql
-- 查看数据库版本
SELECT VERSION();

-- 查看当前数据库
SELECT DATABASE();

-- 查看当前用户
SELECT USER();
```

---

## 故障排查

### 常见问题

#### 1. 连接失败

**问题：** 无法连接到数据库服务器

**解决方法：**
- 检查服务器是否启动
- 检查端口是否正确
- 检查防火墙设置
- 检查网络连接

#### 2. 权限不足

**问题：** 执行操作时提示权限不足

**解决方法：**
- 检查用户权限
- 使用管理员账户授予相应权限

#### 3. 查询性能慢

**问题：** 查询执行时间过长

**解决方法：**
- 使用 EXPLAIN 分析查询计划
- 为查询涉及的列创建索引
- 优化查询语句
- 增加缓冲池大小

#### 4. 磁盘空间不足

**问题：** 数据库空间占用过大

**解决方法：**
- 执行 VACUUM 清理
- 删除不需要的数据
- 增加磁盘空间

### 日志分析

查看日志文件 `qindb.log` 获取详细的错误信息：

```bash
tail -f qindb.log
```

日志级别：
- **DEBUG**：调试信息
- **INFO**：一般信息
- **WARNING**：警告信息
- **ERROR**：错误信息

---

## 附录

### 命令行参数

```
qindb [OPTIONS]

选项：
  --server              以服务器模式启动
  --host HOST           服务器地址（默认：localhost）
  --port PORT           服务器端口（默认：5432）
  --user USER           用户名
  --password PASSWORD   密码
  --database DATABASE   数据库名
  --config FILE         配置文件路径
  --help                显示帮助信息
  --version             显示版本信息
```

### SQL 关键字

常用 SQL 关键字（不区分大小写）：

```
SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER,
FROM, WHERE, JOIN, INNER, LEFT, RIGHT, ON,
GROUP BY, HAVING, ORDER BY, LIMIT,
INDEX, TABLE, DATABASE, USER,
BEGIN, COMMIT, ROLLBACK, TRANSACTION,
GRANT, REVOKE, PRIVILEGES
```

### 数据类型对照表

| 类型 | 大小 | 范围 | 说明 |
|------|------|------|------|
| INT | 4 字节 | -2^31 到 2^31-1 | 整数 |
| BIGINT | 8 字节 | -2^63 到 2^63-1 | 长整数 |
| DOUBLE | 8 字节 | ±1.7E±308 | 双精度浮点数 |
| VARCHAR(n) | 可变 | 最大 n 字符 | 变长字符串 |
| TEXT | 可变 | 无限制 | 长文本 |
| BOOLEAN | 1 字节 | true/false | 布尔值 |
| DATE | 4 字节 | - | 日期 |
| TIMESTAMP | 8 字节 | - | 时间戳 |

---

**版本：** 1.0.0
**最后更新：** 2025-10-26
