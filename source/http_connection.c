#include <arpa/inet.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "http_common.h"
#include "http_connection.h"
#include "http_response.h"

#define MAX_REQUEST_HEADER_SIZE 16384

static const char ALLOW_GET_HEAD_HEADER[] = "Allow: GET, HEAD\r\n";

static void get_timestamp(char *buf, size_t buf_size)
{
    time_t now = time(NULL);
    struct tm local_tm;

    if (buf_size == 0) {
        return;
    }

    localtime_r(&now, &local_tm);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &local_tm);
}

void log_error_message(const struct client_context *ctx, const char *fmt, ...)
{
    char timestamp[32];
    const char *client_ip = "unknown";
    va_list args;

    if (ctx != NULL && ctx->client_ip[0] != '\0') {
        client_ip = ctx->client_ip;
    }

    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(stderr, "[%s] ERROR client=%s ", timestamp, client_ip);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}

void log_info_message(const char *fmt, ...)
{
    char timestamp[32];
    va_list args;

    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(stdout, "[%s] INFO ", timestamp);

    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fputc('\n', stdout);
}

void log_errno_message(const struct client_context *ctx, const char *action)
{
    log_error_message(ctx, "%s: %s", action, strerror(errno));
}

void log_access(const struct client_context *ctx)
{
    char timestamp[32];
    const char *client_ip = "unknown";
    const char *method = "-";
    const char *path = "-";

    if (ctx == NULL) {
        return;
    }

    if (ctx->client_ip[0] != '\0') {
        client_ip = ctx->client_ip;
    }
    if (ctx->method[0] != '\0') {
        method = ctx->method;
    }
    if (ctx->path[0] != '\0') {
        path = ctx->path;
    }

    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(stdout, "[%s] ACCESS client=%s method=%s path=%s status=%d\n",
            timestamp, client_ip, method, path, ctx->status_code);
}

static void fill_client_ip(const struct sockaddr *sa, char *buf, size_t buf_size)
{
    void *addr_ptr = NULL;

    if (buf_size == 0) {
        return;
    }

    buf[0] = '\0';
    if (sa == NULL) {
        snprintf(buf, buf_size, "unknown");
        return;
    }

    if (sa->sa_family == AF_INET) {
        addr_ptr = &((const struct sockaddr_in *)sa)->sin_addr;
    } else if (sa->sa_family == AF_INET6) {
        addr_ptr = &((const struct sockaddr_in6 *)sa)->sin6_addr;
    }

    if (addr_ptr == NULL ||
        inet_ntop(sa->sa_family, addr_ptr, buf, (socklen_t)buf_size) == NULL) {
        snprintf(buf, buf_size, "unknown");
    }
}

static ssize_t find_request_header_end(const char *data, size_t len)
{
    size_t i;

    if (len < 4) {
        return -1;
    }

    for (i = 0; i + 3 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return (ssize_t)(i + 4);
        }
    }

    return -1;
}

void conn_readcd(struct bufferevent *bev, void *user_data)
{
    struct client_context *ctx = user_data;
    struct evbuffer *input;
    const char *raw_data;
    char request_buf[MAX_REQUEST_HEADER_SIZE + 1];
    char method[50] = {0};
    char path[4096] = {0};
    char protocol[32] = {0};
    char *line_end;
    size_t inspect_len;
    size_t input_len;
    ssize_t header_len;

    input = bufferevent_get_input(bev);
    input_len = evbuffer_get_length(input);
    if (input_len == 0) {
        return;
    }

    inspect_len = input_len;
    if (inspect_len > MAX_REQUEST_HEADER_SIZE) {
        inspect_len = MAX_REQUEST_HEADER_SIZE;
    }

    raw_data = (const char *)evbuffer_pullup(input, (ev_ssize_t)inspect_len);
    if (raw_data == NULL) {
        log_error_message(ctx, "failed to read request buffer");
        send_html_response(bev, ctx, 500, "Internal Server Error",
                           "<html><body><h1>500 Internal Server Error</h1></body></html>",
                           1, NULL);
        evbuffer_drain(input, input_len);
        log_access(ctx);
        return;
    }

    header_len = find_request_header_end(raw_data, inspect_len);
    if (header_len < 0 && input_len > MAX_REQUEST_HEADER_SIZE) {
        if (ctx != NULL) {
            snprintf(ctx->method, sizeof(ctx->method), "%s", "-");
            snprintf(ctx->path, sizeof(ctx->path), "%s", "-");
        }
        log_error_message(ctx, "request header too large");
        send_html_response(bev, ctx, 431, "Request Header Fields Too Large",
                           "<html><body><h1>431 Request Header Fields Too Large</h1></body></html>",
                           1, NULL);
        evbuffer_drain(input, input_len);
        log_access(ctx);
        return;
    }

    if (header_len < 0) {
        return;
    }

    memcpy(request_buf, raw_data, (size_t)header_len);
    request_buf[header_len] = '\0';
    evbuffer_drain(input, (size_t)header_len);

    line_end = strstr(request_buf, "\r\n");
    if (line_end != NULL) {
        *line_end = '\0';
    }

    if (sscanf(request_buf, "%49s %4095s %31s", method, path, protocol) != 3) {
        if (ctx != NULL) {
            snprintf(ctx->method, sizeof(ctx->method), "%s", "-");
            snprintf(ctx->path, sizeof(ctx->path), "%s", "-");
        }
        log_error_message(ctx, "invalid HTTP request line");
        send_html_response(bev, ctx, 400, "Bad Request",
                           "<html><body><h1>400 Bad Request</h1></body></html>",
                           1, NULL);
        log_access(ctx);
        return;
    }

    if (ctx != NULL) {
        snprintf(ctx->method, sizeof(ctx->method), "%s", method);
        snprintf(ctx->path, sizeof(ctx->path), "%s", path);
    }

    if (strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0) {
        response_http(bev, method, path, ctx);
    } else {
        send_html_response(bev, ctx, 405, "Method Not Allowed",
                           "<html><body><h1>405 Method Not Allowed</h1></body></html>",
                           1, ALLOW_GET_HEAD_HEADER);
    }

    log_access(ctx);
}

void conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
    struct client_context *ctx = user_data;

    if (events & BEV_EVENT_ERROR) {
        log_errno_message(ctx, "connection error");
    }

    bufferevent_free(bev);
    free(ctx);
}

void signal_cb(evutil_socket_t sig, short events, void *user_data)
{
    struct event_base *base = user_data;
    struct timeval delay = {1, 0};

    (void)sig;
    (void)events;

    log_info_message("received SIGINT, exiting cleanly in one second");
    event_base_loopexit(base, &delay);
}

void listener_cb(struct evconnlistener *listener,
                 evutil_socket_t fd, struct sockaddr *sa, int socklen,
                 void *user_data)
{
    struct event_base *base = user_data;
    struct bufferevent *bev;
    struct client_context *ctx;

    (void)listener;
    (void)socklen;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        log_error_message(NULL, "error allocating client context");
        evutil_closesocket(fd);
        return;
    }
    fill_client_ip(sa, ctx->client_ip, sizeof(ctx->client_ip));

    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        log_error_message(ctx, "error constructing bufferevent");
        evutil_closesocket(fd);
        free(ctx);
        event_base_loopbreak(base);
        return;
    }

    bufferevent_flush(bev, EV_READ | EV_WRITE, BEV_NORMAL);
    bufferevent_setcb(bev, conn_readcd, NULL, conn_eventcb, ctx);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}
