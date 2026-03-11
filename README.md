![example](https://img.shields.io/badge/tiny_web_server-v1.0-green.svg)   ![example](https://img.shields.io/badge/libevent-v2.1.12-blue.svg)

## 介绍

基于 `libevent` 库，使用C语言开发的一个轻量级 `WebServer` 

## 使用

首先安装 `libevent` 库

```shell
# 安装
sudo apt update
sudo apt install -y build-essential pkg-config libevent-dev
# 查看安装是否成功
pkg-config --modversion libevent 
# 如果安装成功，显示： 2.1.12-stable
```

接着 `clone` 项目

```shell
git clone https://github.com/PGwind/WebServer.git
```

然后执行脚本 `start.sh`

```shell
chmod +x start.sh
./start.sh

# 也可以指定端口和静态资源目录
./start.sh 9999 /opt
```

也可以自己编译执行

```c
cd WebServer
cd build
make
cd ..
./server port path

# 举个栗子： 
./server 9999 /opt
```

其中，`port` 为指定的端口号，`path` 为访问根目录，访问链接为：`ip:port`

项目现在内置了默认 404 页面，不再依赖额外的绝对路径配置。

## 已优化

- 修复目录列表中的文件夹大小计算错误
- 内置默认 404 页面，去掉绝对路径依赖
- `start.sh` 支持自定义端口和静态资源目录

## 演示

![](./images/http_server_libevent_1.png)



![](./images/http_server_libevent_2.png)
