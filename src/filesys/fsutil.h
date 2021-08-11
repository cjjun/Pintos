#ifndef FILESYS_FSUTIL_H
#define FILESYS_FSUTIL_H

#include <stdbool.h>

void fsutil_ls (char **argv);
void fsutil_cat (char **argv);
void fsutil_rm (char **argv);
void fsutil_extract (char **argv);
void fsutil_append (char **argv);

bool fsutil_chdir (const char *dir_name);
bool fsutil_mkdir (const char *dir_name);
struct dir *fsutil_file_chdir (const char *file_path, char *file_name);

#endif /* filesys/fsutil.h */
