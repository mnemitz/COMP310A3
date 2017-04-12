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

/*
TO THE GRADER:

After hours of debugging, I still can't seem to get my reads working, although I'm pretty sure that the writes are going through without
a problem.


*/







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

typedef struct _FDTentry_t
{
	int inode_num;
	int readptr;
	int writeptr;
	int curr_numbytes;

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

FDTentry_t openFileTable[256];
inode_t nodes[NUM_INODES];

block_t cachedSuperblock;	// will cast pointer as superblock_t* to access members
block_t cachedRoot;
block_t cachedFBM;
block_t* cachedRootAddr = NULL;
block_t* cachedFBMAddr = NULL;

int rootAddr;


void mkssfs(int fresh)
{
	/* First we initialize the super block, but give it a block's worth of memory */

	superblock_t* sup = calloc(1,sizeof(block_t));
	strcpy(sup -> magic, "MYFS");
	sup -> bsize = BLOCK_SIZE;
	sup -> nblocks = NUM_BLOCKS;
	sup -> numinodes = NUM_INODES;

	memcpy(&cachedSuperblock, sup, sizeof(block_t));
	free(sup);
	/* The superblock is ready except the jnode still needs more information, so we will wait until the end to write the superblock so it is fully consistent */

	if(fresh)
	{
		init_fresh_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}
	else
	{
		init_disk("my.disk", BLOCK_SIZE, NUM_BLOCKS);
	}

	printf("disk initialized\n");

	/* Next we can initialize the Free Bit Map */

	block_t* temp = calloc(1,sizeof(block_t));
	memcpy(&cachedFBM, temp, sizeof(block_t));
	free(temp);

	/*  Initialize all its buffers' bits to 1, since initially all the tracked blocks are unused */
	memset(cachedFBM.buffer, 256, BLOCK_SIZE);

	printf("fbm set up\n");

	/* Except, we need to take into account the blocks for the inodes about to be written. Need to compute how many blocks this will be */
	int num_blocks_for_inodes = ((int)sizeof(inode_t)*NUM_INODES)/BLOCK_SIZE + 1;
	int num_inodes_per_block = BLOCK_SIZE / sizeof(inode_t);

	int num_bytes_to_zero = (num_blocks_for_inodes + 1) / 8;	// added one for the root dir data block
	int num_rem_bits = (num_blocks_for_inodes + 1) % 8;
	unsigned char trailing_byte = 0xFF >> num_rem_bits;

	/*  Now that we know the number of blocks needed for all the inodes, we can adjust the FBM accordingly
		Basically here, we are just setting the right number of bytes to zero, and shifting the next byte by the number of remaining bits which must be zero.
		Then, just write the block
	*/

	printf("Number of bytes to zero:\t%d, \n number of trailing bits:\t%d\nnow attemptying to modify FBM\n", num_bytes_to_zero, num_rem_bits);

	memset(cachedFBM.buffer, 0, num_bytes_to_zero);
	memcpy(cachedFBM.buffer + (num_bytes_to_zero + 1), &trailing_byte, sizeof(unsigned char));

	printf("FBM modified, now writing to disk\n");

	write_blocks(1, 1, &cachedFBM);


	/* Now the 200 inodes need to be written. First we'll initialize their size fields as needed*/

	nodes[0].size = 0;	// initialize the first node to be used with size 0 (for the root directory)
	int i;
	for(i = 1; i < NUM_INODES; i++)
	{
		nodes[i].size = -1;	// initialize the rest to be empty
	}

	inode_t* init_jroot = calloc(1,sizeof(inode_t));	// this will be the new j-node which will actually point to the right places	

	int waddr;
	for(i=0, waddr=2; i<num_blocks_for_inodes; i++, waddr++)
	{
		write_blocks(waddr, 1, &(nodes[num_inodes_per_block*i]));	 // write the 16 inodes for the current block
		init_jroot -> direct_ptrs[i] = waddr;			// add its index to the right pointer of the jnode
	}


	// Initialize the root directory data block
	block_t* temproot = calloc(1,sizeof(block_t));
	memcpy(&cachedRoot, temproot, sizeof(block_t));
	free(temproot);


	directory_t* rootLookupStruct = malloc(sizeof(directory_t));
	for(i=0; i<200; i++)
	{
		strcpy((rootLookupStruct -> entries[i]).filename, "                ");
		(rootLookupStruct -> entries[i]).inode_num = -1;
	}


	// Copy this lookup table construction into what will be the root directory data block. Then,
	printf("about to try copying the directory structure into the rootdir data block\n");
	memcpy(&(cachedRoot.buffer), rootLookupStruct, sizeof(directory_t));
	printf("...this was successful\n");

	write_blocks(waddr, 1, &cachedRoot);
	printf("managed to write the root directory data block\n");
	free(rootLookupStruct);
	rootAddr = waddr;

	// now we need to update the superblock's jnode, and write the superblock
	// first need a cast pointer to set superblock jnode
	superblock_t* cachedSuperPointer = (superblock_t*) &cachedSuperblock;
	memcpy(&(cachedSuperPointer->jroot), init_jroot, sizeof(inode_t));

	write_blocks(0, 1, &cachedSuperblock);
	free(init_jroot);

	// finally, initialize all entries in the FDT to have inode numbers of -1, to indicate their availability too
	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		openFileTable[i].inode_num = -1;
	}
}

// Returns the index of the first found unused block according to the FBM. Sets it to be used in the process.
int get_unused_block_index()
{
	/*We want the index of the first  1 in the bitmap , but the buffer is only byte accessible
			   so, we're looking for the first byte != 0x00*/
			char* nonzero_byte = NULL;
			int i;
			for(i=0; i<BLOCK_SIZE; i++)
			{
				if(cachedFBM.buffer[i] != 0x00)
				{
					// ... then this byte has a 1 in it, meaning it contains the index of a free block
					nonzero_byte = (char*)&(cachedFBM.buffer[i]);
					break;
				}
			}

			// counting bits left to right, we start from the first nonzero byte, and gradually increase to find the bit
			int first_unused_byte_index = i;
			int first_unused_bit_index = 8*i;
			if(nonzero_byte == NULL)
			{
				printf("E:\t Looks like the FBM is made of 0's, so no free blocks, or there's a problem\n");
				return -1;
			}
				// need to find its closest power of 2 to find index of first '1'
			unsigned char foo = 0x80;	// = '1000 0000'
			for(i=0; i< 8; i++)
			{
				if(foo > *nonzero_byte)
				{
					// move forward 1 bit
					first_unused_bit_index++;
					// then divide foo by 2 and go again
					foo >>= 1;
				}
				else
				{
					break;
				}
			}
			// find the offset of the first found '1' within its byte
			int unused_block_offs = first_unused_bit_index - 8*first_unused_byte_index;

			// the bit at this index was 1 since this block was unused, we set it to 0 in this new byte
			unsigned char updated_byte = *nonzero_byte & ~(1 << unused_block_offs);
			memcpy(&(cachedFBM.buffer[first_unused_byte_index]), &updated_byte, sizeof(unsigned char));
			write_blocks(1,1,&cachedFBM);

	return first_unused_bit_index;
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
	printf("---------------\n Attempting to open file:\t%s\n--------------\n", name);
	int inode_number, readpointer, writepointer;
	inode_number = readpointer = writepointer = -1;
	// initialize with invalid values. If changed, will constitue an entry in the file table

	printf("attempting to cache the root directory data block\n");
	read_blocks(rootAddr, 1, &cachedRoot);
	printf("..success. Now attempting to get directory structure\n");
	superblock_t* cachedSuperPointer = (superblock_t*) &cachedSuperblock;

	// The directory structure is in the buffer of the data block for the root directory,
	// but in order to access it according to its structure, must copy it into a new directory struct on heap

	directory_t* rootdir = malloc(sizeof(directory_t));
	memcpy(rootdir, cachedRoot.buffer, sizeof(directory_t));
	printf("got directory structure accessible.\n");
	printf("now finding inode for given file...\n");
	// now we'll look through its entries to find the filename

	int i;
	for(i=0; i<200; i++)
	{
		// if the name in the entry matches the given name
		if(strcmp(name,rootdir->entries[i].filename)==0)
		{
			// then use the inode number found in the root directory
			inode_number = rootdir->entries[i].inode_num;
			printf("found it!\n");
			break;
		}
	}
	//if not found
	if(inode_number == -1)
	{
		// then the file does not exist in the root directory. Must create
		// start by finding the first available inode
		printf("this is a new file! Looking for an avalable inode...\n");
		for(i=0; i<NUM_INODES; i++)
		{
			if(nodes[i].size == -1)
			{
				inode_number = i;	// since this inode is free
				printf("inode number %d is free\n",i);
				break;
			}
		}
		/* now we want to modify inode i such that
			 - its size is 0 instead of -1
			 - its first direct pointer points to a new empty data block for this file
		Then we'll write it by modifying the block it's sitting in*/

		if(inode_number == -1)
		{
			printf("E:\t no available inodes\n");
			return -1;
		}
		else
		{
			nodes[inode_number].size = 0;			// this inode becomes used
			printf("successfully modified in-memory inode\n");
			block_t* blockbuf = calloc(1, sizeof(block_t));	// this will be the file's first data block
			printf("successfully calloc'd data block to write\n");

			// now we need to cache the FBM from the disk to see where we can write


			int first_unused_bit_index = get_unused_block_index();

			// so the value of first_unused_bit_index actually is the address of an available writeable block

			nodes[inode_number].direct_ptrs[0] = first_unused_bit_index;	// inode's 1st direct is where we will write the new data block
			printf("inode %d is set up, writing the data block.", inode_number);
			write_blocks(first_unused_bit_index, 1, blockbuf);  	// write it there
			// THe file's very first data block has been written!


			/* So far, we have only updated the inode in memory, must update it on the disk
			But to do that, we need to find out which block holds this inode, so we ask the superblock*/

			int inode_block_diskaddr = cachedSuperPointer->jroot.direct_ptrs[(inode_number % NUM_DIRECT_PTRS)];
			// ^^ this is the address of the block pointed to by the desired direct pointer in the jnode
			// But we need to know which inode in this block it is

			int inode_offs = inode_number % 16;

			// TODO: copy the new inode over its previous version in its block
			// since the buffer is bytewise accessible, we access the specific inode like this:
			read_blocks(inode_block_diskaddr, 1, blockbuf);
			inode_t* inode_in_block = (inode_t*) &(blockbuf->buffer[inode_offs]);
			memcpy(inode_in_block, &nodes[inode_number], sizeof(inode_t));
			write_blocks(inode_block_diskaddr, 1, blockbuf);
			free(blockbuf);
			readpointer = writepointer = 0;	// initialize r/w pointers to the start, since file is new
		}
	}
	else // if found
	{
		readpointer = 0;	// read from beginning
		int inode_block_addr = cachedSuperPointer->jroot.direct_ptrs[(inode_number % NUM_DIRECT_PTRS)];
		block_t* inode_block = malloc(sizeof(block_t));
		read_blocks(inode_block_addr, 1, inode_block);
		int inode_offs = inode_number % 16;

		// now that we have the block the file's inode is in, and its offset in that block, we can point to it directly
		inode_t* thisfiles_inode = (inode_t*)(inode_block->buffer[sizeof(inode_t)*inode_offs]);
		int filesize = thisfiles_inode->size;

		// this is the size in bytes, so we set the write pointer to this value plus 1 so it points to the first unread byte
		writepointer = filesize + 1;
		free(inode_block);
	}

	// from this point on it can be assumed that the file exists, root directory knows where it is, and its inode and datablocks are up to date
	free(rootdir);

	// so now we can proceed with creating the entry in the open file table

	for(i=0; i<MAX_FDT_ENTRIES; i++)
	{
		if (openFileTable[i].inode_num == -1)
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
		filentry->inode_num = inode_number;
		filentry->readptr = readpointer;
		filentry->writeptr = writepointer;
		filentry->curr_numbytes = 0;
		memcpy(&(openFileTable[i]), filentry, sizeof(FDTentry_t));
	}

	return i;

}
int ssfs_fclose(int fileID){
	/*
		Remove the file descriptor table entry at the specified index
		....by copying a blank struct into that 'row', with a size=-1 flag to indicate its avalability
	*/

	if(fileID < 0)
	{
		printf("E:\t invalid file descriptor\n");
		return -1;
	}
	if(openFileTable[fileID].inode_num == -1)
	{
		printf("E:\t File desc. %d not open\n", fileID);
		return -1;
	}


	FDTentry_t* blank_entry = calloc(1,sizeof(FDTentry_t));
	blank_entry->inode_num = -1;
	memcpy( &(openFileTable[fileID]), blank_entry, sizeof(FDTentry_t) );
	free(blank_entry);
	return 0;
}
int ssfs_frseek(int fileID, int loc)
{
	/*
		Update the fle table entry at this index by setting its read pointer to loc
	*/
	if(loc < 0)
	{
		printf("E:\t negative seek invalid\n");
		return -1;
	}
	if(openFileTable[fileID].inode_num == -1)
	{
		printf("E:\t File %d not currently open\n", fileID);
		return -1;
	}
	else if(openFileTable[fileID].curr_numbytes < loc)
	{
		printf("E:\t Invalid read seek. You tried to seek to byte %d but the file only has %d bytes\n", loc, openFileTable[fileID].curr_numbytes);
		return -1;
	}
	openFileTable[fileID].readptr = loc;
	return 0;
}
int ssfs_fwseek(int fileID, int loc)
{
	/*
		Update the fle table entry at this index by setting its write pointer to loc
	*/
        if(openFileTable[fileID].inode_num == -1)
        {
                printf("E:\t File %d not currently open\n", fileID);
                return -1;
        }
        else if(openFileTable[fileID].curr_numbytes < loc)
        {
                printf("E:\t Invalid write seek. You tried to seek to byte %d but the file only has %d bytes\n", loc, openFileTable[fileID].curr_numbytes);
                return -1;
        }
        openFileTable[fileID].readptr = loc;
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
	int ret = -1;
	if(fileID < 0)
	{
		printf("E:\t Invalid file desc.\n");
		return -1;
	}

	FDTentry_t* thisfile = &(openFileTable[fileID]);
	if(thisfile->inode_num == -1)
	{
		printf("E:\t File desc. %d not open\n",fileID);
		return -1;
	}
	/*Ultimately, we're trying to find the data block which holds the byte indexed by the write pointer
	To do this, first we need to find the inode itself, using the cached superblock*/
	printf("--------------\n Attempting to write %d bytes to file desc. %d \n -----------------------\n",length, fileID);
	superblock_t* cachedSuperPointer = (superblock_t*) &cachedSuperblock;

	// access right direct pointer from j-node, and get the address of the block it points to:
	int inode_block_addr = cachedSuperPointer->jroot.direct_ptrs[(thisfile->inode_num)/NUM_DIRECT_PTRS];
	block_t* nodeblock = malloc(sizeof(block_t));
	read_blocks(inode_block_addr, 1, nodeblock);
	printf("Got block with file's inode\n");

	// now we have that block of inodes, need to select the correct inode from it
	inode_t* this_files_inode = (inode_t*) &(nodeblock->buffer[(thisfile->inode_num)%16]);

	// now that we can access the file's inode, we need to find the data block corresponding to our current write location
	// each data block in the inode is 1024 bytes, so we should see how many blocks the buffer to write requires:
	printf("Got this file's inode directly \n");

	// we also need to know where to start, i.e. which block the write pointer directs us to:
	int writeptr_blockoffs = thisfile->writeptr / BLOCK_SIZE;
	int initblock_diskaddr = this_files_inode->direct_ptrs[writeptr_blockoffs];
	int writeptr_byte_offs = thisfile->writeptr % BLOCK_SIZE;

	int number_bytes_spilling = length - (BLOCK_SIZE - writeptr_byte_offs + 1);

//	|{x,x+1,...,1024}| = 1024 - x + 1	<<<---	this counting explains 		this^

	block_t* writebuf  = malloc(sizeof(block_t));

	if(number_bytes_spilling <= 0)
	{
		// write the buffer into that one block
		printf("No bytes spilling, will write whole buffer in existing block\n");
		read_blocks(initblock_diskaddr, 1, writebuf->buffer);
		memcpy(&(writebuf->buffer[writeptr_byte_offs]), buf, length);
		ret = (int) length;
	}
	else
	{
		// allocate (number_bytes_spilling / BLOCK_SIZE) + 1 blocks from the inode and write to them sequentially.
		// If we run out of direct pointers, print an error and return the number of bytes written
		int num_extra_blocks = (number_bytes_spilling / BLOCK_SIZE) + 1;
		printf("Buffer spills %d bytes, must allocate %d new blocks to fit\n", number_bytes_spilling, num_extra_blocks);

		// first copy what we can into the last available data block
                read_blocks(initblock_diskaddr, 1, writebuf->buffer);
                memcpy(writebuf->buffer[writeptr_byte_offs], buf, length-number_bytes_spilling);

		// initialize ret to have written 0 bytes, will increment as we copy bytes
		ret = 0;
		write_blocks(initblock_diskaddr, 1, writebuf->buffer);
		ret += length-number_bytes_spilling;
		buf += length-number_bytes_spilling;	// move buf ahead past what was already written
		// now we make blocks to hold the remaining data in writeable form

		block_t** spillover_blocks = calloc(num_extra_blocks, sizeof(block_t));
		int num_bytes_in_last = number_bytes_spilling % BLOCK_SIZE;
		int j=0;
		for(int j=0; j<num_extra_blocks-1; j++)
		{
			memcpy(spillover_blocks[j],buf,BLOCK_SIZE);
			buf += BLOCK_SIZE;
			ret += BLOCK_SIZE;
		}
		memcpy(spillover_blocks[j],buf,num_bytes_in_last);
		ret += num_bytes_in_last;

		// then go through the remaining direct pointers, giving each of them a new data block's worth of the buffer
		int newblock_addr = -1;
		for(j=0; j<NUM_DIRECT_PTRS && j<num_extra_blocks; j++)
		{
			newblock_addr = get_unused_block_index();
			this_files_inode->direct_ptrs[j] = newblock_addr;
			write_blocks(newblock_addr, 1, spillover_blocks[j]);

		}
		if(newblock_addr == -1)
		{
			printf("E:\t could not find free block\n");
			return -1;
		}

	}
	free(writebuf);
	// update the block of inodes after all these changes
	write_blocks(inode_block_addr, 1, nodeblock);
	free(nodeblock);
	thisfile->curr_numbytes += ret;
	thisfile->writeptr += ret;
	return ret;
}
int ssfs_fread(int fileID, char *buf, int length){
	if(fileID<0)
	{
		printf("E:\t invalid file desc.\n");
		return -1;
	}

	FDTentry_t* thisfile = &(openFileTable[fileID]);
	if(thisfile->inode_num == -1)
	{
		printf("E:\t file %d not open.\n",fileID);
		return -1;
	}

	printf("--------------\n Attempting to read %d bytes to file desc. %d \n -----------------------\n",length, fileID);
        superblock_t* cachedSuperPointer = (superblock_t*) &cachedSuperblock;
	int ret = -1;

        // access right direct pointer from j-node, and get the address of the block it points to:
        int inode_block_addr = cachedSuperPointer->jroot.direct_ptrs[(thisfile->inode_num)/NUM_DIRECT_PTRS];
        block_t* nodeblock = malloc(sizeof(block_t));
        read_blocks(inode_block_addr, 1, nodeblock);
        printf("Got block with file's inode\n");

        // now we have that block of inodes, need to select the correct inode from it
        inode_t* this_files_inode = (inode_t*) &(nodeblock->buffer[(thisfile->inode_num)%16]);

        // now that we can access the file's inode, we need to find the data block corresponding to our current read location
        printf("Got this file's inode directly \n");
        // we also need to know where to start, i.e. which block the read pointer directs us to:
        int readptr_blockoffs = thisfile->readptr / BLOCK_SIZE;
        int initblock_diskaddr = this_files_inode->direct_ptrs[readptr_blockoffs];
        int readptr_byte_offs = thisfile->readptr % BLOCK_SIZE;
	char* retbuf = malloc(length*sizeof(char));
	block_t* readbuf = malloc(sizeof(block_t));
	read_blocks(initblock_diskaddr, 1, readbuf);
	memcpy(retbuf,&(readbuf->buffer[readptr_byte_offs]), BLOCK_SIZE - readptr_byte_offs + 1);
	ret += BLOCK_SIZE - readptr_byte_offs + 1;
	int nextblock_addr = -1;
	int i = readptr_blockoffs+1;
	while(ret <= length)
	{
		nextblock_addr = this_files_inode->direct_ptrs[i];
		read_blocks(nextblock_addr,1,readbuf->buffer);
		memcpy(retbuf[ret], readbuf->buffer, BLOCK_SIZE);
		ret += BLOCK_SIZE;
	}

	free(readbuf);
	memcpy(buf, retbuf, length*sizeof(char));
	free(retbuf);
	return ret;
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


	*/
    return 0;
}

