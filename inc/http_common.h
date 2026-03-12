#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#define CLIENT_IP_MAX_LEN 64

struct client_context {
    char client_ip[CLIENT_IP_MAX_LEN];
    char method[16];
    char path[4096];
    int status_code;
};

void log_error_message(const struct client_context *ctx, const char *fmt, ...);

void log_info_message(const char *fmt, ...);

void log_errno_message(const struct client_context *ctx, const char *action);

void log_access(const struct client_context *ctx);

#endif
