# 练习

## 练习 1：分配并初始化一个进程控制块（需要编码）

alloc_proc 函数（位于 kern/process/proc.c 中）负责分配并返回一个新的 struct proc_struct 结构，用于存储新建立的内核线程的管理信息。ucore 需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。

### 1.1 设计实现过程

```c
// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        proc->state =PROC_UNINIT; //设置进程为初始态
        proc->pid=-1; //设置pid未初始化值
        proc->runs=0;
        proc->kstack=0;
        proc->need_resched=0;
        proc->parent=NULL;
        proc->mm=NULL;
        memset(&proc->context,0,sizeof(struct context));
        proc->tf=NULL;
        proc->pgdir=boot_pgdir_pa; //使用内核页目录表的基址
        proc->flags=0;
        memset(proc->name,0,PROC_NAME_LEN + 1);
    }
    return proc;
}
```

`alloc_proc` 函数的目标是构建一个处于未就绪状态、干净、安全的空进程结构。这个过程其实就是在给新分配的 `proc_struct` 结构体填上合理的初始值。

首先是一些基本状态的设置。`state` 被设为 `PROC_UNINIT`，表示这个进程还处于创建初期，还没有准备好运行。`pid` 设为 -1，表示进程 ID 还没有初始化，这个值会在 `do_fork` 函数中由 `get_pid()` 来设置。`name` 数组需要清零，这是为了防止 `kmalloc` 分配的内存中可能残留的垃圾数据被误当作进程名。

接下来是内存相关的设置。`pgdir` 被设为 `boot_pgdir_pa`，也就是内核页目录表的基址。这是因为新创建的通常是内核线程，它们需要共享内核空间的映射，所以直接使用内核的页目录表就可以了。`mm` 设为 `NULL`，因为内核线程只运行在内核态，不需要用户态的虚拟内存管理。`kstack` 设为 0，`tf` 设为 `NULL`，表示内核栈和陷阱帧还没有分配，这些资源会在 `do_fork` 中通过 `setup_kstack()` 和 `copy_thread()` 来设置。

执行上下文也需要初始化。`context` 通过 `memset` 清零，表示上下文还没有初始化，这样可以防止在运行时出现未定义行为。`runs`、`need_resched`、`flags` 都设为 0，表示这个进程从未运行过，也没有特殊状态需要处理。`parent` 也设为 `NULL`，因为此时还没有确定父进程，这个关系会在 `do_fork` 中建立。

最后，对于 `list_link` 和 `hash_link` 这两个双向链表节点，我们不需要初始化它们。这有两个原因：一是从实现角度来说，在 `do_fork` 函数中会调用 `list_add` 和 `hash_proc`，这些函数会覆写节点的 `prev` 和 `next` 指针，所以提前初始化是多余的，还能节省一点内存开销。二是从软件设计的角度来看，`alloc_proc` 和 `do_fork` 承担着不同的职责。`alloc_proc` 就像一个工厂，只负责造出一个可用的对象实体，此时这个进程虽然存在了，但在逻辑上它是"孤立"的，不属于任何链表。而 `do_fork` 承担的是注册工作，负责将这个进程加入到所有进程的链表和 hash 表中，让它真正成为系统的一部分。

### 1.2 问题：proc_struct 中 struct context context 和 struct trapframe \*tf 成员变量含义和在本实验中的作用是啥？

```c kern/process/process.h
struct context
{
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};
```

- `context` 是上下文结构体，它保存的是上下文切换时所需的一组寄存器，包括返回地址 `ra`、栈指针 `sp`，以及被调用者保存的寄存器 `s0` 到 `s11`。这些寄存器在 RISC-V 的调用约定中属于被调用者保存寄存器，也就是说，如果一个函数要使用这些寄存器，它必须在进入时保存它们，在退出前恢复它们，而调用者不需要关心这些。

- `context` 的作用是保存进程的执行上下文，实现进程之间的切换。在本实验中，当 `proc_run` 调度新进程时，会调用 `switch_to(&(prev->context), &(proc->context))`。这个函数会把旧进程的寄存器值（`ra`、`sp`、`s0-s11`）写回到 `prev->context` 中保存起来，然后从 `proc->context` 中把新进程的寄存器值恢复到 CPU 上。这样，当新进程开始执行时，它就能从上次被切换的位置继续执行，就像从来没有被中断过一样。

- `context` 还配合 `tf` 完成新线程的启动。当新进程第一次被调度时，`context.ra` 被设置为 `forkret` 的地址，这样 `switch_to` 返回后就会跳转到 `forkret` 函数，从而启动新线程的执行流程。

- `tf`（trapframe）则是异常或中断发生时，硬件和陷阱处理例程共同保存的"完整 CPU 状态快照"。它包含了所有 32 个通用寄存器（通过 `struct pushregs gpr`），以及四个重要的状态寄存器：`status`（状态寄存器，记录特权级、中断使能等信息）、`epc`（异常程序计数器，记录发生异常时的指令地址）、`badvaddr`（异常地址，记录导致异常的虚拟地址）、`cause`（异常原因，记录是什么类型的异常或中断）。

- `tf` 的作用是为新内核线程提供初始执行环境。在 `copy_thread` 函数中，系统会在子进程的内核栈顶构造一份 trapframe，把函数指针、参数、入口地址等信息都保存在里面，然后把指针赋给 `proc->tf`。当新内核线程第一次运行时，会通过 `forkret -> forkrets(proc->tf)` 这个调用链，把 trapframe 中的内容恢复到 CPU 上，然后执行 `sret` 指令，让 CPU 从 `epc` 指定的地址开始执行。这样设计的好处是，新线程的启动与从陷阱返回使用同一套恢复机制，代码更简洁，也更容易维护。

## 练习 2：为新创建的内核线程分配资源（需要编码）

创建一个内核线程需要分配和设置好很多资源。kernel_thread 函数通过调用 do_fork 函数完成具体内核线程的创建工作。do_kernel 函数会调用 alloc_proc 函数来分配并初始化一个进程控制块，但 alloc_proc 只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore 一般通过 do_fork 实际创建新的内核线程。do_fork 的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要"fork"的东西就是 stack 和 trapframe。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在 kern/process/proc.c 中的 do_fork 函数中的处理过程。

```c
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;

//实现步骤

    //1.分配并初始化进程控制块
    if((proc=alloc_proc())==NULL){
        goto fork_out;
    }

    //设置父进程
    proc->parent=current;

    //2.分配内核栈
    if(setup_kstack(proc)!=0){
        goto bad_fork_cleanup_proc;
    }
    //3.复制或共享内存管理结构
    if(copy_mm(clone_flags,proc)!=0){
        goto bad_fork_cleanup_kstack;
    }
    //4.设置陷阱帧和上下文
    copy_thread(proc,stack,tf);
    //5.加入链表
        proc->pid=get_pid();
        hash_proc(proc);
        list_add(&proc_list,&(proc->list_link));
        nr_process++;
    //6.唤醒进程
    wakeup_proc(proc);
    //7.返回子进程pid
    ret=proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

### 2.1 设计实现过程

`do_fork` 函数的核心任务是为新创建的内核线程分配资源并设置好各种状态，让它能够被调度运行。

整个过程从分配进程控制块开始。函数首先调用 `alloc_proc()` 分配一个 `proc_struct` 结构体并初始化基本字段，然后设置 `proc->parent = current` 来建立父子关系。接下来需要为新进程分配内核栈空间，这是通过 `setup_kstack(proc)` 完成的。如果这一步失败了，就需要跳转到错误处理路径，把刚才分配的进程控制块释放掉。

然后系统会调用 `copy_mm(clone_flags, proc)` 来处理内存管理结构。虽然在本实验中内核线程的 `mm` 为 `NULL`，这个步骤主要是做一些检查工作，但这样的设计为将来支持用户进程留下了扩展空间。

最关键的一步是调用 `copy_thread(proc, stack, tf)` 来设置陷阱帧和上下文。这个函数会在内核栈顶创建 trapframe，把父进程的 trapframe 内容复制过去。同时，它还会设置子进程的 `context.ra = forkret`，这样当新进程第一次被调度时，就会从 `forkret` 开始执行。`context.sp` 也会被设置为指向 trapframe 的位置，确保栈指针正确。

设置好执行环境后，新进程需要加入到系统的管理结构中。系统会调用 `get_pid()` 为新进程分配一个唯一的进程 ID，然后通过 `hash_proc(proc)` 把它加入哈希表以便快速查找，再用 `list_add()` 把它加入全局进程链表，最后增加进程计数 `nr_process++`。

最后，调用 `wakeup_proc(proc)` 将进程状态设置为 `PROC_RUNNABLE`，这样它就可以被调度运行了。如果一切顺利，函数会返回子进程的 `pid`。

整个过程中，如果任何一步出现错误，代码会通过 `goto` 语句跳转到相应的错误处理路径，按照相反的顺序释放已分配的资源（比如先释放内核栈，再释放进程控制块）。这种级联回滚的错误处理机制确保了即使创建失败，也不会出现内存泄漏的问题。

### 2.2 问题：ucore 是否做到给每个新 fork 的线程一个唯一的 id？请说明你的分析和理由。

是的，ucore 能够保证给每个新 fork 的线程分配唯一的 id，但这是在特定条件下实现的。

从代码来看，`get_pid()` 函数（位于 `kern/process/proc.c`）采用了一种比较直观的方式来保证唯一性。系统定义了 `MAX_PROCESS = 4096` 和 `MAX_PID = 8192`，这意味着 PID 的有效范围是 1 到 8191（0 通常保留给 idle 进程）。虽然理论上最多只能同时存在 4096 个进程，但 PID 空间设计为 8192，这样即使有进程退出释放了 PID，系统也能通过回绕机制重新利用这些已释放的 PID。

`get_pid()` 的核心思路是：当需要分配新的 pid 时，它会遍历整个进程链表 `proc_list`，检查每个已存在进程的 `pid` 是否与候选的 `last_pid` 冲突。如果发现冲突（即 `proc->pid == last_pid`），就递增 `last_pid` 并重新检查，直到找到一个未被占用的 pid。当 `last_pid` 达到 `MAX_PID` 时，会回绕到 1 继续查找，这样就能充分利用已释放的 pid 空间。

为了提高效率，算法还使用了一个 `next_safe` 变量来记录大于 `last_pid` 的最小已占用 pid。当 `last_pid < next_safe` 时，说明在安全范围内，可以跳过遍历直接返回。

不过，这个实现有一个重要的前提条件：在本实验中，`do_fork` 是在内核态单线程执行的，不存在并发调用 `get_pid()` 的情况，因此不会出现竞态条件。如果未来要支持多核并发创建进程，当前的实现就不是线程安全的了，需要添加锁机制来保护。另外，当系统中同时存在的进程数达到 `MAX_PROCESS`（4096）时，虽然 PID 空间还有余量，但系统也无法创建新进程了。

总的来说，在本实验的单核、单线程内核执行环境下，`get_pid()` 通过遍历进程链表并检查冲突的方式，能够可靠地保证为每个新 fork 的线程分配唯一的 pid。

## 练习 3：编写 proc_run 函数（需要编码）

proc_run 用于将指定的进程切换到 CPU 上运行。它的大致执行步骤包括：

检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。

```c
void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
         bool intr_flag;
         local_intr_save(intr_flag);
         {
            //切换到当前进程为要运行的进程
            struct proc_struct *prev=current;
            current=proc;

            //切换页表，使用新进程的地址空间
            lsatp(proc->pgdir);

            //上下文切换
            switch_to(&(prev->context),&(proc->context));
         }
         local_intr_restore(intr_flag);

    }
}
```

请回答如下问题：

### 问题：在本实验的执行过程中，创建且运行了几个内核线程？

在本实验的执行过程中，系统创建并运行了 2 个内核线程。

这两个线程都是在 `proc_init()` 函数中创建的。第一个是 `idleproc`，它的 PID 是 0，名字叫 "idle"。这个进程比较特殊，它是直接通过 `alloc_proc()` 分配后手动设置属性的，而不是通过 `do_fork()` 创建的。它的内核栈使用的是启动时分配的 `bootstack`，作用也很简单：当没有其他可运行进程时，CPU 就执行这个 idle 进程，通过 `cpu_idle()` 函数不断检查是否有需要调度的进程。

第二个是 `initproc`，PID 是 1，名字叫 "init"。它是通过 `kernel_thread(init_main, "Hello world!!", 0)` 创建的，走的是完整的 fork 流程，所以内核栈是通过 `do_fork()` → `setup_kstack()` 动态分配的。它的任务是执行 `init_main` 函数，打印一些初始化信息，比如 "this initproc, pid = 1" 和 "Hello world!!" 之类的。

在 `proc_init()` 中，系统先将 `idleproc` 的状态设置为 `PROC_RUNNABLE`，并将 `current` 指向它，然后创建 `initproc`。两个线程都被设置为 `PROC_RUNNABLE` 状态，所以系统启动后，调度器会在它们之间进行切换执行。
