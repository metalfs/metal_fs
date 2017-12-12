#include <stddef.h>
#include <string.h>

#include "metal.h"
#include "meta.h"

#include "inode.h"

#define INODES_DB_NAME "inodes"

MDB_dbi inodes_db = 0;

int mtl_ensure_inodes_db_open(MDB_txn *txn) {

    if (inodes_db == 0)
        mdb_dbi_open(txn, INODES_DB_NAME, MDB_CREATE, &inodes_db);

    return MTL_SUCCESS;
}

int mtl_load_inode(MDB_txn *txn, uint64_t inode_id, mtl_inode **inode, void **data, uint64_t *data_length) {

    mtl_ensure_inodes_db_open(txn);

    MDB_val inode_key = { .mv_size = sizeof(inode_id), .mv_data = &inode_id };
    MDB_val inode_value;
    int ret = mdb_get(txn, inodes_db, &inode_key, &inode_value);

    if (ret == MDB_NOTFOUND) {
        return MTL_ERROR_NOENTRY;
    }

    *inode = (mtl_inode*) inode_value.mv_data;
    if (data != NULL)
        *data = inode_value.mv_data + sizeof(mtl_inode);
    if (data_length != NULL)
        *data_length = inode_value.mv_size - sizeof(mtl_inode);

    return MTL_SUCCESS;
}

mtl_directory_entry_head* mtl_load_next_directory_entry(void *dir_data, uint64_t dir_data_length, mtl_directory_entry_head* entry, char **filename) {

    mtl_directory_entry_head *result;

    if (entry == NULL) {
        result = (mtl_directory_entry_head*) dir_data;
    } else {
        result = (mtl_directory_entry_head*) ((void*) entry + sizeof(mtl_directory_entry_head) + entry->name_len);
    }

    if ((void*) result + sizeof(mtl_directory_entry_head) > dir_data + dir_data_length) {
        return NULL;
    }

    *filename = (char*) ((void*) result + sizeof(mtl_directory_entry_head));

    return result;
}

int mtl_load_directory(MDB_txn *txn, uint64_t inode_id, mtl_inode **inode, mtl_directory_entry_head **dir_entries, uint64_t *entries_length) {

    return mtl_load_inode(txn, inode_id, inode, dir_entries, &entries_length);
}

int mtl_load_file(MDB_txn *txn, uint64_t inode_id, mtl_inode **inode, mtl_file_extent **extents, uint64_t *extents_length) {

    uint64_t data_length;
    int res = mtl_load_inode(txn, inode_id, inode, extents, &data_length);
    if (extents_length != NULL)
        *extents_length = data_length / sizeof(mtl_file_extent);

    return res;
}

int mtl_resolve_inode_in_directory(MDB_txn *txn, uint64_t dir_inode_id, char* filename, uint64_t *file_inode_id) {

    // TODO: Check if len(filename) < FILENAME_MAX = 2^8 = 256
    uint64_t filename_length = strlen(filename);

    mtl_inode *dir_inode;
    void *dir_data;
    mtl_load_inode(txn, dir_inode_id, &dir_inode, &dir_data, NULL);

    if (dir_inode->type != MTL_DIRECTORY) {
        return MTL_ERROR_NOTDIRECTORY;
    }

    mtl_directory_entry_head *current_entry = NULL;
    char *current_filename = NULL;
    while ((current_entry = mtl_load_next_directory_entry(dir_data, dir_inode->length, current_entry, &current_filename)) != NULL) {
        // File names are not null-terminated, so we *have* to use strncmp
        if (filename_length != current_entry->name_len)
            continue;

        if (strncmp(current_filename, filename, current_entry->name_len) == 0) {
            // This is the entry we're looking for!
            *file_inode_id = current_entry->inode_id;
            return MDB_SUCCESS;
        }
    }

    return MTL_ERROR_NOENTRY;
}

int mtl_put_inode(MDB_txn *txn, uint64_t inode_id, mtl_inode *inode, void* data, uint64_t data_length) {

    mtl_ensure_inodes_db_open(txn);

    uint64_t inode_data_length = sizeof(*inode) + data_length;
    char inode_data [inode_data_length];

    memcpy(inode_data, inode, sizeof(*inode));
    if (data_length)
        memcpy(inode_data + sizeof(*inode), data, data_length);

    MDB_val inode_key = { .mv_size = sizeof(inode_id), .mv_data = &inode_id };
    MDB_val inode_value = { .mv_size = inode_data_length, .mv_data = inode_data };
    mdb_put(txn, inodes_db, &inode_key, &inode_value, 0);

    return MTL_SUCCESS;
}

int mtl_add_extent_to_file(MDB_txn *txn, uint64_t inode_id, mtl_inode *inode, mtl_file_extent *extents, uint64_t extents_length, mtl_file_extent *new_extent) {

    inode->length += new_extent->length;

    mtl_file_extent extent_data [extents_length+1];
    memcpy(extent_data, extents, extents_length * sizeof(mtl_file_extent));
    memcpy(extent_data + extents_length, new_extent, sizeof(mtl_file_extent));
    return mtl_put_inode(txn, inode_id, inode, extent_data, sizeof(extent_data));
}

int mtl_append_inode_id_to_directory(MDB_txn *txn, uint64_t dir_inode_id, char *filename, uint64_t inode_id) {

    // TODO: Check if len(filename) < FILENAME_MAX = 2^8 = 256
    uint64_t filename_length = strlen(filename);

    mtl_inode *dir_inode;
    void *dir_inode_data;
    mtl_load_inode(txn, dir_inode_id, &dir_inode, &dir_inode_data, NULL);

    if (dir_inode->type != MTL_DIRECTORY) {
        return MTL_ERROR_NOTDIRECTORY;
    }

    // Check if the entry already exists
    mtl_directory_entry_head *current_entry = NULL;
    char *current_filename = NULL;
    while ((current_entry = mtl_load_next_directory_entry(dir_inode_data, dir_inode->length, current_entry, &current_filename)) != NULL) {
        if (filename_length != current_entry->name_len)
            continue;

        if (strncmp(current_filename, filename, current_entry->name_len) == 0) {
            return MTL_ERROR_EXISTS;
        }
    }

    uint64_t new_data_length = dir_inode->length +
                               sizeof(mtl_directory_entry_head) + filename_length;

    mtl_inode new_dir_inode = *dir_inode;
    new_dir_inode.length = new_data_length;

    char new_dir_inode_data[new_data_length];

    // Copy existing data and append the new entry to the end of the data section
    memcpy(new_dir_inode_data, dir_inode_data, dir_inode->length);

    mtl_directory_entry_head *new_entry_head = (mtl_directory_entry_head*) (new_dir_inode_data + dir_inode->length);
    new_entry_head->inode_id = inode_id;
    new_entry_head->name_len = strlen(filename);

    memcpy(new_dir_inode_data + dir_inode->length + sizeof(mtl_directory_entry_head), filename, filename_length);

    return mtl_put_inode(txn, dir_inode_id, &new_dir_inode, new_dir_inode_data, new_data_length);
}

int mtl_create_root_directory(MDB_txn *txn) {

    // Create a root inode
    mtl_inode root_inode = {
        .type = MTL_DIRECTORY,
        .length = 0, //sizeof(dir_data),
        .user = 0,
        .group = 0,
        .accessed = 0,
        .modified = 0,
        .created = 0
    };

    mtl_put_inode(txn, 0, &root_inode, NULL, 0);
    mtl_append_inode_id_to_directory(txn, 0, ".", 0);
    mtl_append_inode_id_to_directory(txn, 0, "..", 0);

    return MTL_SUCCESS;
}

int mtl_create_directory_in_directory(MDB_txn *txn, uint64_t dir_inode_id, char *filename, uint64_t *directory_file_inode_id) {

    int res;

    *directory_file_inode_id = mtl_next_inode_id(txn);
    res = mtl_append_inode_id_to_directory(txn, dir_inode_id, filename, *directory_file_inode_id);
    if (res != MTL_SUCCESS) {
        return res;
    }

    mtl_inode dir_inode = {
        .type = MTL_DIRECTORY,
        .length = 0, // sizeof(dir_data),
        .user = 0,
        .group = 0,
        .accessed = 0,
        .modified = 0,
        .created = 0
    };

    mtl_put_inode(txn, *directory_file_inode_id, &dir_inode, NULL, 0);
    mtl_append_inode_id_to_directory(txn, *directory_file_inode_id, ".", *directory_file_inode_id);
    mtl_append_inode_id_to_directory(txn, *directory_file_inode_id, "..", dir_inode_id);
}

int mtl_create_file_in_directory(MDB_txn *txn, uint64_t dir_inode_id, char *filename, uint64_t *file_inode_id) {

    int res;

    *file_inode_id = mtl_next_inode_id(txn);
    res = mtl_append_inode_id_to_directory(txn, dir_inode_id, filename, *file_inode_id);
    if (res != MTL_SUCCESS) {
        return res;
    }

    mtl_inode file_inode = {
        .type = MTL_FILE,
        .length = 0,
        .user = 0,
        .group = 0,
        .accessed = 0,
        .modified = 0,
        .created = 0
    };

    return mtl_put_inode(txn, *file_inode_id, &file_inode, NULL, 0);
}

int mtl_reset_inodes_db() {
    inodes_db = 0;
    return MTL_SUCCESS;
}
