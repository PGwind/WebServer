#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "libevent_http.h"
#include "url_conver.h"

#define HTTP_CLOSE "Connection: close\r\n"
#define MAX_REQUEST_HEADER_SIZE 16384

struct client_context {
    char client_ip[INET6_ADDRSTRLEN];
    char method[16];
    char path[4096];
    int status_code;
};

static const char CUSTOM_404_PATH[] = "page/404.html";

static const char NOT_FOUND_PAGE[] =
    "<html><head><meta charset=\"utf-8\"><title>404 Not Found</title></head>"
    "<body><h1>404 Not Found</h1><p>The requested resource was not found.</p>"
    "</body></html>";

static const char FORBIDDEN_PAGE[] =
    "<html><head><meta charset=\"utf-8\"><title>403 Forbidden</title></head>"
    "<body><h1>403 Forbidden</h1><p>Path traversal is not allowed.</p>"
    "</body></html>";

static int path_is_safe(const char *path)
{
    const char *segment = path;

    while (*segment != '\0') {
        while (*segment == '/') {
            ++segment;
        }

        if (segment[0] == '.' && segment[1] == '.' &&
            (segment[2] == '/' || segment[2] == '\0')) {
            return 0;
        }

        while (*segment != '\0' && *segment != '/') {
            ++segment;
        }
    }

    return 1;
}

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

static void log_error_message(const struct client_context *ctx, const char *fmt, ...)
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

static void log_info_message(const char *fmt, ...)
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

static void log_errno_message(const struct client_context *ctx, const char *action)
{
    log_error_message(ctx, "%s: %s", action, strerror(errno));
}

static void log_access(const struct client_context *ctx)
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

static int send_html_response(struct bufferevent *bev, struct client_context *ctx,
                              int no, const char *desp, const char *body)
{
    size_t body_len = strlen(body);

    if (ctx != NULL) {
        ctx->status_code = no;
    }

    send_header(bev, no, desp, "text/html; charset=utf-8", (long)body_len);
    bufferevent_write(bev, body, body_len);

    return 0;
}

static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t written = 0;

    if (dst_size == 0) {
        return;
    }

    while (*src != '\0' && written + 1 < dst_size) {
        const char *replacement = NULL;
        size_t replacement_len = 0;

        switch (*src) {
        case '&':
            replacement = "&amp;";
            replacement_len = 5;
            break;
        case '<':
            replacement = "&lt;";
            replacement_len = 4;
            break;
        case '>':
            replacement = "&gt;";
            replacement_len = 4;
            break;
        case '"':
            replacement = "&quot;";
            replacement_len = 6;
            break;
        case '\'':
            replacement = "&#39;";
            replacement_len = 5;
            break;
        default:
            dst[written++] = *src++;
            continue;
        }

        if (written + replacement_len >= dst_size) {
            break;
        }

        memcpy(dst + written, replacement, replacement_len);
        written += replacement_len;
        ++src;
    }

    dst[written] = '\0';
}

int response_http(struct bufferevent *bev, const char *method, char *path,
                  struct client_context *ctx)
{
    if (strcasecmp("GET", method) == 0) {
        strdecode(path, path);
        if (!path_is_safe(path)) {
            log_error_message(ctx, "blocked unsafe path=%s", path);
            send_html_response(bev, ctx, 403, "Forbidden", FORBIDDEN_PAGE);
            return -1;
        }

        char *file_path = &path[1];

        if (strcmp(path, "/") == 0 || strcmp(path, "/.") == 0) {
            file_path = "./";
        }

        if (access(file_path, R_OK) < 0) {
            if (errno == EACCES) {
                log_errno_message(ctx, "access denied");
                send_html_response(bev, ctx, 403, "Forbidden", FORBIDDEN_PAGE);
            } else {
                log_errno_message(ctx, "access file failed");
                send_error(bev, ctx);
            }
            return -1;
        }

        struct stat fs;
        if (stat(file_path, &fs) < 0) {
            log_errno_message(ctx, "stat file failed");
            send_error(bev, ctx);
            return -1;
        }

        if (S_ISDIR(fs.st_mode)) {
            if (ctx != NULL) {
                ctx->status_code = 200;
            }
            return send_dir(bev, file_path, ctx);
        }

        if (ctx != NULL) {
            ctx->status_code = 200;
        }
        send_header(bev, 200, "OK", get_file_type(file_path), fs.st_size);
        if (send_file_to_http(file_path, bev, ctx) < 0) {
            return -1;
        }
    }

    return 0;
}

int send_file_to_http(const char *filename, struct bufferevent *bev,
                      struct client_context *ctx)
{
    int fd = open(filename, O_RDONLY);
    int ret = 0;
    char buf[4096];

    if (fd < 0) {
        log_errno_message(ctx, "open file failed");
        return -1;
    }

    while ((ret = read(fd, buf, sizeof(buf))) > 0) {
        bufferevent_write(bev, buf, (size_t)ret);
    }

    if (ret < 0) {
        log_errno_message(ctx, "read file failed");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int send_header(struct bufferevent *bev, int no, const char *desp,
                const char *type, long len)
{
    char buf[256];

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", no, desp);
    bufferevent_write(bev, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Content-Type: %s\r\n", type);
    bufferevent_write(bev, buf, strlen(buf));

    if (len >= 0) {
        snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n", len);
        bufferevent_write(bev, buf, strlen(buf));
    }

    bufferevent_write(bev, HTTP_CLOSE, strlen(HTTP_CLOSE));
    bufferevent_write(bev, "\r\n", 2);

    return 0;
}

int send_error(struct bufferevent *bev, struct client_context *ctx)
{
    struct stat fs;

    if (ctx != NULL) {
        ctx->status_code = 404;
    }

    if (access(CUSTOM_404_PATH, R_OK) == 0 &&
        stat(CUSTOM_404_PATH, &fs) == 0 &&
        S_ISREG(fs.st_mode)) {
        send_header(bev, 404, "File Not Found", get_file_type(CUSTOM_404_PATH),
                    fs.st_size);
        if (send_file_to_http(CUSTOM_404_PATH, bev, ctx) == 0) {
            return 0;
        }
    }

    return send_html_response(bev, ctx, 404, "File Not Found", NOT_FOUND_PAGE);
}

int send_dir(struct bufferevent *bev, const char *dirname,
             struct client_context *ctx)
{
    char encode_name[1024];
    char escaped_dirname[PATH_MAX * 6];
    char escaped_name[2048];
    char path[PATH_MAX];
    char timestr[64];
    struct stat fs;
    struct dirent **dirinfo = NULL;
    int i;
    int num;
    char buf[4096];

    send_header(bev, 200, "OK", "text/html; charset=utf-8", -1);

    html_escape(dirname, escaped_dirname, sizeof(escaped_dirname));
    snprintf(buf, sizeof(buf),
             "<html><head><meta charset=\"utf-8\"><title>Index of %s</title></head>"
             "<body><h1>Index of %s</h1><table>",
             escaped_dirname, escaped_dirname);
    bufferevent_write(bev, buf, strlen(buf));

    num = scandir(dirname, &dirinfo, NULL, alphasort);
    if (num < 0) {
        const char *dir_error = "</table><p>Failed to read directory.</p></body></html>";
        log_errno_message(ctx, "scandir failed");
        bufferevent_write(bev, dir_error, strlen(dir_error));
        return -1;
    }

    for (i = 0; i < num; i++) {
        unsigned long long entry_size = 0;
        char size_str[32];

        if (strcmp(dirinfo[i]->d_name, ".") == 0 ||
            strcmp(dirinfo[i]->d_name, "..") == 0) {
            free(dirinfo[i]);
            continue;
        }

        strencode(encode_name, sizeof(encode_name), dirinfo[i]->d_name);
        html_escape(dirinfo[i]->d_name, escaped_name, sizeof(escaped_name));
        snprintf(path, sizeof(path), "%s/%s", dirname, dirinfo[i]->d_name);

        if (lstat(path, &fs) < 0) {
            snprintf(buf, sizeof(buf),
                     "<tr><td><a href=\"%s\">%s</a></td>"
                     "<td>-</td><td>Unknown</td></tr>\n",
                     encode_name, escaped_name);
        } else {
            strftime(timestr, sizeof(timestr), "%d %b %Y %H:%M",
                     localtime(&fs.st_mtime));

            if (S_ISDIR(fs.st_mode)) {
                snprintf(size_str, sizeof(size_str), "-");
            } else {
                entry_size = (unsigned long long)fs.st_size;
                if (entry_size >= 1024ULL * 1024ULL) {
                    snprintf(size_str, sizeof(size_str), "%.2f MB",
                             (double)entry_size / (1024.0 * 1024.0));
                } else {
                    snprintf(size_str, sizeof(size_str), "%.2f KB",
                             (double)entry_size / 1024.0);
                }
            }

            if (S_ISDIR(fs.st_mode)) {
                snprintf(buf, sizeof(buf),
                         "<tr><td><a href=\"%s/\">%s/</a></td>"
                         "<td>%s</td><td>%s</td></tr>\n",
                         encode_name, escaped_name, timestr, size_str);
            } else {
                snprintf(buf, sizeof(buf),
                         "<tr><td><a href=\"%s\">%s</a></td>"
                         "<td>%s</td><td>%s</td></tr>\n",
                         encode_name, escaped_name, timestr, size_str);
            }
        }

        bufferevent_write(bev, buf, strlen(buf));
        free(dirinfo[i]);
    }

    free(dirinfo);
    bufferevent_write(bev, "</table></body></html>",
                      strlen("</table></body></html>"));

    return 0;
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
                           "<html><body><h1>500 Internal Server Error</h1></body></html>");
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
                           "<html><body><h1>431 Request Header Fields Too Large</h1></body></html>");
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
                           "<html><body><h1>400 Bad Request</h1></body></html>");
        log_access(ctx);
        return;
    }

    if (ctx != NULL) {
        snprintf(ctx->method, sizeof(ctx->method), "%s", method);
        snprintf(ctx->path, sizeof(ctx->path), "%s", path);
    }

    if (strcasecmp(method, "GET") == 0) {
        response_http(bev, method, path, ctx);
    } else {
        send_html_response(bev, ctx, 405, "Method Not Allowed",
                           "<html><body><h1>405 Method Not Allowed</h1></body></html>");
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
    (void)sig;
    (void)events;
    struct event_base *base = user_data;
    struct timeval delay = {1, 0};

    log_info_message("received SIGINT, exiting cleanly in one second");
    event_base_loopexit(base, &delay);
}

void listener_cb(struct evconnlistener *listener,
                 evutil_socket_t fd, struct sockaddr *sa, int socklen,
                 void *user_data)
{
    (void)listener;
    (void)socklen;
    struct event_base *base = user_data;
    struct bufferevent *bev;
    struct client_context *ctx;

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
