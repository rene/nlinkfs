# NLINKFS

Some file systems does not support symbolic links. NLINKFS was written to act
over some other file system (fs) to deal with symbolic links as regular files
on the original fs.


## How to install

$ ./configure
$ make
$ make install # (as root)

For more information, see INSTALL file.


## How to use

NLINKFS uses the follow syntax:

$ nlinkfs **<source_dir>** **<mount_dir>**

**<source_dir>** - Original file system directory
**<mount_dir>** - Directory to mount NLINKFS file system

Each symbolic link created at **<mount_dir>** will be converted to a regular .LNK
file at **<source_dir>** with the information to where the link points to. In the
same way, nlinkfs will read each .LNK file on **<source_dir>** and show it as
symbolic links at **<mount_dir>**. Files created by NLINKFS contains a signature, so
common .LNK (non link) files are allowed.

Example of usage:

$ mkdir {test,mdir}
$ touch test/hello.txt
$ nlinkfs test mdir
$ cd mdir
$ ln -s hello.txt mylink
$ cd ..
$ fusermount -u mdir


## Bug Reports

Bugs should be reported to rene@renesp.com.br

