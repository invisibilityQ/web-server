# WebServer

### 介绍

该项目是基于C++ 实现的HTTP服务器,除基本功能之外，还实现了日志、定时器等特殊功能。

### 功能

+ 使用 线程池 + 非阻塞socket + epoll + 事件处理(Reactor模拟Proactor实现) 的并发模型

+ 使用状态机解析HTTP请求报文，目前支持GET方法

+ 添加定时器支持HTTP长连接，定时回调handler处理超时连接

+ 使用C++标准库双向链表list来管理定时器

+ 实现同步/异步日志系统，记录服务器运行状态

### 目录结构


```
.
├── http_conn
│   ├── http_conn.cpp
│   └── http_conn.h
├── lst_timer
│   ├── lst_timer.cpp
│   └── lst_timer.h
├── locker
│   └── locker.h
├── log
│   ├── log.cpp
│   └── log.h
├── main.cpp
├── Makefile
├── webserver
├── Threadpool.h
```


### 使用教程

在目录code下

编译代码：`make`，编译完成后`./webserver启动`，可选择端口号（默认为10000）

删除代码：`make clean`


# 压力测试
## 测试机环境
- OS: ubuntu-22.04.1(64位) (虚拟机，宿主机为Win11)

## 测试工具
- webbench

## 运行webbench的机器
- OS: CentOS-7(64位) (虚拟机，宿主机为Win11)

### 测试结果

![1](https://github.com/invisibilityQ/webserverphoto/blob/master/t5.png)

![2](https://github.com/invisibilityQ/webserverphoto/blob/master/t30.png)
### 参考资料

《TCP/IP网络编程》、《Linux高性能服务器》
