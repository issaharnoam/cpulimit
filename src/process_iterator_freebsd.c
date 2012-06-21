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
	it->filter = filter;
	return 0;
}

static void kproc2proc(struct kinfo_proc *kproc, struct process *proc)
{
	proc->pid = kproc->ki_pid;
	proc->ppid = kproc->ki_ppid;
	proc->cputime = kproc->ki_runtime / 1000;
	proc->starttime = kproc->ki_start.tv_sec;
}

static int get_single_process(pid_t pid, struct process *process)
{
	kvm_t *kd;
	int count;
	char errbuf[_POSIX2_LINE_MAX];
	/* Open the kvm interface, get a descriptor */
	if ((kd = kvm_openfiles(NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf)) == NULL) {
		/* fprintf(stderr, "kvm_open: %s\n", errbuf); */
		fprintf(stderr, "kvm_open: %s", errbuf);
		return -1;
	}
	struct kinfo_proc *kproc = kvm_getprocs(kd, KERN_PROC_PID, pid, &count);
	kvm_close(kd);
	if (count == 0 || kproc == NULL)
	{
		fprintf(stderr, "kvm_getprocs: %s", kvm_geterr(kd));
		return -1;
	}
	kproc2proc(kproc, process);
	return 0;
}

int get_next_process(struct process_iterator *it, struct process *p) {
	if (it->i == it->count)
	{
		return -1;
	}
	if (it->filter->pid > 0 && !it->filter->include_children)
	{
		get_single_process(it->filter->pid, p);
		it->i = it->count = 1;
		return 0;
	}
	while (it->i < it->count)
	{
		if (it->filter->pid > 0 && it->filter->include_children)
		{
			kproc2proc(&(it->procs[it->i]), p);
			it->i++;
			if (p->pid != it->filter->pid && p->ppid != it->filter->pid)
				continue;
			return 0;
		}
		else if (it->filter->pid == 0)
		{
			kproc2proc(&(it->procs[it->i]), p);
			it->i++;
			return 0;
		}
	}
	return -1;
}

int close_process_iterator(struct process_iterator *it) {
	return 0;
}
