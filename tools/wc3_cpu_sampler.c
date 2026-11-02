#define _GNU_SOURCE
#include <signal.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* Diagnostic sampler for the benchmark's simulation/render thread. A thread
 * CPU timer excludes limiter sleeps. The handler only stores instruction
 * addresses; symbols and report delivery are resolved after timing ends. */
static uintptr_t *samples;
static unsigned char *phases;
static volatile uint64_t const *owner_depth;
static volatile sig_atomic_t count, overflow;
static int capacity, sample_signal;
static timer_t sample_timer;
static struct sigaction previous_action;
static int sample_fd=-1;
static struct perf_event_mmap_page *sample_ring;
static size_t sample_ring_size;

/* Hardware sampling gives sub-millisecond instruction attribution without a
 * signal handler or a clock syscall for every production function call. */
int sample_start_cycles(int limit,int period) {
    struct perf_event_attr attr={.size=sizeof(attr),.type=PERF_TYPE_HARDWARE,
        .config=PERF_COUNT_HW_CPU_CYCLES,.sample_period=period,
        .sample_type=PERF_SAMPLE_IP,.disabled=1,.exclude_kernel=1,.exclude_hv=1};
    size_t page=sysconf(_SC_PAGESIZE),pages=1;
    if(limit<=0 || period<=0 || samples)return 0;
    while(pages*page<(size_t)limit*32)pages*=2;
    sample_fd=syscall(SYS_perf_event_open,&attr,0,-1,-1,0);
    if(sample_fd<0){perror("WC3 sampler perf_event_open");return 0;}
    sample_ring_size=(pages+1)*page;
    sample_ring=mmap(NULL,sample_ring_size,PROT_READ|PROT_WRITE,MAP_SHARED,sample_fd,0);
    if(sample_ring==MAP_FAILED){perror("WC3 sampler mmap");sample_ring=NULL;close(sample_fd);sample_fd=-1;return 0;}
    samples=calloc(limit,sizeof(*samples)+1);
    if(!samples){perror("WC3 sampler allocation");munmap(sample_ring,sample_ring_size);sample_ring=NULL;close(sample_fd);sample_fd=-1;return 0;}
    phases=(unsigned char *)(samples+limit);capacity=limit;
    if(ioctl(sample_fd,PERF_EVENT_IOC_ENABLE,0)) {
        perror("WC3 sampler enable");
        free(samples);samples=NULL;munmap(sample_ring,sample_ring_size);sample_ring=NULL;
        close(sample_fd);sample_fd=-1;return 0;
    }
    return 1;
}

static void sample_ring_copy(void *dst,uint64_t offset,size_t length) {
    size_t size=sample_ring->data_size,at=offset&(size-1),first=length;
    unsigned char const *data=(unsigned char const *)sample_ring+sample_ring->data_offset;
    if(first>size-at)first=size-at;
    memcpy(dst,data+at,first);
    if(first<length)memcpy((unsigned char *)dst+first,data,length-first);
}

static void sample_stop_cycles(void) {
    uint64_t head,tail=sample_ring->data_tail;
    if(ioctl(sample_fd,PERF_EVENT_IOC_DISABLE,0)){perror("WC3 sampler disable");overflow=1;}
    head=__atomic_load_n(&sample_ring->data_head,__ATOMIC_ACQUIRE);
    if(head-tail>sample_ring->data_size)overflow=1;
    else while(tail<head) {
        struct perf_event_header header;
        sample_ring_copy(&header,tail,sizeof(header));
        if(header.size<sizeof(header) || header.size>head-tail){overflow=1;break;}
        if(header.type==PERF_RECORD_SAMPLE && header.size==sizeof(header)+sizeof(uint64_t)) {
            if(count>=capacity){overflow=1;break;}
            uint64_t ip;sample_ring_copy(&ip,tail+sizeof(header),sizeof(ip));samples[count++]=ip;
        } else if(header.type==PERF_RECORD_LOST)overflow=1;
        tail+=header.size;
    }
    munmap(sample_ring,sample_ring_size);sample_ring=NULL;close(sample_fd);sample_fd=-1;
}


static void sample_handler(int signal, siginfo_t *info, void *context) {
    (void)signal;(void)info;
    ucontext_t const *uc = context;
    if (count >= capacity) {
        overflow = 1;
        return;
    }
#if defined(__x86_64__)
    samples[count] = uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    samples[count] = uc->uc_mcontext.pc;
#else
#error Unsupported sampler architecture
#endif
    phases[count] = owner_depth && *owner_depth != 0;
    count++;
}

int sample_start(int limit, int period_us) {
    struct sigaction action = { .sa_sigaction = sample_handler, .sa_flags = SA_SIGINFO };
    struct sigevent event = { .sigev_notify = SIGEV_THREAD_ID };
    struct itimerspec interval = {0};
    if (limit <= 0 || period_us <= 0 || samples)
        return 0;
    samples = calloc(limit, sizeof(*samples) + 1);
    if (!samples)
        return 0;
    capacity = limit;
    phases = (unsigned char *)(samples + limit);
    sample_signal = SIGRTMIN + 5;
    sigemptyset(&action.sa_mask);
    if (sigaction(sample_signal, &action, &previous_action))
        return 0;
    event.sigev_signo = sample_signal;
    event._sigev_un._tid = syscall(SYS_gettid);
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &sample_timer)) {
        sigaction(sample_signal, &previous_action, NULL);
        return 0;
    }
    interval.it_value.tv_sec = period_us / 1000000;
    interval.it_value.tv_nsec = (period_us % 1000000) * 1000;
    interval.it_interval = interval.it_value;
    if (timer_settime(sample_timer, 0, &interval, NULL)) {
        timer_delete(sample_timer);
        sigaction(sample_signal, &previous_action, NULL);
        return 0;
    }
    return 1;
}

void sample_stop(void) {
    if(sample_fd>=0){sample_stop_cycles();return;}
    /* Block delivery before deleting the timer and restoring the signal.
     * Samples stay allocated for the benchmark to read after collection. */
    sigset_t mask, previous_mask;
    struct timespec zero = {0};
    sigemptyset(&mask);
    sigaddset(&mask, sample_signal);
    sigprocmask(SIG_BLOCK, &mask, &previous_mask);
    timer_delete(sample_timer);
    while (sigtimedwait(&mask, NULL, &zero) >= 0) {}
    sigaction(sample_signal, &previous_action, NULL);
    sigprocmask(SIG_SETMASK, &previous_mask, NULL);
}

uintptr_t *sample_addresses(void) { return samples; }
unsigned char *sample_phases(void) { return phases; }
void sample_set_owner(uint64_t const *depth) { owner_depth = depth; }
int sample_count(void) { return count; }
int sample_overflow(void) { return overflow; }
