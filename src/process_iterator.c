#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/procfs.h>

#include "process_iterator.h"

//See this link to port to other systems: http://www.steve.org.uk/Reference/Unix/faq_8.html#SEC85

#ifdef __linux__

int init_process_iterator(struct process_iterator *it, struct process_filter *filter)
{
	//open a directory stream to /proc directory
	if ((it->dip = opendir("/proc")) == NULL)
	{
		perror("opendir");
		return -1;
	}
	it->filter = filter;
	return 0;
}

static int read_process_info(pid_t pid, struct process *p)
{
	static char buffer[1024];
	static char statfile[32];
	static char exefile[1024];
	p->pid = pid;
	//read stat file
	sprintf(statfile, "/proc/%d/stat", p->pid);
	FILE *fd = fopen(statfile, "r");
	if (fd==NULL) return -1;
	if (fgets(buffer, sizeof(buffer), fd)==NULL) {
		fclose(fd);
		return -1;
	}
	fclose(fd);
	char *token = strtok(buffer, " ");
	int i;
	for (i=0; i<3; i++) token = strtok(NULL, " ");
	p->ppid = atoi(token);
	for (i=0; i<10; i++)
		token = strtok(NULL, " ");
	p->cputime = atoi(token) * HZ;
	token = strtok(NULL, " ");
	p->cputime += atoi(token) * HZ;
	for (i=0; i<7; i++)
		token = strtok(NULL, " ");
	p->starttime = atoi(token) / sysconf(_SC_CLK_TCK);
	//read command line
	sprintf(exefile,"/proc/%d/cmdline", p->pid);
	fd = fopen(statfile, "r");
	if (fgets(buffer, sizeof(buffer), fd)==NULL) {
		fclose(fd);
		return -1;
	}
	fclose(fd);
	sscanf(buffer, "%s", (char*)&p->command);
	return 0;
}

static pid_t getppid_of(pid_t pid)
{
	char statfile[20];
	char buffer[1024];
	sprintf(statfile, "/proc/%d/stat", pid);
	FILE *fd = fopen(statfile, "r");
	if (fd==NULL) return -1;
	if (fgets(buffer, sizeof(buffer), fd)==NULL) {
		fclose(fd);
		return -1;
	}
	fclose(fd);
	char *token = strtok(buffer, " ");
	int i;
	for (i=0; i<3; i++) token = strtok(NULL, " ");
	return atoi(token);
}

static int is_child_of(pid_t child_pid, pid_t parent_pid)
{
	int ppid = child_pid;
	while(ppid > 1 && ppid != parent_pid) {
		ppid = getppid_of(ppid);
	}
	return ppid == parent_pid;
}

int read_next_process(struct process_iterator *it, struct process *p)
{
	if (it->filter->pid > 0 && !it->filter->include_children)
	{
		read_process_info(it->filter->pid, p);
		return -1;
	}
	struct dirent *dit;
	//read in from /proc and seek for process dirs
	while ((dit = readdir(it->dip)) != NULL) {
		if(strtok(dit->d_name, "0123456789") != NULL)
			continue;
		p->pid = atoi(dit->d_name);
		if (it->filter->pid > 0 && it->filter->pid != p->pid && !is_child_of(p->pid, it->filter->pid)) continue;
		read_process_info(p->pid, p);
		break;
	}
	if (dit == NULL)
	{
		//end of processes
		closedir(it->dip);
		it->dip = NULL;
		return -1;
	}
	return 0;
}

int close_process_iterator(struct process_iterator *it) {
	if (it->dip != 0 && closedir(it->dip) == -1) {
		perror("closedir");
		return 1;
	}
	it->dip = NULL;
	return 0;
}

#elif defined __FreeBSD__

#include <sys/sysctl.h>
#include <sys/user.h>
#include <fcntl.h>
#include <paths.h>

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
	kvm_t *kd;
	char errbuf[_POSIX2_LINE_MAX];
	it->i = 0;
	/* Open the kvm interface, get a descriptor */
	if ((kd = kvm_openfiles(NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf)) == NULL) {
		/* fprintf(stderr, "kvm_open: %s\n", errbuf); */
		fprintf(stderr, "kvm_open: %s", errbuf);
		return -1;
	}
	/* Get the list of processes. */
	if ((it->procs = kvm_getprocs(kd, KERN_PROC_PROC, 0, &it->count)) == NULL) {
		kvm_close(kd);
		/* fprintf(stderr, "kvm_getprocs: %s\n", kvm_geterr(kd)); */
		fprintf(stderr, "kvm_getprocs: %s", kvm_geterr(kd));
		return -1;
	}
	kvm_close(kd);
    static int request[2] = { CTL_KERN, KERN_BOOTTIME };
    struct timeval result;
    size_t result_len = sizeof result;
    if (sysctl (request, 2, &result, &result_len, NULL, 0) < 0) return -1;
	it->boot_time = result.tv_sec;
	it->filter = filter;
	return 0;
}

int read_next_process(struct process_iterator *it, struct process *p) {
	if (it->i == it->count) return -1;
	p->pid = it->procs[it->i].ki_pid;
	p->ppid = it->procs[it->i].ki_ppid;
	p->cputime = it->procs[it->i].ki_runtime / 1000;
	p->starttime = it->procs[it->i].ki_start.tv_sec - it->boot_time;
	it->i++;
	if (it->i == it->count) return -1;
	return 0;
}

int close_process_iterator(struct process_iterator *it) {
	return 0;
}

#elif defined __APPLE__

int init_process_iterator(struct process_iterator *it) {
	return 0;
}

int read_next_process(struct process_iterator *it, struct process *p) {
	return -1;
}

int close_process_iterator(struct process_iterator *it) {
	return 0;
}

	// int err;
	// struct kinfo_proc *result = NULL;
	// size_t length;
	// int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};

	// /* We start by calling sysctl with result == NULL and length == 0.
	//    That will succeed, and set length to the appropriate length.
	//    We then allocate a buffer of that size and call sysctl again
	//    with that buffer.
	// */
	// length = 0;
	// err = sysctl(mib, 4, NULL, &length, NULL, 0);
	// if (err == -1) {
	// 	err = errno;
	// }
	// if (err == 0) {
	// 	result = malloc(length);
	// 	err = sysctl(mib, 4, result, &length, NULL, 0);
	// 	if (err == -1)
	// 		err = errno;
	// 	if (err == ENOMEM) {
	// 		free(result); /* clean up */
	// 		result = NULL;
	// 	}
	// }

	// i->procList = result;
	// i->count = err == 0 ? length / sizeof *result : 0;
	// i->c = 0;

// int get_proc_info(struct process *p, pid_t pid) {
// 	int err;
// 	struct kinfo_proc *result = NULL;
// 	size_t length;
// 	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};

// 	/* We start by calling sysctl with result == NULL and length == 0.
// 	   That will succeed, and set length to the appropriate length.
// 	   We then allocate a buffer of that size and call sysctl again
// 	   with that buffer.
// 	*/
// 	length = 0;
// 	err = sysctl(mib, 4, NULL, &length, NULL, 0);
// 	if (err == -1) {
// 		err = errno;
// 	}
// 	if (err == 0) {
// 		result = malloc(length);
// 		err = sysctl(mib, 4, result, &length, NULL, 0);
// 		if (err == -1)
// 			err = errno;
// 		if (err == ENOMEM) {
// 			free(result); /* clean up */
// 			result = NULL;
// 		}
// 	}

// 	p->pid = result->kp_proc.p_pid;
// 	p->ppid = result->kp_eproc.e_ppid;
// 	p->starttime = result->kp_proc.p_starttime.tv_sec;
// 	p->last_jiffies = result->kp_proc.p_cpticks;
// 	//p_pctcpu

// 	return 0;
// }

#elif defined __APPLE__

#endif