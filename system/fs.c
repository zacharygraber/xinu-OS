#include <xinu.h>
#include <kernel.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef FS
#include <fs.h>

static fsystem_t fsd;
int dev0_numblocks;
int dev0_blocksize;
char *dev0_blocks;

extern int dev0;

char block_cache[512];

#define SB_BLK 0 // Superblock
#define BM_BLK 1 // Bitmapblock

#define NUM_FD 16

filetable_t oft[NUM_FD]; // open file table
#define isbadfd(fd) (fd < 0 || fd >= NUM_FD || oft[fd].in.id == EMPTY)

#define INODES_PER_BLOCK (fsd.blocksz / sizeof(inode_t))
#define NUM_INODE_BLOCKS (( (fsd.ninodes % INODES_PER_BLOCK) == 0) ? fsd.ninodes / INODES_PER_BLOCK : (fsd.ninodes / INODES_PER_BLOCK) + 1)
#define FIRST_INODE_BLOCK 2

/**
 * Helper functions
 */
int _fs_fileblock_to_diskblock(int dev, int fd, int fileblock) {
  int diskblock;

  if (fileblock >= INODEDIRECTBLOCKS) {
    errormsg("No indirect block support! (%d >= %d)\n", fileblock, INODEBLOCKS - 2);
    return SYSERR;
  }

  // Get the logical block address
  diskblock = oft[fd].in.blocks[fileblock];

  return diskblock;
}

/**
 * Filesystem functions
 */
int _fs_get_inode_by_num(int dev, int inode_number, inode_t *out) {
  int bl, inn;
  int inode_off;

  if (dev != dev0) {
    errormsg("Unsupported device: %d\n", dev);
    return SYSERR;
  }
  if (inode_number > fsd.ninodes) {
    errormsg("inode %d out of range (> %s)\n", inode_number, fsd.ninodes);
    return SYSERR;
  }

  bl  = inode_number / INODES_PER_BLOCK;
  inn = inode_number % INODES_PER_BLOCK;
  bl += FIRST_INODE_BLOCK;

  inode_off = inn * sizeof(inode_t);

  bs_bread(dev0, bl, 0, &block_cache[0], fsd.blocksz);
  memcpy(out, &block_cache[inode_off], sizeof(inode_t));

  return OK;

}

int _fs_put_inode_by_num(int dev, int inode_number, inode_t *in) {
  int bl, inn;

  if (dev != dev0) {
    errormsg("Unsupported device: %d\n", dev);
    return SYSERR;
  }
  if (inode_number > fsd.ninodes) {
    errormsg("inode %d out of range (> %d)\n", inode_number, fsd.ninodes);
    return SYSERR;
  }

  bl = inode_number / INODES_PER_BLOCK;
  inn = inode_number % INODES_PER_BLOCK;
  bl += FIRST_INODE_BLOCK;

  bs_bread(dev0, bl, 0, block_cache, fsd.blocksz);
  memcpy(&block_cache[(inn*sizeof(inode_t))], in, sizeof(inode_t));
  bs_bwrite(dev0, bl, 0, block_cache, fsd.blocksz);

  return OK;
}

int fs_mkfs(int dev, int num_inodes) {
  int i;

  if (dev == dev0) {
    fsd.nblocks = dev0_numblocks;
    fsd.blocksz = dev0_blocksize;
  } else {
    errormsg("Unsupported device: %d\n", dev);
    return SYSERR;
  }

  if (num_inodes < 1) {
    fsd.ninodes = DEFAULT_NUM_INODES;
  } else {
    fsd.ninodes = num_inodes;
  }

  i = fsd.nblocks;
  while ( (i % 8) != 0) { i++; }
  fsd.freemaskbytes = i / 8;

  if ((fsd.freemask = getmem(fsd.freemaskbytes)) == (void *) SYSERR) {
    errormsg("fs_mkfs memget failed\n");
    return SYSERR;
  }

  /* zero the free mask */
  for(i = 0; i < fsd.freemaskbytes; i++) {
    fsd.freemask[i] = '\0';
  }

  fsd.inodes_used = 0;

  /* write the fsystem block to SB_BLK, mark block used */
  fs_setmaskbit(SB_BLK);
  bs_bwrite(dev0, SB_BLK, 0, &fsd, sizeof(fsystem_t));

  /* write the free block bitmask in BM_BLK, mark block used */
  fs_setmaskbit(BM_BLK);
  bs_bwrite(dev0, BM_BLK, 0, fsd.freemask, fsd.freemaskbytes);

  // Initialize all inode IDs to EMPTY
  inode_t tmp_in;
  for (i = 0; i < fsd.ninodes; i++) {
    _fs_get_inode_by_num(dev0, i, &tmp_in);
    tmp_in.id = EMPTY;
    _fs_put_inode_by_num(dev0, i, &tmp_in);
  }
  fsd.root_dir.numentries = 0;
  for (i = 0; i < DIRECTORY_SIZE; i++) {
    fsd.root_dir.entry[i].inode_num = EMPTY;
    memset(fsd.root_dir.entry[i].name, 0, FILENAMELEN);
  }

  for (i = 0; i < NUM_FD; i++) {
    oft[i].state     = 0;
    oft[i].fileptr   = 0;
    oft[i].de        = NULL;
    oft[i].in.id     = EMPTY;
    oft[i].in.type   = 0;
    oft[i].in.nlink  = 0;
    oft[i].in.device = 0;
    oft[i].in.size   = 0;
    memset(oft[i].in.blocks, 0, sizeof(oft[i].in.blocks));
    oft[i].flag      = 0;
  }

  return OK;
}

int fs_freefs(int dev) {
  if (freemem(fsd.freemask, fsd.freemaskbytes) == SYSERR) {
    return SYSERR;
  }

  return OK;
}

/**
 * Debugging functions
 */
void fs_print_oft(void) {
  int i;

  printf ("\n\033[35moft[]\033[39m\n");
  printf ("%3s  %5s  %7s  %8s  %6s  %5s  %4s  %s\n", "Num", "state", "fileptr", "de", "de.num", "in.id", "flag", "de.name");
  for (i = 0; i < NUM_FD; i++) {
    if (oft[i].de != NULL) printf ("%3d  %5d  %7d  %8d  %6d  %5d  %4d  %s\n", i, oft[i].state, oft[i].fileptr, oft[i].de, oft[i].de->inode_num, oft[i].in.id, oft[i].flag, oft[i].de->name);
  }

  printf ("\n\033[35mfsd.root_dir.entry[] (numentries: %d)\033[39m\n", fsd.root_dir.numentries);
  printf ("%3s  %3s  %s\n", "ID", "id", "filename");
  for (i = 0; i < DIRECTORY_SIZE; i++) {
    if (fsd.root_dir.entry[i].inode_num != EMPTY) printf("%3d  %3d  %s\n", i, fsd.root_dir.entry[i].inode_num, fsd.root_dir.entry[i].name);
  }
  printf("\n");
}

void fs_print_inode(int fd) {
  int i;

  printf("\n\033[35mInode FS=%d\033[39m\n", fd);
  printf("Name:    %s\n", oft[fd].de->name);
  printf("State:   %d\n", oft[fd].state);
  printf("Flag:    %d\n", oft[fd].flag);
  printf("Fileptr: %d\n", oft[fd].fileptr);
  printf("Type:    %d\n", oft[fd].in.type);
  printf("nlink:   %d\n", oft[fd].in.nlink);
  printf("device:  %d\n", oft[fd].in.device);
  printf("size:    %d\n", oft[fd].in.size);
  printf("blocks: ");
  for (i = 0; i < INODEBLOCKS; i++) {
    printf(" %d", oft[fd].in.blocks[i]);
  }
  printf("\n");
  return;
}

void fs_print_fsd(void) {
  int i;

  printf("\033[35mfsystem_t fsd\033[39m\n");
  printf("fsd.nblocks:       %d\n", fsd.nblocks);
  printf("fsd.blocksz:       %d\n", fsd.blocksz);
  printf("fsd.ninodes:       %d\n", fsd.ninodes);
  printf("fsd.inodes_used:   %d\n", fsd.inodes_used);
  printf("fsd.freemaskbytes  %d\n", fsd.freemaskbytes);
  printf("sizeof(inode_t):   %d\n", sizeof(inode_t));
  printf("INODES_PER_BLOCK:  %d\n", INODES_PER_BLOCK);
  printf("NUM_INODE_BLOCKS:  %d\n", NUM_INODE_BLOCKS);

  inode_t tmp_in;
  printf ("\n\033[35mBlocks\033[39m\n");
  printf ("%3s  %3s  %4s  %4s  %3s  %4s\n", "Num", "id", "type", "nlnk", "dev", "size");
  for (i = 0; i < NUM_FD; i++) {
    _fs_get_inode_by_num(dev0, i, &tmp_in);
    if (tmp_in.id != EMPTY) printf("%3d  %3d  %4d  %4d  %3d  %4d\n", i, tmp_in.id, tmp_in.type, tmp_in.nlink, tmp_in.device, tmp_in.size);
  }
  for (i = NUM_FD; i < fsd.ninodes; i++) {
    _fs_get_inode_by_num(dev0, i, &tmp_in);
    if (tmp_in.id != EMPTY) {
      printf("%3d:", i);
      int j;
      for (j = 0; j < 64; j++) {
        printf(" %3d", *(((char *) &tmp_in) + j));
      }
      printf("\n");
    }
  }
  printf("\n");
}

void fs_print_dir(void) {
  int i;

  printf("%22s  %9s  %s\n", "DirectoryEntry", "inode_num", "name");
  for (i = 0; i < DIRECTORY_SIZE; i++) {
    printf("fsd.root_dir.entry[%2d]  %9d  %s\n", i, fsd.root_dir.entry[i].inode_num, fsd.root_dir.entry[i].name);
  }
}

int fs_setmaskbit(int b) {
  int mbyte, mbit;
  mbyte = b / 8;
  mbit = b % 8;

  fsd.freemask[mbyte] |= (0x80 >> mbit);
  return OK;
}

int fs_getmaskbit(int b) {
  int mbyte, mbit;
  mbyte = b / 8;
  mbit = b % 8;

  return( ( (fsd.freemask[mbyte] << mbit) & 0x80 ) >> 7);
}

int fs_clearmaskbit(int b) {
  int mbyte, mbit, invb;
  mbyte = b / 8;
  mbit = b % 8;

  invb = ~(0x80 >> mbit);
  invb &= 0xFF;

  fsd.freemask[mbyte] &= invb;
  return OK;
}

/**
 * This is maybe a little overcomplicated since the lowest-numbered
 * block is indicated in the high-order bit.  Shift the byte by j
 * positions to make the match in bit7 (the 8th bit) and then shift
 * that value 7 times to the low-order bit to print.  Yes, it could be
 * the other way...
 */
void fs_printfreemask(void) { // print block bitmask
  int i, j;

  for (i = 0; i < fsd.freemaskbytes; i++) {
    for (j = 0; j < 8; j++) {
      printf("%d", ((fsd.freemask[i] << j) & 0x80) >> 7);
    }
    printf(" ");
    if ( (i % 8) == 7) {
      printf("\n");
    }
  }
  printf("\n");
}


int fs_open(char *filename, int flags) {
	// Make sure filename isn't empty
	if (strncmp(filename, "", FILENAMELEN) == 0) {
		errormsg("fs_open: filename cannot be empty!\n");
		return SYSERR;
	}
	else if (strlen(filename) > FILENAMELEN) {
		errormsg("fs_open: filename too long\n");
		return SYSERR;
	}
	// Check flags for validity
	if (!((flags == O_RDONLY) || (flags == O_WRONLY) || (flags == O_RDWR))) {
		errormsg("fs_open: invalid flags (permissions)\n");
		return SYSERR;
	}
	
	// Behavior: a file can only be open once, so only one fd
	// Make sure the file isn't already open
	int i;
	for (i = 0; i < NUM_FD; i++) {
		if (oft[i].de != NULL && ((strncmp(filename, (oft[i].de)->name, FILENAMELEN)) == 0)) {
			// Matching filename in filetable already
			if (oft[i].state == FSTATE_OPEN) {
				errormsg("fs_open: file already open\n");
				return SYSERR;
			}
			else {
				// (re)open it
				oft[i].state = FSTATE_OPEN;
				oft[i].flag = flags;
				return i;
			}
		}
	}

	// find the file from root dir to get the dirent, then the inode_num
	char file_found = 0;
	dirent_t* file_dirent;
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		file_dirent = &(fsd.root_dir.entry[i]);
		if (strncmp(file_dirent->name, filename, FILENAMELEN) == 0) {
			file_found = 1;
			break;
		} 
	}
	if (!file_found) {
		errormsg("fs_open: file not found\n");
		return SYSERR;
	}
	// Get a free filetable in oft;
	int fd;
	for (fd = 0; fd < NUM_FD; fd++) {
		if (oft[fd].de == NULL && oft[fd].in.id == EMPTY) {
			break;
		}
		else if (fd == NUM_FD - 1) {
			// OFT is full.
			errormsg("fs_open: open file table is full\n");
			return SYSERR;
		}
	}
	oft[fd].state = FSTATE_OPEN;
	oft[fd].fileptr = 0;
	oft[fd].de = file_dirent;
	oft[fd].flag = flags;
	if (_fs_get_inode_by_num(dev0, file_dirent->inode_num, &(oft[fd].in)) == SYSERR) {
		errormsg("fs_open: _fs_get_inode_by_num returned SYSERR\n");
		return SYSERR;
	} 
  return fd;
}

int fs_close(int fd) {
	// Handle bad/invalid fd
  if (isbadfd(fd)) {
		errormsg("fs_close: bad fd\n");
		return SYSERR;
	}

	// If the file is already closed, give an error
	if ((oft[fd]).state != FSTATE_OPEN) {
		errormsg("fs_close: file is already closed (note open)\n");
		return SYSERR;
	}

	// Otherwise, set the state to closed and return OK
	(oft[fd]).state = FSTATE_CLOSED;
	return OK;
}

int fs_create(char *filename, int mode) {
	// Validate args quickly
	if (mode != O_CREAT) {
		errormsg("Folder creation not supported\n");
		return SYSERR;	
	}
	// Make sure root dir isn't full
	if (fsd.root_dir.numentries >= DIRECTORY_SIZE) {
		errormsg("Root directory full.\n");
		return SYSERR;
	}
	// make sure filename isn't empty, too long, or already exists
	if (strncmp(filename, "", FILENAMELEN) == 0) {
		errormsg("filename can't be empty.\n");
		return SYSERR;
	}
	if (strlen(filename) > FILENAMELEN) {
		errormsg("filename too long\n");
		return SYSERR;
	}
	int i;
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		if (fsd.root_dir.entry[i].inode_num != EMPTY && (strncmp(fsd.root_dir.entry[i].name, filename, FILENAMELEN) == 0)) {
			errormsg("File with name '%s' already exists.\n", filename);
			return SYSERR;
		}
	}
	
	/** Determine next available inode number 
			------------------------------------- **/
	// Make sure there is an inode available
	if (fsd.inodes_used >= fsd.ninodes) {
		errormsg("No more inodes available\n");
		return SYSERR;
	}
	int new_inode_num = fsd.inodes_used;//-1;
	inode_t tmp_inode;
/*
	for (i = 0; i < fsd.ninodes; i++) {
		// pull the inode's data and check to see if it appears to be in use or not
		_fs_get_inode_by_num(dev0, i, &tmp_inode);
		if (tmp_inode.id == EMPTY) {
			new_inode_num = i;
			break;
		}
	}
	if (new_inode_num == -1) {
		errormsg("fs_create: could not find a free inode\n");
		return SYSERR;
	} */
	fsd.inodes_used++;
	_fs_get_inode_by_num(dev0, new_inode_num, &tmp_inode);
	tmp_inode.id = new_inode_num;
	tmp_inode.type = INODE_TYPE_FILE;
	tmp_inode.nlink = 1;
	tmp_inode.device = dev0;
	tmp_inode.size = 0;
	_fs_put_inode_by_num(dev0, new_inode_num, &tmp_inode);

	// update directory
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		// find first empty spot in entry array
		if (fsd.root_dir.entry[i].inode_num == EMPTY) {
			fsd.root_dir.entry[i].inode_num = new_inode_num;
			strcpy(fsd.root_dir.entry[i].name, filename);
			fsd.root_dir.numentries++;
			break;
		}
	}
	
	// Open the new file
	int fd = fs_open(filename, O_RDWR);
	if (fd == SYSERR) {
		errormsg("fs_create: fs_open returned SYSERR\n");
		return SYSERR;
	}
  return fd;
}

int fs_seek(int fd, int offset) {
	// Validate args
	if (isbadfd(fd)) {
		errormsg("fs_seek: bad fd given (%d)\n", fd);
		return SYSERR;
	}
	if (oft[fd].state != FSTATE_OPEN) {
		errormsg("fs_seek: file is not open\n");
		return SYSERR;
	}
	if (offset < 0 || offset > oft[fd].in.size) {
		errormsg("fs_seek: offset out of bounds (%d)\n", offset);
		return SYSERR;
	}

	oft[fd].fileptr = offset;
  return OK;
}

int fs_read(int fd, void *buf, int nbytes) {
	// Validate args (fd)
	if (isbadfd(fd)) {
		errormsg("fs_read: bad fd given\n");
		return SYSERR;
	}	
	// nbytes can't be negative
	if (nbytes < 0) {
		errormsg("fs_read: nbytes cannot be negative\n");
		return SYSERR;
	}
	// Make sure the file is open and with right perms
	if (oft[fd].state != FSTATE_OPEN) {
		errormsg("fs_read: file is not open\n");
		return SYSERR;
	}
	if (oft[fd].flag != O_RDONLY && oft[fd].flag != O_RDWR) {
		errormsg("fs_read: file does not have read-allow flags (is it write-only?)\n");
		return SYSERR;
	}

	int bytes_read = 0;
	int curr_block, curr_offset, curr_len;
	while (nbytes > 0 && oft[fd].fileptr < oft[fd].in.size) {
		curr_block = oft[fd].fileptr / dev0_blocksize; // Truncated (integer division)
		curr_offset = oft[fd].fileptr % dev0_blocksize;
		
		// Determine how much we can read from this block
		curr_len = (nbytes <= (dev0_blocksize - curr_offset)) ? nbytes : (dev0_blocksize - curr_offset);
		if (bs_bread(dev0, oft[fd].in.blocks[curr_block], curr_offset, buf, curr_len) == SYSERR) {
			errormsg("fs_read: read failed (block: %d, offset: %d, length of read: %d bytes)\n", oft[fd].in.blocks[curr_block], curr_offset, curr_len);
			return SYSERR;
		}
		bytes_read += curr_len;
		oft[fd].fileptr += curr_len;
		buf += curr_len;
		nbytes -= curr_len;
	}
  return bytes_read;
}

int fs_write(int fd, void *buf, int nbytes) {
	// Validate args (fd)
	if (isbadfd(fd)) {
		errormsg("fs_write: bad fd given\n");
		return SYSERR;
	}	
	// nbytes can't be negative
	if (nbytes < 0) {
		errormsg("fs_write: nbytes cannot be negative\n");
		return SYSERR;
	}
	// Make sure the file is open and with right perms
	if (oft[fd].state != FSTATE_OPEN) {
		errormsg("fs_write: file is not open\n");
		return SYSERR;
	}
	if (oft[fd].flag != O_WRONLY && oft[fd].flag != O_RDWR) {
		errormsg("fs_write: file does not have write-allow flags (is it read-only?)\n");
		return SYSERR;
	}

	int bytes_written = 0;
	// Start writing wherever fileptr is
	// Each inode has INODEDIRECTBLOCKS # of blocks, each of size dev0_blocksize
	// So then the first block to write to is (fileptr // dev0_blocksize) and offset in that block is (fileptr % dev0_blocksize)
	// If fileptr has reached INODEDIRECTBLOCKS * dev0_blocksize, then we're out of space in the inode.
	int curr_block, curr_offset, curr_len;
	while (nbytes > 0 && oft[fd].fileptr < (INODEDIRECTBLOCKS * dev0_blocksize)) {
		curr_block = oft[fd].fileptr / dev0_blocksize; // Truncated (integer division)
		curr_offset = oft[fd].fileptr % dev0_blocksize;

		// If the current block doesn't exist in the inode yet, then we need to allocate it
		if (oft[fd].in.blocks[curr_block] == 0) {
			// Find the first empty block and claim it
			int i;
			int new_block_id = -1;
			for (i = 0; i < fsd.nblocks; i++) {
				if (!fs_getmaskbit(i)) {
					new_block_id = i;
					break;
				}
			}
			if (new_block_id == -1) {
				break; // Filesystem has no free blocks. Can't continue writing.
			}
			else {
				fs_setmaskbit(new_block_id);
				oft[fd].in.blocks[curr_block] = new_block_id;
			}
		}
		
		// Break off a chunk of the input data that will fit in this block (if necessary)
		curr_len = (nbytes <= (dev0_blocksize - curr_offset)) ? nbytes : (dev0_blocksize - curr_offset);
		if (bs_bwrite(dev0, oft[fd].in.blocks[curr_block], curr_offset, buf, curr_len) == SYSERR) {
			errormsg("fs_write: write failed (block: %d, offset: %d, size of write: %d)\n", oft[fd].in.blocks[curr_block], curr_offset, curr_len);
			return SYSERR;
		}
		// update fileptr, buf, bytes_written, and nbytes
		bytes_written += curr_len;
		nbytes -= curr_len;
		oft[fd].fileptr += curr_len;
		buf += curr_len; // Go forward in the buffer by curr_len bytes
	}

	// update inode's size
	if (oft[fd].fileptr > oft[fd].in.size) {
		oft[fd].in.size = oft[fd].fileptr;
	}
  return bytes_written;
}

int fs_link(char *src_filename, char* dst_filename) {
  // Do some basic argument validation
	if (src_filename == NULL || dst_filename == NULL) {
		errormsg("fs_link: filename pointers cannot be NULL\n");
		return SYSERR;
	}
	if ((strncmp(src_filename, "", FILENAMELEN) == 0) || (strncmp(dst_filename, "", FILENAMELEN) == 0)) {
		errormsg("fs_link: filenames cannot be empty strings\n");
		return SYSERR;
	}
	if (strnlen(dst_filename, FILENAMELEN+1) > FILENAMELEN) {
		errormsg("fs_link: destination filename too long\n");
		return SYSERR;
	}
	if (fsd.root_dir.numentries >= DIRECTORY_SIZE) {
		errormsg("fs_link: root directory is full.\n");
		return SYSERR;
	}

	// Make sure dst_filename isn't already in use somewhere
	int i;
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		if (strncmp(fsd.root_dir.entry[i].name, dst_filename, FILENAMELEN) == 0) {
			errormsg("fs_link: destination filename already in use.\n");
			return SYSERR;
		}
	}

	// Find the source file in the root directory
	int source_file_index = -1;
	int source_file_inode = EMPTY;
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		if (strncmp(fsd.root_dir.entry[i].name, src_filename, FILENAMELEN) == 0) {
			source_file_index = i;
			source_file_inode = fsd.root_dir.entry[i].inode_num;
			break;
		}
	}	
	if (source_file_index == -1 || source_file_inode == EMPTY) {
		errormsg("fs_link: could not find source filename in root dir\n");
		return SYSERR;
	}
	
	// Grab the first free dirent for the new link (filename)
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		// This could be safer (make sure we find an empty one), but we guarantee there's at least one
		// free dirent above.
		if (fsd.root_dir.entry[i].inode_num == EMPTY) {
			fsd.root_dir.entry[i].inode_num = source_file_inode;
			strcpy(fsd.root_dir.entry[i].name, dst_filename);
			fsd.root_dir.numentries++;
			break;
		}
	}

	// Update the inode's nlink field
	inode_t temp_inode;
	_fs_get_inode_by_num(dev0, source_file_inode, &temp_inode);
	temp_inode.nlink++;
	_fs_put_inode_by_num(dev0, source_file_inode, &temp_inode);

	return OK;
}

int fs_unlink(char *filename) {
	// Do some basic argument validation and sanity checks
	if (filename == NULL) {
		errormsg("fs_unlink: filename pointers cannot be NULL\n");
		return SYSERR;
	}
	if (strncmp(filename, "", FILENAMELEN) == 0) {
		errormsg("fs_unlink: filenames cannot be empty strings\n");
		return SYSERR;
	}
	if (fsd.root_dir.numentries == 0) {
		errormsg("fs_unlink: root directory is empty...\n");
		return SYSERR;
	}
	
	// Find the source file in the root directory
	int file_index = -1;
	int file_inode = EMPTY;
	int i;
	for (i = 0; i < DIRECTORY_SIZE; i++) {
		if (strncmp(fsd.root_dir.entry[i].name, filename, FILENAMELEN) == 0) {
			file_index = i;
			file_inode = fsd.root_dir.entry[i].inode_num;
			break;
		}
	}	
	if (file_index == -1 || file_inode == EMPTY) {
		errormsg("fs_unlink: could not find filename in root dir\n");
		return SYSERR;
	}

	// Delete the directory entry and decrement numentries
	fsd.root_dir.entry[file_index].inode_num = EMPTY;
	memset(fsd.root_dir.entry[file_index].name, 0, FILENAMELEN);
	fsd.root_dir.numentries--;

	// Decrement the inode's nlink field. If this was the only link, free up the inode
	inode_t temp_inode;
	_fs_get_inode_by_num(dev0, file_inode, &temp_inode);
	temp_inode.nlink--;
	if (temp_inode.nlink == 0) {
		// It's now empty. Pack it up, boys.
		temp_inode.id = EMPTY;
		temp_inode.size = 0;
		fsd.inodes_used--;
	}
	_fs_put_inode_by_num(dev0, file_inode, &temp_inode);
  return OK;
}

#endif /* FS */

