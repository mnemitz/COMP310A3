#include<stdlib.h>
#include<string.h>
#include "sfs_api.h"
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 1024
#define NUM_INODES 200
#define NUM_DIRECT_PTRS 14

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
	
}FDTentry_t;

typedef struct _openFDT
{
	FDTentry_t entries[NUM_FDT_ENTRIES];
}openFDT;

void mkssfs(int fresh)
{
	/* First we initialize the super block, but give it a block's worth of memory */

	superblock_t* sup = malloc(sizeof(block_t));
	strcpy(sup -> magic, "MYFS");
	sup -> bsize = BLOCK_SIZE;
	sup -> nblocks = NUM_BLOCKS
	sup -> numinodes = NUM_INODES;
	(sup -> jroot).size = 0;
	
	/*  Now the initialized superblock is clean and ready, can now init the disk and write it */
	
	if(fresh)
	{
		init_fresh_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}
	else
	{
		init_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}
	
	write_blocks(0, 1, sup);
	free(sup);

	/* Next we can initialize the Free Bit Map and Write Mask */

	block_t* fbm = malloc(sizeof(block_t));
	block_t* wm = malloc(sizeof(block_t));

	/*  Initialize all their buffers' bits to 1, since initially all the tracked blocks are unused */
	memset(fbm->buffer, 1, BLOCK_SIZE);
	memset(wm->buffer, 1, BLOCK_SIZE);
	

	/* Now write the FBM and WM to the disk */
	write_blocks(1, 1, fbm);
	write_blocks(2, 1, wm);


	/* Now the 200 inodes need to be written. */

	inode_t* upd_jroot = malloc(sizeof(inode_t));	// this will be the new j-node which will actually point to the right places	

	int num_blocks_for_inodes = ((int)sizeof(inode_t)*NUM_INODES)/BLOCK_SIZE;

	int i, waddr;
	for(i=0, waddr=3; i<num_blocks_for_inodes; i++, waddr++)
	{
		write_blocks(waddr, 1, nodes[16*i]);	// write the 16 inodes for the current block
		upd_jroot -> direct_ptrs[i] = waddr;
	}


}
int ssfs_fopen(char *name){
    return 0;
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
