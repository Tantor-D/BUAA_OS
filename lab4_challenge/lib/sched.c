#include <env.h>
#include <pmap.h>
#include <printf.h>

extern struct Env_list env_sched_list[];
extern struct Env *curenv;
extern struct Tcb *curtcb;
/*void sched_yield(void)
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

}*/

void sched_yield(void)
{
	static int times = 0;
	struct Env *e = curenv;
	struct Tcb *t = curtcb;
	static int sched_i = 0;
	if (!t) {
		//printf("in 1\n");
		while (LIST_EMPTY(&tcb_sched_list[sched_i])) {
			sched_i ^= 1;
		//	printf("empty1!\n");
		}
		t = LIST_FIRST(&tcb_sched_list[sched_i]);
		times = t->tcb_pri;
		times -= 1;
		//printf("now tcb is 0x%x\n",t->thread_id);
		//printf("now t.pc is 0x%x\n",t->tcb_tf.pc);
		env_run(t);
	} else if (t->tcb_status != ENV_RUNNABLE) {
		//printf("in 2, tcbid is 0x%x\n",t->thread_id);
		while (LIST_EMPTY(&tcb_sched_list[sched_i])) {
			sched_i ^= 1;
		//	printf("empty2!\n");
		}
		t = LIST_FIRST(&tcb_sched_list[sched_i]);
		times = t->tcb_pri;
		times -= 1;
		//printf("now threadid is 0x%x\n",t->thread_id);
		env_run(t);
	}
	if (times <= 0) {
		//printf("in 3, tcbid is 0x%x\n",t->thread_id);
		LIST_REMOVE(t,tcb_sched_link);
		LIST_INSERT_HEAD(&tcb_sched_list[sched_i^1],t,tcb_sched_link);
		while (LIST_EMPTY(&tcb_sched_list[sched_i])) {
			sched_i ^= 1;
			//printf("empty3!\n");
		}
		t = LIST_FIRST(&tcb_sched_list[sched_i]);
		times = t->tcb_pri;
		times -= 1;
		//printf("now thread id is 0x%x\n",t->thread_id);
		env_run(t);
	} else {
		//printf("in 4, tcbid is 0x%x\n",t->thread_id);
		times -= 1;
		env_run(t);
	}
}



