/*
   Copyright (C) by Ronnie Sahlberg <sahlberg@samba.org> 2008

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#define _FILE_OFFSET_BITS 64

#include <sys/time.h>
#include <stdlib.h>

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <nfsc/libnfs-raw-nlm.h>
#include <nfsc/libnfs-raw-nfs.h>

#include "libnfs-glue.h"
#include "nfsio.h"

#define discard_const(ptr) ((void *)((intptr_t)(ptr)))
#define ZERO_STRUCT(x) memset(&(x), 0, sizeof(x))

#define MAX_FILES 200
#define MAX_OPS 100

const int global_random;
char rw_buf[];

struct options {
	const char *backend;
	int nprocs;
	int sync_open;
	int sync_dirs;
	int do_fsync;
	int no_resolve;
	int fsync_frequency;
	char *tcp_options;
	int timelimit;
	int warmup;
	const char *directory;
	char *loadfile;
	double targetrate;
	int ea_enable;
	int clients_per_process;
	int one_byte_write_fix;
	int stat_check;
	int fake_io;
	int skip_cleanup;
	int per_client_results;
	const char *nfs;
	int nlm;
	const char *server;
	int run_once;
	int allow_scsi_writes;
	int trunc_io;
	const char *scsi_dev;
	const char *iscsi_device;
	const char *iscsi_initiatorname;
	int machine_readable;
	const char *smb_share;
	const char *smb_user;
};

static struct options options;

struct op {
    unsigned count;
    double total_time;
    double max_latency;
};

struct child_struct {
	int id;
	int num_clients;
	int failed;
	int line;
	int done;
	int cleanup;
	int cleanup_finished;
	const char *directory;
	double bytes;
	double bytes_done_warmup;
	double max_latency;
	double worst_latency;
	struct timeval starttime;
	struct timeval lasttime;
	off_t bytes_since_fsync;
	char *cname;
	struct {
		double last_bytes;
		struct timeval last_time;
	} rate;
	struct op ops[MAX_OPS];
	void *private;

	int sequence_point;

	/* Some functions need to be able to access arbitrary child
	 * structures from each child. */
	struct child_struct *all_children;
};

struct dbench_op {
	struct child_struct *child;
	const char *op;
	const char *fname;
	const char *fname2;
	const char *status;
	int64_t params[10];
};

struct backend_op {
    const char *name;
    void (*fn)(struct dbench_op *);
};

struct cb_data {
	struct nfsio *nfsio;
	char *dirname;
};

char *get_next_arg(const char *args, int id) {
    char *tmp = discard_const(args);
    int i;

    for (i = 0; i < id; i++) {
	tmp = strchr(tmp, ',');
	if (tmp == NULL) {
	    tmp = discard_const(args);
	    continue;
	}
	tmp++;
    }
    tmp = strdup(tmp);
    if (strchr(tmp, ',')) {
	*strchr(tmp, ',') = 0;
    }
    return tmp;
}

/*
 * return a timeval for the current time
 */
struct timeval timeval_current (void) {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return tv;
}

static void nfs3_deltree(struct dbench_op *op);

static void nfs3_cleanup(struct child_struct *child)
{
	char *dname;
	struct dbench_op op;
	ZERO_STRUCT(op);

	if (asprintf(&dname, "/clients/client%d", child->id) < 0) {
		exit(1);
	}
	op.fname = dname;
	op.child = child;
	nfs3_deltree(&op);
	free(dname);
}

static void nfs3_setup(struct child_struct *child)
{
	const char *status = "0x00000000";
	nfsstat3 res;
	char *url;

	child->rate.last_time = timeval_current();
	child->rate.last_bytes = 0;

	srandom(getpid() ^ time(NULL));
	url = get_next_arg(options.nfs, child->id);
	child->private = nfsio_connect(url, child->id, global_random + child->id, child->num_clients, options.nlm);
	free(url);
	if (child->private == NULL) {
		child->failed = 1;
		printf("nfsio_connect() failed\n");
		exit(10);
	}

	/* create '/clients' */
	res = nfsio_lookup(child->private, "/clients", NULL);
	if (res == NFS3ERR_NOENT) {
		res = nfsio_mkdir(child->private, "/clients");
		if( (res != NFS3_OK) &&
		    (res != NFS3ERR_EXIST) ) {
			printf("Failed to create '/clients' directory. res:%u\n", res);
			exit(10);
		}
	}
}


static void dirent_cb(struct entryplus3 *e, void *private_data)
{
	struct cb_data *cbd = private_data;
	nfsstat3 res;
	char *objname;

	if (!strcmp(cbd->dirname,".")) {
		return;
	}
	if (!strcmp(cbd->dirname,"..")) {
		return;
	}

	if (asprintf(&objname, "%s/%s", cbd->dirname, e->name) < 0) {
		exit(1);
	}
	if (objname == NULL) {
		printf("Failed to talloc ne object name in dirent_cb\n");
		exit(10);
	}

	if (e->name_attributes.post_op_attr_u.attributes.type == NF3DIR) {
		struct cb_data *new_cbd = malloc(sizeof(struct cb_data));

		new_cbd->nfsio = cbd->nfsio;
		new_cbd->dirname = strdup(objname);

		nfsio_readdirplus(cbd->nfsio, objname, dirent_cb, new_cbd);

		res = nfsio_rmdir(cbd->nfsio, objname);
		if (res != NFS3_OK) {
			printf("Failed to remove object : \"%s\"  %s (%d)\n", objname, nfs_error(res), res);
			free(objname);
			free(new_cbd->dirname);
			free(new_cbd);
			exit(10);
		}


		free(objname);
		free(new_cbd->dirname);
		free(new_cbd);
		return;
	}

	res = nfsio_remove(cbd->nfsio, objname);
	if (res != NFS3_OK) {
		printf("Failed to remove object : \"%s\" %s %d\n", objname, nfs_error(res), res);
		free(objname);
		exit(10);
	}

	free(objname);
}

static void nfs3_deltree(struct dbench_op *op)
{
	struct cb_data *cbd;
	nfsstat3 res;

	cbd = malloc(sizeof(struct cb_data));

	cbd->nfsio = op->child->private;
	cbd->dirname = discard_const(op->fname);

	res = nfsio_lookup(cbd->nfsio, cbd->dirname, NULL);
	if (res != NFS3ERR_NOENT) {
		nfsio_readdirplus(cbd->nfsio, cbd->dirname, dirent_cb, cbd);
		nfsio_rmdir(cbd->nfsio, cbd->dirname);
	}

	res = nfsio_lookup(cbd->nfsio, cbd->dirname, NULL);
	if (res != NFS3ERR_NOENT) {
		printf("Directory \"%s\" not empty. Aborting\n", cbd->dirname);
		free(cbd);
		exit(10);
	}
	free(cbd);
}

static int check_status(int status, const char *expected)
{
	if (strcmp(expected, "*") == 0){
		return 1;
	}
	if (strncmp(expected, "0x", 2) == 0) {
		return status == strtol(expected, NULL, 16);
	}
	return 0;
}

static void failed(struct child_struct *child)
{
	child->failed = 1;
	printf("ERROR: child %d failed at line %d\n", child->id, child->line);
	exit(1);
}

static void nfs3_getattr(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_getattr(op->child->private, op->fname, NULL);
	if (!check_status(res, op->status)) {
		printf("[%d] GETATTR \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_setattr(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_setattr(op->child->private, op->fname, NULL);
	if (!check_status(res, op->status)) {
		printf("[%d] SETATTR \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_pathconf(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_pathconf(op->child->private, discard_const(op->fname));
	if (!check_status(res, op->status)) {
		printf("[%d] PATHCONF \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_readlink(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_readlink(op->child->private, discard_const(op->fname));
	if (!check_status(res, op->status)) {
		printf("[%d] READLINK \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_lookup(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_lookup(op->child->private, op->fname, NULL);
	if (!check_status(res, op->status)) {
		printf("[%d] LOOKUP \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

void do_nfs3_create (nfsio * nio, const char *name) {

    nfsstat3 res;
    res = nfsio_create(nio, name);
    printf("CREATE \"%s\" actual(%x)\n", name, res);
}

static void nfs3_create(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_create(op->child->private, op->fname);
	if (!check_status(res, op->status)) {
		printf("[%d] CREATE \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_write(struct dbench_op *op)
{
	off_t offset = op->params[0];
	int len = op->params[1];
	int stable = op->params[2];
	nfsstat3 res;

	if ((options.trunc_io > 0) && (len > options.trunc_io)) {
		len = options.trunc_io;
	}

	res = nfsio_write(op->child->private, op->fname, rw_buf, offset, len, stable);
	if (!check_status(res, op->status)) {
		printf("[%d] WRITE \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname,
		       res, op->status);
		failed(op->child);
	}
	op->child->bytes += len;
}

static void nfs3_commit(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_commit(op->child->private, op->fname);
	if (!check_status(res, op->status)) {
		printf("[%d] COMMIT \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}


static void nfs3_read(struct dbench_op *op)
{
	off_t offset = op->params[0];
	int len = op->params[1];
	nfsstat3 res = 0;

	if ((options.trunc_io > 0) && (len > options.trunc_io)) {
		len = options.trunc_io;
	}

	res = nfsio_read(op->child->private, op->fname, NULL, offset, len);
	if (!check_status(res, op->status)) {
		printf("[%d] READ \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname,
		       res, op->status);
		failed(op->child);
	}
	op->child->bytes += len;
}

static void nfs3_access(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_access(op->child->private, op->fname, 0, NULL);
	if (!check_status(res, op->status)) {
		printf("[%d] ACCESS \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

void do_nfs3_mkdir (nfsio *nio, const char *name) {

    nfsstat3 res;
    res = nfsio_mkdir (nio, name);
    //FIXME: how to assign this op->status value ? is it 0 ?
    printf ("MKDIR \"%s\" actual(%x)\n", name, res);
}

static void nfs3_mkdir(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_mkdir(op->child->private, op->fname);
	if (!check_status(res, op->status)) {
		printf("[%d] MKDIR \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_rmdir(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_rmdir(op->child->private, op->fname);
	if (!check_status(res, op->status)) {
		printf("[%d] RMDIR \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_fsstat(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_fsstat(op->child->private);
	if (!check_status(res, op->status)) {
		printf("[%d] FSSTAT failed (%x) - expected %s\n",
		       op->child->line, res, op->status);
		failed(op->child);
	}
}

static void nfs3_fsinfo(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_fsinfo(op->child->private);
	if (!check_status(res, op->status)) {
		printf("[%d] FSINFO failed (%x) - expected %s\n",
		       op->child->line, res, op->status);
		failed(op->child);
	}
}

static void nfs3_symlink(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_symlink(op->child->private, op->fname, op->fname2);
	if (!check_status(res, op->status)) {
		printf("[%d] SYMLINK \"%s\"->\"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, op->fname2,
		       res, op->status);
		failed(op->child);
	}
}

static void nfs3_remove(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_remove(op->child->private, op->fname);
	if (!check_status(res, op->status)) {
		printf("[%d] REMOVE \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_readdirplus(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_readdirplus(op->child->private, op->fname, NULL, NULL);
	if (!check_status(res, op->status)) {
		printf("[%d] READDIRPLUS \"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, res, op->status);
		failed(op->child);
	}
}

static void nfs3_link(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_link(op->child->private, op->fname, op->fname2);
	if (!check_status(res, op->status)) {
		printf("[%d] LINK \"%s\"->\"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, op->fname2,
		       res, op->status);
		failed(op->child);
	}
}

static void nfs3_lock(struct dbench_op *op)
{
	off_t offset = op->params[0];
	int len = op->params[1];
	nlmstat4 res;

	res = nfsio_lock(op->child->private, op->fname, offset, len);
	if (!check_status(res, op->status)) {
		printf("[%d] LOCK \"%s\" %u-%u failed (%x) - expected %s\n",
		       op->child->line, op->fname, (unsigned)offset,
		       (unsigned)offset + len,
		       res, op->status);
		failed(op->child);
	}
}

static void nfs3_unlock(struct dbench_op *op)
{
	off_t offset = op->params[0];
	int len = op->params[1];
	nlmstat4 res;

	res = nfsio_unlock(op->child->private, op->fname, offset, len);
	if (!check_status(res, op->status)) {
		printf("[%d] UNLOCK \"%s\" %u-%u failed (%x) - expected %s\n",
		       op->child->line, op->fname, (unsigned)offset,
		       (unsigned)offset + len,
		       res, op->status);
		failed(op->child);
	}
}

static void nfs3_test(struct dbench_op *op)
{
	off_t offset = op->params[0];
	int len = op->params[1];
	nlmstat4 res;

	res = nfsio_test(op->child->private, op->fname, offset, len);
	if (!check_status(res, op->status)) {
		printf("[%d] TEST \"%s\" %u-%u failed (%x) - expected %s\n",
		       op->child->line, op->fname, (unsigned)offset,
		       (unsigned)offset + len,
		       res, op->status);
		failed(op->child);
	}
}

void do_nfs3_rename(nfsio * nio, const char * oldname, const char *newname) {

	nfsstat3 res;
	res = nfsio_rename (nio, oldname, newname);
	printf("RENAME \"%s\"->\"%s\" actual (%x)\n", oldname, newname, res);
}

static void nfs3_rename(struct dbench_op *op)
{
	nfsstat3 res;

	res = nfsio_rename(op->child->private, op->fname, op->fname2);
	if (!check_status(res, op->status)) {
		printf("[%d] RENAME \"%s\"->\"%s\" failed (%x) - expected %s\n",
		       op->child->line, op->fname, op->fname2,
		       res, op->status);
		failed(op->child);
	}
}

static int nfs3_init(void)
{
	void *handle;
	char *url;

	if (options.nfs == NULL) {
		printf("--nfs target was not specified\n");
		return 1;
	}
	url = get_next_arg(options.nfs, 0);
	handle = nfsio_connect(url, 0, global_random, 1, 0);
	free(url);
	if (handle == NULL) {
		printf("Failed to connect to NFS server\n");
		return 1;
	}

	nfsio_disconnect(handle);
	return 0;
}

