/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
/******************************************************************************\
*                                                                              *
*        Copyright (c) 2003, The Regents of the University of California       *
*      See the file COPYRIGHT for a complete copyright notice and license.     *
*                                                                              *
********************************************************************************
*
* Implement of abstract I/O interface for GRAMFS.
*
\******************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#  include <sys/ioctl.h>          /* necessary for: */
#  define __USE_GNU               /* O_DIRECT and */
#  include <fcntl.h>              /* IO operations */
#  undef __USE_GNU
#endif                          /* __linux__ */

#include <errno.h>
#include <fcntl.h>              /* IO operations */
#include <sys/stat.h>
#include <assert.h>

#include "ior.h"
#include "aiori.h"
#include "iordef.h"
#include "utilities.h"

#include <sys/statvfs.h>
#include <gramfs/gramfs.h>

#ifndef   O_BINARY              /* Required on Windows    */
#  define O_BINARY 0
#endif

/**************************** P R O T O T Y P E S *****************************/
static void GRAMFS_Initialize(aiori_mod_opt_t*);
static void GRAMFS_Finalize(aiori_mod_opt_t*);
static aiori_fd_t* GRAMFS_Create(char *, int, aiori_mod_opt_t *);
static int GRAMFS_Mknod(char *);
static aiori_fd_t *GRAMFS_Open(char *, int, aiori_mod_opt_t *);
static void GRAMFS_xfer_hints(aiori_xfer_hint_t * params);
static IOR_offset_t GRAMFS_Xfer(int, aiori_fd_t *, IOR_size_t *,
                           IOR_offset_t, IOR_offset_t, aiori_mod_opt_t *);
static void GRAMFS_Close(aiori_fd_t *, aiori_mod_opt_t *);
static void GRAMFS_Delete(char *, aiori_mod_opt_t *);
static char* GRAMFS_GetVersion();
static void GRAMFS_Fsync(aiori_fd_t *, aiori_mod_opt_t * module_options);
static IOR_offset_t GRAMFS_GetFileSize(aiori_mod_opt_t * module_options, char * filename);
static int GRAMFS_Access(const char *, int mode, aiori_mod_opt_t * module_options);
static int GRAMFS_Statfs(const char *, ior_aiori_statfs_t *, aiori_mod_opt_t * module_options);
static int GRAMFS_Mkdir(const char *path, mode_t mode, aiori_mod_opt_t * module_options);
static int GRAMFS_Rmdir(const char *path, aiori_mod_opt_t * module_options);
static int GRAMFS_Access(const char *path, int mode, aiori_mod_opt_t * module_options);
static int GRAMFS_Stat(const char *path, struct stat *buf, aiori_mod_opt_t * module_options);
static option_help * GRAMFS_options(aiori_mod_opt_t ** init_backend_options, aiori_mod_opt_t* init_values);
static void GRAMFS_Sync(aiori_mod_opt_t*);  /* synchronize every pending operation for this storage */
static int GRAMFS_Rename(const char *oldpath, const char *newpath, aiori_mod_opt_t * module_options);
static int GRAMFS_Check_params(aiori_mod_opt_t *);
/* check if the provided module_optionseters for the given test and the module options are correct, if they aren't print a message and exit(1) or return 1*/


/************************** O P T I O N S *****************************/
typedef struct {
  //char * user;
  char * conf;
  char * prefix;
}gramfs_options_t;

option_help * GRAMFS_options(aiori_mod_opt_t ** init_backend_options, aiori_mod_opt_t* init_values){

    gramfs_options_t* o = malloc(sizeof(gramfs_options_t));
    if(init_values != NULL) {
        memcpy(o, init_values, sizeof(gramfs_options_t));
    } else {
        memset(o, 0, sizeof(gramfs_options_t));
    }

    *init_backend_options = (aiori_mod_opt_t *)o;

    option_help h [] = {
            {0, "gramfs.conf", "Config file for the gramfs client", OPTION_OPTIONAL_ARGUMENT, 's', & o->conf},
            {0, "gramfs.prefix", "mount prefix",  OPTION_OPTIONAL_ARGUMENT, 's', & o->prefix},
            LAST_OPTION
    };
    option_help* help = malloc(sizeof(h));
    memcpy(help, h, sizeof(h));
	return help;
}

static char* GRAMFS_GetVersion() {
	static char ver[1024] = {};

    sprintf(ver, "%s", "GRAMFS-V1.0");
    return ver;
}

/************************** D E C L A R A T I O N S ***************************/
ior_aiori_t gramfs_aiori = {
        .name = "GRAMFS",
        .name_legacy = NULL,
        .create = GRAMFS_Create,
        .mknod = GRAMFS_Mknod,
        .open = GRAMFS_Open,
        .xfer_hints = GRAMFS_xfer_hints,
        .xfer = GRAMFS_Xfer,
        .close = GRAMFS_Close,
        .delete = GRAMFS_Delete,
        .get_version = GRAMFS_GetVersion,
        //.fsync = GRAMFS_Fsync,
        .get_file_size = GRAMFS_GetFileSize,
        .statfs = GRAMFS_Statfs,
        .mkdir = GRAMFS_Mkdir,
        .rmdir = GRAMFS_Rmdir,
        .access = GRAMFS_Access,
        .stat = GRAMFS_Stat,
		.initialize = GRAMFS_Initialize,
		.finalize   = GRAMFS_Finalize,
        .get_options = GRAMFS_options,
        .enable_mdtest = true,
        //.sync = GRAMFS_Sync
};

/***************************** F U N C T I O N S ******************************/
static aiori_xfer_hint_t * hints = NULL;

static void GRAMFS_xfer_hints(aiori_xfer_hint_t * params){
    hints = params;
}

static void GRAMFS_Initialize(aiori_mod_opt_t* options)
{
	return gramfs_initialize();
}

static void GRAMFS_Finalize(aiori_mod_opt_t* options)
{
	return gramfs_finalize();
}
/*
 * Creat and open a file through the GRAMFS interface.
 */

static aiori_fd_t* GRAMFS_Create(char *testFileName, int flags, aiori_mod_opt_t *module_options)
{
    int fd_oflag = O_BINARY;
    int mode = 0664;

    void *fd = NULL;

    if(hints->dryRun)
        return (aiori_fd_t*)0;

    fd_oflag |= O_CREAT | O_RDWR;

    fd = gramfs_open(testFileName, fd_oflag, mode);
    if (!fd)
            ERRF("gramfs open(\"%s\", %d, %#o) failed",
                    testFileName, fd_oflag, mode);

    return (aiori_fd_t *)fd;
}

/*
 * Creat a file through mknod interface.
 */
static int GRAMFS_Mknod(char *testFileName)
{
    void* fd;
    fd = gramfs_create(testFileName, S_IFREG | S_IRUSR);
    if (!fd) {
        ERR("mknod failed");
        return -1;
    }
    return 0;
}

/*
 * Open a file through the GRAMFS interface.
 */
static aiori_fd_t* GRAMFS_Open(char *testFileName, int flags, aiori_mod_opt_t *module_options)
{
	int fd_oflag = O_BINARY;
    if(flags & IOR_RDONLY){
        fd_oflag |= O_RDONLY;
    }else if(flags & IOR_WRONLY){
        fd_oflag |= O_WRONLY;
    }else{
        fd_oflag |= O_RDWR;
    }

	void *fd;
    int mode = 0644;
	fd_oflag |= O_RDWR;

	if(hints->dryRun)   return (aiori_fd_t*)0;

	fd = gramfs_open(testFileName, fd_oflag, mode);
	if(!fd)
		ERRF("gramfs_open(\"%s\", %d) failed", testFileName, fd_oflag);

	return (aiori_fd_t *)fd;
}

/*
 * Write or read access to file using the GRAMFS interface.
 */
static IOR_offset_t GRAMFS_Xfer(int access, aiori_fd_t *file, IOR_size_t *buffer,
                                IOR_offset_t size, IOR_offset_t offset, aiori_mod_opt_t * module_options)
{

	int64_t remaining = (int64_t)size;

	char *ptr = (char *)buffer;
	int64_t rc;

   if(hints->dryRun)    return size;

	while(remaining > 0) {
		if(access == WRITE) {
			rc = gramfs_write(file, ptr, remaining, (int64_t)offset);
			if (rc == -1)
				ERRF("write(%p, %lld) failed", (void*)ptr, remaining);
            //if (param->fsyncPerWrite == TRUE)
            //        Gramfs_Fsync(&fd, param);
		} else {
			rc = gramfs_read(file, ptr, remaining, (int64_t)offset);
			if (rc == 0)
				ERRF("read(%p, %lld) returned EOF prematurely", (void*)ptr, remaining);
			if (rc == -1)
				ERRF("read(%p, %lld) failed", (void*)ptr, remaining);
		}
        remaining -= rc;
        ptr += rc;
	}

	return (size);
}

/*
 * Perform fsync().
 */
static void GRAMFS_Fsync(aiori_fd_t *fd, aiori_mod_opt_t *module_options)
{
       // if (fsync(*(int *)fd) != 0)
       //         EWARNF("fsync(%d) failed", *(int *)fd);
}

static void GRAMFS_Sync(aiori_mod_opt_t *module_options)
{
	/*int ret = system("sync");
	if (ret != 0){
		FAIL("Error executing the sync command, ensure it exists.");
	}*/
}

/*
 * Close a file through the GRAMFS interface.
 */
static void GRAMFS_Close(aiori_fd_t *afd, aiori_mod_opt_t *module_options)
{
        if(hints->dryRun)   return;
        gramfs_close((void*)afd);
}

/*
 * Delete a file through the GRAMFS interface.
 */
static void GRAMFS_Delete(char *testFileName, aiori_mod_opt_t * module_options)
{
	if(hints->dryRun)
		return;
	if(gramfs_delete(testFileName)) {
		EWARNF("[RANK %03d]: unlink() of file \"%s\" failed\n",
				   rank, testFileName);
	}
}

static int GRAMFS_Statfs(const char *path, ior_aiori_statfs_t *stat_buf, aiori_mod_opt_t * module_options) {
	int ret;
	struct statvfs statfs_buf;
	ret = gramfs_statfs(path, &statfs_buf);

    stat_buf->f_bsize = statfs_buf.f_bsize;
    stat_buf->f_blocks = statfs_buf.f_blocks;
    stat_buf->f_bfree = statfs_buf.f_bfree;
    stat_buf->f_files = statfs_buf.f_files;
    stat_buf->f_ffree = statfs_buf.f_ffree;
	//stat_buf->f_bavail = statfs_buf.f_bavail;

	return ret;
}

static int GRAMFS_Mkdir(const char *path, mode_t mode, aiori_mod_opt_t * module_options) {
	int ret;

	ret = gramfs_mkdir(path, mode);

	return ret;
}

static int GRAMFS_Rmdir(const char *path, aiori_mod_opt_t * module_options) {
	int ret;

	ret = gramfs_rmdir(path);

	return ret;
}

static int GRAMFS_Access(const char *path, int mode, aiori_mod_opt_t * module_options) {
	int ret;

	ret = gramfs_access(path, mode);

	return ret;
}

static int GRAMFS_Stat(const char *path, struct stat *buf, aiori_mod_opt_t * module_options) {
	int ret;

	ret = gramfs_stat(path, buf);

	return ret;
}

/*
 * Use Gramfs stat() to return aggregate file size.
 */
static IOR_offset_t GRAMFS_GetFileSize(aiori_mod_opt_t * test, char *testFileName)
{
        if(hints->dryRun)    return 0;

        struct stat stat_buf;
        IOR_offset_t aggFileSizeFromStat;

        if(gramfs_stat(testFileName, &stat_buf)) {
        	ERRF("stat(\"%s\", ...) failed", testFileName);
        }
        aggFileSizeFromStat = stat_buf.st_size;

        return (aggFileSizeFromStat);

/*      IOR_offset_t aggFileSizeFromStat, tmpMin, tmpMax, tmpSum;

        if (stat(testFileName, &stat_buf) != 0) {
                ERRF("stat(\"%s\", ...) failed", testFileName);
        }
        aggFileSizeFromStat = stat_buf.st_size;

        if (test->filePerProc == TRUE) {
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpSum, 1,
                                        MPI_LONG_LONG_INT, MPI_SUM, testComm),
                          "cannot total data moved");
                aggFileSizeFromStat = tmpSum;
        } else {
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpMin, 1,
                                        MPI_LONG_LONG_INT, MPI_MIN, testComm),
                          "cannot total data moved");
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpMax, 1,
                                        MPI_LONG_LONG_INT, MPI_MAX, testComm),
                          "cannot total data moved");
                if (tmpMin != tmpMax) {
                        if (rank == 0) {
                                WARN("inconsistent file size by different tasks");
                        }
                         incorrect, but now consistent across tasks
                        aggFileSizeFromStat = tmpMin;
                }
        }
        return (aggFileSizeFromStat);
        */
}
