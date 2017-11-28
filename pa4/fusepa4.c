/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  Minor modifications and note by Andy Sayler (2012) <www.andysayler.com>

  Source: fuse-2.8.7.tar.gz examples directory
  http://sourceforge.net/projects/fuse/files/fuse-2.X/

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags` fusexmp.c -o fusexmp `pkg-config fuse --libs`

  Note: This implementation is largely stateless and does not maintain
        open file handels between open and release calls (fi->fh).
        Instead, files are opened and closed as necessary inside read(), write(),
        etc calls. As such, the functions that rely on maintaining file handles are
        not implmented (fgetattr(), etc). Those seeking a more efficient and
        more complete implementation may wish to add fi->fh support to minimize
        open() and close() calls and support fh dependent functions.

*/

#define FUSE_USE_VERSION 28
#define HAVE_SETXATTR

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
// defines useful limits like PATH_MAX
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

// create struct that stores log file and root dir to pass to fuse_main
struct xmp_user_data{
  char* key;
  char* root_directory; 
};
#define DATA ((struct xmp_user_data *) fuse_get_context()->private_data)

/*Pfeiffer, Joseph. Writing a FUSE Filesystem: a Tutorial. January 10th, 2011. http:
//www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/.  */
static void getpath(char file_path[PATH_MAX], const char* path){
  strcpy(file_path, DATA->root_directory);
  strncat(file_path, path, PATH_MAX);
  
}
/*Get file attrubutes*/
static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;

  char file_path[PATH_MAX];
  getpath(file_path, path);
	res = lstat(file_path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);
	res = access(file_path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Read the target of a symbolic link */
static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);
	res = readlink(file_path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

  char file_path[PATH_MAX];
  getpath(file_path, path);

	dp = opendir(file_path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

/*  Create a file node. This is called for createion of all non-directory, non-symlink nodes.  */
static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(file_path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(file_path, mode);
	else
		res = mknod(file_path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Create a directory.  */
static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path,path);
	res = mkdir(file_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Remove a file.  */
static int xmp_unlink(const char *path)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);

	res = unlink(file_path);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Remove a directory.  */
static int xmp_rmdir(const char *path)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);

	res = rmdir(file_path);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Create a symbolic link. 
 * @from: the path where the link points
 * @to: the link itself
 * purpose: to insert the link into the mounted directory 
 */
static int xmp_symlink(const char *from, const char *to)
{
	int res;
  char link[PATH_MAX];
  getpath(link, to);

	res = symlink(from, link);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Rename a file 
 * @from: old path name
 * @to: new path name
 */
static int xmp_rename(const char *from, const char *to)
{
	int res;
  char newfrom[PATH_MAX];
  char newto[PATH_MAX]; 
  getpath(newfrom, from);
  getpath(newto, to);
	res = rename(newfrom, newto);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Create a hard link to a file  */
static int xmp_link(const char *from, const char *to)
{
	int res;
  char newfrom[PATH_MAX], newto[PATH_MAX];
  getpath(newfrom, from);
  getpath(newto, to);
	res = link(newfrom, newto);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Change the permissions bits of a file  */
static int xmp_chmod(const char *path, mode_t mode)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);
 

	res = chmod(file_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Change the owner and group of a file  */
static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);
  
	res = lchown(file_path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

/* Change the size of a file */
static int xmp_truncate(const char *path, off_t size)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);

	res = truncate(file_path, size);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Change the access and modification times of a file with nanoseconf resolution  */
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
  char file_path[PATH_MAX];
  getpath(file_path, path);

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	res = utimes(file_path, tv);
	if (res == -1)
		return -errno;

	return 0;
}

/*  Open a file. Open flags are in fi->flags 
 *  Filesystem may store an arbitrary file handle in fi->fh and use this
 * in all other file operations 
*/
static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int file_handle;
  char file_path[PATH_MAX];
  getpath(file_path, path);
  file_handle = open(file_path, fi->flags);
  if (file_handle == -1)
    return -errno;
  /*  set file handle in fi struct to file_handle */
  fi->fh = file_handle;

  
  /* FILE* infile; */
  /* FILE* outfile; */
  /* char* keyphrase = DATA->key; */
  /* infile = open(file_path, fi->flags); */
  /* if(infile == -1) */
  /*   return -errno; */
  /* // c standard lib function that creates a temporary file that is automatically closed  */
  /* outfile = tmpfile(); */
  /* // call do_crypt */
  /* do_crypt(infile, outfile, 0, keyphrase); */
  /* close(infile); */

  /* res = open(outfile, fi->flags); */
  /* if (res == -1) */
  /*   return -errno; */
  

 


	return 0;
}

/*  Read data from an open file.  */
static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{


  int res;
  res = pread(fi->fh, buf, size, offset);
  if (res == -1)
    return -errno;
	/* int fd; */
	/* int res; */
  /* char file_path[PATH_MAX]; */
  /* getpath(file_path, path); */

	/* (void) fi; */
	/* fd = open(file_path, O_RDONLY); */
	/* if (fd == -1) */
	/* 	return -errno; */

	/* res = pread(fd, buf, size, offset); */
	/* if (res == -1) */
	/* 	res = -errno; */

	/* close(fd); */
	return res;
}

/*  write date to an open file.  */
static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	/* int fd; */
	/* int res; */
  /* char file_path[PATH_MAX]; */
  /* getpath(file_path, path); */

	/* (void) fi; */
	/* fd = open(file_path, O_WRONLY); */
	/* if (fd == -1) */
	/* 	return -errno; */

	/* res = pwrite(fd, buf, size, offset); */
	/* if (res == -1) */
	/* 	res = -errno; */

	/* close(fd); */

  int res;
  res = pwrite(fi->fh, buf, size, offset);
  if (res == -1)
    return -errno;
	return res;
}

/*  Get file system statistics  */
static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
  char file_path[PATH_MAX];
  getpath(file_path, path);

	res = statvfs(file_path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_create(const char* path, mode_t mode, struct fuse_file_info* fi) {

    (void) fi;
    char file_path[PATH_MAX];
    getpath(file_path, path);

    int res;
    res = creat(file_path, mode);
    if(res == -1)
	return -errno;

    close(res);

    return 0;
}


/*  need to close the file here  */
static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
  int res;
  
  res = close(fi->fh);
  if(res == -1)
    return -errno;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_SETXATTR
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
  char file_path[PATH_MAX];
  getpath(file_path, path);
	int res = lsetxattr(file_path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
  char file_path[PATH_MAX];
  getpath(file_path, path);
	int res = lgetxattr(file_path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
  char file_path[PATH_MAX];
  getpath(file_path, path);
	int res = llistxattr(file_path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
  char file_path[PATH_MAX];
  getpath(file_path, path);
	int res = lremovexattr(file_path, name);
	if (res == -1)
		return -errno;
	return 0;
}

/*  Init filesystem
 * Return Value will pass in the private_data field of fuse_context to all file ops and 
 * as a parameter to the destroy(method)
 * fuse_context is set up before this function is called
 * fuse_get_context()->private_data returns the user_data passed to fuse_main() */
static void* xmp_init(struct fuse_conn_info *conn)
{
  return DATA;
}
static int xmp_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
  int res;
  res = fstat(fi->fh, statbuf);
  if(res < 0)
    return -errno;
  return res;
    
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.utimens	= xmp_utimens,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.create         = xmp_create,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif

  .init = xmp_init,
  .fgetattr = xmp_fgetattr
};

int main(int argc, char *argv[])
{
	umask(0);

// get path from argv[] and store in struct xmp_user_data
  struct xmp_user_data *data;
  data = malloc(sizeof(struct xmp_user_data));
  data->key = argv[argc-3];
  data->root_directory = realpath(argv[argc-2], NULL);
  argv[argc-3] = argv[argc-1];
  argv[argc-2] = NULL;
  argv[argc-1] = NULL;
  argc = argc-2;
    
  
  
  
  
  
	return fuse_main(argc, argv, &xmp_oper, data);
}
