/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/reboot.h>
#include <kern/unistd.h>
#include <limits.h>
#include <lib.h>
#include <uio.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <synch.h>
#include <vfs.h>
#include <sfs.h>
#include <syscall.h>
#include <test.h>
#include "opt-synchprobs.h"
#include "opt-sfs.h"
#include "opt-net.h"

/*
 * In-kernel menu and command dispatcher.
 */

#define _PATH_SHELL "/bin/sh"

#define MAXMENUARGS  16

// XXX this should not be in this file
void
getinterval(time_t s1, uint32_t ns1, time_t s2, uint32_t ns2,
	    time_t *rs, uint32_t *rns)
{
	if (ns2 < ns1) {
		ns2 += 1000000000;
		s2--;
	}

	*rns = ns2 - ns1;
	*rs = s2 - s1;
}

////////////////////////////////////////////////////////////
//
// Command menu functions 

/*
 * Function for a thread that runs an arbitrary userlevel program by
 * name.
 *
 * Note: this cannot pass arguments to the program. You may wish to 
 * change it so it can, because that will make testing much easier
 * in the future.
 *
 * It copies the program name because runprogram destroys the copy
 * it gets by passing it to vfs_open(). 
 */
static
void
cmd_progthread(void *ptr, unsigned long nargs)
{
	char **args = ptr;
	char progname[128];
	int result;

	KASSERT(nargs >= 1);



	/* Hope we fit. */
	KASSERT(strlen(args[0]) < sizeof(progname));

	strcpy(progname, args[0]);
	result = runprogram(progname, nargs, args);
	if (result) {
		kprintf("Running program %s failed: %s\n", args[0],
			strerror(result));
		return;
	}

	/* NOTREACHED: runprogram only returns on error. */
}

/*
 * Common code for cmd_prog and cmd_shell.
 *
 * Note that this does not wait for the subprogram to finish, but
 * returns immediately to the menu. This is usually not what you want,
 * so you should have it call your system-calls-assignment waitpid
 * code after forking.
 *
 * Also note that because the subprogram's thread uses the "args"
 * array and strings, until you do this a race condition exists
 * between that code and the menu input code.
 */
static
int
common_prog(int nargs, char **args)
{
	struct proc *proc;
	int result;

#if OPT_SYNCHPROBS
	kprintf("Warning: this probably won't work with a "
		"synchronization-problems kernel.\n");
#endif

	/* Create a process for the new program to run in. */
	proc = proc_create_runprogram(args[0] /* name */);
	if (proc == NULL) {
		return ENOMEM;
	}

	result = thread_fork(args[0] /* thread name */,
			proc /* new process */,
			cmd_progthread /* thread function */,
			args /* thread arg */, nargs /* thread arg */);
	if (result) {
		kprintf("thread_fork failed: %s\n", strerror(result));
		proc_destroy(proc);
		return result;
	}

#ifdef UW
	/* wait until the process we have just launched - and any others that it 
	   may fork - is finished before proceeding */
	P(no_proc_sem);
#endif // UW

	return 0;
}

/*
 * Command for running an arbitrary userlevel program.
 */
static
int
cmd_prog(int nargs, char **args)
{
	if (nargs < 2) {
		kprintf("Usage: p program [arguments]\n");
		return EINVAL;
	}

	/* drop the leading "p" */
	args++;
	nargs--;

	return common_prog(nargs, args);
}
static
int
cmd_dth()
{
	dbflags=DB_THREADS; //penis
	return 0;
}
	
/*
 * Command for starting the system shell.
 */
static
int
cmd_shell(int nargs, char **args)
{
	(void)args;
	if (nargs != 1) {
		kprintf("Usage: s\n");
		return EINVAL;
	}

	args[0] = (char *)_PATH_SHELL;
	kprintf("s: %s", args[2]);
	return common_prog(nargs, args);
}

/*
 * Command for changing directory.
 */
static
int
cmd_chdir(int nargs, char **args)
{
	if (nargs != 2) {
		kprintf("Usage: cd directory\n");
		return EINVAL;
	}

	return vfs_chdir(args[1]);
}

/*
 * Command for printing the current directory.
 */
static
int
cmd_pwd(int nargs, char **args)
{
	char buf[PATH_MAX+1];
	int result;
	struct iovec iov;
	struct uio ku;

	(void)nargs;
	(void)args;

	uio_kinit(&iov, &ku, buf, sizeof(buf)-1, 0, UIO_READ);
	result = vfs_getcwd(&ku);
	if (result) {
		kprintf("vfs_getcwd failed (%s)\n", strerror(result));
		return result;
	}

	/* null terminate */
	buf[sizeof(buf)-1-ku.uio_resid] = 0;

	/* print it */
	kprintf("%s\n", buf);

	return 0;
}

/*
 * Command for running sync.
 */
static
int
cmd_sync(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	vfs_sync();

	return 0;
}

/*
 * Command for doing an intentional panic.
 */
static
int
cmd_panic(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	panic("User requested panic\n");
	return 0;
}

/*
 * Command for shutting down.
 */
static
int
cmd_quit(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	vfs_sync();
	sys_reboot(RB_POWEROFF);
	thread_exit();
	return 0;
}

/*
 * Command for mounting a filesystem.
 */

/* Table of mountable filesystem types. */
static const struct {
	const char *name;
	int (*func)(const char *device);
} mounttable[] = {
#if OPT_SFS
	{ "sfs", sfs_mount },
#endif
	{ NULL, NULL }
};

static
int
cmd_mount(int nargs, char **args)
{
	char *fstype;
	char *device;
	int i;

	if (nargs != 3) {
		kprintf("Usage: mount fstype device:\n");
		return EINVAL;
	}

	fstype = args[1];
	device = args[2];

	/* Allow (but do not require) colon after device name */
	if (device[strlen(device)-1]==':') {
		device[strlen(device)-1] = 0;
	}

	for (i=0; mounttable[i].name; i++) {
		if (!strcmp(mounttable[i].name, fstype)) {
			return mounttable[i].func(device);
		}
	}
	kprintf("Unknown filesystem type %s\n", fstype);
	return EINVAL;
}

static
int
cmd_unmount(int nargs, char **args)
{
	char *device;

	if (nargs != 2) {
		kprintf("Usage: unmount device:\n");
		return EINVAL;
	}

	device = args[1];

	/* Allow (but do not require) colon after device name */
	if (device[strlen(device)-1]==':') {
		device[strlen(device)-1] = 0;
	}

	return vfs_unmount(device);
}

/*
 * Command to set the "boot fs". 
 *
 * The boot filesystem is the one that pathnames like /bin/sh with
 * leading slashes refer to.
 *
 * The default bootfs is "emu0".
 */
static
int
cmd_bootfs(int nargs, char **args)
{
	char *device;

	if (nargs != 2) {
		kprintf("Usage: bootfs device\n");
		return EINVAL;
	}

	device = args[1];

	/* Allow (but do not require) colon after device name */
	if (device[strlen(device)-1]==':') {
		device[strlen(device)-1] = 0;
	}

	return vfs_setbootfs(device);
}

static
int
cmd_kheapstats(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	return 0;
}

////////////////////////////////////////
//
// Menus.

static
void
showmenu(const char *name, const char *x[])
{
	int ct, half, i;

	kprintf("\n");
	kprintf("%s\n", name);
	
	for (i=ct=0; x[i]; i++) {
		ct++;
	}
	half = (ct+1)/2;

	for (i=0; i<half; i++) {
		kprintf("    %-36s", x[i]);
		if (i+half < ct) {
			kprintf("%s", x[i+half]);
		}
		kprintf("\n");
	}

	kprintf("\n");
}

static const char *opsmenu[] = {
	"[s]       Shell                     ",
	"[p]       Other program             ",
	"[mount]   Mount a filesystem        ",
	"[unmount] Unmount a filesystem      ",
	"[bootfs]  Set \"boot\" filesystem     ",
	"[pf]      Print a file              ",
	"[cd]      Change directory          ",
	"[pwd]     Print current directory   ",
	"[sync]    Sync filesystems          ",
	"[panic]   Intentional panic         ",
	"[q]       Quit and shut down        ",
	NULL
};

static
int
cmd_opsmenu(int n, char **a)
{
	(void)n;
	(void)a;

	showmenu("OS/161 operations menu", opsmenu);
	return 0;
}

static const char *testmenu[] = {
	"[at]  Array test                    ",
	"[bt]  Bitmap test                   ",
	"[km1] Kernel malloc test            ",
	"[km2] kmalloc stress test           ",
	"[tt1] Thread test 1                 ",
	"[tt2] Thread test 2                 ",
	"[tt3] Thread test 3                 ",
#if OPT_NET
	"[net] Network test                  ",
#endif
	"[sy1] Semaphore test                ",
	"[sy2] Lock test             (1)     ",
	"[sy3] CV test               (1)     ",
#ifdef UW
	"[uw1] UW lock test          (1)     ",
	"[uw2] UW vmstats test       (3)     ",
#endif // UW
	"[fs1] Filesystem test               ",
	"[fs2] FS read stress        (4)     ",
	"[fs3] FS write stress       (4)     ",
	"[fs4] FS write stress 2     (4)     ",
	"[fs5] FS create stress      (4)     ",
	NULL
};

static
int
cmd_testmenu(int n, char **a)
{
	(void)n;
	(void)a;

	showmenu("OS/161 tests menu", testmenu);
	kprintf("    (1) These tests will fail until you finish the "
		"synch assignment.\n");
	kprintf("    (4) These tests may fail until you finish the "
		"file system assignment.\n");
	kprintf("\n");

	return 0;
}

static const char *mainmenu[] = {
	"[?o] Operations menu                ",
	"[?t] Tests menu                     ",
#if OPT_SYNCHPROBS
	"[sp1] Whale Mating                  ",
#ifdef UW
	"[sp2] Cat/mouse                     ",
	"[sp3] Traffic                       ",
#endif /* UW */
#endif
	"[kh] Kernel heap stats              ",
	"[q] Quit and shut down              ",
	NULL
};

static
int
cmd_mainmenu(int n, char **a)
{
	(void)n;
	(void)a;

	showmenu("OS/161 kernel menu", mainmenu);
	return 0;
}

////////////////////////////////////////
//
// Command table.

static struct {
	const char *name;
	int (*func)(int nargs, char **args);
} cmdtable[] = {
	/* menus */
	{ "?",		cmd_mainmenu },
	{ "h",		cmd_mainmenu },
	{ "help",	cmd_mainmenu },
	{ "?o",		cmd_opsmenu },
	{ "?t",		cmd_testmenu },

	/* operations */
	{ "s",		cmd_shell },
	{ "p",		cmd_prog },
	{ "mount",	cmd_mount },
	{ "unmount",	cmd_unmount },
	{ "bootfs",	cmd_bootfs },
	{ "pf",		printfile },
	{ "cd",		cmd_chdir },
	{ "pwd",	cmd_pwd },
	{ "sync",	cmd_sync },
	{ "panic",	cmd_panic },
	{ "q",		cmd_quit },
	{ "exit",	cmd_quit },
	{ "halt",	cmd_quit },
	{ "dth",        cmd_dth },

#if OPT_SYNCHPROBS
	/* in-kernel synchronization problem(s) */
	{ "sp1",	whalemating },
#ifdef UW
	{ "sp2",	catmouse },
	{ "sp3",	traffic_simulation },
#endif /* UW */
#endif

	/* stats */
	{ "kh",         cmd_kheapstats },

	/* base system tests */
	{ "at",		arraytest },
	{ "bt",		bitmaptest },
	{ "km1",	malloctest },
	{ "km2",	mallocstress },
#if OPT_NET
	{ "net",	nettest },
#endif
	{ "tt1",	threadtest },
	{ "tt2",	threadtest2 },
	{ "tt3",	threadtest3 },
	{ "sy1",	semtest },

	/* synchronization assignment tests */
	{ "sy2",	locktest },
	{ "sy3",	cvtest },
#ifdef UW
	{ "uw1",	uwlocktest1 },
	{ "uw2",	uwvmstatstest },
#endif

	/* file system assignment tests */
	{ "fs1",	fstest },
	{ "fs2",	readstress },
	{ "fs3",	writestress },
	{ "fs4",	writestress2 },
	{ "fs5",	createstress },

	{ NULL, NULL }
};

/*
 * Process a single command.
 */
static
int
cmd_dispatch(char *cmd)
{
	time_t beforesecs, aftersecs, secs;
	uint32_t beforensecs, afternsecs, nsecs;
	char *args[MAXMENUARGS];
	int nargs=0;
	char *word;
	char *context;
	int i, result;

	for (word = strtok_r(cmd, " \t", &context);
	     word != NULL;
	     word = strtok_r(NULL, " \t", &context)) {

		if (nargs >= MAXMENUARGS) {
			kprintf("Command line has too many words\n");
			return E2BIG;
		}
		args[nargs++] = word;
	}

	if (nargs==0) {
		return 0;
	}

	for (i=0; cmdtable[i].name; i++) {
		if (*cmdtable[i].name && !strcmp(args[0], cmdtable[i].name)) {
			KASSERT(cmdtable[i].func!=NULL);

			gettime(&beforesecs, &beforensecs);

			result = cmdtable[i].func(nargs, args);

			gettime(&aftersecs, &afternsecs);
			getinterval(beforesecs, beforensecs,
				    aftersecs, afternsecs,
				    &secs, &nsecs);

			kprintf("Operation took %lu.%09lu seconds\n",
				(unsigned long) secs,
				(unsigned long) nsecs);

			return result;
		}
	}

	kprintf("%s: Command not found\n", args[0]);
	return EINVAL;
}

/*
 * Evaluate a command line that may contain multiple semicolon-delimited
 * commands.
 *
 * If "isargs" is set, we're doing command-line processing; print the
 * comamnds as we execute them and panic if the command is invalid or fails.
 */
static
void
menu_execute(char *line, int isargs)
{
	char *command;
	char *context;
	int result;

	for (command = strtok_r(line, ";", &context);
	     command != NULL;
	     command = strtok_r(NULL, ";", &context)) {

		if (isargs) {
			kprintf("OS/161 kernel: %s\n", command);
		}

		result = cmd_dispatch(command);
		if (result) {
			kprintf("Menu command failed: %s\n", strerror(result));
			if (isargs) {
				panic("Failure processing kernel arguments\n");
			}
		}
	}
}

/*
 * Command menu main loop.
 *
 * First, handle arguments passed on the kernel's command line from
 * the bootloader. Then loop prompting for commands.
 *
 * The line passed in from the bootloader is treated as if it had been
 * typed at the prompt. Semicolons separate commands; spaces and tabs
 * separate words (command names and arguments).
 *
 * So, for instance, to mount an SFS on lhd0 and make it the boot
 * filesystem, and then boot directly into the shell, one would use
 * the kernel command line
 *
 *      "mount sfs lhd0; bootfs lhd0; s"
 */

void
menu(char *args)
{
	char buf[64];

	menu_execute(args, 1);

	while (1) {
		kprintf("OS/161 kernel [? for menu]: ");
		kgets(buf, sizeof(buf));
		menu_execute(buf, 0);
	}
}
