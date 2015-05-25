Subject			:	TLB miss aware Linux scheduler for multicore SMT architecture

Guide			:	Mr. Saidalavi Kalady  

Team members	:	1.	Nitish Kumar Gupta
					2.	Vivek Kumar Gopalani
					3.	Nachiket C. Chaudhary
					
***   About project   ***

-	We have implemented two scheduling policy in Linux 3.19.1 kernel
	Corresponding code can be found out in 'strategy_1.c' and 'strategy_2.c'
	
-	For inseting a particular module, corresponding changes need to be made in 'Makefile'    

-	Some changes have also been made to following files
	1.	/linux-3.19.1/include/linux/sched.h
	2.	/linux-3.19.1/kernel/sched/core.c
	3.	/linux-3.19.1/kernel/sched/fair.c
	4.	/linux-3.19.1/kernel/fork.c
	5.	/linux-3.19.1/kernel/exit.c
	
	Modifications made in above files can be searched using keyword 'modification'
	
-	Some files were also added
	1.	/linux-3.19.1/include/linux/rb_tree.h	
	
-	Steps to use modules
	1.	Keep Makefile and corresponding module in same directory
	2.	Make corresponding changes in Makefile
	3.	To insert use:
		sudo insmod ./strategy_1.ko
		OR
		sudo insmod ./strategy_2.ko
	4.	For removing the inserted module:
		sudo rmmod strategy_1
		OR
		sudo rmmod strategy_2
