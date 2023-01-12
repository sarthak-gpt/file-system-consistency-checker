/*
Created by Sarthak Gupta
All rights reserved © sarthak-gpt
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>

#include "types.h"
#include "fs.h"

#define BLOCK_SIZE (BSIZE)

typedef struct file_image {
    struct superblock *sb;
    char *base_address;

    uint num_total_blocks;

    uint num_inodes;
    uint num_blocks_for_inodes;
    char *first_inode_address; // First inode address

    uint num_bitmap_blocks;
    char *first_bitmap_address;

    uint num_data_blocks;
    char *first_data_block_address;
    uint first_data_block_logical_address;
} file_image;


void check_rule_1(struct dinode *inode){
    if (inode->type < 0 || inode->type > 3) {
        fprintf(stderr, "ERROR: bad inode.\n");
        exit(1);
    }
}

void check_rule_2(file_image *image, struct dinode *inode) {
    int i;
    uint block_address;
    uint indirect_block_address;
    uint *indirect_block;

    for (i = 0; i < NDIRECT; i++){ // checking direct address for 0 - 11
         block_address = inode->addrs[i];

        if (block_address != 0 && (block_address < image->first_data_block_logical_address || block_address >= image->num_total_blocks )){
            fprintf(stderr, "ERROR: bad direct address in inode.\n");
            exit(1);
        }
    }

    indirect_block_address = inode->addrs[NDIRECT]; // This is indirect address
    
    if (indirect_block_address != 0) {
        if (indirect_block_address < image->first_data_block_logical_address || indirect_block_address >= image->num_total_blocks) {
            fprintf(stderr, "ERROR: bad indirect address in inode.\n");
            exit(1);
        }
        
        indirect_block = (uint *) (image->base_address + indirect_block_address * BLOCK_SIZE);
        for (i = 0; i < NINDIRECT; i++) { // checking Direct address inside the Indirect Address
        
        uint block_address = *indirect_block;
        
        if (block_address != 0 && (block_address < image->first_data_block_logical_address || block_address >= image->num_total_blocks )){
            fprintf(stderr, "ERROR: bad direct address in inode.\n");
            exit(1);
        } 
        }
    }
       
}

void check_rule_3(file_image *image, struct dinode *inode) {
    
    if (inode->type != 1) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }

    uint root_data_block_address = inode->addrs[0];
    struct dirent *de = (struct dirent *)(image->base_address + root_data_block_address * BLOCK_SIZE);
    de++;
    if (de->inum != 1) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }
    
}

void check_rule_4(file_image *image, struct dinode *inode, int inum){
    uint block_address = inode->addrs[0];
    struct dirent *de = (struct dirent *)(image->base_address + block_address * BLOCK_SIZE);
    if (strcmp(de->name, ".")!= 0 || de->inum != inum) {
        fprintf(stderr, "ERROR: directory not properly formatted.\n");
        exit(1);
    }

    de++;

    if (strcmp(de->name, "..")!=0) {
        fprintf(stderr, "ERROR: directory not properly formatted.\n");
        exit(1);
    }
}

void check_rule_5(file_image *image, struct dinode *inode){
    int i;
    uint block_address;
    uint *indirect_block;
    char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

    for (i = 0; i < NDIRECT; i++){ // checking direct address for 0 - 11
        block_address = inode->addrs[i];
        if (block_address != 0 && !((*(image->first_bitmap_address + block_address / 8)) & (bitarr[block_address % 8]))){
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
        }  
    }

    block_address = inode->addrs[NDIRECT]; // Checking indirect Address
    indirect_block = (uint *)(image->base_address + block_address * BLOCK_SIZE);
    for (i = 0; i < NINDIRECT; i++ , indirect_block++) { // Checking direct addresses inside indirect data block
        block_address = *indirect_block;

        if (block_address != 0 && !((*(image->first_bitmap_address + block_address / 8)) & (bitarr[block_address % 8]))){
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
        }  
        
    }
}

void check_rule_6_7_8(file_image *image) {
    int data_blocks_used[image->num_total_blocks];
    int direct_address_block_count[image->num_total_blocks];
    int indirect_address_block_count[image->num_total_blocks];

    int i, j;
    uint block_address;
    uint *indirect_block_address; 
    char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

    for (i = 0; i < image->num_total_blocks; i++) {
        data_blocks_used[i] = 0;
        direct_address_block_count[i] = 0;
        indirect_address_block_count[i] = 0;
    }
        
    
    struct dinode *inode = (struct dinode *) image->first_inode_address;

    for (i = 0; i < image->num_inodes; i++, inode++){
        if (inode->type != 0){

            for (j = 0; j < NDIRECT; j++){
                block_address = inode->addrs[j];

                if (block_address != 0) {
                    data_blocks_used[block_address]++; // marking in-use direct blocks as in use
                    direct_address_block_count[block_address]++;
                }
                    
            }

            block_address = inode->addrs[NDIRECT];

            if (block_address != 0){

                data_blocks_used[block_address]++; // marking indirect block as in use
                indirect_address_block_count[block_address]++;

                indirect_block_address = (uint *)(image->base_address + block_address * BLOCK_SIZE);

                for (j = 0; j < NINDIRECT; j++, indirect_block_address++){
                    block_address = *indirect_block_address;

                    if (block_address != 0){ // marking in-use direct blocks as in use
                        data_blocks_used[block_address]++;
                        direct_address_block_count[block_address]++;
                    }
                }
            }
            
        }
    }

    //checking for Rule 6 for Bitmap in-use but not in-use in data block
    for (i = image->first_data_block_logical_address ; i < ( image->first_data_block_logical_address + image->num_data_blocks); i++) 

        if (((*(image->first_bitmap_address + (uint) i / 8)) & (bitarr[(uint)i % 8])) && data_blocks_used[i] == 0) {
            fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
            exit(1);
        }

    // Checking for Rule 7 and 8 : Direct and Indirect address should be used only once
    for (i = image->first_data_block_logical_address ; i < ( image->first_data_block_logical_address + image->num_data_blocks); i++) {
        if (direct_address_block_count[i] > 1) { 
            fprintf(stderr, "ERROR: direct address used more than once.\n");
            exit(1);
        }

        if (indirect_address_block_count[i] > 1) { 
            fprintf(stderr, "ERROR: indirect address used more than once.\n");
            exit(1);
        }

    }

     
}

void DFS(file_image *image, struct dinode *root_inode, int *inode_count){
    int i;
    int j;
    struct dinode *inode;
    struct dirent *de;
    uint block_address;
    uint *indirect_block_address;

    if (root_inode->type != 1) // base case
        return;

    for (i = 0; i < NDIRECT; i++) {
        block_address = root_inode->addrs[i];

        if (block_address != 0) {  // checking in-use data block
            de = (struct dirent *)(image->base_address + block_address * BLOCK_SIZE); 

            for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++ , de++) {// iterating through all directory entries in the in-use data block
                if (de->inum == 0)
                    continue;
                if (strcmp(de->name, ".")== 0)
                    continue;
                if (strcmp(de->name, "..") == 0)
                    continue;

                inode_count[de->inum]++;
                inode = ((struct dinode *)(image->base_address + 2 * BLOCK_SIZE)) + de->inum;
                DFS(image, inode, inode_count);
            }
        }
    }

    block_address = root_inode->addrs[NDIRECT];

    if (block_address == 0)
        return ;

    // indirect pointer in-use
    indirect_block_address = (uint *)(image->base_address + block_address * BLOCK_SIZE);

    for(i = 0; i < NINDIRECT; i++, indirect_block_address++) {
        block_address = *indirect_block_address;

        if (block_address != 0) {  // checking in-use data block
            de = (struct dirent *)(image->base_address + block_address * BLOCK_SIZE); 

            for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++ , de++) {// iterating through all directory entries in the in-use data block
                if (de->inum == 0)
                    continue;
                if (strcmp(de->name, ".")== 0)
                    continue;
                if (strcmp(de->name, "..") == 0)
                    continue;

                inode_count[de->inum]++;
                inode = ((struct dinode *)(image->base_address + 2 * BLOCK_SIZE)) + de->inum;
                DFS(image, inode, inode_count);
            }
        }
    }

    


}

void check_rule_9_10_11_12(file_image *image){
    int i;
    int indode_count[image->num_inodes];
    for (i = 0; i < image->num_inodes; i++) {
        if (i > 1)
            indode_count[i] = 0;
        else 
            indode_count[i] = 1;
    }

    struct dinode *inode = (struct dinode *) (image->base_address + (2 * BLOCK_SIZE));
    //first_inode is always empty
    inode++ ; // now we are at root inode

    struct dinode *root_inode = inode;

    DFS(image, root_inode, indode_count);

    inode++; // Now we are at inode number 2

    for(i = 2; i < image->num_inodes; i++, inode++){ // iterating through all inodes starting from inode 2
        if (inode->type != 0 && indode_count[i] == 0) {
            fprintf(stderr, "ERROR: inode marked use but not found in directory.\n %d", i);
            exit(1);
        }

        if (inode->type == 0 && indode_count[i] > 0) {
            fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
            exit(1);
        }

        if (inode->type == 2 && inode->nlink != indode_count[i]) {
            fprintf(stderr, "ERROR: bad reference count for file.\n");
            exit(1);
        }

        if (inode->type == 1 && indode_count[i] > 1) {
            fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
            exit(1);
        }
    }   
}

int
main(int argc, char *argv[])
{
    int i;

    if(argc < 2){
        fprintf(stderr, "Usage: fcheck <file_system_image>\n");
        exit(1);
    }

    int fsfd;

    fsfd = open(argv[1], O_RDONLY);
    if(fsfd < 0){
        fprintf(stderr, "image not found\n");
        exit(1);
    }

    struct stat st;
    if (fstat(fsfd, &st) != 0){
        fprintf(stderr, "File size could not be found\n");
        exit(1);
    }

    file_image image;

    image.base_address = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (image.base_address == MAP_FAILED){
        perror("mmap failed\n");
        exit(1);
    }

    // Super Block details
    image.sb = (struct superblock *) (image.base_address + 1 * BLOCK_SIZE); 
    image.num_total_blocks = image.sb->size;
    image.num_data_blocks = image.sb->nblocks;
    image.num_inodes = image.sb->ninodes;
    image.num_blocks_for_inodes = image.num_inodes / IPB + 1;
    //printf("Size of file system in blocks %d , Number of data blocks %d, Number of inodes %d\n", image.num_total_blocks, image.num_data_blocks, image.num_inodes);

    //First Inode address
    image.first_inode_address = (char *)(image.base_address + IBLOCK((uint)0)*BLOCK_SIZE); 
    //printf("num of blocks for inode = %d \n", image.num_blocks_for_inodes);

    //First Bitmap address 
    image.first_bitmap_address = (char *)(image.base_address + (1 + 1 + image.num_blocks_for_inodes) * BLOCK_SIZE);
    image.num_bitmap_blocks = image.num_total_blocks / (BLOCK_SIZE * 8) + 1;
    //printf("num of blocks for bitmap = %d \n", image.num_bitmap_blocks);
    

    //First Data Block Address
    image.first_data_block_logical_address = 1 + 1 + image.num_blocks_for_inodes + image.num_bitmap_blocks;
    //printf("first data block logical address = %d \n", image.first_data_block_logical_address);
    image.first_data_block_address = (char *) (image.base_address + image.first_data_block_logical_address * BLOCK_SIZE);

    struct dinode *inode = (struct dinode *) image.first_inode_address;
    
    for ( i = 0; i < image.num_inodes; i++, inode++) {
        
        check_rule_1(inode);

        if (inode->type == 0)
            continue;

        // All inodes below this are in-use
        
        check_rule_2(&image, inode); // check Rule 2 : valid direct address and indirect address
        
        if (i == 1)// checking for Rule 3 : Root directory
            check_rule_3(&image, inode);
        

        if (inode->type == 1)// check Rule 4 : Each Directory has . and ..
            check_rule_4(&image, inode, i);

        check_rule_5(&image, inode); // Check Rule 5 : in-use inode should be marked as used in Bitmap
        

    }

    check_rule_6_7_8(&image); // Check Rule 6 : marked Bitmap must be in-use in inode 
                              // Check Rule 7 - 8 : Direct and Indirect Address used only once

    check_rule_9_10_11_12(&image); // Check Rules 9-12

    exit(0);
    
}

