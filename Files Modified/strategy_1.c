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
#define cpu_rq(cpu)             (&per_cpu(runqueues, (cpu)))

MODULE_LICENSE("GPL");

//	MSR functions
static void set_up(void);
static void reset_counter(void);
static unsigned long long int read_counter(void);

//	Operate on the TREE

void insert_TID(struct rb_root * hroot , struct rbt_TID * data);
void insert_TLB(struct rb_root * hroot , struct rbt_TLB * data);
struct rbt_TID * search_TID (struct rb_root * tree , pid_t TID);

//  Algorithm
   	struct sched_entity * compare_threads_fun (struct task_struct * cfs_ts , struct task_struct * sib_ts);
	void insert_miss_data_fun (struct task_struct * ts_1 , struct task_struct * ts_2);
	struct task_struct * get_thread_from_history ( struct sched_entity * sib_se);

//  Validation functions
void set_valid (int index);
void set_invalid_fun(struct task_struct * ts);
int  get_index (pid_t tid);
int check_validation (pid_t tid);

//  dummy/kernel functions
    //  For core.c
static void (*insert_miss_data_cached)(struct task_struct *ts_1, struct task_struct *ts_2);
extern void (*insert_miss_data_kernel)(struct task_struct *ts_1, struct task_struct *ts_2);
    //  In fair.c
static struct sched_entity * ( * compare_threads_cached ) (struct task_struct * cfs_ts , struct task_struct * sib_ts);
extern struct sched_entity * ( * compare_threads_kernel ) (struct task_struct * cfs_ts , struct task_struct * sib_ts);
    //  In exit.c
static void (*set_invalid_cached)(struct task_struct * ts);
extern void (*set_invalid_kernel)(struct task_struct * ts);

#	define THRESHOLD 	  10	    //	vruntime difference
# 	define MAX_CPU	      8		    //	number of cores
#	define MAX_NODES	  30	    //	number of nodes per tree
#	define MAX_MISS       50000	    //	maximum eligible MISS count to be added to tree
#   define MAX_THREADS    100000    //  maximum threads that can run in total              
#   define true           1
#   define false          0
#   define bool           int       //  REMEMBER !!!

long long int Times = 0;
spinlock_t SMT_WQlock;
spinlock_t SMT_RQlock;
spinlock_t SIB_WQlock;
spinlock_t SIB_RQlock;

unsigned long long int validity [(MAX_THREADS / 64) + 1];
static struct proc_dir_entry* sched_debug_file;
static int countt   =   0;
int debug_counter   =   0;

//  Choose between CFS and our ALGORITHM
struct sched_entity * compare_threads_fun (struct task_struct * cfs_ts , struct task_struct * sib_ts)
{

	struct task_struct * history_thread;
	
	history_thread  =   get_thread_from_history (&(sib_ts->se));

	
	if (history_thread == NULL)
	{
		return NULL;
	}
	else
	{

		if (cfs_ts->on_rq != history_thread->on_rq)
			return NULL;

		if ( history_thread->pid != cfs_ts->pid && ((history_thread->se).vruntime - (cfs_ts->se).vruntime) < 1000 ) 
		{
			return (&(history_thread->se));
		}
	}	

	return NULL;
}

struct task_struct * get_thread_from_history ( struct sched_entity * sib_se )
{
	struct task_struct * temp;
	struct rbt_TLB *data;
	struct rb_node * root;

	root = rb_first(&( (sib_se->history_root).history_TLB));

	if (root == NULL)
	{	
		return NULL;          
	}
	data = container_of(root, struct rbt_TLB, node);

	if (data == NULL)
	{	
		return NULL;          
	}

	temp = (data->self_TID)->ts;
	
	
	if (temp != NULL && temp->state == 0)
	{	
		return temp;	
	}
	else
	{
		return NULL;
	}    
}

void insert_miss_data_fun (struct task_struct * ts_1 , struct task_struct * ts_2 )
{
	struct rbt_TID * _2in_1, *_1in_2 ;
	unsigned long long int TLB_Miss;

	_2in_1 		= 	search_TID(&((ts_1->se).history_root.history_TID) , ts_2->pid );

	TLB_Miss	= 	read_counter();
	reset_counter(); 
	
	if ( _2in_1 == NULL) 
	{
		if ( (ts_1->se).history_root.nodes_count <= MAX_NODES)
		{
			struct rbt_TID * temp1;
			struct rbt_TLB * temp2;

			temp1  =   (struct rbt_TID *) kmalloc(sizeof(struct rbt_TID), GFP_ATOMIC);
			temp2  =   (struct rbt_TLB *) kmalloc(sizeof(struct rbt_TLB), GFP_ATOMIC);

			if(!temp1 || !temp2)
				return;

			temp1->TID      =   ts_2->pid;
			temp1->ts       =   ts_2;
			temp1->self_TLB =   temp2;

			temp2->TLB_Miss =   TLB_Miss;
			temp2->self_TID =   temp1;    
			
			insert_TID(&((ts_1->se).history_root.history_TID), temp1);
			insert_TLB(&((ts_1->se).history_root.history_TLB), temp2);
			(ts_1->se).history_root.nodes_count++;
			
		}
	}
	else 
	{       
		if(_2in_1->self_TLB && _2in_1->self_TLB->TLB_Miss*10<TLB_Miss)
		{
			struct rbt_TLB * temp2, *t;
			t=_2in_1->self_TLB;
			
			rb_erase(&(t->node), &((ts_1->se).history_root.history_TLB));
			t->TLB_Miss             =   TLB_Miss;
			insert_TLB( &((ts_1->se).history_root.history_TLB), t);        		
		}		
	}
}

struct rbt_TID * search_TID (struct rb_root * root, pid_t TID)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct rbt_TID *data = container_of(node, struct rbt_TID, node);

		if (TID < (data->TID))
			node = node->rb_left;
		else if (TID > (data->TID))
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

void insert_TID(struct rb_root *root, struct rbt_TID *data)
{

	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct rbt_TID * this = container_of(*new, struct rbt_TID, node);

		parent = *new;

		if (this->TID >= data->TID)
			new = &((*new)->rb_left);
		else if (this->TID < data->TID)
			new = &((*new)->rb_right);
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

void insert_TLB(struct rb_root *root, struct rbt_TLB *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct rbt_TLB * this = container_of(*new, struct rbt_TLB, node);

		parent = *new;

		if (this->TLB_Miss >= data->TLB_Miss)
			new = &((*new)->rb_left);
		else if (this->TLB_Miss < data->TLB_Miss)
			new = &((*new)->rb_right);
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

void set_valid (int index)
{
	unsigned long long temp=1;
	validity[index/64]  = validity[index/64] | (temp<<(index%64));	
}

struct rbt_TID * dum = NULL;

void deleteTree (struct rb_node * node) 
{
	if (node == NULL) 
		return;

	deleteTree(node->rb_left);
	deleteTree(node->rb_right);

	dum = container_of(node, struct rbt_TID ,node);

    kfree(dum->self_TLB);            //  TLB
    kfree(dum);                      //  TID
} 

void set_invalid_fun(struct task_struct * ts)
{
	int index = get_index(ts->pid);
	unsigned long long temp=1;
	
	struct rb_node * temp1 = ( (ts->se).history_root.history_TID.rb_node);

	if (temp1 != NULL)
	{
		spin_lock(&SMT_WQlock); 
		spin_lock(&SMT_RQlock); 

		deleteTree(temp1);

		(ts->se).history_root.history_TID=RB_ROOT;
		(ts->se).history_root.history_TLB=RB_ROOT;

		spin_unlock(&SMT_WQlock); 
		spin_unlock(&SMT_RQlock); 
	}
}

int get_index (pid_t tid)
{

	return (int)tid;
}

bool check_validation (pid_t tid)
{
	int index =get_index(tid);
	unsigned long long temp=1;
	temp =validity[index/64] & temp<<index%64;

	if(temp==0)
		return false;
	else
		return true;
}

static void set_up()
{
	int reg_addr	=	0x186; 		// 	EVTSEL0
	int event_no	=	0x08;		//	L2 cache miss
	int umask	=       0x01;		// 	Demand data requests that missed L2 cache
	int enable_bits	=	0x430000; 	// 	All the other control bits
	int event=event_no | umask | enable_bits;

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
    //  unsigned long       =   32
    //  unsigned long long  =   64
	return count;
}

void reset_counter(void)
{
	__asm__ ("wrmsr" : : "c"(0xC1), "a"(0x00), "d"(0x00));
}

int __init init_module(void)
{
	printk(KERN_ALERT "NCC  Counter	=	%llu\n",read_counter());

	set_up();

	printk(KERN_ALERT "Init Counter	=	%llu\n",read_counter());

	spin_lock_init(&SMT_WQlock);
	spin_lock_init(&SMT_RQlock);
	spin_lock_init(&SIB_WQlock);
	spin_lock_init(&SIB_RQlock);
	
	insert_miss_data_cached =   insert_miss_data_kernel;
	insert_miss_data_kernel =   & insert_miss_data_fun ;

	compare_threads_cached  =   compare_threads_kernel;
	compare_threads_kernel  =   & compare_threads_fun;

	set_invalid_cached      =   set_invalid_kernel;
	set_invalid_kernel      =   & set_invalid_fun;

	return 0;
}

void __exit cleanup_module(void)
{
	debug_counter = 8;
	printk(KERN_ALERT "Exit Counter	=	%llu\n",read_counter());

	insert_miss_data_kernel =   insert_miss_data_cached;
	compare_threads_kernel  =   compare_threads_cached;
	set_invalid_kernel      =   set_invalid_cached ;

	return;
}