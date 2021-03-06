#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <metal-filesystem/metal.h>
#include "meta.h"

#include <metal-filesystem/heap.h>

#define HEAP_DB_NAME "heap"

mtl_heap_node_id INVALID_NODE = {0, 0};

int mtl_ensure_heap_db_open(MDB_txn *txn, MDB_dbi *heap_db) {
  return mdb_dbi_open(txn, HEAP_DB_NAME, MDB_CREATE, heap_db);
}

int mtl_heap_insert(MDB_txn *txn, uint64_t key, uint64_t value,
                    mtl_heap_node_id *node_id) {
  MDB_dbi heap_db;
  mtl_ensure_heap_db_open(txn, &heap_db);

  mtl_heap_node_id id = {.k = key, .v = value};

  MDB_val k = {.mv_size = sizeof(*node_id), .mv_data = &id};
  MDB_val v = {.mv_size = sizeof(uint64_t), .mv_data = &value};
  mdb_put(txn, heap_db, &k, &v, 0);

  if (node_id) *node_id = id;

  return MTL_SUCCESS;
}

int mtl_heap_extract_max(MDB_txn *txn, uint64_t *max_value) {
  MDB_dbi heap_db;
  mtl_ensure_heap_db_open(txn, &heap_db);

  MDB_cursor *cursor;
  mdb_cursor_open(txn, heap_db, &cursor);

  MDB_val key;
  MDB_val value;
  int res = mdb_cursor_get(cursor, &key, &value, MDB_LAST);

  mdb_cursor_close(cursor);

  if (res == MDB_NOTFOUND) return MTL_ERROR_NOENTRY;

  *max_value = *(uint64_t *)value.mv_data;

  return mtl_heap_delete(txn, *(mtl_heap_node_id *)key.mv_data);
}

int mtl_heap_delete(MDB_txn *txn, mtl_heap_node_id node_id) {
  MDB_dbi heap_db;
  mtl_ensure_heap_db_open(txn, &heap_db);

  MDB_val key = {.mv_size = sizeof(node_id), .mv_data = &node_id};
  if (mdb_del(txn, heap_db, &key, NULL) == MDB_NOTFOUND)
    return MTL_ERROR_INVALID_ARGUMENT;

  return MTL_SUCCESS;
}

int mtl_heap_increase_key(MDB_txn *txn, mtl_heap_node_id node_id, uint64_t key,
                          mtl_heap_node_id *updated_node_id) {
  int res;

  res = mtl_heap_delete(txn, node_id);
  if (res != MTL_SUCCESS) {
    return res;
  }

  return mtl_heap_insert(txn, key, 0, updated_node_id);
}
