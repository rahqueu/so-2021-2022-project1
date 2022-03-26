#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    if (state_init() == -1) return -1;
    
    /* Create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                int size = BLOCK_SIZE/sizeof(int), j = 0, i = 0, *pointer = NULL;
                lock_write_datablocks();
                pthread_rwlock_wrlock(&inode->rwlock);
                while (j < size) {
                    if (i > MAX_DIRECT_BLOCKS) {
                        if (pointer[j] == -1) break;
                        else data_block_free(pointer[j]);
                        j++;
                    } else {
                        if (inode->i_data_block[i] == -1) break; // Get a direct block
                        if (i == MAX_DIRECT_BLOCKS) pointer = data_block_get(inode->i_data_block[i]); // Set pointer to the allocation block
                        else data_block_free(inode->i_data_block[i]);
                    }
                    i++;
                }
                if (i >= MAX_DIRECT_BLOCKS) data_block_free(inode->i_data_block[MAX_DIRECT_BLOCKS]);
                inode->i_size = 0;
            }
            pthread_rwlock_unlock(&inode->rwlock);
            unlock_datablocks();
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size % BLOCK_SIZE;
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}

int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) return -1;

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) return -1;

    if (to_write > 0) {
        int counter = 0, *pointer = NULL, *indexes = NULL;
        size_t size = BLOCK_SIZE/sizeof(int), index_direct = 0, index_allocated = 0;
        void *block = NULL;
        pthread_rwlock_wrlock(&inode->rwlock);
        if (inode->i_size == 0) {
            if (to_write > (size_t)BLOCK_SIZE*(size + (size_t)MAX_DIRECT_BLOCKS)) 
                to_write = (size_t)BLOCK_SIZE*(size + (size_t)MAX_DIRECT_BLOCKS);
            indexes = to_alloc(to_write);
        } else {
            lock_read_datablocks();
            while (index_allocated < size) {
                if (index_direct < MAX_DIRECT_BLOCKS) {
                    if (inode->i_data_block[index_direct] == -1) break;
                    index_direct++;
                } else if (index_direct == MAX_DIRECT_BLOCKS) {
                    pointer = data_block_get(inode->i_data_block[MAX_DIRECT_BLOCKS]);
                    index_direct++;
                } else {
                    if (pointer[index_allocated] == -1) break;
                    index_allocated++;
                }
            }
            unlock_datablocks();
            index_direct--;
            if (to_write > (size_t)BLOCK_SIZE*(size - index_allocated + 
            (size_t)MAX_DIRECT_BLOCKS - index_direct) + (size_t)BLOCK_SIZE - file->of_offset) {
                to_write = (size_t)BLOCK_SIZE*(size - index_allocated +
                (size_t)MAX_DIRECT_BLOCKS - index_direct) + (size_t)BLOCK_SIZE - file->of_offset;
            }
            size_t temp = to_write - (BLOCK_SIZE-file->of_offset);
            if (temp > to_write) temp = 0;
            // If we have to allocate more than 10 blocks, we also need to allocate the indirect block
            else if (temp >= MAX_DIRECT_BLOCKS*BLOCK_SIZE && index_allocated == 0) temp += BLOCK_SIZE;
            indexes = to_alloc(temp);
        }
        if (indexes != NULL) {
            int size_indexes = give_size(indexes);
            for (; index_direct < MAX_DIRECT_BLOCKS && counter < size_indexes; index_direct++, counter++) {
                inode->i_data_block[index_direct] = indexes[counter];
            }
            if (size_indexes != 0 && index_direct != MAX_DIRECT_BLOCKS) inode->i_data_block[index_direct] = -1;
            if (index_direct == MAX_DIRECT_BLOCKS) {
                lock_write_datablocks();
                pointer = data_block_get(inode->i_data_block[MAX_DIRECT_BLOCKS]);
                for (int z = counter; index_allocated < size && z < size_indexes; index_allocated++, z++) {
                    pointer[index_allocated] = indexes[z];
                }
                if (index_allocated != size) pointer[index_allocated] = -1;
                unlock_datablocks();
            }
            index_allocated = index_allocated > 0 ? index_allocated - 1 : index_allocated;
            index_direct = index_direct > 0 && index_direct < MAX_DIRECT_BLOCKS ? index_direct - 1 : index_direct;
        } else return -1;

        lock_write_datablocks();
        for (size_t aux = to_write; aux > 0 && aux <= to_write && index_allocated < size; aux -= BLOCK_SIZE) {
            size_t temp = aux / BLOCK_SIZE > 1 ? BLOCK_SIZE : aux;
            if (index_direct < MAX_DIRECT_BLOCKS) {
                block = data_block_get(inode->i_data_block[index_direct]); // Get a direct block
                if (block == NULL) {
                    unlock_datablocks();
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
                if (aux == to_write) {
                    block += file->of_offset;
                    temp -= file->of_offset;
                    aux += file->of_offset;
                }
                memcpy(block, buffer, temp); // Write an entire block or the last part if smaller than a block
                index_direct++; 
            } else {
                // Again apenas leitura, atÃ© chegar ao memcpy
                if (index_direct++ == MAX_DIRECT_BLOCKS) 
                    pointer = data_block_get(inode->i_data_block[MAX_DIRECT_BLOCKS]); // Set pointer to the allocation block
                block = data_block_get(pointer[index_allocated++]);
                if (block == NULL) {
                    unlock_datablocks();
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
                if (aux == to_write) {
                    block += file->of_offset;
                    temp -= file->of_offset;
                    aux += file->of_offset;
                }
                memcpy(block, buffer, temp); // Write an entire block or the last part if smaller than a block
            }
            inode->i_size += temp;
        }
        unlock_datablocks();
        pthread_rwlock_unlock(&inode->rwlock);
        file->of_offset = to_write % BLOCK_SIZE;
    }

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) to_read = len;

    if (to_read > 0) {
        int i = 0, j = 0, *pointer = NULL;
        void *block = NULL;
        pthread_rwlock_wrlock(&inode->rwlock);
        lock_write_datablocks();
        for (size_t aux = to_read; aux > 0 && aux <= to_read; aux -= BLOCK_SIZE) {
            size_t temp = aux / BLOCK_SIZE > 1 ? BLOCK_SIZE : aux;
            if (i < MAX_DIRECT_BLOCKS) {
                block = data_block_get(inode->i_data_block[i]);
                if (block == NULL)  {
                    unlock_datablocks();
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
            } else {
                if (i == MAX_DIRECT_BLOCKS) pointer = block; // Set pointer to the allocation block
                block = data_block_get(pointer[j++]);
                if (block == NULL) {
                    unlock_datablocks();
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
            }
            if (aux == to_read) {
                block += file->of_offset;
                temp -= file->of_offset;
                aux += file->of_offset;
            }
            memcpy(buffer, block, temp); // Perform the actual read
            i++;
        }
    unlock_datablocks();
    pthread_rwlock_unlock(&inode->rwlock);
    }

    return (ssize_t)to_read;
}

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {

    int sourcefhandle, file_inum;
    FILE *destination;
    inode_t *source;
    size_t to_write;
    char *array;

    // Check if both pathnames are valid and if source_path 
    if (!valid_pathname(source_path)) return -1;
    if ((file_inum = tfs_lookup(source_path)) == -1) return -1;

    if ((destination = fopen(dest_path, "w")) == NULL) return -1;

    source = inode_get(file_inum);
    if (source == NULL) return -1;
    to_write = source->i_size;

    sourcefhandle = tfs_open(source_path, 0);
    if (sourcefhandle == -1) return -1;
    array = (char *)malloc(to_write + 1);
    if (array == NULL) return -1;

    if (tfs_read(sourcefhandle, array, to_write) == -1) return -1;
    if (fwrite(array, sizeof(char), to_write, destination) != to_write) return -1;
    if (tfs_close(sourcefhandle) == -1) return -1;
    if (fclose(destination) == EOF) return -1;
    free(array);

    return 0;
}