#ifndef _URL_CONVER_H
#define _URL_CONVER_H

#include <stddef.h>

const char *get_file_type(const char *name);

void strdecode(char *to, char *from);

int hexit(char c);

void strencode(char* to, size_t tosize, const char* from);

#endif
