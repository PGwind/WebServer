#ifndef _URL_CONVER_H
#define _URL_CONVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

const char *get_file_type(char *name);

void strdecode(char *to, char *from);

int hexit(char c);

void strencode(char* to, size_t tosize, const char* from);

#endif
