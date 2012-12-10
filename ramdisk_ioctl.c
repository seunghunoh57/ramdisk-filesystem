/**************************************************************************/
/**** Ramdisk IOCTL module for HW3 in Operating systems                ****/
/**** The actual file system functions are stored in a separate file   ****/
/**** This file contains initialization and the entry point             ****/
/**************************************************************************/

/**
  * @author Raphael Landaverde
  * @author Chenkai Liu
  */
#include "defines.h"

MODULE_LICENSE("GPL");

static int ramdisk_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static struct file_operations pseudo_dev_proc_operations;
static struct proc_dir_entry *proc_entry;

// @var The ramdisk memory in the kernel */
static char *RAM_memory;

/**
 * Utility function to set a specified bit within a byte
 *
 * @param[in]  index  The byte index into RAM_memory for which to set the bit
 * @param[in]  bit  the specified bit to set
 * @remark  The most significant bit is 7, while the least significant bit is 0
 */
void setBit(int index, int bit)
{
    int mask;
    mask = 1;// 00000001b
    mask <<= bit;  // Shift the one to specified position
    RAM_memory[index] |= mask; // Set the bit using OR
}

/**
 * Utility function to clear a specified bit within a byte
 *
 * @param[in]  index  The byte index into RAM_memory for which to clear the bit
 * @param[in]  bit  the specified bit to clear
 * @remark  The most significant bit is 7, while the least significant bit is 0
 */
void clearBit(int index, int bit)
{
    int mask;
    mask = 1;// 00000001b
    mask <<= bit;  // Shift the one to specified position
    mask = ~mask;
    RAM_memory[index] &= mask; // Set the bit using OR
}

/**
 * Utility function to check if a specified bit is set within a byte
 *
 * @param[in]  index  The byte index into RAM_memory for which to check the bit
 * @param[in]  bit  the specified bit to check
 * @return  int   returns 1 if the bit is set, 0 if it is empty
 * @remark  The most significant bit is 7, while the least significant bit is 0
 */
int checkBit(int index, int bit)
{
    int mask;
    mask = 1;
    mask <<= bit;
    return (mask & RAM_memory[index]);
}

/**
 * Changes the block count in the superblock
 *
 * @param[in]  delta  1 if incrementing (block freed), -1 if decrementing (block added)
 * @remark    delta can be greater than 1 in magnitude, but only if you know what you are doing
 */
void changeBlockCount(int delta)
{
    int blockCount;
    memcpy(&blockCount, RAM_memory, sizeof(int));
    blockCount += delta;
    memcpy(RAM_memory, &blockCount, sizeof(int));
}

/**
 * Changes the index node count in the superblock
 *
 * @param[in]  delta  1 if incrementing (index node deleted), -1 if decrementing (index node added)
 * @remark    delta can be greater in magnitude, but only if you know what you are doing
 */
void changeIndexNodeCount(int delta)
{
    int blockCount;
    memcpy(&blockCount, RAM_memory + 4, sizeof(int));
    blockCount += delta;
    memcpy(RAM_memory + 4, &blockCount, sizeof(int));
}

/**
 * RAMDISK initialization
 * Initializes the ramdisk superblock, indexnodes, and bitmap to include the root directory and nothing else
 *
 */
void init_ramdisk(void)
{
    // First, we must clear all of the bits of RAM_memory to ensure they are all 0
    int ii, data;
    for (ii = 0 ; ii < FS_SIZE ; ii++)
        RAM_memory[ii] = '\0';  // Null terminator is 0

    /****** Set up the superblock *******/
    // Consists of two values, a 4 byte value containing the free block count
    // and a 4 byte value containing the number of free index nodes.  Initialized with
    // everything free
    data = TOT_AVAILABLE_BLOCKS;
    memcpy(RAM_memory, &data, sizeof(int));
    data = INDEX_NODE_COUNT;
    memcpy(RAM_memory + 4, &data, sizeof(int));
    // For now, thats all that our superblock contains, may expand more in the future

    /****************Create the root directory******************/
    createIndexNode("dir\0", "/\0",  256);

    /****** At start, root directory has no files, so its block is empty (but claimed) at the moment ******/
    printk("RAMDISK has been initialized with memory\n");
}

/************************ INTERNAL HELPER FUNCTIONS **************************/

/**
 * Fills the input array with the block numbers of all the allocated blocks for a given index node, valid for both dir and fil
 *
 * @param[in-out]  blockArray  An int array which will hold the values of allocated blocks.
 * @param[in]  inodeNum  the inode number to search through
 * @require blockArray MUST be allocated with size 4168 before being passed into this functino
 */
void getAllocatedBlockNumbers(int *blockArray, int inodeNum)
{
    int ii, jj, counter, offset, value;
    char *inodePointer;
    char *singleIndirPointer;
    char *doubleIndirPointer;
    char *blockPointer;

    /* Now, travel through the directory to get all of the block numbers */
    counter = 0;

    /* Direct */
    inodePointer = RAM_memory + INDEX_NODE_ARRAY_OFFSET + inodeNum * INDEX_NODE_SIZE;
    for (ii = 0 ; ii < NUM_DIRECT ; ii++)
    {
        value = (int) * ( (int *)(inodePointer + DIRECT_1 + ii * 4) );
        blockArray[counter] = value;
        /* Now after value is set, check for -1 ( we need to have at least one -1 as an end condition for the external check) */
        if (value == -1)
            return;

        counter++;
    }

    /* Singly Indirect */
    singleIndirPointer = inodePointer + SINGLE_INDIR;
    offset = (int) * ( (int *)(singleIndirPointer) );
    if (offset == -1)
    {
        /* This means there are no single indirect pointers, so set the next to -1 and return */
        blockArray[counter] = -1;
        return;
    }

    blockPointer = RAM_memory + DATA_BLOCKS_OFFSET + offset * RAM_BLOCK_SIZE;
    for (ii = 0 ; ii < 64 ; ii++)
    {
        value = (int) * ( (int *)(blockPointer + ii * 4) );
        blockArray[counter] = value;
        /* Once again, check for termination */
        if (value == -1)
            return;
        counter++;
    }

    /* Double Indirect */
    singleIndirPointer = inodePointer + DOUBLE_INDIR; /* The top level doubly indirect pointer */
    offset = (int) * ( (int *)(singleIndirPointer) );
    if (offset == -1)
    {
        /* This means there are no doubly indirect pointers, so set the next to -1 and return */
        blockArray[counter] = -1;
        return;
    }
    doubleIndirPointer = RAM_memory + DATA_BLOCKS_OFFSET + offset * RAM_BLOCK_SIZE;
    for (ii = 0 ; ii < 64 ; ii++)
    {
        /* Get the next offset for the next pointer block */
        offset = (int) * ( (int *)(doubleIndirPointer + ii * 4) );
        if (offset == -1)
        {
            /* This means there are no more doubly indirect pointers, so set the next to -1 and return */
            blockArray[counter] = -1;
            return;
        }
        blockPointer = RAM_memory + DATA_BLOCKS_OFFSET + offset * RAM_BLOCK_SIZE;
        for (jj = 0 ; jj < 64 ; jj++)
        {
            /* This is the innermost loop, where the values are not actually data blocks */
            value = (int) * ( (int *)(blockPointer + ii * 4) );
            blockArray[counter] = value;
            if (value == -1)
                return;
            counter++;
        }
    }
    /* If it made it here, then this file has the total max amount of blocks possible! */
}

/**
 * Returns the index node of a file stored in a directory
 *
 * @return  int  the index node index for the desired file, returns -1 if not found or -2 if indexNode is not a directory
 * @param[in]  indexNode  the indexNode to search, must be a valid directory
 * @param[in]  filename  the filename to search for
 */
int findFileIndexNodeInDir(int indexNode, char *filename)
{
    /* Some variables */
    short fileCount;
    char *directory;
    char *blockPointer;
    int counter, ii, jj;
    int outputNode;

    /* The array of all of the allocated blocks for this indexNode */
    int nodeBlocks[MAX_BLOCKS_ALLOCATABLE];

    /* The index node we want */
    directory = RAM_memory + INDEX_NODE_ARRAY_OFFSET + (INDEX_NODE_SIZE * indexNode);
    if ( strcmp(directory + INODE_TYPE, "dir\0") )
    {
        /* These are not equal, thus the inode is not a directory, fail here */
        return -2;
    }

    /* Now, get the file count of this directory for use in iterating through */
    memcpy(&fileCount, directory + INODE_FILE_COUNT, sizeof(short) );

    /* Finally, get the aray of all of the blocks allocated for this index node */
    getAllocatedBlockNumbers(nodeBlocks, indexNode);

    /* Now, just loop through this array until hit a -1 or the desired file is found */
    counter = 0;
    for (ii = 0 ; ii < MAX_BLOCKS_ALLOCATABLE ; ii++)
    {
        if (nodeBlocks[ii] == -1)
        {
            /* Did not find the file */
            if (counter < fileCount)
                printk("Data corruption, saved fileCount and actual file count mismatch\n");
            return -1;
        }
        blockPointer = RAM_memory + DATA_BLOCKS_OFFSET + nodeBlocks[ii] * RAM_BLOCK_SIZE;
        /* Now, look through this block for the filename */
        for (jj = 0 ; jj < 64 ; jj++)
        {
            if (counter >= fileCount)
                return -1; /* Exceeded the file count */

            if ( !strcmp(blockPointer, filename) )
            {
                /* We found the file */
                outputNode = (int) * ( (short *) (blockPointer + INODE_NUM_OFFSET) );
                return outputNode;
            }
            counter++;
        }
    }
    /* If it made it to this point, then directory had max possible files, and the file was also not found */
    return -1;
}

/* Definition of function in the defines file */
int getIndexNodeNumberFromPathname(char *pathname, int dirFlag)
{
    char delim = '/';
    int ii;
    int counter;
    int pathsize, numDirs;
    char nextFile[14];
    int currentIndexNode, nextIndexNode;

    counter = 0;
    numDirs = 0; /* Count up the number of directories found for later use */
    pathsize = 1;
    while (1)
    {
        if (pathname[counter] == delim)
        {
            numDirs++;
        }
        else if (pathname[counter] == '\0')
        {
            break; /* I don't count the null character in the path size */
        }
        pathsize++;
        counter++;
    }

    /* We now know how many dirs we are dealing with, and the pathsize, so we can extrac the names of all directories and put them in an array */
    currentIndexNode = 0; /* Root always at 0 */
    counter = 1; /* Used to keep track of the pathname index, starts at 1 to ignore root */
    if (dirFlag)
    {
        numDirs--;  /* Loop through one less directory to return the directory inode, now the file inode */
    }
    for (numDirs ; numDirs > 0 ; numDirs--)
    {
        for (ii = 0 ; ii < 14 ; ii++ )
        {
            /* Get the name of the current dir */
            nextFile[ii] = pathname[counter];
            /* First check and see if we have the filename for the regular file(last iteration of outer loop) */
            if (nextFile[ii] == '\0')
            {
                /* The final iteration, we have the filename */
                break;
            }

            /* Increment the counter for the pathname */
            counter++;

            /* Break if we have the whole filename now for a directory*/
            if (nextFile[ii] == '/')
            {
                nextFile[ii] = '\0';
                break;
            }
        }
        /* Get the index node of the next directory */
        nextIndexNode = findFileIndexNodeInDir(currentIndexNode, nextFile);

        if (nextIndexNode == -1)
        {
            return -1; /*Directory or file does not exist */
        }

        currentIndexNode = nextIndexNode;
    }

    /* At this point, currentIndexNode has the index node for the file, so we return this */
    return currentIndexNode;
}

/**
 * Get the next free IndexNodeNumber. Also clears up the block pointers.
 *
 * @return  int return index node number of a free index node
 */

int getNewIndexNodeNumber(void)
{

    int ii;
    char *indexNodeType;

    for (ii = 0; ii < INDEX_NODE_COUNT; ii++)
    {

        indexNodeType = RAM_memory + INDEX_NODE_ARRAY_OFFSET + ii * INDEX_NODE_SIZE + INODE_TYPE;
        if (!(strlen(indexNodeType) > 1))
        {
            /* Clear up this index node before giving it back */
            negateIndexNodePointers(ii);
            /* Subtract 1 from the index node count */
            changeIndexNodeCount(-1);
            /* Return the index node number */
            return ii;
        }
    }

    return -1; /* No index node was found, so return this as an error */
}

/**
 * Helper method to clear an index node when a file/dir is removed
 *
 * @return  void
 * @param[indexNodeNumber]  int index node number to clear
 */
void clearIndexNode(int IndexNodeNumber)
{

    int i, j, blocknumber, blocknumberInner;
    char *indexNodeStart;
    char *singleIndirectBlockStart;
    char *doubleIndirectBlockStart;

    indexNodeStart = RAM_memory + INDEX_NODE_ARRAY_OFFSET + IndexNodeNumber * INDEX_NODE_SIZE;

    /****** Free memory used by index node *****/
    // Direct memory freeing
    for (i = 0; i < NUM_DIRECT; i++)
    {
        blocknumber = (int) * (indexNodeStart + DIRECT_1 + i * 4);

        // If we received an unallocated block, we are done freeing memory
        if (blocknumber <= 0)
            break;

        freeBlock(blocknumber);
    }

    // Single indirect memory freeing
    blocknumber = (int) * (int *)(indexNodeStart + SINGLE_INDIR);
    singleIndirectBlockStart =  RAM_memory + DATA_BLOCKS_OFFSET + (blocknumber * RAM_BLOCK_SIZE);
    freeBlock(blocknumber);

    for (i = 0; i < 64; i++)
    {
        blocknumber = (int) * (int *)(singleIndirectBlockStart + i * 4);

        // If we received an unallocated block, we are done freeing memory
        if (blocknumber == -1)
            break;

        freeBlock(blocknumber);
    }

    // Double indirect memory freeing
    blocknumber = (int) * (int *)(indexNodeStart + DOUBLE_INDIR);
    singleIndirectBlockStart =  RAM_memory + DATA_BLOCKS_OFFSET + (blocknumber * RAM_BLOCK_SIZE);
    freeBlock(blocknumber);

    for (i = 0; i < 64; i++)
    {
        blocknumber = (int) * (int *)(singleIndirectBlockStart + i * 4);

        // If we received an unallocated block, we are done freeing memory
        if (blocknumber <= 0)
            break;

        doubleIndirectBlockStart = RAM_memory + DATA_BLOCKS_OFFSET + (blocknumber * RAM_BLOCK_SIZE);

        for (j = 0; j < 64; j++)
        {
            blocknumberInner = (int) * (int *)(doubleIndirectBlockStart + j * 4);

            if (blocknumberInner <= 0)
                break;

            freeBlock(blocknumberInner);
        }
        freeBlock(blocknumber);
    }

    /****** End of Free memory used by index node *****/

    // Clear index node bits
    for (i = 0; i < INDEX_NODE_SIZE; i++)
        indexNodeStart[i] = '\0';

    /* Update the superblock index node count */
    changeIndexNodeCount(1);
}

/**
 * Helper function for setting memory region to -1 for easier tracking
 * Set Direct 1-8, single indirect 9 and double indirect 10 byte to -1
 *
 * @param[in-out]  indexNodeNumber - id of the index number
 */
void negateIndexNodePointers(int indexNodeNumber)
{
    int ii, negate;
    char *indexNodeMemoryRegion;
    negate = -1;
    indexNodeMemoryRegion = RAM_memory + INDEX_NODE_ARRAY_OFFSET + indexNodeNumber * INDEX_NODE_SIZE + DIRECT_1;
    for (ii = 0 ; ii < 10 ; ii++)
    {
        memcpy(indexNodeMemoryRegion + ii * 4, &negate, sizeof(int));
    }
}

/**
 * Creates a new index node
 *
 * @return  type  description
 * @param[in-out]  type  type of the node, 'dir' or 'reg'
 * @param[in-out]  filename  name of the file or dir, 'dir' or 'reg'
 * @param[in-out]  memorysize  size of the file or dir in bytes
 */

int createIndexNode(char *type, char *pathname, int memorysize)
{
    int indexNodeNumber;
    int data;
    int directoryNodeNum;
    int numberOfBlocksRequired, numBlocksPlusPointers, numBlocksDoubleIndir;
    int blocksAvailable;
    short shortData;
    char *indexNodeStart, *filename;

    // Calculate the actual number of blocks needed for the file
    numberOfBlocksRequired = (memorysize / RAM_BLOCK_SIZE) + 1;
    numBlocksPlusPointers = numberOfBlocksRequired;
    if (numberOfBlocksRequired > NUM_DIRECT)
    {
        /* Add an extra block for the singly indirect block */
        numBlocksPlusPointers += 1;
        if (numberOfBlocksRequired > 72)
        {
            /* Need extra blocks for the double indirect pointer blocks */
            numBlocksPlusPointers += 1; /* Add one for the pointer block */
            numBlocksDoubleIndir = numberOfBlocksRequired - 72;
            /* Add the number of blocks necessary for all the double indir necessary */
            numBlocksPlusPointers += (numBlocksDoubleIndir / 64) + 1;
        }
    }

    memcpy(&blocksAvailable, RAM_memory, sizeof(int));
    if (numBlocksPlusPointers > blocksAvailable)
    {
        printk("Not enough blocks available!\n");
        return -1;
    }

    if (memorysize > MAX_FILE_SIZE)
    {
        printk("File too large!\n");
        return -1;
    }

    // String parsing to get the file name and directory node


    /* Set the index node values */
    indexNodeNumber = getNewIndexNodeNumber();
    allocMemoryForIndexNode(indexNodeNumber, numberOfBlocksRequired);
    indexNodeStart = RAM_memory + INDEX_NODE_ARRAY_OFFSET + indexNodeNumber * INDEX_NODE_SIZE;

    // Insert the file into the right directory node
    if (strcmp(type, "reg\0") == 0)
    {
        // directoryNodeNum = getIndexNodeNumberFromPathname(pathname, 1);
        directoryNodeNum = 0;
        filename = getFileNameFromPath(pathname);
        insertFileIntoDirectoryNode(directoryNodeNum, indexNodeNumber, filename);
    }
    else
    {
        filename = pathname;
    }


    /* Set the index node values */
    // Set the type
    strcpy(indexNodeStart + INODE_TYPE, type);
    // Set indexNode size
    data = memorysize;
    memcpy(indexNodeStart + INODE_SIZE, &data, sizeof(int));
    // Set the file count, default to 0
    shortData = 0;
    memcpy(indexNodeStart + INODE_FILE_COUNT, &shortData , sizeof(short));
    strcpy(indexNodeStart + INODE_FILE_NAME, filename);

    printk("New index node: %d created\n", indexNodeNumber);

    return indexNodeNumber;
}

char *getFileNameFromPath(char *pathname)
{

    char delim, temp, *returnFilename;
    int delimPosition, index;
    delim =  '/';
    index = 0;
    temp = pathname[index];
    while (temp != '\0')
    {
        if (temp == delim)
            delimPosition = index;
        index++;
        temp = pathname[index];
    }

    returnFilename = pathname + delimPosition + 1;
    return returnFilename;
}

/**
 * Helper method that returns how many files are stored
 * in a memory block
 *
 * @returns number of memory block
 * @param[in-out]  name  description
 */
int numberOfFilesInMemoryBlock(int memoryBlock)
{

    if (memoryBlock == -1)
        return 0;

    char *filename;
    char *memoryblockStart;
    short inodeNum;
    int i, numberOfFiles;
    numberOfFiles = 0;
    memoryblockStart = RAM_memory + DATA_BLOCKS_OFFSET + (memoryBlock * RAM_BLOCK_SIZE);

    for (i = 0; i < (RAM_BLOCK_SIZE / FILE_INFO_SIZE); i++)
    {
        inodeNum = (short) * (short *)(memoryblockStart + i * FILE_INFO_SIZE + INODE_NUM_OFFSET);
        if (inodeNum > 0)
       {
            numberOfFiles++;
            filename = (memoryblockStart + i * FILE_INFO_SIZE);
            printk("NODE: %i  FILENAME: %s\n", inodeNum, filename);
        }
    }
    return numberOfFiles;
}

void insertFileIntoDirectoryNode(int directoryNodeNum, int fileNodeNum, char *filename)
{

    char *indexNodeStart, *dirlistingstart;
    int i, blocknumber, freeblock, numOfFiles;
    short inodeNum;
    freeblock = -1;
    blocknumber = 0;

    indexNodeStart = RAM_memory + INDEX_NODE_ARRAY_OFFSET + directoryNodeNum * INDEX_NODE_SIZE;

    // Look for the last memory block that is free
    for (i = 0; i < NUM_DIRECT; i++)
    {
        blocknumber = (int) * (int *)(indexNodeStart + DIRECT_1 + i * 4);

        if (blocknumber != -1)
        {
            numOfFiles = numberOfFilesInMemoryBlock(blocknumber);
            if (numOfFiles < (RAM_BLOCK_SIZE / FILE_INFO_SIZE))
            {
                freeblock = blocknumber;
                break;
            }
        }
    }
    dirlistingstart = RAM_memory + DATA_BLOCKS_OFFSET + (freeblock * RAM_BLOCK_SIZE);

    // Find the next unused directry file index
    for (i = 0; i < (RAM_BLOCK_SIZE / FILE_INFO_SIZE); i++)
    {
        inodeNum = (short) * (short *) (dirlistingstart + i * FILE_INFO_SIZE + INODE_NUM_OFFSET);

        // We have find a directory block with no blocknumber, so its unused
        if (inodeNum <= 0)
        {
            strcpy(dirlistingstart + i * FILE_INFO_SIZE, filename);
            memcpy(dirlistingstart + i * FILE_INFO_SIZE + INODE_NUM_OFFSET, (short *)&fileNodeNum , sizeof(short));
            return;
        }
    }

    // If we here, we did not find any free direct memory blocks for our new file, look in single indirect memory blocks
    //blocknumber

}

/**
 * Allocate memory for indexNode
 *
 * @param[in]  indexNodeNumber - index Node number to allocate memory for
 * @param[in]  numberOfBlocks - number of blocks to allocate
 */

void allocMemoryForIndexNode(int indexNodeNumber, int numberOfBlocks)
{

    char *indexNodeStart;
    char *singleIndirectBlockStart;
    char *doubleIndirectBlockStart;
    int i, j, blockNumber, singleIndirectMemBlock, doubleIndirectMemBlock, noallocationFlag;
    indexNodeStart = RAM_memory + INDEX_NODE_ARRAY_OFFSET + indexNodeNumber * INDEX_NODE_SIZE;
    noallocationFlag = -1;

    // Allocate memory for direct blocks first
    for (i = 0; i < NUM_DIRECT; i++)
    {
        if (numberOfBlocks == 0)
        {
            memcpy(indexNodeStart + DIRECT_1 + 4 * i, &noallocationFlag, sizeof(int));
        }
        else
        {
            blockNumber = getFreeBlock();
            // @todo Weird situation, pointers are 8 bytes, so storing them doesn't make sense, store number instead
            memcpy(indexNodeStart + DIRECT_1 + 4 * i, &blockNumber, sizeof(int));

            numberOfBlocks--;
        }
    }

    if (numberOfBlocks == 0)
    {
        return;
    }

    // Allocate memory for single indirect block second
    singleIndirectMemBlock = getFreeBlock();
    memcpy(indexNodeStart + SINGLE_INDIR, &singleIndirectMemBlock, sizeof(int));
    singleIndirectBlockStart =  RAM_memory + DATA_BLOCKS_OFFSET + (singleIndirectMemBlock * RAM_BLOCK_SIZE);

    // Each data block hold 64 pointers to further memory blocks
    for (i = 0; i < 64; i++)
    {
        if (numberOfBlocks == 0)
        {
            memcpy(singleIndirectBlockStart + 4 * i, &noallocationFlag, sizeof(int));
        }
        else
        {
            blockNumber = getFreeBlock();
            memcpy(singleIndirectBlockStart + 4 * i, &blockNumber, sizeof(int));

            numberOfBlocks--;
        }

    }

    if (numberOfBlocks == 0)
    {
        return;
    }

    // Allocate memory for double indirect block third
    doubleIndirectMemBlock = getFreeBlock();
    memcpy(indexNodeStart + DOUBLE_INDIR, &doubleIndirectMemBlock, sizeof(int));
    doubleIndirectBlockStart = RAM_memory + DATA_BLOCKS_OFFSET + (doubleIndirectMemBlock * RAM_BLOCK_SIZE);

    // For each data block, we will allocate another data block of 64 pointers
    for (i = 0; i < 64; i++)
    {
        if (numberOfBlocks == 0)
        {
            memcpy(doubleIndirectBlockStart + 4 * i, &noallocationFlag, sizeof(int));
        }
        else
        {
            singleIndirectMemBlock = getFreeBlock();
            singleIndirectBlockStart =  RAM_memory + DATA_BLOCKS_OFFSET + (singleIndirectMemBlock * RAM_BLOCK_SIZE);
            memcpy(doubleIndirectBlockStart + 4 * i, &singleIndirectMemBlock, sizeof(int));

            // Each data block hold 64 pointers to further memory blocks
            for (j = 0; j < 64; j++)
            {
                if (numberOfBlocks == 0)
                {
                    memcpy(singleIndirectBlockStart + 4 * j, &noallocationFlag, sizeof(int));
                }
                else
                {
                    blockNumber = getFreeBlock();
                    memcpy(singleIndirectBlockStart + 4 * j, &blockNumber, sizeof(int));

                    numberOfBlocks--;
                }

            }
        }
    }
}

/************************ MEMORY MANAGEMENT *****************************/
int getFreeBlock(void)
{

    int i, j;

    for (i = 0; i < BLOCK_BITMAP_SIZE; i++)
    {
        for (j = 7; j >= 0; j--)
        {
            if (!checkBit(BLOCK_BITMAP_OFFSET + i, j))
            {
                // Return block number
                setBit(BLOCK_BITMAP_OFFSET + i, j);
                /* Decrement the block count in the superblock */
                changeBlockCount(-1);
                // Convert the loop indices into a block bitmap index
                return i * 8 + (7 - j);
            }

        }

    }
    // No free blocks
    return -1;
}

void freeBlock(int blockindex)
{
    int major, minor, blockCount;
    major = blockindex / 8;
    minor = blockindex % 8;
    minor = 7 - minor;
    clearBit(BLOCK_BITMAP_OFFSET + major, minor);

    /* Increment block count in the superblock */
    changeBlockCount(1);
}

/************************ DEBUGGING FUNCTIONS *****************************/

/**
 * This debugging function prints the specified number of bitmap numberOfBits
 *
 * @return  void prints the bitmap bits in 25 bit chunks
 * @param[in]  numberOfBits specifies the number of bits to print
 */
void printBitmap(int numberOfBits)
{
    int i, j, bitCount;
    bitCount = 0;
    for (i = 0; i < BLOCK_BITMAP_SIZE; i++)
    {
        for (j = 7; j >= 0; j--)
        {
            // We have printed the right number of bits
            if (bitCount >= numberOfBits)
                return;
            if (bitCount % 25 == 0)
                printk("Printing %d - %d bitmaps\n", bitCount, bitCount + 24);
            if (!checkBit(BLOCK_BITMAP_OFFSET + i, j))
                printk("0 ");
            else
                printk("1 ");
            bitCount++;
            if (bitCount % 25 == 0)
                printk("\n");
        }
    }
}

void printIndexNode(int nodeIndex)
{

    char *indexNodeStart;
    char *singleIndirectStart;
    char *doubleIndirectStart, *dirlistingstart, *filename;
    int singleDirectBlock, doubleDirectBlock, memoryblock, memoryblockinner, i, j;
    short indexNodeNum;

    indexNodeStart = RAM_memory + INDEX_NODE_ARRAY_OFFSET + nodeIndex * INDEX_NODE_SIZE;
    printk("-----Printing indexNode %d-----\n", nodeIndex);
    printk("NODE TYPE:%.4s\n", indexNodeStart + INODE_TYPE);
    printk("NODE SIZE:%d\n", (int) * ( (int *) (indexNodeStart + INODE_SIZE) ) );
    printk("FILE COUNT:%hi\n", (short) * ( (short *)(indexNodeStart + INODE_FILE_COUNT) ) );
    printk("FILE NAME: %s\n", indexNodeStart + INODE_FILE_NAME);

    // Prints the Direct memory channels
    printk("MEM DIRECT: ");
    for (i = 0; i <= 7; i++)
    {
        printk("%d  ", (int) * ((int *)(indexNodeStart + DIRECT_1 + 4 * i)));
    }
    printk("\n");

    // Prints the Single indirect channels
    singleDirectBlock = (int) * (indexNodeStart + SINGLE_INDIR) ;
    singleIndirectStart = RAM_memory + DATA_BLOCKS_OFFSET + (singleDirectBlock * RAM_BLOCK_SIZE);

    printk("MEM SINGLE INDIR: \n");
    if (singleDirectBlock != -1)
    {
        for (i = 0; i < RAM_BLOCK_SIZE / 4; i++)
        {
            memoryblock = (int)(*(singleIndirectStart + 4 * i));
            if (memoryblock != 0 || memoryblock != -1)
                printk("%d  ", memoryblock);
        }
        printk("\n");
    }


    // Prints the Double indirect channels
    doubleDirectBlock = (int) * (indexNodeStart + DOUBLE_INDIR) ;
    doubleIndirectStart = RAM_memory + DATA_BLOCKS_OFFSET + (doubleDirectBlock * RAM_BLOCK_SIZE);

    printk("MEM DOUBLE INDIR: \n");
    if (doubleDirectBlock != -1)
    {
        for (i = 0; i < RAM_BLOCK_SIZE / 4; i++)
        {
            singleDirectBlock = (int) * ((int *)(doubleIndirectStart + 4 * i));
            singleIndirectStart = RAM_memory + DATA_BLOCKS_OFFSET + (singleDirectBlock * RAM_BLOCK_SIZE);

            if (singleDirectBlock > -1)
            {
                printk("Sector %d  block %d:\n ", i, singleDirectBlock);
                for (j = 0; j < RAM_BLOCK_SIZE / 4; j++)
                {
                    memoryblockinner =  (int) * ((int *)(singleIndirectStart + 4 * j));

                    if (memoryblockinner > -1)
                        printk("%d ", memoryblockinner);
                }
                printk("\n");
            }

        }
    }

    // If directory, print all the files in the directory

    if (strcmp("dir\0",  indexNodeStart + INODE_TYPE) == 0)
    {
        printk("Directory Listing: \n");
        for (i = 0; i <= 7; i++)
        {

            memoryblock = (int) * ((int *)(indexNodeStart + DIRECT_1 + 4 * i));
            dirlistingstart = RAM_memory + DATA_BLOCKS_OFFSET + (memoryblock * RAM_BLOCK_SIZE);

            for (j = 0; j < RAM_BLOCK_SIZE / FILE_INFO_SIZE; j++)
            {
                indexNodeNum = (short) * (short *)(dirlistingstart + FILE_INFO_SIZE * j + INODE_NUM_OFFSET);
                if (indexNodeNum > 0 && memoryblock > -1)
                {
                    filename = (dirlistingstart + FILE_INFO_SIZE * j);
                    printk("Filename: %s  Inode: %hd\n", filename, indexNodeNum);
                }
            }
        }
        printk("\n");
    }

    printk("-----End of Printing indexNode %d-----\n", nodeIndex);
}

/************************INIT AND EXIT ROUTINES*****************************/

/**
* The main init routine for the kernel module.  Initializes proc entry
*/
static int __init initialization_routine(void)
{
    int indexNodeNum;

    printk("<1> Loading RAMDISK filesystem\n");

    pseudo_dev_proc_operations.ioctl = &ramdisk_ioctl;

    /* Start create proc entry */
    proc_entry = create_proc_entry("ramdisk", 0444, NULL);
    if (!proc_entry)
    {
        printk("<1> Error creating /proc entry for ramdisk.\n");
        return 1;
    }

    //proc_entry->owner = THIS_MODULE; <-- This is now deprecated
    proc_entry->proc_fops = &pseudo_dev_proc_operations;

    // Initialize the ramdisk here now
    RAM_memory = (char *)vmalloc(FS_SIZE);

    // Initialize the superblock and all other memory segments
    init_ramdisk();

    // printk("MEM BEFORE\n");
    // printBitmap(400);
    indexNodeNum = createIndexNode("reg\0", "/myfile.txt\0",  64816);
    printIndexNode(indexNodeNum);
    printIndexNode(0);

    // clearIndexNode(indexNodeNum);
    // printk("MEM AFTER\n");
    // printBitmap(400);

    // Verify that memory is correctly set up initially

    return 0;
}

/**
* Clean up routine
*/
static void __exit cleanup_routine(void)
{

    printk("<1> Dumping RAMDISK module\n");
    remove_proc_entry("ramdisk", NULL);

    return;
}

/****************************IOCTL ENTRY POINT********************************/

static int ramdisk_ioctl(struct inode *inode, struct file *file,
                         unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {

    case RAM_CREATE:
        my_printk("Creating file...\n");
        break;

    case RAM_MKDIR:
        my_printk("Making directory...\n");
        break;

    case RAM_OPEN:
        my_printk("Opening file...\n");
        break;

    case RAM_CLOSE:
        my_printk("Closing file...\n");
        break;

    case RAM_READ:
        my_printk("Reading file...\n");
        break;

    case RAM_WRITE:
        my_printk("Writing file...\n");
        break;

    case RAM_LSEEK:
        my_printk("Seeking into file...\n");
        break;

    case RAM_UNLINK:
        my_printk("Unlinking file...\n");
        break;

    case RAM_READDIR:
        my_printk("Reading file from directory...\n");
        break;

    default:
        return -EINVAL;
        break;
    }

    return 0;
}

// Init and Exit declaration
module_init(initialization_routine);
module_exit(cleanup_routine);