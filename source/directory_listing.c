#include <dirent.h>
#include <event2/bufferevent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "directory_listing.h"
#include "http_common.h"
#include "http_response.h"
#include "url_conver.h"

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

int send_dir(struct bufferevent *bev, const char *dirname,
             struct client_context *ctx, int send_body)
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

    send_header(bev, 200, "OK", "text/html; charset=utf-8", -1, NULL);

    if (!send_body) {
        return 0;
    }

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
