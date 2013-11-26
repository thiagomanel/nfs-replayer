#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <nfsc/libnfs-raw-nfs.h>
#include <nfsc/libnfs-raw-nlm.h>

#include "libnfs-glue.h"
#include "nfsio.h"

int print_list (struct nfs_context *nfs) {

    int ret;
    struct stat st;
    struct nfsdirent *nfsdirent;
    struct nfsdir *nfsdir;

    ret = nfs_opendir (nfs, "/", &nfsdir);
    if (ret != 0) {
        printf ("opendir has failed. Error:%s\n", nfs_get_error (nfs));
	return ret;
    }

    while ((nfsdirent = nfs_readdir (nfs, nfsdir)) != NULL) {

        char path[1024];
        if (!strcmp (nfsdirent->name, ".") || !strcmp (nfsdirent->name, "..")) {
   	    continue;
	}

	sprintf (path, "%s/%s", "/", nfsdirent->name);
	ret = nfs_stat (nfs, path, &st);
	if (ret != 0) {
    	    fprintf (stderr, "Failed to stat(%s) %s\n",
			path, nfs_get_error (nfs));
	    continue;
	}

	switch (st.st_mode & S_IFMT) {
#ifndef WIN32
	case S_IFLNK:
#endif
	case S_IFREG:
  	    printf ("-");
	    break;
	case S_IFDIR:
    	    printf ("d");
 	    break;
	case S_IFCHR:
 	    printf ("c");
	    break;
	case S_IFBLK:
   	    printf ("b");
  	    break;
	}
	printf ("%c%c%c",
		"-r"[!!(st.st_mode & S_IRUSR)],
		"-w"[!!(st.st_mode & S_IWUSR)],
		"-x"[!!(st.st_mode & S_IXUSR)]
		);
	printf ("%c%c%c",
		"-r"[!!(st.st_mode & S_IRGRP)],
		"-w"[!!(st.st_mode & S_IWGRP)],
		"-x"[!!(st.st_mode & S_IXGRP)]
		);
	printf ("%c%c%c",
		"-r"[!!(st.st_mode & S_IROTH)],
		"-w"[!!(st.st_mode & S_IWOTH)],
		"-x"[!!(st.st_mode & S_IXOTH)]
		);
	printf (" %5d", st.st_uid);
	printf (" %5d", st.st_gid);
	printf (" %12" PRId64, st.st_size);

	printf (" %s\n", nfsdirent->name);
    }
    nfs_closedir (nfs, nfsdir);
    return 0;
}

int main(int argc, char *argv[]) {

    int ret;
    uint64_t count, offset;
    struct nfs_context *nfs = NULL;
    struct nfsfh* creat_fh = NULL;
    struct nfsfh* open_fh = NULL;

    char * buf = (char*) malloc (sizeof (char) * 4096);
    char *server = argv[1];
    char *export = "/local/nfs_server";
    nfsio * io = do_nfsio_connect (server, export);

    //list root
    print_list (io->nfs);

    printf ("mkdir\n");
    nfs_mkdir (io->nfs, "/new_dir0");
    nfs_mkdir (io->nfs, "/new_dir1");
    print_list (io->nfs);

    printf ("rename dir\n");
    nfs_rename (io->nfs, "/new_dir0", "/new_dir0_renamed");
    print_list (io->nfs);

    printf ("rmdir\n");
    nfs_rmdir (io->nfs, "/new_dir1");
    print_list (io->nfs);

    printf ("creat\n");
    struct nfsfh* fh = NULL;
    nfs_creat (io->nfs, "/newfile", 0660, &creat_fh);
    print_list (io->nfs);

    printf ("open\n");
    ret = nfs_open (io->nfs, "/newfile", O_RDONLY, &open_fh);
    printf ("open ret=%d\n", ret);

    offset = 0;
    count = 4096;

    printf ("pwrite\n");
    ret = nfs_pwrite (io->nfs, open_fh, offset, count, buf);
    printf ("pwrite ret=%d\n", ret);

    printf ("pread\n");
    ret = nfs_pread (io->nfs, open_fh, offset, count, buf);
    printf ("pread ret=%d\n", ret);

    printf ("close\n");
    ret = nfs_close (io->nfs, open_fh);
    printf ("close ret=%d\n", ret);

    printf ("unlink\n");
    nfs_unlink (io->nfs, "/newfile");
    print_list (io->nfs);

    return ret;
}

