/**
 * NLINKFS - A File System that handles symbolic links transparently
 *
 * Copyright (C)2010 Renê de Souza Pinto (rene at renesp.com.br)
 *
 * This file is part of NLINKFS, NLINKFS is a File System.
 *
 * NLINKFS is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2.
 *
 * NLINKFS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bash; see the file COPYING.  If not, write to the Free
 * Software Foundation, 59 Temple Place, Suite 330, Boston, MA 02111 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <glib-2.0/glib.h>
#include <nlinkfs.h>


#define DEBUG(fmt, ...) { FILE *fp; fp=fopen("/tmp/nlinkfs", "a+"); fprintf(fp, fmt, ##__VA_ARGS__); fclose(fp); }


/**
 * get_realpath
 *
 * Return the real path from mounted to original directory
 */
static GString *get_realpath(GString *rpath, const char *mpath)
{
	rpath = g_string_new(nlk_getdata->srcdir);
	rpath = g_string_append(rpath, mpath);
	return rpath;
}

/**
 * get_nlinkfs
 * Read nlinkfs .LNK file and return where the link points to
 */
static GString *get_nlinkfs(const char *path)
{
	GString *link;
	struct stat buf;
	char *contents;
	int fd;

	if (lstat(path, &buf) < 0) {
		return NULL;
	}

	/* There is a .LNK file, check if it's a NLINKFS file */
	contents = g_malloc(buf.st_size);
	if (contents == NULL) {
		return NULL;
	} else {
		if ((fd = open(path, O_RDONLY)) < 0) {
			g_free(contents);
			return NULL;
		} else {
			if (read(fd, contents, buf.st_size) < buf.st_size) {
				g_free(contents);
				close(fd);
				return NULL;
			}
			close(fd);
		}
	}

	if (strncmp(contents, NLINKFS_MAGIC, NLINKFS_MAGIC_SIZE) == 0) {
		link = g_string_new(&contents[NLINKFS_MAGIC_SIZE+1]);
		link->str[buf.st_size - NLINKFS_MAGIC_SIZE - 1] = '\0';
	} else {
		link = NULL;
	}

	g_free(contents);
	return link;
}

/**
 * is_nlinkfs
 *
 * Return where the symbolic points to if the path is really a symbolic
 * link. If the path isn't a symbolic link, return NULL.
 */
static GString *is_nlinkfs(const char *path)
{
	GString *rpath = NULL;
	GString *link;

	rpath = get_realpath(rpath, path);
	rpath = g_string_append(rpath, ".LNK");
	link  = get_nlinkfs(rpath->str);

	g_string_free(rpath, TRUE);
	return link;
}


/* --------- FUSE CALLBACKS IMPLEMENTATION --------- */

/**
 * mknod callback
 *
 * Call mknod()
 */
static int nlinkfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	int ret;
	GString *rpath = NULL;
	
	rpath = get_realpath(rpath, path);

    // According to bbfs (fuse-tutorial), this way is more portable 
	// than just call mknod
    if (S_ISREG(mode)) {
        ret = open(rpath->str, O_CREAT | O_EXCL | O_WRONLY, mode);
        ret = close(ret);
    } else if (S_ISFIFO(mode)) {
       	ret = mkfifo(rpath->str, mode);
    } else {
      	ret = mknod(rpath->str, mode, dev);
    }
    
	g_string_free(rpath, TRUE);
	return ret;
}

/**
 * unlink callback
 *
 * Call unlink()
 */
static int nlinkfs_unlink(const char *path)
{
	GString *rpath = NULL;
	int ret = 0;

	if ( is_nlinkfs(path) ) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		/* Remove .LNK file */
		ret = unlink(rpath->str);

		g_string_free(rpath, TRUE);
	} else {
		rpath = get_realpath(rpath, path);
		ret = unlink(rpath->str);
		g_string_free(rpath, TRUE);
	}
	
	return ret;
}

/**
 * symlink callback
 *
 * This function creates a symbolic link on the mounted directory. However, on
 * the source directory a text file with .LNK extension will be created.
 * The first line of this file contais the magic signature NLINKFS_MAGIC, the second
 * line contains the full path to where the link points.
 *
 * params:
 * 	path - Where the link points
 * 	link - The link itself
 */
static int nlinkfs_symlink(const char *path, const char *link)
{
	int fd;
	ssize_t w1, w2, w3, s1, s2, s3;
	GString *rpath = NULL;

	rpath = get_realpath(rpath, link);
	rpath = g_string_append(rpath, ".LNK");


	/* First we try to create the .LNK file */
	fd = open(rpath->str, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		g_string_free(rpath, TRUE);
		return fd;
	}
	s1 = strlen(NLINKFS_MAGIC) * sizeof(char);
	s2 = sizeof(char);
	s3 = strlen(path) * sizeof(char);
	w1 = write(fd, NLINKFS_MAGIC, s1);
	w2 = write(fd, "\n", s2);
	w3 = write(fd, path, s3);
	close(fd);
	if (w1 < s1 || w2 < s2 || w3 < s3) {
		return -EIO;
	}

	g_string_free(rpath, TRUE);
	return 0;
}

/**
 * getattr callback
 *
 * Call lstat()
 */
static int nlinkfs_getattr(const char *path, struct stat *stbuf)
{
	GString *rpath = NULL;
	GString *link;
	int ret = 0;

	rpath = get_realpath(rpath, path);

	if ( (link = is_nlinkfs(path)) ) {
		rpath = g_string_append(rpath, ".LNK");
		ret   = lstat(rpath->str, stbuf);
		
		/* We don't need to care about symbolic link permissions */
		stbuf->st_mode |= (S_IRWXU | S_IRWXG | S_IRWXO);

		/* Change mode to symbolic link */
		stbuf->st_mode |= S_IFLNK;

		/* Set file size to the length of path where it points to */
		stbuf->st_size = strlen(link->str);
		g_string_free(link, TRUE);
	} else {
		ret = lstat(rpath->str, stbuf);
	}
	
	if (ret < 0) {
		return -errno;
	} else {
		return 0;
	}
}

/**
 * readlink callback
 *
 * Should fill buf with the link pointed by path.
 */
static int nlinkfs_readlink(const char *path, char *buf, size_t size)
{
	ssize_t llen;
	GString *link;

	/* Check if the file is really a symbolic link */
	if ( (link = is_nlinkfs(path)) ) {
		llen = strlen(link->str);

		if (size < 0) {
			return -EFAULT;
		} else {
			if (llen > size) {
				strncpy(buf, link->str, size);
				buf[size] = '\0';
			} else {
				strncpy(buf, link->str, llen);
				buf[llen] = '\0';
			}
			return 0;
		}
	} else {
		return -EINVAL;
	}
}

/**
 * mkdir callback
 *
 * Call mkdir()
 */
static int nlinkfs_mkdir(const char *path, mode_t mode)
{
	int ret = 0;
	GString *rpath = NULL;
	
	rpath = get_realpath(rpath, path);
	ret = mkdir(rpath->str, mode);
	g_string_free(rpath, TRUE);
	
	return ret;
}

/**
 * rmdir callback
 *
 * Call rmdir()
 */
static int nlinkfs_rmdir(const char *path)
{
	int ret;
	GString *rpath = NULL;

	rpath = get_realpath(rpath, path);
	ret = rmdir(rpath->str);
	g_string_free(rpath, TRUE);

	return ret;
}

/**
 * opendir callback
 *
 * This function should check if open operatoin is permitted for
 * this directory
 */
static int nlinkfs_opendir(const char *path, struct fuse_file_info *fi)
{
	GString *rpath = NULL;
	DIR *dp;

	rpath = get_realpath(rpath, path);

	dp = opendir(rpath->str);
	fi->fh = (intptr_t)dp;
	g_string_free(rpath, TRUE);

	if (dp == NULL) {
		return -errno;
	} else {
		return 0;
	}
}

/**
 * closedir callback
 *
 * Call closedir()
 */
static int nlinkfs_closedir(const char *path, struct fuse_file_info *fi)
{
	return closedir((DIR*)(uintptr_t)fi->fh);
}

/**
 * readdir callback
 *
 * This function reads the contents from source directory, if a .LNK file is
 * detect, the corret name is placed on mounted directory.
 */
static int nlinkfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	size_t len;
	char islnk = 0;
	GString *rpath = NULL;
	GString *linkp;

	/* open source directory */
	rpath = get_realpath(rpath, path);
	dp = opendir(rpath->str);
	if (dp == NULL) {
		return -errno;
	}
	de = readdir(dp);
	if (de < 0) {
		closedir(dp);
		return -errno;
	}

	/* Read source directory */
	do {
		len = strlen(de->d_name);
		if (len >= 4) {
			len -= 4;
			if (strncmp(&de->d_name[len], ".LNK", 4) == 0) {
				/* We have a possible LNK file,
				 * open it to check */
				linkp = g_string_new(rpath->str);
				linkp = g_string_append(linkp, "/");
				linkp = g_string_append(linkp, de->d_name);

				if ( get_nlinkfs(linkp->str) ) {
					islnk = 1;
					de->d_name[len] = '\0';
					g_string_free(linkp, TRUE);
					filler(buf, de->d_name, NULL, 0);
				} else {
					islnk = 0;
				}
			} else {
				islnk = 0;
			}
		} else {
			islnk = 0;
		}

		/* Add file/dir */
		if (!islnk) {
			if (filler(buf, de->d_name, NULL, 0) != 0) {
				closedir(dp);
				g_string_free(rpath, TRUE);
				return -ENOMEM;
			}
		}
	} while((de = readdir(dp)) != NULL);

	g_string_free(rpath, TRUE);
	closedir(dp);
	return 0;
}

/**
 * open callback
 *
 * Call open()
 */
static int nlinkfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd;
	GString *rpath = NULL;

	rpath = get_realpath(rpath, path);
	fd = open(rpath->str, fi->flags);
	g_string_free(rpath, TRUE);

	fi->fh = fd;

	if (fd < 0) {
		return fd;
	} else {
		return 0;
	}
}

/**
 * read callback
 *
 * Call read()
 */
static int nlinkfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	return pread(fi->fh, buf, size, offset);
}

/**
 * write callback
 *
 * Call write()
 */
static int nlinkfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	return pwrite(fi->fh, buf, size, offset);
}

/**
 * close callback
 *
 * Do nothing, close file descriptor is doing by flush method
 */
static int nlinkfs_close(const char *path, struct fuse_file_info *fi)
{
	close(fi->fh);
	return 0;
}

/**
 * access callback
 *
 * Call access()
 */
static int nlinkfs_access(const char *path, int mask)
{
	int ret = 0;
	GString *rpath = NULL;
	
	rpath = get_realpath(rpath, path);
	ret = access(rpath->str, mask);
	g_string_free(rpath, TRUE);
	
	return ret;
}

/**
 * chmod callback
 *
 * Call chmod()
 */
static int nlinkfs_chmod(const char *path, mode_t mode)
{
	GString *rpath = NULL;
	int ret = 0;

	/* We don't need to check if the file is a symbolic link 
	 * because chmod never change attributes of symbolic links
	 */
	rpath = get_realpath(rpath, path);
	ret = chmod(rpath->str, mode);
	g_string_free(rpath, TRUE);
		
	return ret;
}

/**
 * chown callback
 *
 * Call chown()
 */
static int nlinkfs_chown(const char *path, uid_t uid, gid_t gid)
{
	GString *rpath = NULL;
	int ret = 0;

	/* First we check if the file is a symbolic link */
	if ( is_nlinkfs(path) ) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		/* Change owner of the .LNK file */
		ret = chown(rpath->str, uid, gid);

		g_string_free(rpath, TRUE);
	} else {
		rpath = get_realpath(rpath, path);
		ret = chown(rpath->str, uid, gid);
		g_string_free(rpath, TRUE);
	}
	
	return ret;
}

/**
 * rename callback
 *
 * Call rename()
 */
static int nlinkfs_rename(const char *path, const char *newpath)
{
	GString *rpath = NULL;
	GString *dpath = NULL;
	int ret = 0;

	if ( is_nlinkfs(path) ) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		dpath = get_realpath(dpath, newpath);
		dpath = g_string_append(dpath, ".LNK");

		/* Rename .LNK file */
		ret = rename(rpath->str, dpath->str);

		g_string_free(rpath, TRUE);
		g_string_free(dpath, TRUE);
	} else {
		rpath = get_realpath(rpath, path);
		dpath = get_realpath(dpath, newpath);
		ret = rename(rpath->str, dpath->str);
		g_string_free(rpath, TRUE);
		g_string_free(dpath, TRUE);
	}
	
	return ret;
}

/**
 * truncate callback
 *
 * Call truncate()
 */
static int nlinkfs_truncate(const char *path, off_t newsize)
{
	int ret = 0;
	GString *rpath = NULL;
	
	rpath = get_realpath(rpath, path);
	ret = truncate(rpath->str, newsize);
	g_string_free(rpath, TRUE);
	
	return ret;
}

/**
 * utime callback
 *
 * Call utime()
 */
static int nlinkfs_utime(const char *path, struct utimbuf *ubuf)
{
	int ret = 0;
	GString *rpath = NULL;
	
	rpath = get_realpath(rpath, path);
	ret = utime(rpath->str, ubuf);
	g_string_free(rpath, TRUE);
	
	return ret;
}

/**
 * fgetattr callback
 *
 * Call fstat()
 */
static int nlinkfs_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
	return fstat(fi->fh, statbuf);
}

/**
 * fsync callback
 *
 * Call fsync() or fdatasync()
 * If the datasync parameter is non-zero, then only the user data should be
 * flushed, not the meta data.
 */
static int nlinkfs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	int ret = 0;
	
	if (datasync) {
		ret = fdatasync(fi->fh);
	} else {
		ret = fsync(fi->fh);
	}
	
	return ret;

}

/**
 * fsyncdir callback
 *
 * Do nothing :)
 */
static int nlinkfs_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
	return 0;
}

/**
 * flush callback
 *
 * Call sync() to commit buffer cache to disk
 */
static int nlinkfs_flush(const char *path, struct fuse_file_info *fi)
{
	sync();
	return 0;
}

/* ------------------------------------------------- */

/**
 * fuse callbacks structure
 */
static struct fuse_operations nlinkfs_opfs = {
	.mknod = nlinkfs_mknod,
	.unlink = nlinkfs_unlink,
	.symlink = nlinkfs_symlink,
	.getattr = nlinkfs_getattr,
	.readlink = nlinkfs_readlink,
	.mkdir = nlinkfs_mkdir,
	.opendir = nlinkfs_opendir,
	.readdir = nlinkfs_readdir,
	.rmdir = nlinkfs_rmdir,
	.open = nlinkfs_open,
	.read = nlinkfs_read,
	.write = nlinkfs_write,
	.release = nlinkfs_close,
	.releasedir = nlinkfs_closedir,
	.access = nlinkfs_access,
	.chmod = nlinkfs_chmod,
	.chown = nlinkfs_chown,
	.rename = nlinkfs_rename,
	.truncate = nlinkfs_truncate,
	.utime = nlinkfs_utime,
	.fgetattr = nlinkfs_fgetattr,
	.fsync = nlinkfs_fsync,
	.flush = nlinkfs_flush,
	.fsyncdir = nlinkfs_fsyncdir,
	.destroy = NULL,
	/* deprecated */
	.getdir = NULL,
};

/**
 * main
 */
int main(int argc, const char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	int i;
	int ret;
	nlk_data *nlinkfs_data;
	size_t slen;

	/**
	 * FIXME: Do a decent argument parser...
	 */
	nlinkfs_data = (nlk_data*)g_malloc(sizeof(nlk_data));
	if (nlinkfs_data == NULL) {
		return EXIT_FAILURE;
	}

	for (i = 0; i < argc; i++) {
		if (i == 1) {
			slen = strlen(argv[i]);
			if (argv[i][slen-1] == '/') {
				slen--;
			}
			nlinkfs_data->srcdir = strndup(argv[i], slen);
			nlinkfs_data->srcdir[slen] = '\0';
		} else {
			fuse_opt_add_arg(&args, argv[i]);
		}
	}

	ret = fuse_main(args.argc, args.argv, &nlinkfs_opfs, nlinkfs_data);

	return ret;
}

