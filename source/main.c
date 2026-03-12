#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libevent_http.h"

void perr_exit(const char *str)
{
	perror(str);
	exit(1);
}

int main(int argc, char **argv)
{
    char *endptr = NULL;
    long port;

    if (argc < 3) {
        printf("Usage: ./server <port> <file_path>\n");
        printf("Example: ./server 9999 /opt\n");
        exit(1);
    }

    port = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || *endptr != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    if(chdir(argv[2]) < 0) {
        printf("Directory does not exist: %s\n", argv[2]);
        perr_exit("Chdir err:");
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
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons((unsigned short)port);

    listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
    				LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
    				(struct sockaddr *)&server, sizeof(server));
    if (!listener) {
    	fprintf(stderr, "Could not create a listener!\n");
    	return 1;
    }

    signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);
    if (!signal_event || event_add(signal_event, NULL) < 0) {
    	fprintf(stderr, "Could not create/add a signal event\n");
    	return 1;
    }

    printf("Serving directory: %s\n", argv[2]);
    printf("Listening on port: %ld\n", port);

    event_base_dispatch(base);

    evconnlistener_free(listener);
    event_free(signal_event);
    event_base_free(base);

    printf("Server Done!\n");

    return 0;
}
