#include<stdlib.h>
#include<string.h>
#include "sfs_api.h"
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 1024
#define NUM_INODES 200
#define NUM_DIRECT_PTRS 14
#define MAX_FDT_ENTRIES 4
#define MAX_NAMESIZE 16

typedef struct _inode_t
{
	int size;
	int direct_ptrs[NUM_DIRECT_PTRS];
	int indirect_ptr;
}inode_t;


typedef struct _block_t
{
	void* buffer[BLOCK_SIZE];
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

/*  Global inode_t array for writing purposes */
inode_t nodes[NUM_INODES];

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

/* Global instance of file descriptor table */

FDT_t openFileTable;

void mkssfs(int fresh)
{
	/* First we initialize the super block, but give it a block's worth of memory */

	superblock_t* sup = malloc(sizeof(block_t));
	strcpy(sup -> magic, "MYFS");
	sup -> bsize = BLOCK_SIZE;
	sup -> nblocks = NUM_BLOCKS
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

	/*  Now that we know the number of blocks needed for all the inodes, we can shift the FBM and WM accordingly */
	/*The convention here is that the bitmaps are read left to right, so a shift to the right makes the next sequential block used/readonly*/
	
	*fbm = *fbm >> num_blocks_for_inodes + 1;
	*wm = *wm >> num_blocks_for_inodes + 1;
	// mark each inode data block, plus one extra block for the root dir datablock, then write them!
	write_blocks(1, 1, fbm);
	write_blocks(2, 1, wm);
	free(fbm);
	free(wm);

	/* Now the 200 inodes need to be written. First we'll initialize their size fields as needed*/

	nodes[0].size = 0;	// initialize the first node to be used with size 0 (for the root directory)
	int i;
	for(i = 1; i < NUM_INODES; i++)
	{
		nodes[i].size = -1;	// initialize the rest to be empty
		
	}

	inode_t* init_jroot = malloc(sizeof(inode_t));	// this will be the new j-node which will actually point to the right places	

	int i, waddr;
	for(i=0, waddr=3; i<num_blocks_for_inodes; i++, waddr++)
	{
		write_blocks(waddr, 1, nodes[num_inodes_per_block*i]);	 // write the 16 inodes for the current block
		init_jroot -> direct_ptrs[i] = waddr;			// add its index to the right pointer of the jnode
	}


	// TODO: root directory data block
	block_t* rootdir = malloc(sizeof(block_t));


	// write_blocks(waddr, 1, rootdir);	is the rootdir data block just empty?? how does that work??

	// now we need to update the superblock's jnode, and write the superblock 

	memcpy(sup -> jroot, init_jroot);
	free(init_jroot);
	write_blocks(0, 1, sup);
	free(sup);

	// finally, initialize all entries in the FDT to have inode numbers of -1, to indicate their availability too
	for(i=0; i<MAX_NUM_FILES; i++)
	{
		openFileTable[i].inode_num = -1;
	}

	return 0;
	
}
int ssfs_fopen(char *name){
    
	// create an entry in the open file descriptor table for this file
	// 	- find the inode corresponding to the file, if none exists, give it one
	//	- what is the inode number exactly? is it the index of the direct pointer which points to that inode?
	//		...anyway whatever it is, put it as the inode number field
	//	- then set the read pointer to point to the first byte of the first block (or however the beginning is addressable),
	//		... and the write pointer to the end of the file (ask about whether it's block or byte indexing)
	// once that's all set, just return the index of this entry in the table. It is the file descriptor, for use in further operations.

	int i;
	for(i=0; i<MAX_NUM_FILES; i++)
	{
		if (openFileTable[i].inode_num == -1)
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
		filentry->read_ptr = // TODO: get read pointer
		filentry->write_ptr = // TODO: get write pointer
		memcpy(*openFileTable[i], filentry);
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
int ssfs_restore(
