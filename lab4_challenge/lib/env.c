/* Notes written by Qian Liu <qianlxc@outlook.com>
  If you find any bug, please contact with me.*/

#include <mmu.h>
#include <error.h>
#include <env.h>
#include <kerelf.h>
#include <sched.h>
#include <pmap.h>
#include <printf.h>

struct Env *envs = NULL;   // All environments
struct Env *curenv = NULL; // the current env
struct Tcb *curtcb = NULL;

static struct Env_list env_free_list; // Free list
struct Env_list env_sched_list[2];    // Runnable list
struct Tcb_list tcb_sched_list[2];

extern Pde *boot_pgdir;
extern char *KERNEL_SP;
static u_int asid_bitmap[2] = {0};

static u_int asid_alloc()
{
    int i, index, inner;
    for (i = 0; i < 64; ++i)
    {
        index = i >> 5;
        inner = i & 31;
        if ((asid_bitmap[index] & (1 << inner)) == 0)
        {
            asid_bitmap[index] |= 1 << inner;
            return i;
        }
    }
    panic("too many processes!");
}

/* Overview:
 *  When a process is killed, free its ASID
 *
 * Post-Condition:
 *  ASID is free and can be allocated again later
 */
static void asid_free(u_int i)
{
    int index, inner;
    index = i >> 5;
    inner = i & 31;
    asid_bitmap[index] &= ~(1 << inner);
}

u_int mkenvid(struct Env *e)
{
    u_int idx = e - envs;
    u_int asid = asid_alloc();
    return (asid << (1 + LOG2NENV)) | (1 << LOG2NENV) | idx;
}

u_int mktcbid(struct Tcb *t)
{
    struct Env *e = t->tcb_parent_env;
    u_int idx = t - (e->env_threads);
    // printf("in mkTcbId: e = %x\n", e);
    // printf("in mkTcbId: idx = %d\n", idx);
    return ((e->env_id << 3) | idx);
}

int threadid2tcb(u_int threadid, struct Tcb **ptcb)
{
    struct Tcb *t;
    struct Env *e;

    if (threadid == 0)
    {
        *ptcb = curtcb;
        return 0;
    }

    e = envs + ENVX(threadid >> 3);
    t = &e->env_threads[TCBX(threadid)];

    if (t->tcb_status == ENV_FREE || t->thread_id != threadid)
    {
        *ptcb = 0;
        return -E_BAD_ENV;
    }
    *ptcb = t;
    return 0;
}

int envid2env(u_int envid, struct Env **penv, int checkperm)
{
    struct Env *e;
    /* Hint: If envid is zero, return curenv.*/
    /* Step 1: Assign value to e using envid. */
    if (envid == 0)
    {
        *penv = curenv;
        return 0;
    }
    e = envs + ENVX(envid);
    if (e->env_id != envid)
    {
        *penv = 0;
        return -E_BAD_ENV;
    }
    /* Hints:
     *  Check whether the calling env has sufficient permissions
     *    to manipulate the specified env.
     *  If checkperm is set, the specified env
     *    must be either curenv or an immediate child of curenv.
     *  If not, error! */
    /*  Step 2: Make a check according to checkperm. */

    if (checkperm)
    {
        if (e != curenv && e->env_parent_id != curenv->env_id)
        {
            *penv = 0;
            return -E_BAD_ENV;
        }
    }

    *penv = e;
    return 0;
}


void env_init(void)
{
    int i;
    /* Step 1: Initialize env_free_list. */
    LIST_INIT(&env_free_list);
    LIST_INIT(&tcb_sched_list[0]);
    LIST_INIT(&tcb_sched_list[1]);

    /* Step 2: Traverse the elements of 'envs' array,
     *   set their status as free and insert them into the env_free_list.
     * Choose the correct loop order to finish the insertion.
     * Make sure, after the insertion, the order of envs in the list
     *   should be the same as that in the envs array. */
    for (i = NENV - 1; i >= 0; i--)
    {
        // envs[i].env_status = ENV_FREE;
        LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
    }
}

/* Overview:
 *  Initialize the kernel virtual memory layout for 'e'.
 *  Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
 *    and initialize the kernel portion of the new env's address space.
 *  DO NOT map anything into the user portion of the env's virtual address space.
 */
/*** exercise 3.4 ***/
static int
env_setup_vm(struct Env *e)
{

    int i, r;
    struct Page *p = NULL;
    Pde *pgdir;

    /* Step 1: Allocate a page for the page directory
     *   using a function you completed in the lab2 and add its pp_ref.
     *   pgdir is the page directory of Env e, assign value for it. */
    r = page_alloc(&p);
    if (r != 0)
    {
        panic("env_setup_vm - page alloc error\n");
        return r;
    }

    p->pp_ref++;
    pgdir = (Pde *)page2kva(p);

    /* Step 2: Zero pgdir's field before UTOP. */
    for (i = 0; i < PDX(UTOP); i++)
    {
        pgdir[i] = 0;
    }

    /* Step 3: Copy kernel's boot_pgdir to pgdir. */

    /* Hint:
     *  The VA space of all envs is identical above UTOP
     *  (except at UVPT, which we've set below).
     *  See ./include/mmu.h for layout.
     *  Can you use boot_pgdir as a template?
     */
    for (i = PDX(UTOP); i < PTE2PT; i++)
    {
        if (i != PDX(VPT) && i != PDX(UVPT))
        {
            pgdir[i] = boot_pgdir[i];
        }
    }

    e->env_pgdir = pgdir;
    e->env_cr3 = PADDR(pgdir);

    /* UVPT maps the env's own page table, with read-only permission.*/
    e->env_pgdir[PDX(VPT)] = e->env_cr3;
    e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_V;
    return 0;
}

int env_alloc(struct Env **new, u_int parent_id)
{
    int r;
    struct Env *e;
    struct Tcb *t;

    /* Step 1: Get a new Env from env_free_list*/
    if (LIST_EMPTY(&env_free_list))
    {
        *new = NULL;
        return -E_NO_FREE_ENV;
    }
    e = LIST_FIRST(&env_free_list);

    /* Step 2: Call a certain function (has been completed just now) to init kernel memory layout for this new Env.
     *The function mainly maps the kernel address to this new Env address. */
    r = env_setup_vm(e);
    if (r < 0)
        return r;

    /* Step 3: Initialize every field of new Env with appropriate values.*/
    e->env_id = mkenvid(e);
    e->env_parent_id = parent_id;
    // e->env_runs = 0;
    // e->env_status = ENV_RUNNABLE;
    r = thread_alloc(e, &t);
    if (r < 0)
        return r;

    /* Step 4: Focus on initializing the sp register and cp0_status of env_tf field, located at this new Env. */
    t->tcb_tf.cp0_status = 0x1000100C;
    t->tcb_tf.regs[29] = USTACKTOP;
    t->tcb_status = ENV_RUNNABLE;

    LIST_REMOVE(e, env_link);
    *new = e;
    printf("in env_alloc, successful, envId is 0x%x\n", e->env_id);
    //printf("in env_alloc, successful, envId is 0x%x\n", e->env_id);
    return 0;
}

int thread_alloc(struct Env *e, struct Tcb **new)
{
    printf("start thread alloc\n");
    struct Tcb *t;

    if (e->env_thread_count >= THREAD_MAX)
        return E_THREAD_MAX;
    u_int thread_no = e->env_thread_count;
    for (; ; thread_no++) {
        if (thread_no >= THREAD_MAX) {
            return E_THREAD_MAX;
        }
        if (e->env_threads[thread_no].tcb_status == ENV_FREE) {
            break;
        }
    }
    e->env_thread_count++;
    t = &e->env_threads[thread_no];

    // basic
    t->tcb_parent_env = e;
    t->thread_id = mktcbid(t);
    printf("in thread_alloc: thread id is 0x%x\n",t->thread_id);
    t->tcb_status = ENV_RUNNABLE;
    t->tcb_tf.cp0_status = 0x1000100C;
    t->tcb_tf.regs[29] = USTACKTOP - 4 * BY2PG * (thread_no);
    t->tcb_pri = 1;
    //printf("in thread_alloc: thread id is 0x%x\n",t->thread_id);
    // exit
    t->tcb_exit_value = 0;
    t->tcb_exit_ptr = (void *)&(t->tcb_exit_value);
    
    // cancel
    t->tcb_cancelstate = THREAD_CANNOT_BE_CANCELED;
    t->tcb_canceltype = THREAD_CANCEL_IMI;
    t->tcb_canceled = 0;
    
    // join
    t->tcb_detach = 0;
    t->tcb_join_value_ptr = NULL;
    LIST_INIT(&t->tcb_joined_list);

    *new = t;
    printf("thread_alloc finish\n");
    return 0;
}

static int load_icode_mapper(u_long va, u_int32_t sgsize,
                             u_char *bin, u_int32_t bin_size, void *user_data)
{
    struct Env *env = (struct Env *)user_data;
    struct Page *p = NULL;
    u_long i = 0;
    int r;
    u_long offset = va - ROUNDDOWN(va, BY2PG);
    int size;

    if (offset)
    {
        p = page_lookup(env->env_pgdir, va + i, NULL);
        if (p == 0)
        {
            r = page_alloc(&p);
            if (r != 0)
            {
                return r; // fail to have a new page
            }
            page_insert(env->env_pgdir, p, va + i, PTE_R); // page_insert only mapping, no copy
                                                           // int page_insert(Pde *pgdir, struct Page *pp, u_long va, u_int perm)
        }
        size = MIN(bin_size - i, BY2PG - offset);
        bcopy((void *)bin, (void *)(page2kva(p) + offset), size);
        i += size;
    }

    /* Step 1: load all content of bin into memory. */
    // for (i = 0; i < bin_size; i += BY2PG) {
    /* Hint: You should alloc a new page. */
    //}
    while (i < bin_size)
    {
        size = MIN(BY2PG, bin_size - i);
        r = page_alloc(&p);
        if (r != 0)
        {
            return r;
        }
        page_insert(env->env_pgdir, p, va + i, PTE_R);
        bcopy((void *)bin + i, (void *)(page2kva(p)), size);
        i += size;
    }

    /* Step 2: alloc pages to reach `sgsize` when `bin_size` < `sgsize`.
     * hint: variable `i` has the value of `bin_size` now! */
    offset = va + i - ROUNDDOWN(va + i, BY2PG);
    if (offset)
    {
        p = page_lookup(env->env_pgdir, va + i, NULL);
        if (p == 0)
        {
            r = page_alloc(&p);
            if (r != 0)
            {
                return r;
            }
            page_insert(env->env_pgdir, p, va + i, PTE_R);
        }
        size = MIN(sgsize - i, BY2PG - offset);
        //		bzero((void*)((page2kva(p)) + offset), size);

        { // this is bzero, but better than "bzero" in ./init/inin.c
            // there is no need to make b = 4 * x
            void *b = (void *)(page2kva(p) + offset);
            size_t len = size;
            void *max = b + len;

            // finish the remaining 0-3 bytes
            while (b < max)
            {
                *(char *)b++ = 0;
            }
        }
        i += size;
    }

    while (i < sgsize)
    {
        size = MIN(sgsize - i, BY2PG);
        r = page_alloc(&p);
        if (r != 0)
        {
            return r;
        }
        page_insert(env->env_pgdir, p, va + i, PTE_R);
        //		bzero((void*)page2kva(p), size);
        i += size;
    }
    return 0;
}

static void
load_icode(struct Env *e, u_char *binary, u_int size)
{
    /* Hint:
     *  You must figure out which permissions you'll need
     *  for the different mappings you create.
     *  Remember that the binary image is an a.out format image,
     *  which contains both text and data.
     */
    struct Page *p = NULL;
    u_long entry_point;
    u_long r;
    u_long perm = PTE_V | PTE_R;
    u_int va;

    for(va = USTACKTOP - 4 * BY2PG * THREAD_MAX; va < USTACKTOP; va += BY2PG){
        /* Step 1: alloc a page. */
        if((r = page_alloc(&p)) != 0){
            return;
        }
        /* Step 2: Use appropriate perm to set initial stack for new Env. */
        /* Hint: Should the user-stack be writable? */
        if((r = page_insert(e->env_pgdir, p, va, perm)) != 0){
            return;
        }
    }

    /* Step 3: load the binary using elf loader. */
    load_elf(binary, size, &entry_point, (void *)e, load_icode_mapper);

    /* Step 4: Set CPU's PC register as appropriate value. */
    e->env_threads[0].tcb_tf.pc = entry_point;
}

void env_create_priority(u_char *binary, int size, int priority)
{
    struct Env *e;
    int r;
    /* Step 1: Use env_alloc to alloc a new env. */
    r = env_alloc(&e, 0);
    if (r != 0)
    {
        return;
    }
    // printf("in env_create_priority: env_alloc  successfully\n");

    /* Step 2: assign priority to the new env. */
    e->env_threads[0].tcb_pri = (u_int)priority;

    /* Step 3: Use load_icode() to load the named elf binary,
       and insert it into env_sched_list using LIST_INSERT_HEAD. */
    load_icode(e, binary, size);

    // printf("in env_create_priority:  load_icode successfully\n");
    LIST_INSERT_HEAD(&tcb_sched_list[0], &e->env_threads[0], tcb_sched_link);
}

void env_create(u_char *binary, int size)
{
    /* Step 1: Use env_create_priority to alloc a new env with priority 1 */
    env_create_priority(binary, size, 1);
}

/* Overview:
 *  Free env e and all memory it uses.
 */
void env_free(struct Env *e)
{
    Pte *pt;
    u_int pdeno, pteno, pa;

    /* Hint: Note the environment's demise.*/
    printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

    /* Hint: Flush all mapped pages in the user portion of the address space */
    for (pdeno = 0; pdeno < PDX(UTOP); pdeno++)
    {
        /* Hint: only look at mapped page tables. */
        if (!(e->env_pgdir[pdeno] & PTE_V))
        {
            continue;
        }
        /* Hint: find the pa and va of the page table. */
        pa = PTE_ADDR(e->env_pgdir[pdeno]);
        pt = (Pte *)KADDR(pa);
        /* Hint: Unmap all PTEs in this page table. */
        for (pteno = 0; pteno <= PTX(~0); pteno++)
            if (pt[pteno] & PTE_V)
            {
                page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
            }
        /* Hint: free the page table itself. */
        e->env_pgdir[pdeno] = 0;
        page_decref(pa2page(pa));
    }
    /* Hint: free the page directory. */
    pa = e->env_cr3;
    e->env_pgdir = 0;
    e->env_cr3 = 0;
    /* Hint: free the ASID */
    asid_free(e->env_id >> (1 + LOG2NENV));
    page_decref(pa2page(pa));
    /* Hint: return the environment to the free list. */
    int i;
    for (i = 0; i < THREAD_MAX; i++) {
        e->env_threads[i].tcb_status = ENV_FREE;
        e->env_threads[i].tcb_exit_ptr = NULL;
    }
    e->env_thread_count = 0;
    e->env_runs = 0;
    LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

void thread_free(struct Tcb *t)
{
    struct Env *e = t->tcb_parent_env;
    printf("env:[%08x] free tcb %08x\n", e->env_id, t->thread_id);
    (e->env_thread_count)--;
    if (e->env_thread_count <= 0)
        env_free(e);
    t->tcb_status = ENV_FREE;
}

void env_destroy(struct Env *e)
{
    /* Hint: free e. */
    env_free(e);

    /* Hint: schedule to run a new environment. */
    if (curenv == e)
    {
        curenv = NULL;
        /* Hint: Why this? */
        bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
              (void *)TIMESTACK - sizeof(struct Trapframe),
              sizeof(struct Trapframe));
        printf("i am killed ... \n");
        sched_yield();
    }
}

void thread_destroy(struct Tcb *t)
{
    if (t->tcb_status == ENV_RUNNABLE)
        LIST_REMOVE(t, tcb_sched_link);
    thread_free(t);
    if (curtcb == t)
    {
        curtcb = NULL;
        bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
              (void *)TIMESTACK - sizeof(struct Trapframe),
              sizeof(struct Trapframe));
        printf("i am thread, i am killed ... \n");
        sched_yield();
    }
}

extern void env_pop_tf(struct Trapframe *tf, int id);
extern void lcontext(u_int contxt);

void env_run(struct Tcb *t)
{
    /* Step 1: save register state of curenv. */
    /* Hint: if there is an environment running,
     *   you should switch the context and save the registers.
     *   You can imitate env_destroy() 's behaviors.*/
    if (curtcb)
    {
        struct Trapframe *old;
        old = (struct Trapframe *)(TIMESTACK - sizeof(struct Trapframe));
        bcopy((old), &(curtcb->tcb_tf), sizeof(struct Trapframe));
        curtcb->tcb_tf.pc = old->cp0_epc;
    }

    /* Step 2: Set 'curenv' to the new environment. */
    curtcb = t;
    curenv = t->tcb_parent_env;
    curenv->env_runs++;

    /* Step 3: Use lcontext() to switch to its address space. */
    lcontext((u_int)curenv->env_pgdir);

    /* Step 4: Use env_pop_tf() to restore the environment's
     *   environment   registers and return to user mode.
     *
     * Hint: You should use GET_ENV_ASID there. Think why?
     *   (read <see mips run linux>, page 135-144)
     */
    env_pop_tf(&(curtcb->tcb_tf), GET_ENV_ASID(curenv->env_id));
}

/*void env_check()
{
    struct Env *temp, *pe, *pe0, *pe1, *pe2;
    struct Env_list fl;
    int re = 0;
    // should be able to allocate three envs 
    pe0 = 0;
    pe1 = 0;
    pe2 = 0;
    assert(env_alloc(&pe0, 0) == 0);
    assert(env_alloc(&pe1, 0) == 0);
    assert(env_alloc(&pe2, 0) == 0);

    assert(pe0);
    assert(pe1 && pe1 != pe0);
    assert(pe2 && pe2 != pe1 && pe2 != pe0);

    // temporarily steal the rest of the free envs
    fl = env_free_list;
    // now this env_free list must be empty!
    LIST_INIT(&env_free_list);

    // should be no free memory
    assert(env_alloc(&pe, 0) == -E_NO_FREE_ENV);

    // recover env_free_list
    env_free_list = fl;

    printf("pe0->env_id %d\n", pe0->env_id);
    printf("pe1->env_id %d\n", pe1->env_id);
    printf("pe2->env_id %d\n", pe2->env_id);

    assert(pe0->env_id == 1024);
    assert(pe1->env_id == 3073);
    assert(pe2->env_id == 5122);
    printf("env_init() work well!\n");

    // check envid2env work well
    pe2->env_status = ENV_FREE;
    re = envid2env(pe2->env_id, &pe, 0);

    assert(pe == 0 && re == -E_BAD_ENV);

    pe2->env_status = ENV_RUNNABLE;
    re = envid2env(pe2->env_id, &pe, 0);

    assert(pe->env_id == pe2->env_id && re == 0);

    temp = curenv;
    curenv = pe0;
    re = envid2env(pe2->env_id, &pe, 1);
    assert(pe == 0 && re == -E_BAD_ENV);
    curenv = temp;
    printf("envid2env() work well!\n");

    // check env_setup_vm() work well
    printf("pe1->env_pgdir %x\n", pe1->env_pgdir);
    printf("pe1->env_cr3 %x\n", pe1->env_cr3);

    assert(pe2->env_pgdir[PDX(UTOP)] == boot_pgdir[PDX(UTOP)]);
    assert(pe2->env_pgdir[PDX(UTOP) - 1] == 0);
    printf("env_setup_vm passed!\n");

    assert(pe2->env_tf.cp0_status == 0x10001004);
    printf("pe2`s sp register %x\n", pe2->env_tf.regs[29]);

    // free all env allocated in this function
    LIST_INSERT_HEAD(env_sched_list, pe0, env_sched_link);
    LIST_INSERT_HEAD(env_sched_list, pe1, env_sched_link);
    LIST_INSERT_HEAD(env_sched_list, pe2, env_sched_link);

    env_free(pe2);
    env_free(pe1);
    env_free(pe0);

    printf("env_check() succeeded!\n");
}*/

/*void load_icode_check()
{
    // check_icode.c from init/init.c
    extern u_char binary_user_check_icode_start[];
    extern u_int binary_user_check_icode_size;
    env_create(binary_user_check_icode_start, binary_user_check_icode_size);
    struct Env *e;
    Pte *pte;
    u_int paddr;
    assert(envid2env(1024, &e, 0) == 0);
    // text & data: 0x00401030 - 0x00409adc left closed and right open interval
    assert(pgdir_walk(e->env_pgdir, 0x00401000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 0xc) == 0x8fa40000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x26300001);
    assert(pgdir_walk(e->env_pgdir, 0x00402000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x10800004);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00801821);
    assert(pgdir_walk(e->env_pgdir, 0x00403000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x80a20000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x24060604);
    assert(pgdir_walk(e->env_pgdir, 0x00404000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x04400043);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x00405000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x00406000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x00407000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x7f400000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x00408000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x0000fffe);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x00409000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 0x2aa) == 0x004099fc);
    printf("text & data segment load right!\n");
    // bss        : 0x00409adc - 0x0040aab4 left closed and right open interval
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 0x2b7) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);
    assert(pgdir_walk(e->env_pgdir, 0x0040a000, 0, &pte) == 0);
    assert(*((int *)KADDR(PTE_ADDR(*pte))) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 0x2ac) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 0x2ad) == 0x00000000);
    assert(*((int *)KADDR(PTE_ADDR(*pte)) + 1023) == 0x00000000);

    printf("bss segment load right!\n");

    env_free(e);
    printf("load_icode_check() succeeded!\n");
}*/


