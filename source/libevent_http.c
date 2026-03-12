#include <dirent.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "libevent_http.h"
#include "url_conver.h"

#define HTTP_CLOSE "Connection: close\r\n"

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

static int send_html_response(struct bufferevent *bev, int no, const char *desp,
                              const char *body)
{
    size_t body_len = strlen(body);

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

int response_http(struct bufferevent *bev, const char *method, char *path)
{
    if (strcasecmp("GET", method) == 0) {
        strdecode(path, path);
        if (!path_is_safe(path)) {
            fprintf(stderr, "Blocked unsafe path: %s\n", path);
            send_html_response(bev, 403, "Forbidden", FORBIDDEN_PAGE);
            return -1;
        }

        char *file_path = &path[1];

        if (strcmp(path, "/") == 0 || strcmp(path, "/.") == 0) {
            file_path = "./";
        }
        printf("Http Request Resource Path = %s, File_path = %s\n", path, file_path);

        if (access(file_path, R_OK) < 0) {
            perror("access file err");
            if (errno == EACCES) {
                send_html_response(bev, 403, "Forbidden", FORBIDDEN_PAGE);
            } else {
                send_error(bev);
            }
            return -1;
        }

        struct stat fs;
        if (stat(file_path, &fs) < 0) {
            perror("stat file err");
            send_error(bev);
            return -1;
        }

        if (S_ISDIR(fs.st_mode)) {
            return send_dir(bev, file_path);
        }

        send_header(bev, 200, "OK", get_file_type(file_path), fs.st_size);
        if (send_file_to_http(file_path, bev) < 0) {
            fprintf(stderr, "Failed to send file: %s\n", file_path);
            return -1;
        }
    }

    return 0;
}

int send_file_to_http(const char *filename, struct bufferevent *bev)
{
    int fd = open(filename, O_RDONLY);
    int ret = 0;
    char buf[4096];

    if (fd < 0) {
        perror("open file err");
        return -1;
    }

    while ((ret = read(fd, buf, sizeof(buf))) > 0) {
        bufferevent_write(bev, buf, (size_t)ret);
    }

    if (ret < 0) {
        perror("read file err");
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

int send_error(struct bufferevent *bev)
{
    struct stat fs;

    if (access(CUSTOM_404_PATH, R_OK) == 0 &&
        stat(CUSTOM_404_PATH, &fs) == 0 &&
        S_ISREG(fs.st_mode)) {
        send_header(bev, 404, "File Not Found", get_file_type(CUSTOM_404_PATH),
                    fs.st_size);
        if (send_file_to_http(CUSTOM_404_PATH, bev) == 0) {
            return 0;
        }
    }

    return send_html_response(bev, 404, "File Not Found", NOT_FOUND_PAGE);
}

int send_dir(struct bufferevent *bev, const char *dirname)
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
        perror("scandir err");
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

    printf("Dir Read OK\n");

    return 0;
}

void conn_readcd(struct bufferevent *bev, void *user_data)
{
    (void)user_data;
    printf("Begin call %s.........\n", __FUNCTION__);

    char buf[4096] = {0};
    char method[50] = {0};
    char path[4096] = {0};
    char protocol[32] = {0};
    size_t read_len;

    read_len = bufferevent_read(bev, buf, sizeof(buf) - 1);
    if (read_len == 0) {
        return;
    }

    printf("\n%s\n", buf);
    if (sscanf(buf, "%49s %4095s %31s", method, path, protocol) != 3) {
        fprintf(stderr, "Invalid HTTP request line\n");
        send_html_response(bev, 400, "Bad Request",
                           "<html><body><h1>400 Bad Request</h1></body></html>");
        return;
    }
    printf("Method[%s] Path[%s] Protocol[%s]\n", method, path, protocol);

    if (strcasecmp(method, "GET") == 0) {
        response_http(bev, method, path);
    } else {
        send_html_response(bev, 405, "Method Not Allowed",
                           "<html><body><h1>405 Method Not Allowed</h1></body></html>");
    }

    printf("End call %s.........\n", __FUNCTION__);
}

void conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
    (void)user_data;
    printf("Begin call %s.........\n", __FUNCTION__);

    if (events & BEV_EVENT_EOF) {
        printf("Connection closed.\n");
    } else if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "Got an error on the connection: %s\n", strerror(errno));
    }

    bufferevent_free(bev);

    printf("End call %s.........\n", __FUNCTION__);
}

void signal_cb(evutil_socket_t sig, short events, void *user_data)
{
    (void)sig;
    (void)events;
    struct event_base *base = user_data;
    struct timeval delay = {1, 0};

    printf("Caught an interrupt signal; exiting cleanly in one second.\n");
    event_base_loopexit(base, &delay);
}

void listener_cb(struct evconnlistener *listener,
                 evutil_socket_t fd, struct sockaddr *sa, int socklen,
                 void *user_data)
{
    (void)listener;
    (void)sa;
    (void)socklen;
    printf("Begin call-------%s\n", __FUNCTION__);
    printf("fd is %d\n", fd);

    struct event_base *base = user_data;
    struct bufferevent *bev;

    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        fprintf(stderr, "Error constructing bufferevent\n");
        event_base_loopbreak(base);
        return;
    }

    bufferevent_flush(bev, EV_READ | EV_WRITE, BEV_NORMAL);
    bufferevent_setcb(bev, conn_readcd, NULL, conn_eventcb, NULL);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    printf("End call-------%s\n", __FUNCTION__);
}
