#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_NUM 12

const block_sector_t DIRECT_MAX = 12;
const block_sector_t INDIRECT_MAX = 140;
const block_sector_t DOUBLE_MAX = 16524;

// const off_t DIRECT_MAX = DIRECT_NUM;
// const off_t INDIRECT_MAX = DIRECT_MAX + BLOCK_SECTOR_SIZE / sizeof (block_sector_t);
// const off_t DOUBLE_MAX = INDIRECT_MAX + BLOCK_SECTOR_SIZE / sizeof (block_sector_t) * (INDIRECT_MAX - DIRECT_MAX);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
// struct inode_disk
//   {
//     block_sector_t start;               /* First data sector. */
//     off_t length;                       /* File size in bytes. */
//     unsigned magic;                     /* Magic number. */
//     uint32_t unused[125];               /* Not used. */
//   };
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[DIRECT_NUM];  /* Direct sector pointers */
    block_sector_t indirect;            /* Indirect sector pointer */
    block_sector_t double_indirect;     /* Double indirect sector pointer */
    
    uint32_t mode;                      /* Dir/file and other privileges */
    uint32_t unused[111];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock length_lock;            /* File length lock */
  };

static block_sector_t
lookup_sector (struct inode_disk *inode_disk, off_t pos, bool create)
{
  ASSERT (inode_disk != NULL);
  if (pos > inode_disk->length)
    return -1;

  block_sector_t sector_idx = pos / BLOCK_SECTOR_SIZE;

  // Direct
  if (sector_idx < DIRECT_MAX) {
    if (inode_disk->direct[sector_idx] == -1) 
      if (!create || !free_map_allocate (1, &inode_disk->direct[sector_idx]) )
        return -1;
    // printf("---direct %d %d\n", inode_disk->direct[sector_idx], create);
    return inode_disk->direct[sector_idx];
  }

  // Indirect list
  if (sector_idx < INDIRECT_MAX) {
    block_sector_t *indirect = NULL;

    if (inode_disk->indirect == -1) {
      if (!create || !free_map_allocate (1, &inode_disk->indirect) )
          return -1;
      indirect = (block_sector_t *)malloc(BLOCK_SECTOR_SIZE);
      ASSERT (indirect != NULL);

      memset(indirect, -1, BLOCK_SECTOR_SIZE );
    } else {
      indirect = (block_sector_t *)malloc(BLOCK_SECTOR_SIZE);
      ASSERT (indirect != NULL);
      block_buffer_read (inode_disk->indirect, indirect);
    }

    sector_idx -= DIRECT_MAX;

    if (indirect[sector_idx] == -1){
      if ( !create || !free_map_allocate (1, &indirect[sector_idx])) {
        free (indirect);
        return -1;
      }
      ASSERT (indirect[sector_idx] != -1);
    }
    block_buffer_write (inode_disk->indirect, indirect);
    sector_idx = indirect[sector_idx];
    free(indirect);
    // printf("---indirect %d %d\n", sector_idx,  create);
    return sector_idx;
  }

  // Double indirect 
  block_sector_t **double_indirect = NULL, *indirect = NULL;
  if (sector_idx < DOUBLE_MAX) {
    if( inode_disk->double_indirect == -1) {
      if (!create || !free_map_allocate (1, &inode_disk->double_indirect)) 
          return -1;

      double_indirect = (block_sector_t *) malloc (BLOCK_SECTOR_SIZE);
      ASSERT (double_indirect != NULL);
      memset(double_indirect, -1, BLOCK_SECTOR_SIZE);

    } else {
      double_indirect = (block_sector_t *) malloc (BLOCK_SECTOR_SIZE);
      ASSERT (double_indirect != NULL);
      block_buffer_read (inode_disk->double_indirect, double_indirect);
    }

    // Decide entry block for the second layer
    sector_idx -= INDIRECT_MAX;
    block_sector_t group = sector_idx / (INDIRECT_MAX - DIRECT_MAX);
    block_sector_t offset = sector_idx % (INDIRECT_MAX - DIRECT_MAX);
    block_sector_t indirect_sector;

    if (double_indirect[group] == -1) {
      if (!create || !free_map_allocate (1, &double_indirect[group])) {
        free (double_indirect);
        return -1;
      } 
      // Write back since double_indirect directory has been changed
      block_buffer_write (inode_disk->double_indirect, (block_sector_t *)double_indirect);

      indirect_sector = double_indirect[group];
      indirect = (block_sector_t *)double_indirect;
      memset(indirect, -1, BLOCK_SECTOR_SIZE);

    } else {
      indirect_sector = double_indirect[group];
      indirect = (block_sector_t *)double_indirect;
      block_buffer_read (double_indirect[group], indirect);
    }

    // Process the second layer with mapping [sector_idx: buffer] = [indirect_sector: indirect]
    if (indirect[offset] == -1) {
      // If not create or failed
      if (!create || !free_map_allocate (1, &indirect[offset] )) {
        free (indirect);
        return -1;
      } 
    }
    block_buffer_write (indirect_sector, indirect);
    sector_idx = indirect[offset];
    free (indirect);
    // printf("---double_indirect %d\n", sector_idx);
    return sector_idx;

  }
  NOT_REACHED ();
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return lookup_sector (&inode->data, pos, false);
  else 
    return -1;
}
// static block_sector_t
// byte_to_sector (const struct inode *inode, off_t pos) 
// {
//   ASSERT (inode != NULL);
//   if (pos < inode->data.length)
//     return inode->data.start + pos / BLOCK_SECTOR_SIZE;
//   else
//     return -1;
// }

static bool 
inode_extend (struct inode_disk *disk_inode, off_t max_length)
{
  ASSERT (disk_inode != NULL);
  static char zeros[BLOCK_SECTOR_SIZE];
  if ( disk_inode->length < max_length) {
    size_t sec_begin = bytes_to_sectors (disk_inode->length);
    size_t sec_end = bytes_to_sectors (max_length);

    // printf("sector: %d %d\n", sec_begin, sec_end);
    disk_inode->length = sec_begin * BLOCK_SECTOR_SIZE;
    // Lookup and create space
    // block_sector_t faq;
    // ASSERT( free_map_allocate (1,&faq) );
    for (int i = sec_begin; i < sec_end; i++) {
      block_sector_t file_sector = lookup_sector (disk_inode, i * BLOCK_SECTOR_SIZE, true);
      if (file_sector == -1) {
        break;
      }
      else {
        disk_inode->length += BLOCK_SECTOR_SIZE;
        block_buffer_write (file_sector, zeros);
      }
    }
  }
  if ( disk_inode->length >= max_length ) {
    disk_inode->length = max_length;
    return true;
  }
  else 
    return false;
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t mode)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = 0;
    disk_inode->magic = INODE_MAGIC;
    memset (disk_inode->direct, -1, sizeof (disk_inode->direct));
    disk_inode->indirect = -1;
    disk_inode->double_indirect = -1;
    disk_inode->mode = mode;
    // printf("before----%d\n", disk_inode->length);
    // printf("---sector %d, length %d, real length %d\n", sector, length, disk_inode->length);
    inode_extend (disk_inode, length);
    // printf("after----%d\n", disk_inode->length);
    
    block_buffer_write (sector, disk_inode);
    free (disk_inode);

    return true;
  } else {
    return false;
  }
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }
  
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init (&inode->length_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_buffer_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Returns INODE's file mode. */
uint32_t 
inode_get_mode (const struct inode *inode)
{
  return inode->data.mode;
}


/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          // Release inode disk
          free_map_release (inode->sector, 1);
          // Release files
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
          struct inode_disk *disk = &inode->data;
          // Release direct map
          for (int i = 0; i < DIRECT_NUM; i++)
            if ( disk->direct[i] != -1 )
              free_map_release (disk->direct[i], 1);
          // Release indirect map
          if (disk->indirect != -1) {
            block_sector_t *indirect = (block_sector_t *)malloc(BLOCK_SECTOR_SIZE);
            block_buffer_read (disk->indirect, indirect);
            for (int i = 0; i < INDIRECT_MAX - DIRECT_MAX; i++) {
              if ( indirect[i] != -1)
                 free_map_release (indirect[i], 1);
            }
            free (indirect);
          }

          if (disk->double_indirect != -1) {
            block_sector_t **double_indirect, *indirect;
            double_indirect = malloc(BLOCK_SECTOR_SIZE);
            indirect = malloc(BLOCK_SECTOR_SIZE);
            block_buffer_read (disk->double_indirect, double_indirect);
            for (int i = 0; i < INDIRECT_MAX - DIRECT_MAX; i ++) {
              if (double_indirect[i] != -1) {
                block_buffer_read (double_indirect[i], indirect);
                for (int j = 0; j < INDIRECT_MAX - DIRECT_MAX; j++)
                  if (indirect[j] == -1) {
                    free_map_release (indirect[j], 1);
                  }
                free_map_release(double_indirect[i], 1);
              }
            }
            free_map_release (disk->double_indirect, 1);

            free (double_indirect);
            free (indirect);
          }
        }
        else {
          block_buffer_write (inode->sector, &inode->data);
        }
      free (inode); 
    }
}

void inode_close_all (void)
{
  struct list_elem *e;
  struct inode *inode;

  for (e = list_begin (&open_inodes); e != list_end (&open_inodes); e = list_begin (&open_inodes)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->open_cnt) 
        {
          inode->open_cnt = 1;
          inode_close (inode);
        }
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  // read availiable size 
  bool hold = false;
  if ( !lock_held_by_current_thread (&inode->length_lock) ) {
    lock_acquire (&inode->length_lock);
    hold = true;
  }
  size = size < inode_length (inode) - offset? size : inode_length (inode) - offset;
  if (hold) 
        lock_release (&inode->length_lock);
  
  // offset + size <= inode_length (inode) 

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      // off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      // int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_buffer_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_buffer_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // printf("???\n");
  // printf("%d %d %d\n", inode->data.length, offset, size);
  bool hold_lock = false;

  if ( offset + size > inode_length (inode)) {
    if ( !lock_held_by_current_thread (&inode->length_lock) ) {
      lock_acquire (&inode->length_lock);
      hold_lock = true;
    }
    inode_extend (&inode->data, offset + size);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      // off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      // int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_buffer_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_buffer_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_buffer_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  if (hold_lock)
    lock_release (&inode->length_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}


/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  
  // lock_acquire (&inode->length_lock);
  off_t length = inode->data.length;
  // lock_release (&inode->length_lock);

  return length;
}

