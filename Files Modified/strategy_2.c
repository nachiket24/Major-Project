//	Libraries
#include 	<linux/module.h>
#include 	<linux/slab.h>
#include 	<linux/init.h>
#include    <linux/pid.h>
#include 	<linux/sched.h>
#include 	<linux/percpu-defs.h>
#include    <linux/fs.h>		// for basic filesystem
#include    <linux/proc_fs.h>	// for the proc filesystem
#include    <linux/seq_file.h>	// for sequence files
#include    <linux/jiffies.h>	// for jiffies
#include    <linux/spinlock.h>
#define     cpu_rq(cpu)             (&per_cpu(runqueues, (cpu)))

//	Licence
MODULE_LICENSE("GPL");

//	MSR functions
static void set_up(void);
static void reset_counter(void);
static unsigned long long int read_counter(void);

struct sched_entity * compare_threads_fun (struct task_struct * cfs_ts , struct task_struct * sib_ts);
struct task_struct * get_thread_from_members ( struct sched_entity * cfs_se);

//  Validation functions
void set_valid (int index);
void set_invalid_fun(struct task_struct * ts);
int  get_index (pid_t tid);
extern struct pid_namespace * task_active_pid_ns (struct task_struct *);

//  dummy/kernel functions
    //  In fair.c
    static struct sched_entity * ( * compare_threads_cached ) (struct task_struct * cfs_ts , struct task_struct * sib_ts);
    extern struct sched_entity * ( * compare_threads_kernel ) (struct task_struct * cfs_ts , struct task_struct * sib_ts);
    //  In fork.c
    static void (*insert_members_data_cached)(struct task_struct * group_leader, struct task_struct * member);
    extern void (*insert_members_data_kernel)(struct task_struct * group_leader, struct task_struct * member);

#	define THRESHOLD 	  1000	    //	vruntime difference
# 	define MAX_CPU	      8		    //	number of cores
#	define MAX_NODES	  100	    //	number of nodes per tree
#   define MAX_MEMBERS    9 
#	define MAX_MISS       50000	    //	maximum eligible MISS count to be added to tree
#   define MAX_THREADS    100000    //  maximum threads that can run in total              
#   define true           1
#   define false          0
#   define bool           int       //  REMEMBER !!!

spinlock_t SMT_WQlock;
spinlock_t SMT_RQlock;
spinlock_t SIB_WQlock;
spinlock_t SIB_RQlock;
spinlock_t SIBI_WQlock;
spinlock_t SIBI_RQlock;
spinlock_t S1_WQlock;
spinlock_t S1_RQlock;
spinlock_t VALIDITY_WQlock;
spinlock_t VALIDITY_RQlock;

unsigned long long int validity [(MAX_THREADS / 64) + 1];
static struct proc_dir_entry* sched_debug_file;
static int countt   =   0;
int debug_counter   =   0;

struct sched_entity * compare_threads_fun (struct task_struct * cfs_ts , struct task_struct * sib_ts)
{
	struct task_struct * history_thread = NULL;

	history_thread  =   get_thread_from_members (&current->se);

    if (history_thread == NULL)
        return NULL;
    else
    {
        if (cfs_ts->on_rq != history_thread->on_rq)
            return NULL;

        if (  ((history_thread->se).vruntime - (cfs_ts->se).vruntime) < THRESHOLD ) 
        {
            return (&(history_thread->se));
        }
    }	
    return NULL;
}

struct task_struct * get_thread_from_members ( struct sched_entity * cfs_se )
{
    struct task_struct * temp = NULL, * temp1=NULL , *temp2=NULL , *temp3=NULL;
    int i=0,j=0;
    
    temp1 = container_of(cfs_se, struct task_struct, se);
    
    if (temp1 == NULL)
        return NULL;
    
    if ( (temp1->pid == temp1->tgid) )
        return NULL;
    
    if ((temp1->group_leader == NULL) || (temp1 == temp1->group_leader))
        return NULL;

    temp2 = pid_task(find_vpid(get_index(temp1->tgid) ),PIDTYPE_PID);
    
    if (temp2 == NULL)
    {   
        //spin_unlock(&S1_WQlock); 
        return NULL;
    }
    else if (temp2->se.child_count == 0)
    {
        //spin_unlock(&S1_WQlock); 
        return NULL;
    }
    else
    {        
        if (get_index(temp2->se.chldn[0]) > 0)    
        temp = pid_task(find_vpid(get_index(temp2->se.chldn[0]) ),PIDTYPE_PID);
        
        for (i=1;i<MAX_MEMBERS;i++)
        {
            temp3=NULL;
            if (get_index(temp2->se.chldn[i]) > 0)
            temp3 = pid_task(find_vpid( get_index( temp2->se.chldn[i] ) ),PIDTYPE_PID);

            if ( (temp3 != NULL ) && (temp!=NULL) && (temp3->se.vruntime < temp->se.vruntime))
                temp = temp3;
        }
    
        if (temp != NULL && (temp2->tgid == temp->tgid) )
        {
            return temp;
        }
        else
        {
            return NULL;
        }
    }

    return NULL;
}

void insert_members_data_fun (struct task_struct * group_leader , struct task_struct * member )
{
    int i = 0;
    if( (member->pid != member->tgid) && (member->tgid == group_leader->tgid) )
    {
        if (group_leader->se.child_count >= MAX_MEMBERS)
            return;

        spin_lock(&SIBI_WQlock); 
        spin_lock(&SIBI_RQlock); 

            if (group_leader->se.child_count == 0)
            {
                for (i=0 ; i < MAX_MEMBERS ; i++)
                    group_leader->se.chldn[i] = -1;        
            }
          
            group_leader->se.chldn[group_leader->se.child_count] = member->pid;
            
            group_leader->se.members_root.members_count++;
            group_leader->se.child_count++;

        spin_unlock(&SIBI_WQlock);
        spin_unlock(&SIBI_RQlock);      
    }
    else
        return;
}

void set_valid (int index)
{
	unsigned long long temp=1;

    spin_lock(&VALIDITY_WQlock); 
    spin_lock(&VALIDITY_RQlock); 

	   validity[index/64]  = validity[index/64] | (temp<<(index%64));
	
    spin_unlock(&VALIDITY_WQlock); 
    spin_unlock(&VALIDITY_RQlock);
}

int get_index (pid_t tid)
{
    return (int)tid;
}

static void set_up()
{
	int reg_addr	=	0x186; 		// 	EVTSEL0
	int event_no	=	0x0024;		//	L2 cache miss
	int umask	=       0x3F00;		// 	Demand data requests that missed L2 cache
	int enable_bits	=	0x430000; 	// 	All the other control bits
	int event=event_no | umask | enable_bits;
    
    debug_counter = 0101;

	__asm__ ("wrmsr" : : "c"(reg_addr), "a"(event), "d"(0x00));
	
	__asm__ ("wrmsr" : : "c"(0xC1), "a"(0x00), "d"(0x00));
	
	return;
}

static unsigned long long int read_counter()
{
	long long int count;
	long int eax_low, edx_high;
    int reg_addr=0xC1;
    debug_counter = 5;
	__asm__("rdmsr" : "=a"(eax_low), "=d"(edx_high) : "c"(reg_addr));
	count = ((long int)eax_low | (unsigned long long int)edx_high << 32);

	return count;
}

void reset_counter(void)
{
	debug_counter = 6;
	__asm__ ("wrmsr" : : "c"(0xC1), "a"(0x00), "d"(0x00));
}

int __init init_module(void)
{
	set_up();
    
    printk(KERN_ALERT "Init     =   %llu\n",read_counter());
    spin_lock_init(&SMT_WQlock);
	spin_lock_init(&SMT_RQlock);
    spin_lock_init(&SIB_WQlock);
    spin_lock_init(&SIB_RQlock);
    spin_lock_init(&SIBI_WQlock);
    spin_lock_init(&SIBI_RQlock);
    spin_lock_init(&S1_WQlock);
    spin_lock_init(&S1_RQlock);
    spin_lock_init(&VALIDITY_WQlock);
    spin_lock_init(&VALIDITY_RQlock);
	
    compare_threads_cached  =   compare_threads_kernel;
    compare_threads_kernel  =   & compare_threads_fun;
    
    insert_members_data_cached =   insert_members_data_kernel;
    insert_members_data_kernel =   & insert_members_data_fun ;

    return 0;
}

void __exit cleanup_module(void)
{
	debug_counter = 8;
	printk(KERN_ALERT "Exit	   =	%llu\n",read_counter());
	
    compare_threads_kernel  =   compare_threads_cached;

    insert_members_data_kernel  =   insert_members_data_cached;
	
    return;
}



