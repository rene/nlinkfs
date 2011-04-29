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
#include <glib.h>
#include <semaphore.h>

#include <nlinkfs.h>

/** semaphore to make add/remove linked list operations atomic */
sem_t mutex_llist;

/* Linked list operations with mutex */

inline GList *g_list_append_mt(GList *list, gpointer data)
{
	GList *ret;

	sem_wait(&mutex_llist);
	ret = g_list_append(list, data);
	sem_post(&mutex_llist);

	return ret;
}

inline GList *g_list_remove_mt(GList *list, gpointer data)
{
	GList *ret;

	sem_wait(&mutex_llist);
	ret = g_list_remove(list, data);
	sem_post(&mutex_llist);

	return ret;
}

inline void g_list_free_mt(GList *list)
{
	sem_wait(&mutex_llist);
	g_list_free(list);
	sem_post(&mutex_llist);
}
/*************************************/

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
 * create_link_st
 *
 * Create the symbolic link structure
 */
static nlk_slink *create_link_st(const char *path, const char *filename, const char *link)
{
	nlk_slink *newlink;
	GString *lfile;

	newlink = (nlk_slink*)g_malloc(sizeof(nlk_slink));
	if (newlink == NULL) {
		return NULL;
	}

	lfile = g_string_new(path);
	
	if (path != NULL && strlen(path) > 0) {
		if (path[strlen(path) - 1] != '/') {
			g_string_append(lfile,"/");
		}
	}

	lfile = g_string_append(lfile, filename);
	newlink->lfilename = lfile;
	newlink->path = g_string_new(link);

	return newlink;
}

/**
 * compare_link
 *
 * Compare list elements path
 */
gint compare_link(gconstpointer a, gconstpointer b)
{
	nlk_slink *linka = (nlk_slink*)a;
	char *path = (char*)b;
	return strcmp(linka->lfilename->str, path);
}

/**
 * insert_link
 *
 * Insert a new link into the list
 */
static void insert_link(nlk_slink *newlink)
{
	GList *llist = nlk_getdata->links_list;

	if (g_list_find_custom(llist, newlink->lfilename->str, compare_link) == NULL) {
		nlk_getdata->links_list = g_list_append_mt(llist, newlink);
	}
}

/**
 * clear_slist
 *
 * Clear symbolic links list
 */
void clear_slist(void)
{
	GList *slist = nlk_getdata->links_list;
	GList *element;
	nlk_slink *link;
	int i;

	for (i = 0; i < g_list_length(slist); i++) {
		element = g_list_nth(slist, i);
		link = (nlk_slink*)element->data;

		if (link != NULL) {
			g_string_free(link->lfilename, TRUE);
			g_string_free(link->path, TRUE);
			nlk_getdata->links_list = g_list_remove_mt(slist, element);
		}
	}
	g_list_free_mt(slist);
	nlk_getdata->links_list = NULL;
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
	GList *llist = nlk_getdata->links_list;
	int ret = 0;
	GList *element;

	if ((element = g_list_find_custom(llist, path, compare_link)) != NULL) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		/* Remove from list */
		llist = g_list_remove_mt(llist, element);

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
	nlk_slink *newlink;

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


	/* Now add the "virtual" symbolic link */
	newlink = (nlk_slink*)g_malloc(sizeof(nlk_slink));
	if (newlink == NULL) {
		g_string_free(rpath, TRUE);
		return -ENOMEM;
	}
	newlink->lfilename = g_string_new(link);
	newlink->path = g_string_new(path);
	insert_link(newlink);

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
	GList *llist = nlk_getdata->links_list;
	int ret = 0;
	GList *element;
	nlk_slink *slink;

	/* First we check if the file is a symbolic link */
	if ((element = g_list_find_custom(llist, path, compare_link)) != NULL) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		slink = (nlk_slink*)element->data;

		/* Give the same permission of file .LNK */
		ret = lstat(rpath->str, stbuf);

		/* Change mode to symbolic link */
		stbuf->st_mode |= S_IFLNK;

		/* Change the size of file to the length of the pathname it contains */
		stbuf->st_size = strlen(slink->path->str);

		g_string_free(rpath, TRUE);
	} else {
		rpath = get_realpath(rpath, path);
		ret = lstat(rpath->str, stbuf);
		g_string_free(rpath, TRUE);
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
	GString *rpath = NULL;
	GList *llist = nlk_getdata->links_list;
	GList *element;
	nlk_slink *slink;


	/* Check if the file is really a symbolic link */
	if ((element = g_list_find_custom(llist, path, compare_link)) != NULL) {
		rpath = get_realpath(rpath, path);
		rpath = g_string_append(rpath, ".LNK");

		slink = (nlk_slink*)element->data;

		llen = strlen(slink->path->str);

		if (size < 0) {
			return -EFAULT;
		} else {
			if (llen > size) {
				strncpy(buf, slink->path->str, size);
				buf[size] = '\0';
			} else {
				strncpy(buf, slink->path->str, llen);
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
	int fd;
	off_t fsize, slen;
	DIR *dp;
	struct dirent *de;
	size_t i, len;
	char islnk = 0;
	GString *rpath = NULL;
	GString *linkp;
	char magic[NLINKFS_MAGIC_SIZE];
	char *lpath;
	nlk_slink *nlink;


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

				fd = open(linkp->str, O_RDONLY);
				if (fd < 0) {
					islnk = 0;
				} else {
					fsize = lseek(fd, 0, SEEK_END);
					lseek(fd, 0, SEEK_SET);

					if (read(fd, magic, sizeof(char) * NLINKFS_MAGIC_SIZE) < NLINKFS_MAGIC_SIZE) {
						islnk = 0;
					} else {
						/* check magic */
						if (strncmp(magic, NLINKFS_MAGIC, NLINKFS_MAGIC_SIZE) == 0) {
							/* Create symbolic link structure */
							islnk = 1;
							slen = fsize - lseek(fd, 1, SEEK_CUR); /* 1 = skip carrige return */
							
							lpath = (char*)g_malloc(sizeof(char) * (slen + 1));
							if (lpath == NULL) {
								close(fd);
								continue;
							}
							if (read(fd, lpath, sizeof(char) * slen) < slen) {
								close(fd);
								continue;
							} else {
								/* Get just the path, remove other parts of the file */
								for (i = 0; i < slen; i++) {
									if (lpath[i] == '\n') {
										slen = i;
										break;
									}
								}
								lpath[slen] = '\0';

								/* Add to the list */
								de->d_name[len] = '\0';
								nlink = create_link_st(path, de->d_name, lpath);
								g_free(lpath);
								if (nlink == NULL) {
									close(fd);
									continue;
								} else {
									/* Finally, add to the list and to the dir */
									insert_link(nlink);
									filler(buf, de->d_name, NULL, 0);
								}
							}
						} else {
							islnk = 0;
						}
					}
					close(fd);
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
	lseek(fi->fh, offset, SEEK_SET);
	return read(fi->fh, buf, size);
}

/**
 * write callback
 *
 * Call write()
 */
static int nlinkfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	lseek(fi->fh, offset, SEEK_SET);
	return write(fi->fh, buf, size);
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
	GList *llist = nlk_getdata->links_list;
	int ret = 0;
	GList *element;

	/* First we check if the file is a symbolic link */
	if ((element = g_list_find_custom(llist, path, compare_link)) != NULL) {
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
	GList *llist = nlk_getdata->links_list;
	int ret = 0;
	GList *element;

	if ((element = g_list_find_custom(llist, path, compare_link)) != NULL) {
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

/* --- */

/**
 * destroy callback
 *
 * Called on filesystem exit
 */
void nlinkfs_destroy(void *userdata)
{
	clear_slist();
	sem_destroy(&mutex_llist);
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
	.destroy = nlinkfs_destroy,
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

	if (sem_init(&mutex_llist, 0, 1) < 0) {
		perror("Could not initialize semaphore.\n");
		return EXIT_FAILURE;
	}

	/**
	 * FIXME: Do a decent argument parser...
	 */
	nlinkfs_data = (nlk_data*)g_malloc(sizeof(nlk_data));
	if (nlinkfs_data == NULL) {
		return EXIT_FAILURE;
	} else {
		nlinkfs_data->links_list = NULL;
	}

	for (i = 0; i < argc; i++) {
		if (i == 1) {
			nlinkfs_data->srcdir = strdup(argv[i]);
		} else {
			fuse_opt_add_arg(&args, argv[i]);
		}
	}

	ret = fuse_main(args.argc, args.argv, &nlinkfs_opfs, nlinkfs_data);

	return ret;
}

