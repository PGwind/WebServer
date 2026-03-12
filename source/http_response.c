#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "directory_listing.h"
#include "http_common.h"
#include "http_response.h"
#include "url_conver.h"

#define HTTP_CLOSE "Connection: close\r\n"
#define SERVER_HEADER "Server: TinyWeb/libevent\r\n"

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

static void format_http_date(time_t when, char *buf, size_t buf_size)
{
    struct tm gm_tm;

    if (buf_size == 0) {
        return;
    }

    gmtime_r(&when, &gm_tm);
    strftime(buf, buf_size, "%a, %d %b %Y %H:%M:%S GMT", &gm_tm);
}

static int request_should_send_body(const char *method)
{
    return strcasecmp(method, "HEAD") != 0;
}

static void build_last_modified_header(time_t mtime, char *buf, size_t buf_size)
{
    char http_date[64];

    if (buf_size == 0) {
        return;
    }

    format_http_date(mtime, http_date, sizeof(http_date));
    snprintf(buf, buf_size, "Last-Modified: %s\r\n", http_date);
}

int send_html_response(struct bufferevent *bev, struct client_context *ctx,
                       int no, const char *desp, const char *body,
                       int send_body, const char *extra_headers)
{
    size_t body_len = strlen(body);

    if (ctx != NULL) {
        ctx->status_code = no;
    }

    send_header(bev, no, desp, "text/html; charset=utf-8", (long)body_len,
                extra_headers);
    if (send_body) {
        bufferevent_write(bev, body, body_len);
    }

    return 0;
}

int response_http(struct bufferevent *bev, const char *method, char *path,
                  struct client_context *ctx)
{
    int send_body = request_should_send_body(method);

    if (strcasecmp("GET", method) == 0 || strcasecmp("HEAD", method) == 0) {
        strdecode(path, path);
        if (!path_is_safe(path)) {
            log_error_message(ctx, "blocked unsafe path=%s", path);
            send_html_response(bev, ctx, 403, "Forbidden", FORBIDDEN_PAGE,
                               send_body, NULL);
            return -1;
        }

        char *file_path = &path[1];

        if (strcmp(path, "/") == 0 || strcmp(path, "/.") == 0) {
            file_path = "./";
        }

        if (access(file_path, R_OK) < 0) {
            if (errno == EACCES) {
                log_errno_message(ctx, "access denied");
                send_html_response(bev, ctx, 403, "Forbidden", FORBIDDEN_PAGE,
                                   send_body, NULL);
            } else {
                log_errno_message(ctx, "access file failed");
                send_error(bev, ctx, send_body);
            }
            return -1;
        }

        struct stat fs;
        if (stat(file_path, &fs) < 0) {
            log_errno_message(ctx, "stat file failed");
            send_error(bev, ctx, send_body);
            return -1;
        }

        if (S_ISDIR(fs.st_mode)) {
            if (ctx != NULL) {
                ctx->status_code = 200;
            }
            return send_dir(bev, file_path, ctx, send_body);
        }

        if (ctx != NULL) {
            ctx->status_code = 200;
        }

        {
            char last_modified_header[96];

            build_last_modified_header(fs.st_mtime, last_modified_header,
                                       sizeof(last_modified_header));
            send_header(bev, 200, "OK", get_file_type(file_path), fs.st_size,
                        last_modified_header);
        }

        if (send_file_to_http(file_path, bev, ctx, send_body) < 0) {
            return -1;
        }
    }

    return 0;
}

int send_file_to_http(const char *filename, struct bufferevent *bev,
                      struct client_context *ctx, int send_body)
{
    int fd = open(filename, O_RDONLY);
    int ret = 0;
    char buf[4096];
    struct stat file_stat;
    struct evbuffer *output;

    if (fd < 0) {
        log_errno_message(ctx, "open file failed");
        return -1;
    }

    if (!send_body) {
        close(fd);
        return 0;
    }

    if (fstat(fd, &file_stat) < 0) {
        log_errno_message(ctx, "fstat file failed");
        close(fd);
        return -1;
    }

    if (S_ISREG(file_stat.st_mode)) {
        output = bufferevent_get_output(bev);
        if (evbuffer_add_file(output, fd, 0, file_stat.st_size) == 0) {
            return 0;
        }
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
                const char *type, long len, const char *extra_headers)
{
    char buf[256];
    char date_header[96];
    char http_date[64];
    time_t now = time(NULL);

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", no, desp);
    bufferevent_write(bev, buf, strlen(buf));

    format_http_date(now, http_date, sizeof(http_date));
    snprintf(date_header, sizeof(date_header), "Date: %s\r\n", http_date);
    bufferevent_write(bev, date_header, strlen(date_header));

    bufferevent_write(bev, SERVER_HEADER, strlen(SERVER_HEADER));

    snprintf(buf, sizeof(buf), "Content-Type: %s\r\n", type);
    bufferevent_write(bev, buf, strlen(buf));

    if (len >= 0) {
        snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n", len);
        bufferevent_write(bev, buf, strlen(buf));
    }

    if (extra_headers != NULL && extra_headers[0] != '\0') {
        bufferevent_write(bev, extra_headers, strlen(extra_headers));
    }

    bufferevent_write(bev, HTTP_CLOSE, strlen(HTTP_CLOSE));
    bufferevent_write(bev, "\r\n", 2);

    return 0;
}

int send_error(struct bufferevent *bev, struct client_context *ctx, int send_body)
{
    struct stat fs;
    char last_modified_header[96];

    if (ctx != NULL) {
        ctx->status_code = 404;
    }

    if (access(CUSTOM_404_PATH, R_OK) == 0 &&
        stat(CUSTOM_404_PATH, &fs) == 0 &&
        S_ISREG(fs.st_mode)) {
        build_last_modified_header(fs.st_mtime, last_modified_header,
                                   sizeof(last_modified_header));
        send_header(bev, 404, "File Not Found", get_file_type(CUSTOM_404_PATH),
                    fs.st_size, last_modified_header);
        if (send_file_to_http(CUSTOM_404_PATH, bev, ctx, send_body) == 0) {
            return 0;
        }
    }

    return send_html_response(bev, ctx, 404, "File Not Found", NOT_FOUND_PAGE,
                              send_body, NULL);
}
