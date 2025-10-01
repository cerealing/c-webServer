# C语言系统编程实践项目集合

这个仓库包含多个用C语言实现的系统编程项目，每个项目都专注于不同的技术领域和编程概念。

## 项目结构

```
c-webServer/
├── part1/          # 多线程HTTP Web服务器
├── part2/          # (计划中) 
├── part3/          # (计划中)
├── part4/          # (计划中) 
├── part5/          # (计划中)
└── README.md       # 本文件
```

## Part 1 - 多线程HTTP Web服务器

📁 **目录**: `part1/`

一个完整的HTTP/1.0 Web服务器实现，展示了：

### 核心技术
- **网络编程**: Socket API、TCP连接管理
- **多线程编程**: pthread库、线程安全
- **HTTP协议**: 请求解析、响应生成
- **文件I/O**: 高效的文件传输
- **内存管理**: 动态内存分配、资源清理

### 特色功能
- ✅ 并发连接处理
- ✅ 静态文件服务
- ✅ MIME类型自动识别
- ✅ 路径安全验证
- ✅ 错误页面处理
- ✅ 高性能文件传输

### 学习价值
- 理解HTTP协议工作原理
- 掌握Socket网络编程
- 学习多线程并发处理
- 实践系统调用和文件操作

[查看Part 1详细文档 →](part1/README.md)

---

## 未来计划

### Part 2 - 计划中
可能的方向：
- 进程间通信(IPC)示例
- 信号处理机制
- 管道和消息队列

### Part 3 - 计划中  
可能的方向：
- 内存映射文件
- 共享内存编程
- 文件系统操作

### Part 4 - 计划中
可能的方向：
- 数据结构实现
- 算法优化
- 性能分析工具

### Part 5 - 计划中
可能的方向：
- 网络协议实现
- 加密和安全
- 系统监控工具

---

## 编译环境

推荐使用以下环境：
- **操作系统**: Linux (Ubuntu 20.04+)
- **编译器**: GCC 9.0+
- **标准**: C99或更新
- **依赖**: pthread库

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/cerealing/c-webServer.git
cd c-webServer

# 运行Part 1项目
cd part1
gcc -Wall -Wextra -pthread myweb.c -o myweb
./myweb 8080

# 在浏览器中访问 http://localhost:8080
```

## 贡献

欢迎提交Issue和Pull Request！

## 许可证

MIT License

---

**作者**: cerealing  
**仓库**: https://github.com/cerealing/c-webServer