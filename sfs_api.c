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

void mkssfs(int fresh)
{
	/* First we initialize the super block, but give it a block's worth of memory */

	superblock_t* sup = malloc(sizeof(block_t));
	strcpy(sup -> magic, "MYFS");
	sup -> bsize = BLOCK_SIZE;
	sup -> nblocks = NUM_BLOCKS
	sup -> numinodes = NUM_INODES;
	(sup -> jroot).size = 0;

	/* Need to make sure all the pointers in the root node are 0 */
	int i,j;
	for(i=0; i<NUM_DIRECT_PTRS; i++)
	{
		(sup -> jroot).direct_ptrs[i] = 0;
	
		/* ... as well as the shadows' pointers*/
		for(j=0;j<4;j++)
		{
			(sup -> shadows[j]).direct_ptrs[i] = 0;
		}
	}
	(sup -> jroot).indirect_ptr = 0;
	
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
