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


typedef struct _directory_entry_t
{
	char filename[MAX_NAMESIZE];
	int inode_num;
}directory_entry_t;

typedef struct _directory_t
{
	directory_entry_t entries[200];
}directory_t;


/* Global instance of file descriptor table, inodes, and root directory block */

FDTentry_t openFileTable[16];
inode_t nodes[NUM_INODES];
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
	

	/* Next we can initialize the Free Bit Map */

	block_t* fbm = malloc(sizeof(block_t));

	/*  Initialize all its buffers' bits to 1, since initially all the tracked blocks are unused */
	memset(fbm->buffer, 256, BLOCK_SIZE);
	
	/* Except, we need to take into account the blocks for the inodes about to be written. Need to compute how many blocks this will be */
	int num_blocks_for_inodes = ((int)sizeof(inode_t)*NUM_INODES)/BLOCK_SIZE + 1;
	int num_inodes_per_block = BLOCK_SIZE / sizeof(inode_t);

	int num_bytes_to_zero = (num_blocks_for_inodes + 1) / 8;	// added one for the root dir data block
	int num_rem_bits = (num_blocks_for_inodes + 1) % 8;

	/*  Now that we know the number of blocks needed for all the inodes, we can adjust the FBM accordingly 
		Basically here, we are just setting the right number of bytes to zero, and shifting the next byte by the number of remaining bits which must be zero.
		Then, just write the block
	*/
	memset(fbm->buffer, 0, num_bytes_to_zero);
	fbm->buffer[num_bytes_to_zero + 1] = fbm->buffer[num_bytes_to_zero + 1] >> num_rem_bits;
	write_blocks(1, 1, fbm);
	free(fbm);

	/* Now the 200 inodes need to be written. First we'll initialize their size fields as needed*/

	nodes[0].size = 0;	// initialize the first node to be used with size 0 (for the root directory)
	int i;
	for(i = 1; i < NUM_INODES; i++)
	{
		nodes[i].size = -1;	// initialize the rest to be empty
		
	}

	inode_t* init_jroot = malloc(sizeof(inode_t));	// this will be the new j-node which will actually point to the right places	

	int waddr;
	for(i=0, waddr=2; i<num_blocks_for_inodes; i++, waddr++)
	{
		write_blocks(waddr, 1, &(nodes[num_inodes_per_block*i]));	 // write the 16 inodes for the current block
		init_jroot -> direct_ptrs[i] = waddr;			// add its index to the right pointer of the jnode
	}


	// Initialize the root directory data block, zero it out for safety, and give it a directory struct
	block_t* rootdir = malloc(sizeof(block_t));
	memset(rootdir->buffer, 0, BLOCK_SIZE);
	
	directory_t* rootLookupStruct = malloc(sizeof(directory_t));
	for(i=0; i<200; i++)
	{
		strcpy((rootLookupStruct -> entries[i]).filename, "                ");
		(rootLookupStruct -> entries[i]).inode_num = -1;
	}
	

	// Copy this lookup table construction into what will be the root directory data block. Then, 
	memcpy(rootdir->buffer, rootLookupStruct, sizeof(directory_t));

	write_blocks(waddr, 1, rootdir);
	free(rootdir);
	free(rootLookupStruct);
	rootAddr = waddr;

	// now we need to update the superblock's jnode, and write the superblock 

	memcpy(&(sup -> jroot), init_jroot, sizeof(inode_t));
	free(init_jroot);
	write_blocks(0, 1, sup);
	free(sup);

	// finally, initialize all entries in the FDT to have inode numbers of -1, to indicate their availability too
	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		openFileTable.entries[i].inode_num = -1;
	}
	
}



int ssfs_fopen(char *name){
    
	/* - Read the root directory and look for the specified file name
		if the name is found:
			make note of the inode number given. 
			set the read pointer to be 0, i.e. first byte of the file
			set the write pointer to be the last byte of the file, i.e. the SIZE as specified by the inode



		else, find the first available inode (one with size -1). 
			The number for this inode is the index of the direct pointer from the jnode which points to it.
			Set THIS inode's size to 0
			Update the root directory to map the given filename to THIS found inode	
			set both the read and write pointers to 0		

		Create an entry in the file descriptor table corresponding to this information
						
	*/

	
	read_blocks(rootAddr, 1, &cachedRoot);
	

	int i;
	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		if (openFileTable.entries[i].inode_num == -1)
		{
			// found free slot in table
			break;		
		}
	}

	if(i == MAX_FDT_ENTRIES)
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
		memcpy(&(openFileTable.entries[i]), filentry, sizeof(FDTentry_t));
	}
	
	return i;

}
int ssfs_fclose(int fileID){
	/*
		Remove the file descriptor table entry at the specified index 
		....by copying a blank struct into that 'row', with a size=-1 flag to indicate its avalability
	*/
    return 0;
}
int ssfs_frseek(int fileID, int loc){
	/*
		Update the fle table entry at this index by setting its read pointer to loc
	*/
    return 0;
}
int ssfs_fwseek(int fileID, int loc){
	/*
		Update the fle table entry at this index by setting its write pointer to loc	
	*/
    return 0;
}
int ssfs_fwrite(int fileID, char *buf, int length){

	/*
		Look at the write pointer for the entry in the file table at this ID index
		Read the block with this file's inode in it	(accessible from the (inode no. / 16)-th direct pointer in the j-node)
		get the length of the buffer to be written ( strlen(buf) )
			(this will determine how many blocks we need to update)

		number_of_blocks_needed_for_write = strlen(buf) / BLOCK_SIZE (1024)

	*/

    return 0;
}
int ssfs_fread(int fileID, char *buf, int length){
    return 0;
}
int ssfs_remove(char *file){
	/*
		Read the root directory into cache, look for specified file name
			if it does not exist return an error (-1)
		Assuming it exists, make note of its inode number,
		then, remove it from the directory using memset
		Now we want this inode to be free, so we go to the superblock to find out where it is.
		Each superblock pointer points to 1 block = 16 inodes so:
			
			 (inode number) / 16 gives the right block, as indexed by the j-node
			 (inode number) % 16 tells us which inode we want in that block 

		So we read from address
			
	*/
    return 0;
}

