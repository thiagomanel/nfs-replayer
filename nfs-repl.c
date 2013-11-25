#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>

#include <nfsc/libnfs.h>
//#include <nfsc/libnfs-raw.h>
//#include <nfsc/libnfs-raw-nfs.h>
//#include <nfsc/libnfs-raw-nlm.h>

int main(int argc, char *argv[]) {

    int ret;
    struct nfs_context *nfs = NULL;
    struct nfsdir *nfsdir;
    struct nfsdirent *nfsdirent;

    struct stat st;

    char *server = argv[1];
    char *export = "/local/nfs_server";

    nfs = nfs_init_context ();
    if (nfs == NULL) {
        printf ("init_context has failed.\n");
	goto finished;
    }

    ret = nfs_mount (nfs, server, export);
    if (ret != 0) {
	printf ("nfs_mount to server=%s export=%s has failed. Error: %s\n",
		server, export, nfs_get_error (nfs));
	goto finished;
    }

    ret = nfs_opendir (nfs, "/", &nfsdir);
    if (ret != 0) {
        printf ("opendir has failed. Error:%s\n", nfs_get_error (nfs));
	goto finished;
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

finished:
    if (nfs != NULL) {
        nfs_destroy_context (nfs);
    }
    return 0;
}

