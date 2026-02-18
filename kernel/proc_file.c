/*
 * Interface functions between file system and kernel/processes. added @lab4_1
 */

#include "proc_file.h"

#include "hostfs.h"
#include "pmm.h"
#include "process.h"
#include "ramdev.h"
#include "rfs.h"
#include "riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "util/string.h"
#include "vmm.h"

// ***************** helpers for path resolution *****************

// 从dentry构建绝对路径
// 通过遍历parent链从根到当前目录构建完整路径
int build_absolute_path_from_dentry(struct dentry* d, char* path) {
  char temp[MAX_PATH_LEN];
  int pos = MAX_PATH_LEN - 1;
  temp[pos] = '\0';
  
  // Special case: if d is root
  if (d->parent == NULL || d->parent == d) {
    strcpy(path, "/");
    return 0;
  }
  
  // Traverse from d to root, building path backwards
  while (d->parent != NULL && d->parent != d) {
    int name_len = strlen(d->name);
    pos -= name_len;
    if (pos < 0) return -1; // path too long
    memcpy(temp + pos, d->name, name_len);
    
    pos--;
    if (pos < 0) return -1;
    temp[pos] = '/';
    
    d = d->parent;
  }
  
  // Copy the result
  strcpy(path, temp + pos);
  return 0;
}

// 处理路径中可能含有的.或者..的核心逻辑
int normalize_path(char* path) {
  char tokens[MAX_PATH_DEPTH][MAX_DENTRY_NAME_LEN];
  int tokens_count = 0;

  // collect dentry names
  char *saveptr = NULL;
  char* token = strtok_r(path, "/", &saveptr);
  while(token != NULL) {
    // sprint("[DEBUG] normalize_path: token: %s\n", token);
    if(strcmp(token, ".") == 0) {
      // skip through this situation
    } else if(strcmp(token, "..") == 0) {
      if(tokens_count > 0) {
        tokens_count--;
      } // else do nothing
    } else if(token[0] == '\0') {
      // skip throught this situation
      // 处理"//"这样的情况，虽然这种路径是错的，但这里暂时不实现异常处理
    } else {
      strcpy(tokens[tokens_count++], token);
    }
    token = strtok_r(NULL, "/", &saveptr);
  }

  // sprint("[DEBUG] normalize_path: tokens_count: %d\n", tokens_count);

  // reconstruct the path
  path[0] = '\0';
  strcat(path, "/");
  for (int i = 0; i < tokens_count; i++) {
      strcat(path, tokens[i]);
      if (i < tokens_count - 1) {
          strcat(path, "/");
      }
  }

  // boundaries? maybe
  
  return 0;
}

int resolve_path(char* path, char* res_path, struct dentry* cwd) {
  if(path[0] == '/') {
    // absolute path
    strcpy(res_path, path);
    return normalize_path(res_path);
  } else {
    // relative path: build full cwd path first, then append relative path
    char cwd_path[MAX_PATH_LEN];
    if (build_absolute_path_from_dentry(cwd, cwd_path) < 0) {
      return -1;
    }
    
    // Append relative path to cwd
    int len = strlen(cwd_path);
    if (cwd_path[len - 1] != '/') {
      cwd_path[len] = '/';
      cwd_path[len + 1] = '\0';
    }
    strcat(cwd_path, path);
    strcpy(res_path, cwd_path);
    
    // sprint("[DEBUG] resolve_path: normalized path: %s\n", res_path);
    return normalize_path(res_path);
  }
}



// **************** main proc_file functions *****************
//
// initialize file system
//
void fs_init(void) {
  // initialize the vfs
  vfs_init();

  // register hostfs and mount it as the root
  if( register_hostfs() < 0 ) panic( "fs_init: cannot register hostfs.\n" );
  struct device *hostdev = init_host_device("HOSTDEV");
  vfs_mount("HOSTDEV", MOUNT_AS_ROOT);

  // register and mount rfs
  if( register_rfs() < 0 ) panic( "fs_init: cannot register rfs.\n" );
  struct device *ramdisk0 = init_rfs_device("RAMDISK0");
  rfs_format_dev(ramdisk0);
  vfs_mount("RAMDISK0", MOUNT_DEFAULT);
}

//
// initialize a proc_file_management data structure for a process.
// return the pointer to the page containing the data structure.
//
proc_file_management *init_proc_file_management(void) {
  proc_file_management *pfiles = (proc_file_management *)alloc_page();
  pfiles->cwd = vfs_root_dentry; // by default, cwd is the root
  pfiles->nfiles = 0;

  for (int fd = 0; fd < MAX_FILES; ++fd)
    pfiles->opened_files[fd].status = FD_NONE;

  sprint("FS: created a file management struct for a process.\n");
  return pfiles;
}

//
// reclaim the open-file management data structure of a process.
// note: this function is not used as PKE does not actually reclaim a process.
//
void reclaim_proc_file_management(proc_file_management *pfiles) {
  free_page(pfiles);
  return;
}

//
// get an opened file from proc->opened_file array.
// return: the pointer to the opened file structure.
//
struct file *get_opened_file(int fd) {
  struct file *pfile = NULL;

  // browse opened file list to locate the fd
  int hid = read_tp();
  for (int i = 0; i < MAX_FILES; ++i) {
    pfile = &(current[hid]->pfiles->opened_files[i]);  // file entry
    if (i == fd) break;
  }
  if (pfile == NULL) panic("do_read: invalid fd!\n");
  return pfile;
}

//
// open a file named as "pathname" with the permission of "flags".
// return: -1 on failure; non-zero file-descriptor on success.
//
int do_open(char *pathname, int flags, struct dentry* cwd) {
  int hid = read_tp();
  char resolved_path[MAX_PATH_LEN];
  resolve_path(pathname, resolved_path, cwd);  

  struct file *opened_file = NULL;
  if ((opened_file = vfs_open(resolved_path, flags)) == NULL) return -1;
  int fd = 0;
  if (current[hid]->pfiles->nfiles >= MAX_FILES) {
    panic("do_open: no file entry for current process!\n");
  }
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current[hid]->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current[hid]->pfiles->nfiles;
  return fd;
}

//
// read content of a file ("fd") into "buf" for "count".
// return: actual length of data read from the file.
//
int do_read(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->readable == 0) panic("do_read: no readable file!\n");

  char buffer[count + 1];
  int len = vfs_read(pfile, buffer, count);
  buffer[count] = '\0';
  strcpy(buf, buffer);
  return len;
}

//
// write content ("buf") whose length is "count" to a file "fd".
// return: actual length of data written to the file.
//
int do_write(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->writable == 0) panic("do_write: cannot write file!\n");

  int len = vfs_write(pfile, buf, count);
  return len;
}

//
// reposition the file offset
//
int do_lseek(int fd, int offset, int whence) {
  struct file *pfile = get_opened_file(fd);
  return vfs_lseek(pfile, offset, whence);
}

//
// read the vinode information
//
int do_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_stat(pfile, istat);
}

//
// read the inode information on the disk
//
int do_disk_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_disk_stat(pfile, istat);
}

//
// close a file
//
int do_close(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_close(pfile);
}

//
// open a directory
// return: the fd of the directory file
//
int do_opendir(char *pathname, struct dentry* cwd) {
  int hid = read_tp();
  char resolved_path[MAX_PATH_LEN];
  resolve_path(pathname, resolved_path, cwd);

  struct file *opened_file = NULL;
  if ((opened_file = vfs_opendir(resolved_path)) == NULL) return -1;
  int fd = 0;
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current[hid]->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }
  if (pfile->status != FD_NONE)  // no free entry
    panic("do_opendir: no file entry for current process!\n");

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current[hid]->pfiles->nfiles;
  return fd;
}

//
// read a directory entry
//
int do_readdir(int fd, struct dir *dir) {
  struct file *pfile = get_opened_file(fd);
  return vfs_readdir(pfile, dir);
}

//
// make a new directory
//
int do_mkdir(char *pathname) {
  return vfs_mkdir(pathname);
}

//
// close a directory
//
int do_closedir(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_closedir(pfile);
}

//
// create hard link to a file
//
int do_link(char *oldpath, char *newpath) {
  return vfs_link(oldpath, newpath);
}

//
// remove a hard link to a file
//
int do_unlink(char *path) {
  return vfs_unlink(path);
}

int do_rcwd(char* path){
  // Build full path from root by traversing parent chain
  int hid = read_tp();
  return build_absolute_path_from_dentry(current[hid]->pfiles->cwd, path);
}

int do_ccwd(char * path, struct dentry** cwd){
  char resolved_path[MAX_PATH_LEN];
  resolve_path(path, resolved_path, *cwd);
  vfs_lock();
  struct dentry* start = vfs_root_dentry;
  char miss[MAX_PATH_LEN];
  struct dentry* d = lookup_final_dentry(resolved_path, &start, miss);
  if(!d || d->dentry_inode->type != DIR_I) {
    sprint("do_ccwd: cannot change cwd to a non-exist directory!\n");
    vfs_unlock();
    return -1;
  }
  *cwd = d;
  vfs_unlock();
  return 0;
}
