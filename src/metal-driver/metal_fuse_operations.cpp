#include "metal_fuse_operations.hpp"

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <spdlog/spdlog.h>
#include <cxxopts.hpp>
#include <thread>

#include <metal-filesystem/inode.h>
#include <metal-filesystem/metal.h>
#include <metal-pipeline/data_source.hpp>
#include <metal-pipeline/snap_action.hpp>

#include "pseudo_operators.hpp"
#include "server.hpp"

namespace metal {

uint8_t PlaceholderBinary[] = {
  #include "metal-driver-placeholder.hex"
};

int fuse_chown(const char *path, uid_t uid, gid_t gid) {
  spdlog::trace("fuse_chown {}", path);
  auto &c = Context::instance();
  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) != 0) {
    spdlog::warn("FUSE: function fuse_chown not implemented");
    return -ENOSYS;
  }

  return mtl_chown(path + c.files_prefix().size(), uid, gid);
}

int fuse_getattr(const char *path, struct stat *stbuf) {
  spdlog::trace("fuse_getattr {}", path);
  memset(stbuf, 0, sizeof(struct stat));

  int res;

  auto &c = Context::instance();

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

    (void)stbuf->st_dev;   // device ID? can we put something meaningful here?
    (void)stbuf->st_ino;   // TODO: inode-ID
    (void)stbuf->st_mode;  // set according to filetype below
    (void)
        stbuf->st_nlink;  // number of hard links to file. since we don't
                          // support hardlinks as of now, this will always be 0.
    stbuf->st_uid = inode.user;   // user-ID of owner
    stbuf->st_gid = inode.group;  // group-ID of owner
    (void)stbuf->st_rdev;  // unused, since this field is meant for special
                           // files which we do not have in our FS
    stbuf->st_size = inode.length;  // length of referenced file in byte
    (void)stbuf->st_blksize;        // our blocksize. 4k?
    (void)stbuf
        ->st_blocks;  // number of 512B blocks belonging to file. TODO: needs to
                      // be set in inode whenever we write an extent
    stbuf->st_atime = inode.accessed;  // time of last read or write
    stbuf->st_mtime = inode.modified;  // time of last write
    stbuf->st_ctime =
        inode.created;  // time of last change to either content or inode-data

    stbuf->st_mode = inode.type == MTL_FILE ? S_IFREG | 0755 : S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  if (strcmp(path, c.socket_name().c_str()) == 0) {
    stbuf->st_mode = S_IFLNK | 0777;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(c.socket_filename().c_str());
    return 0;
  }

  for (const auto &op : c.operators()) {
    auto operator_filename = c.operators_prefix() + op;
    if (strcmp(path, operator_filename.c_str()) != 0) {
      continue;
    }

    stbuf->st_mode = S_IFREG | 0004;
    stbuf->st_nlink = 1;
    stbuf->st_uid = 0;
    stbuf->st_gid = 0;
    stbuf->st_size = sizeof(PlaceholderBinary);

    return 0;
  }

  return -ENOENT;
}

int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
  (void)offset;
  (void)fi;

  spdlog::trace("fuse_readdir {}", path);

  auto &c = Context::instance();

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

    for (const auto &op : c.operators()) {
      filler(buf, op.c_str(), NULL, 0);
    }

    return 0;
  }

  if ((strlen(path) == 6 &&
       strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) ||
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
    while ((readdir_status =
                mtl_readdir(dir, current_filename, sizeof(current_filename))) !=
           MTL_COMPLETE) {
      filler(buf, current_filename, NULL, 0);
    }
  }

  return 0;
}

int fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  spdlog::trace("fuse_create {}", path);
  int res;
  (void)mode;  // TODO

  auto &c = Context::instance();

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    uint64_t inode_id;
    res = mtl_create(path + 6, &inode_id);
    if (res != MTL_SUCCESS) return -res;

    fi->fh = inode_id;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_create not implemented");
  return -ENOSYS;
}

int fuse_readlink(const char *path, char *buf, size_t size) {
  spdlog::trace("fuse_readlink {}", path);
  auto &c = Context::instance();
  if (strncmp(path, "/", 1) == 0) {  // probably nonsense
    if (strcmp(path + 1, c.socket_alias().c_str()) == 0) {
      strncpy(buf, c.socket_filename().c_str(), size);
      return 0;
    }
  }

  return -ENOENT;
}

int fuse_open(const char *path, struct fuse_file_info *fi) {
  spdlog::trace("fuse_open {}", path);

  int res;

  auto &c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);

    if (res != MTL_SUCCESS) return -res;

    fi->fh = inode_id;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_open not implemented");
  return -ENOSYS;
}

int fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  spdlog::trace("fuse_read {}", path);

  auto &c = Context::instance();

  if (fi->fh != 0) {
    return static_cast<int>(mtl_read(c.storage(), fi->fh, buf, size, offset));
  }

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    int res;

    // TODO: It would be nice if this would work within a single transaction
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);
    if (res != MTL_SUCCESS) return -res;

    return static_cast<int>(mtl_read(c.storage(), inode_id, buf, size, offset));
  }

  if (strncmp(path, c.operators_dir().c_str(), c.operators_dir().size()) == 0) {
    auto basename = std::string(path + c.operators_prefix().size());
    for (const auto &op : c.operators()) {
      if (basename != op) {
        continue;
      }

      memcpy(buf, PlaceholderBinary + offset, size);

      return 0;
    }
  }

  spdlog::warn("FUSE: function fuse_read not implemented");
  return -ENOSYS;
}

int fuse_release(const char *path, struct fuse_file_info *fi) {
  spdlog::trace("fuse_release {}", path);
  (void)path;
  (void)fi;
  return 0;
}

int fuse_truncate(const char *path, off_t size) {
  spdlog::trace("fuse_truncate {}", path);
  // struct fuse_file_info* is not available in truncate :/
  int res;

  auto &c = Context::instance();

  if (strncmp(path, c.files_dir().c_str(), c.files_dir().size()) == 0) {
    uint64_t inode_id;
    res = mtl_open(path + 6, &inode_id);

    if (res != MTL_SUCCESS) return -res;

    res = mtl_truncate(inode_id, size);

    if (res != MTL_SUCCESS) return -res;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_truncate not implemented");
  return -ENOSYS;
}

int fuse_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {
  (void)path;

  spdlog::trace("fuse_write {}", path);

  auto &c = Context::instance();

  if (fi->fh != 0) {
    mtl_write(c.storage(), fi->fh, buf, size, offset);

    // TODO: Return the actual length that was written (to be returned from
    // mtl_write)
    return size;
  }

  spdlog::warn("FUSE: function fuse_write not implemented");
  return -ENOSYS;
}

int fuse_unlink(const char *path) {
  spdlog::trace("fuse_unlink {}", path);

  int res;

  auto &c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    res = mtl_unlink(path + 6);

    if (res != MTL_SUCCESS) return -res;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_unlink not implemented");
  return -ENOSYS;
}

int fuse_mkdir(const char *path, mode_t mode) {
  int res;
  (void)mode;  // TODO

  spdlog::trace("fuse_mkdir {}", path);

  auto &c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    res = mtl_mkdir(path + 6);

    if (res != MTL_SUCCESS) return -res;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_mkdir not implemented");
  return -ENOSYS;
}

int fuse_rmdir(const char *path) {
  spdlog::trace("fuse_rmdir {}", path);
  int res;

  auto &c = Context::instance();

  if (strncmp(path, c.files_prefix().c_str(), c.files_prefix().size()) == 0) {
    res = mtl_rmdir(path + 6);

    if (res != MTL_SUCCESS) return -res;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_rmdir not implemented");
  return -ENOSYS;
}

int fuse_rename(const char *from_path, const char *to_path) {
  spdlog::trace("fuse_rename {} {}", from_path, to_path);
  int res;

  auto &c = Context::instance();

  if (strncmp(from_path, c.files_prefix().c_str(), c.files_prefix().size()) ==
          0 &&
      strncmp(to_path, c.files_prefix().c_str(), c.files_prefix().size()) ==
          0) {
    res = mtl_rename(from_path + 6, to_path + 6);

    if (res != MTL_SUCCESS) return -res;

    return 0;
  }

  spdlog::warn("FUSE: function fuse_rename not implemented");
  return -ENOSYS;
}

Context &Context::instance() {
  static Context _instance;
  return _instance;
}

void Context::initialize(bool in_memory, std::string metadata_dir, int card) {
  _card = card;

  if (!in_memory) {
    SnapAction action(card);
    _registry =
        std::make_shared<OperatorFactory>(OperatorFactory::fromFPGA(action));
  } else {
    _registry = std::make_shared<OperatorFactory>(
        OperatorFactory::fromManifestString("{\"operators\": {}}"));
  }

  for (const auto &op : _registry->operatorSpecifications()) {
    _operators.emplace(op.first);
  }
  _operators.emplace(DatagenOperator::id());

  _files_dirname = "files";
  _operators_dirname = "operators";

  // Set a file name for the server socket
  _socket_alias = ".hello";

  char socket_filename[255];
  char socket_dir[] = "/tmp/metal-socket-XXXXXX";
  if (mkdtemp(socket_dir) == nullptr) {
    throw std::runtime_error("Could not create temporary directory.");
  }
  auto socket_file = std::string(socket_dir) + "/metal.sock";
  strncpy(socket_filename, socket_file.c_str(), 255);
  _socket_filename = std::string(socket_filename);

  DIR *dir = opendir(metadata_dir.c_str());
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

}  // namespace metal
