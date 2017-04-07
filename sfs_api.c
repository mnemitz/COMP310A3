#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include "sfs_api.h"
#include "disk_emu.h"
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 1024
#define NUM_INODES 200
#define NUM_DIRECT_PTRS 14
#define MAX_FDT_ENTRIES 16
#define MAX_NAMESIZE 16

typedef struct _inode_t
{
	int size;
	int direct_ptrs[NUM_DIRECT_PTRS];
	int indirect_ptr;
}inode_t;


typedef struct _block_t
{
	char buffer[BLOCK_SIZE];
}block_t;


typedef struct _superblock_t
{
	char magic[4];
	int bsize;
	int nblocks;
	int numinodes;
	inode_t jroot;
	inode_t shadows[4];
}superblock_t;

typedef struct _FDTentry_t
{
	int inode_num;
	char* readptr, writeptr;
	
}FDTentry_t;

typedef struct _FDT_t
{
	int num_openfiles;	
	FDTentry_t entries[MAX_FDT_ENTRIES];

}FDT_t;

typedef struct _directory_entry_t
{
	char filename[MAX_NAMESIZE];
	int inode_num;
}directory_entry_t;

typedef struct _directory_t
{
	directory_entry_t entries[200];
}directory_t;

/*  Global inode_t array for writing purposes */
inode_t nodes[NUM_INODES];


/* Global instance of file descriptor table */

FDT_t openFileTable;

/* globally accessible cached root directory block*/

block_t cachedRoot;
int rootAddr;

void mkssfs(int fresh)
{
	/* First we initialize the super block, but give it a block's worth of memory */

	superblock_t* sup = malloc(sizeof(block_t));
	strcpy(sup -> magic, "MYFS");
	sup -> bsize = BLOCK_SIZE;
	sup -> nblocks = NUM_BLOCKS;
	sup -> numinodes = NUM_INODES;
	
	/* The superblock is ready except the jnode still needs more information, so we will wait til the end to write the superblock so it is fully consistent */
	
	if(fresh)
	{
		init_fresh_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}
	else
	{
		init_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}
	

	/* Next we can initialize the Free Bit Map and Write Mask */

	block_t* fbm = malloc(sizeof(block_t));
	block_t* wm = malloc(sizeof(block_t));

	/*  Initialize all their buffers' bits to 1, since initially all the tracked blocks are unused */
	memset(fbm->buffer, 1, BLOCK_SIZE);
	memset(wm->buffer, 1, BLOCK_SIZE);
	
	/* Except, we need to take into account the blocks for the inodes about to be written. Need to compute how many blocks this will be */
	int num_blocks_for_inodes = ((int)sizeof(inode_t)*NUM_INODES)/BLOCK_SIZE;
	int num_inodes_per_block = BLOCK_SIZE / sizeof(inode_t);

	/*  Now that we know the number of blocks needed for all the inodes, we can adjust the FBM and WM accordingly */
	int i;
	for(i=0; i<
	// mark each inode data block, plus one extra block for the root dir datablock, then write them!
	write_blocks(1, 1, fbm);
	write_blocks(2, 1, wm);
	free(fbm);
	free(wm);

	/* Now the 200 inodes need to be written. First we'll initialize their size fields as needed*/

	nodes[0].size = 0;	// initialize the first node to be used with size 0 (for the root directory)

	for(i = 1; i < NUM_INODES; i++)
	{
		nodes[i].size = -1;	// initialize the rest to be empty
		
	}

	inode_t* init_jroot = malloc(sizeof(inode_t));	// this will be the new j-node which will actually point to the right places	

	int waddr;
	for(i=0, waddr=3; i<num_blocks_for_inodes; i++, waddr++)
	{
		write_blocks(waddr, 1, nodes[num_inodes_per_block*i]);	 // write the 16 inodes for the current block
		init_jroot -> direct_ptrs[i] = waddr;			// add its index to the right pointer of the jnode
	}


	// Initialize the root directory data block, zero it out for safety
	block_t* rootdir = malloc(sizeof(block_t));
	memset(rootdir->buffer, 0, BLOCK_SIZE);
	write_blocks(waddr, 1, rootdir);
	rootAddr = waddr;

	// now we need to update the superblock's jnode, and write the superblock 

	memcpy(sup -> jroot, init_jroot, sizeof(inode_t));
	free(init_jroot);
	write_blocks(0, 1, sup);
	free(sup);

	// finally, initialize all entries in the FDT to have inode numbers of -1, to indicate their availability too
	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		openFileTable.entries[i].inode_num = -1;
	}

	return 0;
	
}

/* Helping function which updates the cached root */

void cache_newest_root()
{
	read_blocks(rootAddr, 1, &cachedRoot);
}


int ssfs_fopen(char *name){
    
	// create an entry in the open file descriptor table for this file
	//	- copy the root directory data block from the disk (i.e. update the cached block structure used for the root)
	// 	- find the inode corresponding to the file, if none exists, give it one
	//	- what is the inode number exactly? is it the index of the direct pointer which points to that inode?
	//		...anyway whatever it is, put it as the inode number field
	//	- then set the read pointer to point to the first byte of the first block (or however the beginning is addressable),
	//		... and the write pointer to the end of the file (ask about whether it's block or byte indexing)
	// once that's all set, just return the index of this entry in the table. It is the file descriptor, for use in further operations.

	cache_newest_root();

	int i;
	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		if (openFileTable.entries[i].inode_num == -1)
		{
			// found free slot in table
			break;		
		}
	}

	if(i == MAX_NUM_FILES)
	{
		printf("E:\t Too many open files. Please close one.\n");
		return -1;
	}
	else
	{
		FDTentry_t* filentry = malloc(sizeof(FDTentry_t));
		filentry->inode_num = // TODO: get inode number
		filentry->readptr = // TODO: get read pointer
		filentry->writeptr = // TODO: get write pointer
		memcpy(*(openFileTable.entries[i]), filentry);
	}
	
	return i;

}
int ssfs_fclose(int fileID){
    return 0;
}
int ssfs_frseek(int fileID, int loc){
    return 0;
}
int ssfs_fwseek(int fileID, int loc){
    return 0;
}
int ssfs_fwrite(int fileID, char *buf, int length){
    return 0;
}
int ssfs_fread(int fileID, char *buf, int length){
    return 0;
}
int ssfs_remove(char *file){
    return 0;
}
int ssfs_commit(){
    return 0;
}
int ssfs_restore()
{
	return 0;
}
