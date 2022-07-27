#include <env.h>
#include <pmap.h>
#include <printf.h>

/* Overview:
 *  Implement simple round-robin scheduling.
 *
 *
 * Hints:
 *  1. The variable which is for counting should be defined as 'static'.
 *  2. Use variable 'env_sched_list', which is a pointer array.
 *  3. CANNOT use `return` statement!
 */
/*** exercise 3.15 ***/

extern struct Env_list env_sched_list[];
extern struct Env *curenv;

void sched_yield(void)
{
	static int cur_lasttime = 1; // remaining time slices of current env
    static int cur_head_index = 0; // current env_sched_list index
    struct Env *next_env;
	int now_have = 0;
	cur_lasttime--;

	if (cur_lasttime <= 0 || curenv == NULL || curenv->env_status != ENV_RUNNABLE) {
		if (curenv != NULL) {
			LIST_REMOVE(curenv, env_sched_link);
			LIST_INSERT_TAIL(&env_sched_list[1 - cur_head_index], curenv, env_sched_link);
		}
		now_have = 0;
		while(1) {
			if (LIST_EMPTY(&env_sched_list[cur_head_index])) {
				cur_head_index = 1 - cur_head_index;
				break;
			}
			next_env = LIST_FIRST(&env_sched_list[cur_head_index]);
			if (next_env->env_status == ENV_RUNNABLE) {
				now_have = 1;
				break;
			}
			LIST_REMOVE(next_env, env_sched_link);
			LIST_INSERT_HEAD(&env_sched_list[1 - cur_head_index], next_env, env_sched_link);
		}
		if(!now_have) {
			while(1) {
				if (LIST_EMPTY(&env_sched_list[cur_head_index])) {
					panic("^^^^^^No env is RUNNABLE!^^^^^^");
				}
				next_env = LIST_FIRST(&env_sched_list[cur_head_index]);
				if (next_env->env_status == ENV_RUNNABLE) {
					now_have = 1;
					break;
				}
				LIST_REMOVE(next_env, env_sched_link);
				LIST_INSERT_HEAD(&env_sched_list[1 - cur_head_index], next_env, env_sched_link);
			}
		}
//		LIST_REMOVE(next_env, env_sched_link);
//		LIST_INSERT_TAIL(&env_sched_list[1 - cur_head_index], next_env, env_sched_link);
		cur_lasttime = next_env->env_pri;
		env_run(next_env);
		//panic("^^^^^^sched yield jump faild^^^^^^");
	}
	else {
		env_run(curenv);
	}
	//panic("^^^^^^sched yield reached end^^^^^^");
    
	/*  hint:
     *  1. if (count==0), insert `e` into `env_sched_list[1-point]`
     *     using LIST_REMOVE and LIST_INSERT_TAIL.
     *  2. if (env_sched_list[point] is empty), point = 1 - point;
     *     then search through `env_sched_list[point]` for a runnable env `e`, 
     *     and set count = e->env_pri
     *  3. count--
     *  4. env_run()
     *
     *  functions or macros below may be used (not all):
     *  LIST_INSERT_TAIL, LIST_REMOVE, LIST_FIRST, LIST_EMPTY
     */
}
