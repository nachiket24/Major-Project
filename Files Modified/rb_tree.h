#include <linux/sched.h>
#include<linux/types.h>
#include<linux/rbtree.h>

//	Strategy one
struct rbt_TLB;

struct rbt_TID
{
	struct rb_node node;
	pid_t TID;	//	KEY
    struct task_struct * ts;
	struct rbt_TLB * self_TLB;
};

struct rbt_TLB
{
	struct rb_node node;
	unsigned long long int TLB_Miss;	//	KEY
	struct rbt_TID *self_TID;	
};

struct history_node
{
	struct rb_root history_TID;
	struct rb_root history_TLB;
	int nodes_count;
};

struct group_members
{
	struct rb_root group_root;
	int members_count;
};

struct rbt_members
{
	struct rb_node node;
	u64 vruntime;	//	KEY
	struct task_struct * ts;
};
