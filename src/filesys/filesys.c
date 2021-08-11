#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "filesys/fsutil.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
  inode_init ();
  free_map_init ();

  printf("Initializing filesys swap...");
  block_buffer_init (64);
  printf ("done.\n");

  if (format) 
    do_format ();

  free_map_open ();
  // fsutil_ls (NULL);
  thread_current ()->dir = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  // fsutil_ls (NULL);
  free_map_close ();
  inode_close_all ();
  cache_save ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  // printf("creating %s, length %d\n", name, strlen(name));
  if (!name[0])
    return false;
  if (name[0] == '/' && name[1] == 0)
    return false;
  char file_name[18];
  struct dir *dir = fsutil_file_chdir (name, file_name);
  // printf("%s %s\n", name, file_name);
  if (dir == NULL) 
    return false;
  ASSERT (dir != NULL )
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, MODE_FILE)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // printf("trying to open %s\n", name);
  if (!name[0])
    return false;
  if (name[0] == '/' && name[1] == 0) {
    return file_open ( dir_get_inode (dir_open_root()) );
  }

  // struct dir *dir = dir_open_root ();
  char file_name[18];
  struct dir *dir = fsutil_file_chdir (name, file_name);
  // printf("%s %s\n", name, file_name);
  if (dir == NULL)
    return false;

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (!name[0])
    return false;
  if (name[0] == '/' && name[1] == 0)
    return false;
  // struct dir *dir = dir_open_root ();
  char file_name[18];
  struct dir *dir = fsutil_file_chdir (name, file_name);
  // printf("%s %s\n", name, file_name);
  if (dir == NULL)
    return false;

  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  struct dir *root = dir_open_root ();
  ASSERT( dir_add (root, ".", ROOT_DIR_SECTOR) );
  ASSERT( dir_add (root, "..", ROOT_DIR_SECTOR) );
  free_map_close ();
  printf ("done.\n");
  
}
