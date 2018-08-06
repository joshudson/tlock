/*
 * automatically lock terminal on timeout
 *
 * Copyright (C) Joshua Hudson 2018
 *
 * Use this software for good, not evil.
 *
 * Good and evil are objective.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

static volatile int pending = 0;

static void handle_hup(int ignored)
{
	_exit(0); /* terminal is closing down */
}

static void handle_ttin(int ignored)
{
	setpgid(getpid(), tcgetpgrp(0));
}

static void handle_abrt(int ignored)
{
	pending = 1;
}

static pid_t getdaemonprocess(void)
{
	char name[34];
	dev_t devzero;
	ino_t inozero;
	struct stat statbuf;
	dev_t devexe;
	ino_t inoexe;
	if (stat("/proc/self/exe", &statbuf)) {
		perror("/proc/self/exe");
		exit(3);
	}
	devexe = statbuf.st_dev;
	inoexe = statbuf.st_ino;
	if (fstat(0, &statbuf)) {
		perror("[0]");
		exit(3);
	}
	devzero = statbuf.st_dev;
	inozero = statbuf.st_ino;
	DIR *dir = opendir("/proc");
	if (!dir) {
		perror("/proc");
		_exit(3);
	}
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] > '0' && entry->d_name[0] <= '9' && strlen(entry->d_name) < 21) {
			sprintf(name, "/proc/%s/exe", entry->d_name);
			if (!stat(name, &statbuf) && statbuf.st_dev == devexe && statbuf.st_ino == inoexe) {
				sprintf(name, "/proc/%s/fd/0", entry->d_name);
				if (!stat(name, &statbuf) && statbuf.st_dev == devzero && statbuf.st_ino == inozero) {
					pid_t value = atoi(entry->d_name); /* no use after free */
					closedir(dir);
					return value;
				}
			}
		}
	}
	closedir(dir);
	return 0;
}

int main(int argc, char **argv)
{
	const char *name = argv[0];
	if (!argv[1]) {
		pid_t service = getdaemonprocess();
		if (service == 0) {
			fprintf(stderr, "%s daemon not running: run lock command yourself.\n", name);
			return 2;
		}
		if (kill(service, SIGABRT)) {
			perror(name);
			return 2;
		}
		return 0;
	} else if (argv[1][0] == '-' && argv[1][1] == 'k') {
		pid_t service = getdaemonprocess();
		if (service > 0 && kill(service, SIGTERM)) {
			perror(name);
			return 2;
		}
		return 0; /* already not running is not an error */
	}
	char k = 0;
	int tsecs = 10 * 60;
	struct sigaction sighup;
	pid_t owner = getppid();
	sighup.sa_handler = handle_hup;
	sigemptyset(&sighup.sa_mask);
	sighup.sa_flags = 0;
	sigaction(SIGHUP, &sighup, NULL);
	struct sigaction sigttin;
	sigttin.sa_handler = handle_ttin;
	sigemptyset(&sigttin.sa_mask);
	sigttin.sa_flags = 0;
	sigaction(SIGTTIN, &sigttin, NULL);
	sigaction(SIGTTOU, &sigttin, NULL);
	struct sigaction sigabrt;
	sigabrt.sa_handler = handle_abrt;
	sigemptyset(&sigabrt.sa_mask);
	sighup.sa_flags = 0;
	sigaction(SIGABRT, &sigabrt, NULL);
	sigset_t mask;
	sigset_t origmask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, &origmask);
	
	if (argv[1] && argv[1][0] == '-' && argv[1][1] == 'l') {
		k = 1;
		++argv;
	}
	if (argv[1] && argv[1][0] >= '0' && argv[1][0] <= '9') {
		tsecs = atoi(argv[1]);
		++argv;
	}
	if (argv[1] && argv[1][0] == '-' && argv[1][1] == '-') {
		++argv;
	}
	if (!argv[1]) {
		fprintf(stderr, "Usage: %s [[-l] [timeout] [--] command [arguments]|-k]\n", argv[0]);
		return 1;
	}
	if (fork() > 0) _exit(0);
	struct timespec tv;
	for (;;) {
		char ibuf = 1;
		tv.tv_sec = tsecs;
		tv.tv_nsec = 0;
		int n = pselect(1, (fd_set *)&ibuf, NULL, NULL, &tv, &origmask);
		if (n == 0 || pending) {
			pending = 0;
			kill(owner, SIGSTOP);
			sleep(1);
			pid_t pgid = tcgetpgrp(0);
			pid_t pid;
			struct termios tcsavedattr;
			struct termios tcnewattr;
			volatile int error = 0;
			tcgetattr(0, &tcsavedattr);
			tcnewattr = tcsavedattr;
			tcnewattr.c_iflag = tcnewattr.c_iflag & ~(tcflag_t)(IGNCR | INLCR) | (tcflag_t)(ICRNL);
			tcnewattr.c_lflag = tcnewattr.c_lflag | (tcflag_t)(ICANON | ECHO);
			tcnewattr.c_oflag = tcnewattr.c_oflag | (tcflag_t)(ONLCR);
			if ((pid = vfork()) == 0) {
				pid_t npgid = getpid();
				if (setpgid(npgid, npgid)) {
					perror("setpgid");
				}
				pid_t pid2;
				int junk;
				if ((pid2 = vfork()) == 0) {
					setpgid(getpid(), pgid);
					if (tcsetpgrp(0, npgid)) {
						perror("tcsetpgrp");
					}
					_exit(0);
				}
				if (pid2 > 0) waitpid(pid2, &junk, 0);
				tcsetattr(0, TCSANOW, &tcnewattr);
				error = -1;
				errno = 0;
				if ((pid2 = vfork()) == 0) {
					execvp(argv[1], argv + 1);
					error = errno;
					_exit(0);
				}
				if (error == -1 && pid2 > 0) errno = 0; /* BUG! execvp trashes errno */
				int error2 = error;
				if (error2 > 0) errno = error2;
				if (errno) {
					perror(argv[1]);
					if (k) {
						kill(owner, 9);
						kill(0, 9);
						_exit(0); /* should never get here */
					}
				}
				if (pid2 > 0) waitpid(pid2, &junk, 0);
				if (tcsetattr(0, TCSANOW, &tcsavedattr)) {
					perror("tcsetattr");
				}
				if (tcsetpgrp(0, pgid)) {
					perror("tcsetpgrp");
				}
				_exit(0);
			}
			if (pid > 0) waitpid(pid, &n, 0);
			kill(owner, SIGCONT);
		}
	}
}
