#include "total.h"

void perr_exit(const char *str)
{
	perror(str);
	exit(1);
}

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        printf("Please Input: ./server port path\n");
        exit(1);
    }

    // 切换当前工作目录到由命令行参数 argv[2] 指定的目录
    if(chdir(argv[2]) < 0) {
        printf("dir does not exist: %s\n", argv[2]);
        perr_exit("chdir err:");
    }

    struct event_base *base;
    struct evconnlistener *listener;
    struct event *signal_event;

    struct sockaddr_in server;
    base = event_base_new();
    if (!base) {
    	fprintf(stderr, "Could not initialize!\n");
    	return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));

    // 创建监听的套接字，绑定，监听，接受连接请求
    listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
    				LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
    				(struct sockaddr *)&server, sizeof(server));
    if (!listener) {
    	fprintf(stderr, "Could not create a listener!\n");
    	return 1;
    }

    // 创建信号事件，捕捉并处理
    signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);
    if (!signal_event || event_add(signal_event, NULL) < 0) {
    	fprintf(stderr, "Could not create/add a signal event\n");
    	return 1;
    }

    // 事件循环
    event_base_dispatch(base);

    // 释放
    evconnlistener_free(listener);
    event_free(signal_event);
    event_base_free(base);

    printf("Server Done!\n");

    return 0;
}
