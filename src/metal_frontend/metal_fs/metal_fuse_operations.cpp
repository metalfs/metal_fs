#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <dirent.h>

#include <pthread.h>

extern "C" {
#include "../../metal_filesystem/metal.h"
#include "../../metal_filesystem/inode.h"
};

#include <metal_filesystem_pipeline/metal_pipeline_storage.hpp>
#include <thread>
#include "server.hpp"
#include "../../../third_party/cxxopts/include/cxxopts.hpp"
#include "metal_fuse_operations.hpp"

namespace metal {

//static char agent_filepath[255];
//
//static const std::string socket_alias = ".hello";
//static char socket_filename[255];
//const std::string socket_name = "/" + socket_alias;
//
//static mtl_storage_backend storage;
//
//const std::string files_dirname = "files";
//const std::string files_dir = "/" + files_dirname;
//const std::string files_prefix = files_dir + "/";
//
//const std::string operators_dirname = "operators";
//const std::string operators_dir = "/" + operators_dirname;
//const std::string operators_prefix = operators_dir + "/";
//
//std::unique_ptr<metal::OperatorRegistry> _registry;

int fuse_chown(const char *path, uid_t uid, gid_t gid) {
  auto & c = Context::instance();
  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) != 0)
    return -ENOSYS;

  return mtl_chown(path + c.files_prefix().size(), uid, gid);
}

int fuse_getattr(const char *path, struct stat *stbuf) {
  memset(stbuf, 0, sizeof(struct stat));

  int res;

  auto & c = Context::instance();

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  if (strcmp(path, c.operators_dir().c_str()) == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  if (strcmp(path, c.files_dir().c_str()) == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    mtl_inode inode;

    res = mtl_get_inode(path + 6, &inode);
    if (res != MTL_SUCCESS) {
      return -res;
    }

    (void) stbuf->st_dev; // device ID? can we put something meaningful here?
    (void) stbuf->st_ino; // TODO: inode-ID
    (void) stbuf->st_mode; // set according to filetype below
    (void) stbuf->st_nlink; // number of hard links to file. since we don't support hardlinks as of now, this will always be 0.
    stbuf->st_uid = inode.user; // user-ID of owner
    stbuf->st_gid = inode.group; // group-ID of owner
    (void) stbuf->st_rdev; // unused, since this field is meant for special files which we do not have in our FS
    stbuf->st_size = inode.length; // length of referenced file in byte
    (void) stbuf->st_blksize; // our blocksize. 4k?
    (void) stbuf->st_blocks; // number of 512B blocks belonging to file. TODO: needs to be set in inode whenever we write an extent
    stbuf->st_atime = inode.accessed; // time of last read or write
    stbuf->st_mtime = inode.modified; // time of last write
    stbuf->st_ctime = inode.created; // time of last change to either content or inode-data

    stbuf->st_mode =
            inode.type == MTL_FILE
            ? S_IFREG | 0755
            : S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  if (strcmp(path, c.socket_name().c_str()) == 0) {
    stbuf->st_mode = S_IFLNK | 0777;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(c.socket_filename().c_str());
    return 0;
  }

  for (const auto &op : c.registry().operators()) {
    auto operator_filename = c.operators_prefix() + op.first;
    if (strcmp(path, operator_filename.c_str()) != 0) {
      continue;
    }

    res = lstat(c.agent_filepath().c_str(), stbuf);
    if (res == -1)
      return -errno;

    return 0;
  }

  return -ENOENT;
}

int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
  (void) offset;
  (void) fi;

  auto & c = Context::instance();

  if (strcmp(path, "/") == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, c.socket_alias().c_str(), NULL, 0);
    filler(buf, c.operators_dirname().c_str(), NULL, 0);
    filler(buf, c.files_dirname().c_str(), NULL, 0);
    return 0;
  }

  if (strcmp(path, c.operators_dir().c_str()) == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (const auto &op : c.registry().operators()) {
      filler(buf, op.first.c_str(), NULL, 0);
    }

    return 0;
  }

  if ((strlen(path) == 6 && strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) ||
      strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {

    mtl_dir *dir;

    // +6, because path = "/files/<filename>", but we only want "/filename"
    int res;
    if (strlen(path + 6) == 0) {
      res = mtl_opendir("/", &dir);
    } else {
      res = mtl_opendir(path + 6, &dir);
    }

    if (res != MTL_SUCCESS) {
      return -res;
    }

    char current_filename[FILENAME_MAX];
    int readdir_status;
    while ((readdir_status = mtl_readdir(dir, current_filename, sizeof(current_filename))) != MTL_COMPLETE) {
      filler(buf, current_filename, NULL, 0);
    }
  }

  return 0;
}

int fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  int res;
  (void) mode; // TODO

  auto & c = Context::instance();

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    uint64_t inode_id;
    res = mtl_create(path + 6, &inode_id);
    if (res != MTL_SUCCESS)
      return -res;

    fi->fh = inode_id;

    return 0;
  }

  return -ENOSYS;
}

int fuse_readlink(const char *path, char *buf, size_t size) {
  auto & c = Context::instance();
  if (strncmp(path, "/", 1) == 0) {  // probably nonsense
    if (strcmp(path + 1, c.socket_alias().c_str()) == 0) {
      strncpy(buf, c.socket_filename().c_str(), size);
      return 0;
    }
  }

  return -ENOENT;
}

int fuse_open(const char *path, struct fuse_file_info *fi) {

  int res;

  auto & c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);

    if (res != MTL_SUCCESS)
      return -res;

    fi->fh = inode_id;

    return 0;
  }

  return -ENOSYS;
}

int fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {

  auto & c = Context::instance();

  if (fi->fh != 0) {
    return static_cast<int>(mtl_read(c.storage(), fi->fh, buf, size, offset));
  }

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    int res;

    // TODO: It would be nice if this would work within a single transaction
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);
    if (res != MTL_SUCCESS)
      return -res;

    return static_cast<int>(mtl_read(c.storage(), inode_id, buf, size, offset));
  }

  for (const auto &op : c.registry().operators()) {
    if (strcmp(path, op.first.c_str()) != 0) {
      continue;
    }

    int fd;
    int res;

    // if (fi == NULL || true)
    fd = open(c.agent_filepath().c_str(), O_RDONLY);
    // else
    //     fd = fi->fh;

    if (fd == -1)
      return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
      res = -errno;

    if (fi == NULL || true)
      close(fd);
    return res;
  }

  return -ENOSYS;
}

int fuse_release(const char *path, struct fuse_file_info *fi) {
  (void) path;
  (void) fi;
  return 0;
}

int fuse_truncate(const char *path, off_t size) {
  // struct fuse_file_info* is not available in truncate :/
  int res;

  auto & c = Context::instance();

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);

    if (res != MTL_SUCCESS)
      return -res;

    res = mtl_truncate(inode_id, size);

    if (res != MTL_SUCCESS)
      return -res;

    return 0;
  }

  return -ENOSYS;
}

int fuse_write(const char *path, const char *buf, size_t size,
               off_t offset, struct fuse_file_info *fi) {
  (void) path;

  auto & c = Context::instance();

  if (fi->fh != 0) {
    mtl_write(c.storage(), fi->fh, buf, size, offset);

    // TODO: Return the actual length that was written (to be returned from mtl_write)
    return size;
  }

  return -ENOSYS;
}

int fuse_unlink(const char *path) {

  int res;

  auto & c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {

    res = mtl_unlink(path + 6);

    if (res != MTL_SUCCESS)
      return -res;

    return 0;
  }

  return -ENOSYS;
}

int fuse_mkdir(const char *path, mode_t mode) {
  int res;
  (void) mode; // TODO

  auto & c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {

    res = mtl_mkdir(path + 6);

    if (res != MTL_SUCCESS)
      return -res;

    return 0;
  }

  return -ENOSYS;
}


int fuse_rmdir(const char *path) {
  int res;

  auto & c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {

    res = mtl_rmdir(path + 6);

    if (res != MTL_SUCCESS)
      return -res;

    return 0;
  }

  return -ENOSYS;
}

int fuse_rename(const char *from_path, const char *to_path) {
  int res;

  auto & c = Context::instance();

  if (strncmp(from_path, c.files_prefix().c_str(), c.files_prefix().size()) == 0 &&
      strncmp(to_path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {

    res = mtl_rename(from_path + 6, to_path + 6);

    if (res != MTL_SUCCESS)
      return -res;

    return 0;
  }

  return -ENOSYS;
}

Context &Context::instance() {
  static Context _instance;
  return _instance;
}

void Context::initialize(std::string operators, bool in_memory, std::string bin_path, std::string metadata_dir, int card) {
  _card = card;
  _registry = std::make_shared<metal::OperatorRegistry>(operators);

  _files_dirname = "files";
  _operators_dirname = "operators";

  // Set a file name for the server socket
  _socket_alias = ".hello";

  char socket_filename[255];
  char socket_dir[] = "/tmp/metal-socket-XXXXXX";
  mkdtemp(socket_dir);
  auto socket_file = std::string(socket_dir) + "/metal.sock";
  strncpy(socket_filename, socket_file.c_str(), 255);
  _socket_filename = std::string(socket_filename);

  // Determine the path to the operator_agent executable
  char agent_filepath[255];
  strncpy(agent_filepath, bin_path.c_str(), sizeof(agent_filepath));
  dirname(agent_filepath);
  strncat(agent_filepath, "/operator_agent", sizeof(agent_filepath) - 1);
  _agent_filepath = std::string(agent_filepath);

  DIR* dir = opendir(metadata_dir.c_str());
  if (dir) {
    closedir(dir);
  } else if (ENOENT == errno) {
    mkdir(metadata_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  if (in_memory) {
    _storage = in_memory_storage;
  } else {
    _storage = metal::PipelineStorage::backend();
  }

  if (mtl_initialize(metadata_dir.c_str(), &_storage) != MTL_SUCCESS)
    throw std::runtime_error("Could not initialize Metal FS");
}

}