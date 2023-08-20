![example](https://img.shields.io/badge/tiny_web_server-v1.0-green.svg)   ![example](https://img.shields.io/badge/libevent-v2.1.12-blue.svg)

## 介绍

基于 `libevent` 库，使用C语言开发的一个轻量级 `WebServer` 

## 使用

首先安装 `libevent` 库

```shell
# 安装
sudo apt install libevent-dev
# 查看安装是否成功
pkg-config --modversion libevent 
# 如果安装成功，显示： 2.1.12-stable
```

接着 `clone` 项目

```shell
git clone https://github.com/PGwind/WebServer.git
cd WebServer
cd build
make
cd ..
./server port path
```

其中，`port` 为指定的端口号，`path` 为访问根目录，访问链接为：`ip:port`

## 演示

![](./images/http_server_libevent_1.png)



![](./images/http_server_libevent_2.png)

