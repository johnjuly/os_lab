# 实验五

## 练习

### 练习 1: 加载应用程序并执行（需要编码）

**do_execve**函数调用`load_icode`（位于 kern/process/proc.c 中）来加载并解析一个处于内存中的 ELF 执行文件格式的应用程序。你需要补充`load_icode`的第 6 步，建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好`proc_struct`结构中的成员变量 trapframe 中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的 trapframe 内容。

请在实验报告中简要说明你的设计实现过程。

- 请简要描述这个用户态进程被 ucore 选择占用 CPU 执行（RUNNING 态）到具体执行应用程序第一条指令的整个经过。

---

#### 1.1 设计实现过程

在 `load_icode` 函数的第 6 步中，需要正确设置 trapframe 结构，这是整个加载过程的关键收尾工作。trapframe 记录了进程从内核返回用户态时需要恢复的所有状态信息。

首先设置用户栈指针 `tf->gpr.sp` 为 `USTACKTOP`（0x80000000）。用户栈已经在前面步骤中建立，栈指针需要指向栈顶，以便用户程序能够正常使用栈空间进行函数调用和局部变量存储。

接下来设置程序入口点 `tf->epc` 为 `elf->e_entry`。该地址是 ELF 文件头中记录的入口地址，当 CPU 执行 `sret` 指令从内核态返回时，会从 `epc` 指向的地址开始执行，因此必须准确设置为用户程序的入口点。

最后设置状态寄存器 `tf->status`。需要清除 SPP 位（设置为 0），表示之前处于用户模式，这样 `sret` 返回时会切回用户模式；同时设置 SPIE 位为 1，允许在用户模式下中断，使进程返回用户态后中断机制能够正常工作。实现时使用 `tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;`，保留原 sstatus 的其他位，只修改这两个关键位。

当这些设置完成后，进程通过 `trapret` 返回用户态时，CPU 会从程序入口点开始执行，使用正确的用户栈，并且处于用户模式，从而能够正确执行用户程序。

#### 1.2 整个经过

用户态进程从被调度器选中到执行第一条用户指令的完整过程如下。

整个过程从 `schedule()` 开始。当需要切换进程时（如时间片用完或进程主动让出 CPU），调度器从进程链表中选择可运行的进程，然后调用 `proc_run()` 进行切换。`proc_run()` 首先切换页表（`lsatp`），使 CPU 能够访问新进程的地址空间；然后调用 `switch_to()` 进行上下文切换，保存旧进程的寄存器，恢复新进程的寄存器。这个过程完成后，CPU 的执行流转移到新进程的内核栈上。

对于新创建的用户进程，`switch_to()` 返回后会执行 `forkret()`。该函数准备返回到用户态，它调用 `forkrets()` 将 trapframe 的地址传递过去，然后跳转到 `__trapret`。`__trapret` 是内核返回用户态的通用路径，它会调用 `RESTORE_ALL` 宏从 trapframe 中恢复所有寄存器，包括通用寄存器、`sstatus` 和 `sepc`。如果要返回用户模式，还会设置 `sscratch` 保存内核栈指针，以便下次进入内核时能快速找到内核栈。

最后执行 `sret` 指令。`sret` 根据 `sstatus` 的 SPP 位决定返回到哪个特权级别，将 `sepc` 加载到程序计数器，并根据 SPIE 位设置中断状态。执行完 `sret` 后，CPU 回到用户模式，程序计数器指向用户程序的入口地址，栈指针指向用户栈，开始执行用户程序的第一条指令。

整个流程的核心是通过 trapframe 机制完整保存和恢复用户态的上下文，使进程能够从内核切换回用户程序并继续执行。

### 练习 2: 父进程复制自己的内存空间给子进程（需要编码）

创建子进程的函数`do_fork`在执行中将拷贝当前进程（即父进程）的用户内存地址空间中的合法内容到新进程中（子进程），完成内存资源的复制。具体是通过`copy_range`函数（位于 kern/mm/pmm.c 中）实现的，请补充`copy_range`的实现，确保能够正确执行。

请在实验报告中简要说明你的设计实现过程。

- 如何设计实现`Copy on Write`机制？给出概要设计，鼓励给出详细设计。

#### 2.1 设计实现过程

`copy_range` 函数的作用是将父进程的用户内存空间完整地复制给子进程，使子进程拥有和父进程相同的内存内容。该函数按页为单位进行复制。

函数从 `start` 到 `end` 按页遍历父进程的地址空间。对每一页，首先通过 `get_pte()` 获取父进程的页表项，如果页表项存在且有效（`*ptep & PTE_V`），说明该页面已被分配，需要复制。然后为子进程获取或创建对应的页表项，并从父进程的页表项中提取页面权限信息（如用户可访问权限）。

接下来为子进程分配一个新的物理页面，该页面与父进程的页面是独立的。然后进行复制：通过 `page2kva()` 获取父子进程页面的内核虚拟地址（在内核态可以直接访问所有物理内存），使用 `memcpy()` 将整个页面（PGSIZE 字节）从父进程页面复制到子进程页面。复制完成后，调用 `page_insert()` 在子进程的页表中建立映射，使子进程能够通过相同的线性地址访问新复制的物理页面。

处理完一页后，继续处理下一页，直到整个地址范围都复制完成。最终结果是，父子进程在相同的线性地址处有相同的内存内容，但它们使用不同的物理页面，因此后续的修改不会相互影响。这种设计既保证了子进程能够获得父进程的内存状态，又保证了进程间的隔离性。

#### 2.2 Copy on Write 概要设计

Copy-on-Write（COW）是一种优化策略，其核心思想是在创建子进程时不立即复制内存，而是让父子进程先共享相同的物理页面，只有当某个进程真正要写入时才进行复制。这样可以减少 `do_fork` 的开销，特别是对于内存占用较大的进程。

实现 COW 需要几个关键改动。首先在 `do_fork` 中，如果启用 COW 模式，不进行完整的内存复制，而是让子进程共享父进程的内存管理结构，或者建立共享映射。然后在 `copy_range` 函数中，COW 模式下不分配新页面，直接将父进程的物理页面映射到子进程的页表中，但需要清除写权限位（`PTE_W`），使页面变为只读。

当进程尝试写入这个只读页面时，会触发页错误。在 `do_pgfault` 中需要检测这是否是 COW 页错误，判断条件是页表项有效但写权限被清除，且页面引用计数大于 1（说明有多个进程共享）。如果满足条件，则分配新页面、复制内容、更新页表项恢复写权限，并减少原页面的引用计数。这样，只有真正需要写入的页面才会被复制，其他页面继续共享。

页面引用计数的管理很重要。`Page` 结构中的 `ref` 字段记录引用该物理页面的进程数。共享时增加计数，复制后减少计数，当计数为 0 时才能释放页面。这需要仔细处理，避免内存泄漏。

COW 的优势在于：如果子进程创建后立即执行 `exec`（这是常见的场景），可以避免不必要的内存复制。而且只有真正需要写入的页面才会被复制，节省了内存和时间。实现时需要注意正确区分 COW 页错误和真正的权限错误，还要考虑多进程并发访问的情况。

### 练习 3: 阅读分析源代码，理解进程执行 fork/exec/wait/exit 的实现，以及系统调用的实现（不需要编码）

请在实验报告中简要说明你对 fork/exec/wait/exit 函数的分析。并回答如下问题：

#### 3.1 四个函数的分析

这四个函数是操作系统进程管理的核心，它们共同构成了进程的创建、执行、等待和退出的完整生命周期。

**fork** 负责创建子进程。它通过 `do_fork` 完成：分配新的进程控制块、复制父进程的内存空间、设置父子关系、分配 PID 并加入进程链表，最后唤醒子进程使其可运行。关键点在于子进程会获得父进程的完整状态，包括内存内容、打开的文件等，但拥有独立的进程控制块和内存空间。

```text
用户程序: sys_fork()
  ↓
ecall 指令
  ↓
__alltraps → trap() → syscall()
  ↓
sys_fork() (syscall.c:16)
  - 从 trapframe 获取用户栈指针
  - 调用 do_fork(0, stack, tf)
  ↓
do_fork() (proc.c:436)
  1. alloc_proc() - 分配进程控制块
  2. setup_kstack() - 分配内核栈
  3. copy_mm() - 复制/共享内存空间
  4. copy_thread() - 设置 trapframe 和 context
  5. get_pid() + hash_proc() + set_links() - 分配 PID 并加入链表
  6. wakeup_proc() - 唤醒子进程（设为 PROC_RUNNABLE）
  7. 返回子进程 PID
  ↓
返回值写入 trapframe->gpr.a0
  ↓
__trapret → sret → 返回用户态
  ↓
父进程：返回子进程 PID
子进程：返回 0（在 copy_thread 中设置 tf->gpr.a0 = 0）
```

**exec** 用于加载并执行新程序。`do_execve` 会先清理当前进程的旧内存空间，然后调用 `load_icode` 解析 ELF 文件，建立新的内存映射，设置代码段和数据段，最后配置 trapframe 让进程从新程序的入口点开始执行。这个过程会替换当前进程的内存内容，但保持进程 ID 和某些属性不变。

```text
用户程序: sys_exec(name, len, binary, size)
  ↓
ecall 指令
  ↓
__alltraps → trap() → syscall()
  ↓
sys_exec() (syscall.c:30)
  - 从 trapframe 获取参数
  - 调用 do_execve(name, len, binary, size)
  ↓
do_execve() (proc.c:768)
  1. 检查参数合法性
  2. 如果 mm != NULL：
     - lsatp(boot_pgdir_pa) - 切换到内核页表
     - exit_mmap() - 释放旧内存映射
     - put_pgdir() - 释放页目录
     - mm_destroy() - 销毁内存管理结构
  3. load_icode(binary, size) - 加载新程序
     - 解析 ELF 文件
     - 建立新的内存映射
     - 设置代码段和数据段
     - 设置 trapframe（epc = 程序入口，sp = 用户栈）
  4. set_proc_name() - 设置进程名
  5. 返回 0（成功）
  ↓
如果 load_icode 失败：
  - do_exit(ret) - 退出进程
  ↓
返回值写入 trapframe->gpr.a0
  ↓
__trapret → sret → 返回到新程序的入口点
  ↓
开始执行新程序的第一条指令
```

**wait** 让父进程等待子进程结束。`do_wait` 会遍历子进程列表，查找处于 ZOMBIE 状态的子进程。如果找到了，就清理子进程的资源并返回；如果没找到，父进程会进入 SLEEPING 状态，等待子进程退出时被唤醒。这实现了进程间的同步机制。

```text
用户程序: sys_wait(pid, code_store)
  ↓
ecall 指令
  ↓
__alltraps → trap() → syscall()
  ↓
sys_wait() (syscall.c:23)
  - 从 trapframe 获取参数
  - 调用 do_wait(pid, store)
  ↓
do_wait() (proc.c:819)
  1. 检查 code_store 参数合法性
  2. 查找子进程：
     - 如果 pid != 0：查找指定 PID 的子进程
     - 如果 pid == 0：查找任意 ZOMBIE 状态的子进程
  3. 如果找到 ZOMBIE 状态的子进程：
     - 获取退出码（写入 code_store）
     - 释放子进程的内核栈
     - 释放子进程的进程控制块
     - 返回子进程 PID
  4. 如果子进程存在但未退出：
     - current->state = PROC_SLEEPING
     - current->wait_state = WT_CHILD
     - schedule() - 让出 CPU，等待子进程退出
     - 被唤醒后，goto repeat 重新查找
  5. 如果没有子进程：
     - 返回 -E_BAD_PROC
  ↓
返回值写入 trapframe->gpr.a0
  ↓
__trapret → sret → 返回用户态

```

**exit** 处理进程退出。`do_exit` 会释放进程的内存空间（页表、页目录等），将进程状态设为 ZOMBIE，唤醒等待的父进程，并将孤儿进程交给 initproc 收养，最后调用调度器切换到其他进程。进程的进程控制块要等到父进程调用 wait 才会被真正释放。

```text
用户程序: sys_exit(error_code)
  ↓
ecall 指令
  ↓
__alltraps → trap() → syscall()
  ↓
sys_exit() (syscall.c:10)
  - 从 trapframe 获取退出码
  - 调用 do_exit(error_code)
  ↓
do_exit() (proc.c:523)
  1. 检查是否是 idleproc 或 initproc（不允许退出）
  2. 释放内存空间：
     - lsatp(boot_pgdir_pa) - 切换到内核页表
     - exit_mmap() - 释放内存映射
     - put_pgdir() - 释放页目录
     - mm_destroy() - 销毁内存管理结构
  3. 设置进程状态：
     - current->state = PROC_ZOMBIE
     - current->exit_code = error_code
  4. 处理父子关系：
     - 如果父进程在等待（wait_state == WT_CHILD）：
       - wakeup_proc(parent) - 唤醒父进程
     - 将所有子进程交给 initproc 收养
  5. schedule() - 切换到其他进程
  6. panic() - 不应该执行到这里
  ↓
进程永远不会返回到用户态

```

#### 3.2 执行流程分析

```text
用户程序调用系统调用函数
  ↓
ecall 指令（触发异常）
  ↓
__alltraps（保存上下文到 trapframe）
  ↓
trap() 函数（异常分发）
  ↓
syscall() 函数（系统调用处理）
  ↓
执行对应的 do_xxx 函数
  ↓
__trapret（恢复上下文）
  ↓
sret 指令（返回用户态）
  ↓
用户程序继续执行
```

这四个系统调用的执行流程体现了用户态和内核态的紧密配合。

**用户态部分**：用户程序调用封装好的系统调用函数（如 `sys_fork()`），这些函数通过内联汇编发出 `ecall` 指令，触发异常进入内核。在系统调用返回后，用户程序继续执行，从寄存器 `a0` 中获取返回值。

**内核态部分**：当 `ecall` 触发后，CPU 切换到监督态，跳转到 `__alltraps` 保存上下文，然后调用 `trap` 函数分发异常。对于系统调用（`CAUSE_USER_ECALL`），会调用 `syscall()` 函数，根据系统调用号从函数表中找到对应的处理函数（如 `sys_fork`），执行相应的内核操作（如 `do_fork`），最后将返回值写入 trapframe 的 `a0` 寄存器。

**交错执行的过程**：整个过程是"用户态 → 内核态 → 用户态"的循环。用户程序在用户态执行，需要内核服务时通过 `ecall` 进入内核态，内核完成服务后通过 `sret` 返回用户态。这种切换是透明的，用户程序感觉就像调用了一个普通函数，但实际上经历了特权级别的切换、上下文保存和恢复等复杂操作。

**结果返回机制**：内核通过修改 trapframe 中的寄存器来返回结果。系统调用的返回值放在 `a0` 寄存器中，错误码也通过这个寄存器传递。当执行 `sret` 返回用户态时，恢复的寄存器中就包含了系统调用的结果，用户程序可以直接使用。

### 3.3 进程状态生命周期图

```
+---------------------+
| 1. PROC_UNINIT      |
|    (未初始化)        |
+---------------------+
           |
           | [T1: alloc_proc() 分配控制块]
           v
+---------------------+
| 2. PROC_RUNNABLE    | <────┐
|    (可运行 / 队列中)  |      |
+---------------------+      | [T3: 时间片用完 / yield / need_resched=1]
           |                 |
           | [T2: proc_run() / schedule() 选择]
           v                 |
+---------------------+      |
| 3. RUNNING          |  ────┘
|    (正在运行)        |
+---------------------+
  |          |
  |          | [T4: do_wait() / do_sleep() 等待事件]
  |          |
  |          v
  |  +---------------------+
  |  | 4. PROC_SLEEPING    |
  |  |    (睡眠 / 等待中)    |
  |  +---------------------+
  |           |
  |           | [T5: wakeup_proc() 事件发生被唤醒]
  |           v
  |  [回到 2. PROC_RUNNABLE]
  |
  | [T6: do_exit() 进程退出]
  v
+---------------------+
| 5. PROC_ZOMBIE      |
|    (僵尸状态)        |
+---------------------+
           |
           | [T7: do_wait() 父进程回收]
           v
[进程控制块和内核栈被释放]
```

**状态转换说明**：

- **PROC_UNINIT → PROC_RUNNABLE**：通过 `alloc_proc()` 分配进程控制块后，进程进入可运行状态，等待被调度。

- **PROC_RUNNABLE → RUNNING**：调度器（`schedule()`）选择进程后，调用 `proc_run()` 切换页表和上下文，进程开始执行。

- **RUNNING → PROC_RUNNABLE**：时间片用完（时钟中断设置 `need_resched=1`）或主动调用 `do_yield()` 让出 CPU，进程重新进入可运行队列。

- **RUNNING → PROC_SLEEPING**：进程调用 `do_wait()`、`do_sleep()` 等函数等待某些事件（如子进程退出、定时器到期），进入睡眠状态。

- **PROC_SLEEPING → PROC_RUNNABLE**：等待的事件发生时（如子进程退出调用 `wakeup_proc()`），进程被唤醒，重新进入可运行队列。

- **RUNNING → PROC_ZOMBIE**：进程调用 `do_exit()` 退出，释放大部分资源，但保留进程控制块等待父进程回收。

- **PROC_ZOMBIE → [释放]**：父进程调用 `do_wait()` 回收子进程，释放进程控制块和内核栈，子进程彻底消失。

### 扩展练习 Challenge

用户程序是在**编译链接阶段**被预先加载到内核镜像中的。具体来说，在 Makefile 的链接阶段（第 172 行），使用 `--format=binary` 选项将编译好的用户程序二进制文件（`USER_BINS`）直接嵌入到内核 ELF 文件中。这些二进制数据会被链接器转换为特殊的符号，如 `_binary_obj___user_hello_out_start` 和 `_binary_obj___user_hello_out_size`，它们指向嵌入在内核镜像中的用户程序数据。

当内核需要执行用户程序时（如 `user_main` 调用 `KERNEL_EXECVE(hello)`），会通过 `kernel_execve` 函数找到这些符号，获取用户程序的二进制数据，然后调用 `do_execve` → `load_icode` 将程序加载到用户进程的内存空间中。

**与常用操作系统的区别**：

在常见的操作系统（如 Linux）中，用户程序通常存储在文件系统中（如 `/bin/ls`），当需要执行时，内核会：

1. 从文件系统读取程序文件
2. 解析 ELF 格式
3. 动态分配内存并加载程序
4. 建立内存映射

而 ucore 采用了**静态链接嵌入**的方式，所有用户程序都预先编译并嵌入到内核镜像中。这样做的好处是简化了实现，不需要实现文件系统就能运行用户程序，适合教学和实验环境。缺点是缺乏灵活性，无法动态加载新程序，而且内核镜像会变得很大。这种设计体现了教学操作系统"先实现核心功能，再逐步完善"的思路。

## 调试

### 调试 1 页表查询过程

**执行结果**

1. 终端 3 查看当前指令`sd ra,8(sp)`![alt text](<Pasted image 20251213144904.png>)
2. 查看栈指针![alt text](<Pasted image 20251213145122.png>)
3. 终端 2 即 调试 qemu 的 gdb 查看当前地址内容，为 sp+8.
   ![alt text](<Pasted image 20251213132935.png>)
4. 查看调用栈![alt text](<Pasted image 20251213134148.png>)
5. 单步执行中 打印有关页表配置的信息![alt text](<Pasted image 20251213151407.png>)
6. 关于 tlb 在`store_helper`函数打断点，查看调用函数的信息![alt text](<Pasted image 20251213172936.png>)
7. 观察 tlb 命中情况，为 false $2 的值![alt text](<Pasted image 20251213173646.png>)
8. 对于 mbare 模式下代码的调试，终端 3kern_entry 处打断点，可以看到 satp 为 0![alt text](<Pasted image 20251213175521.png>)
9. mode 也为 0 ![alt text](<Pasted image 20251213175719.png>)
10. 地址也没有改变![alt text](<Pasted image 20251213175910.png>)

- 关于配置问题，在容器里，没有下载源码，所以又重新编译调试版本 放到了工作目录下，很曲折了。
- 关于两个 gdb 有时候不动的问题，把 gdb 看作是一个 toolkit,相比于简单的 tool 例如 printf 调试，它的功能是蛮多了。这更加深了 everything is a state machine 的执念。

#### 1.1 理解调用路径和关键分支

通过双重 GDB 调试，完整追踪了从 ucore 访存指令到 QEMU 地址翻译的完整调用路径。

**关键调用路径：**

```
ucore 访存指令 (sd/lw 等)
  ↓
QEMU 翻译代码 (code_gen_buffer)
  ↓
helper_le_stq_mmu / helper_le_ldq_mmu
  ↓
store_helper / load_helper (accel/tcg/cputlb.c)
  ↓
tlb_fill (accel/tcg/cputlb.c:878)
  ↓
riscv_cpu_tlb_fill (target/riscv/cpu_helper.c:435)
  ↓
get_physical_address (target/riscv/cpu_helper.c:155)
  ↓
页表遍历循环
```

**关键分支语句：**

1. **第 171 行**（`target/riscv/cpu_helper.c`）：检查是否在机器模式或没有 MMU

   ```c
   if (mode == PRV_M || !riscv_feature(env, RISCV_FEATURE_MMU)) {
       *physical = addr;  // 直接返回虚拟地址作为物理地址
       *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
       return TRANSLATE_SUCCESS;
   }
   ```

   如果满足条件，说明处于 MBARE 模式，虚拟地址直接等于物理地址，无需页表遍历。

2. **第 187-202 行**：根据 SATP 模式选择页表格式

   ```c
   switch (vm) {
       case VM_1_10_SV32: levels = 2; ptidxbits = 10; ptesize = 4; break;
       case VM_1_10_SV39: levels = 3; ptidxbits = 9; ptesize = 8; break;
       case VM_1_10_SV48: levels = 4; ptidxbits = 9; ptesize = 8; break;
       case VM_1_10_SV57: levels = 5; ptidxbits = 9; ptesize = 8; break;
       case VM_1_10_MBARE: /* 直接返回 */ break;
   }
   ```

   根据 SATP 寄存器的 MODE 字段确定页表级数和格式。

3. **第 256 行**：检查 PTE 有效位

   ```c
   if (!(pte & PTE_V)) {
       return TRANSLATE_FAIL;  // PTE 无效
   }
   ```

4. **第 260 行**：判断是否为内部 PTE（继续遍历）还是叶子 PTE
   ```c
   else if (!(pte & (PTE_R | PTE_W | PTE_X))) {
       base = ppn << PGSHIFT;  // 继续遍历下一级页表
   }
   ```
   如果 PTE 没有 R/W/X 权限位，说明是内部节点，需要继续遍历；否则是叶子节点，可以计算物理地址。

**调试演示：**

在终端 2 中设置断点 `break get_physical_address`，当 ucore 执行访存指令（如 `sd zero,16(sp)`）时，可以观察到：

- 虚拟地址 `addr = 0xffffffffc0203f30`（sp + 16）
- 经过页表遍历后，得到物理地址 `*physical = 0x80203f30`
- 在 SV39 模式下，内核虚拟地址空间通过 1GB superpage 映射到物理地址空间

#### 1.2 单步调试页表翻译部分

通过单步执行 `get_physical_address` 函数，详细观察了页表遍历的完整过程。

**页表翻译流程：**

1. **初始化阶段**：

   - 从 SATP 寄存器获取页表基址：`base = get_field(env->satp, SATP_PPN) << PGSHIFT`
   - 确定页表格式：SV39 模式下 `levels = 3`，`ptidxbits = 9`，`ptesize = 8`

2. **地址分解**：
   虚拟地址在 SV39 模式下分解为：

   - VPN[2] (位 38-30)：第 2 级索引
   - VPN[1] (位 29-21)：第 1 级索引
   - VPN[0] (位 20-12)：第 0 级索引
   - 页内偏移 (位 11-0)

3. **逐级遍历**（第 237-351 行）：

   ```c
   for (i = 0; i < levels; i++, ptshift -= ptidxbits) {
       target_ulong idx = (addr >> (PGSHIFT + ptshift)) & ((1 << ptidxbits) - 1);
       target_ulong pte_addr = base + idx * ptesize;
       target_ulong pte = ldq_phys(cs->as, pte_addr);
       // ... 检查 PTE 有效性和权限 ...
   }
   ```

   - **第 1 级（i=0）**：使用 VPN[2] 索引，读取页表项
     - 如果是叶子 PTE（有 R/W/X 标志），说明是 1GB superpage，直接计算物理地址
     - 如果是内部 PTE，更新 `base = ppn << PGSHIFT`，继续遍历
   - **第 2 级（i=1）**：使用 VPN[1] 索引，读取页表项
     - 如果是叶子 PTE，说明是 2MB superpage
     - 如果是内部 PTE，继续遍历
   - **第 3 级（i=2）**：使用 VPN[0] 索引，读取页表项
     - 必须是叶子 PTE，获取物理页号

4. **权限验证**（第 279-288 行）：

   - 检查读权限：`access_type == MMU_DATA_LOAD && !((pte & PTE_R) || ...)`
   - 检查写权限：`access_type == MMU_DATA_STORE && !(pte & PTE_W)`
   - 检查执行权限：`access_type == MMU_INST_FETCH && !(pte & PTE_X)`
   - 检查用户/超级用户权限：`(pte & PTE_U) && (mode != PRV_U)`

5. **A/D 位更新**（第 291-329 行）：
   如果 PTE 的访问位（A）或脏位（D）需要更新，使用原子操作更新页表项。

6. **计算物理地址**（第 334 行）：
   ```c
   target_ulong vpn = addr >> PGSHIFT;
   *physical = (ppn | (vpn & ((1L << ptshift) - 1))) << PGSHIFT;
   ```
   物理页号 + 页内偏移 = 最终物理地址

**关键观察：**

在调试过程中发现，内核的初始页表使用了 1GB superpage 映射（在 `kern_entry` 中设置的 `boot_page_table_sv39`），因此在第 1 级页表（i=0）就找到了叶子 PTE，跳过了后续两级页表的遍历。这是正常的优化策略，可以加快地址翻译速度。

#### 1.3 TLB 查找的代码实现

在 QEMU 源码中找到了 TLB 查找的 C 代码实现，位于 `accel/tcg/cputlb.c`。

**TLB 查找流程：**

1. **TLB 索引计算**（第 1505 行）：

   ```c
   uintptr_t index = tlb_index(env, mmu_idx, addr);
   ```

   根据虚拟地址和 MMU 索引计算 TLB 条目索引。

2. **TLB 条目获取**（第 1506 行）：

   ```c
   CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
   ```

   获取对应的 TLB 条目指针。

3. **TLB 地址获取**（第 1507 行）：

   ```c
   target_ulong tlb_addr = tlb_addr_write(entry);
   ```

   获取 TLB 中缓存的地址。

4. **TLB 命中检查**（第 1519 行）：

   ```c
   if (!tlb_hit(tlb_addr, addr)) {
       // TLB miss，需要填充
   }
   ```

   `tlb_hit` 函数检查 TLB 是否命中，比较 TLB 中的地址和当前访问的地址。

5. **Victim TLB 检查**（第 1520 行）：

   ```c
   if (!victim_tlb_hit(env, mmu_idx, index, tlb_off, addr & TARGET_PAGE_MASK)) {
       tlb_fill(...);  // 填充 TLB
   }
   ```

   如果主 TLB 未命中，检查 Victim TLB（二级缓存）。

6. **TLB 填充**（第 1522 行）：
   ```c
   tlb_fill(env_cpu(env), addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
   ```
   调用 `tlb_fill` 进行页表遍历并填充 TLB。

**调试观察：**

在 `store_helper` 函数中设置断点，可以观察到：

- TLB 命中的情况：直接使用 TLB 中的地址，无需页表遍历
- TLB miss 的情况：调用 `tlb_fill` → `riscv_cpu_tlb_fill` → `get_physical_address` 进行页表遍历，然后将结果填充到 TLB 中

#### 1.4 QEMU 模拟 TLB 与真实 CPU TLB 的逻辑区别

通过对比 MBARE 模式和 SV39 模式的访存路径，理解了 QEMU 模拟 TLB 与真实 CPU TLB 的逻辑区别。

**MBARE 模式下的访存路径：**

在 `kern_entry` 中，页表设置之前（第 11 行 `sd a0, 0(t0)` 和第 13 行 `sd a1, 0(t0)`），系统处于 MBARE 模式：

- 调用 `get_physical_address` 时，第 171 行的分支直接返回：`*physical = addr`
- 不经过 TLB 查找，不经过页表遍历
- 虚拟地址直接等于物理地址

**SV39 模式下的访存路径：**

在页表设置之后（`csrw satp, t0`），系统进入 SV39 模式：

- 首先在 `store_helper` 中查找 TLB（第 1519 行）
- 如果 TLB miss，调用 `tlb_fill` 进行页表遍历
- 页表遍历完成后，将结果填充到 TLB 中

**关键区别：**

1. **实现方式**：

   - **真实 CPU TLB**：硬件实现，查找速度极快（几个时钟周期），存储在 CPU 的专用缓存中
   - **QEMU 模拟 TLB**：软件实现，存储在内存中（`env->tlb`），查找需要执行 C 代码

2. **TLB miss 处理**：

   - **真实 CPU TLB**：TLB miss 时，硬件 MMU 自动进行页表遍历，对软件透明
   - **QEMU 模拟 TLB**：TLB miss 时，通过软件函数调用（`tlb_fill`）进行页表遍历，可以观察到完整的调用路径

3. **可观察性**：

   - **真实 CPU TLB**：无法直接观察 TLB 的内容和查找过程
   - **QEMU 模拟 TLB**：可以通过 GDB 单步执行，查看 TLB 条目的内容、命中/未命中状态、填充过程等

4. **性能**：
   - **真实 CPU TLB**：查找速度极快，是 CPU 性能的关键组件
   - **QEMU 模拟 TLB**：查找速度较慢，但可以通过软件优化（如 Victim TLB）提高命中率

**调试对比要点：**

- MBARE 模式：直接返回虚拟地址，不经过 TLB 和页表
- SV39 模式：先查找 TLB，miss 后遍历页表，然后填充 TLB
- 观察调用路径的差异：MBARE 模式下可能不会调用 `store_helper` 中的 TLB 查找代码

#### 1.5 调试过程中的有趣细节

1. **1GB Superpage 的发现**：
   在调试过程中发现，内核的初始页表使用了 1GB superpage 映射，因此在第 1 级页表就找到了叶子 PTE，跳过了后续两级页表的遍历。这让我理解了 superpage 的作用：减少页表级数，加快地址翻译速度。

2. **双重 GDB 调试的复杂性**：
   使用两个 GDB 实例（一个调试 QEMU，一个调试 ucore）时，需要协调两个调试器的执行。当终端 3 的 GDB 单步执行时，终端 2 的 GDB 也会相应暂停，这体现了调试器的状态机特性。(everything is a state machine)

3. **页表遍历的循环结构**：
   页表遍历使用了一个巧妙的循环结构：`for (i = 0; i < levels; i++, ptshift -= ptidxbits)`，通过递减 `ptshift` 来逐级提取虚拟地址的不同部分，体现了代码的优雅设计。

#### 1.6 通过大模型解决的问题

1. **GDB 断点设置问题**：

   - **问题**：`break get_physical_address` 提示函数未定义
   - **思路**：意识到可能是静态函数或符号未加载
   - **解决**：通过大模型了解到需要先加载符号文件（`file /workspace/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64`），或者使用文件行号设置断点
   - **交互过程**：询问了 GDB 如何处理静态函数和符号加载的问题，得到了多种解决方案

2. **Docker 路径配置问题**：

   - **问题**：`directory /home/petto/learning/os/labcodes/qemu-4.1.1` 提示路径不存在
   - **思路**：检查 Docker 容器内的实际路径
   - **解决**：通过大模型了解到 Docker 容器内的路径是 `/workspace/qemu-4.1.1`，需要根据环境调整路径
   - **交互过程**：提供了错误信息，大模型帮助分析了 Docker 环境下的路径映射关系

3. **Superpage 的理解**：

   - **问题**：为什么页表遍历只执行了一次循环就返回了？
   - **思路**：怀疑是 superpage 映射
   - **解决**：通过大模型解释了 superpage 的概念，以及如何在第 1 级页表就找到叶子 PTE
   - **交互过程**：提供了调试输出，大模型帮助分析了 PTE 的标志位，确认了这是 1GB superpage 映射

4. **TLB 查找代码的定位**：

   - **问题**：如何在 QEMU 源码中找到 TLB 查找的代码？
   - **思路**：从 `store_helper` 函数入手，查找 TLB 相关的函数调用
   - **解决**：通过大模型了解到 TLB 查找在 `accel/tcg/cputlb.c` 中，关键函数是 `tlb_hit` 和 `tlb_fill`
   - **交互过程**：询问了 TLB 查找的调用路径，大模型提供了详细的代码位置和函数名

5. **MBARE 模式的观察方法**：
   - **问题**：如何在未开启虚拟地址空间时观察访存行为？
   - **思路**：在 `kern_entry` 中，页表设置之前应该处于 MBARE 模式
   - **解决**：通过大模型确认了可以在 `kern_entry` 的前半部分（`csrw satp, t0` 之前）观察 MBARE 模式
   - **交互过程**：询问了内核启动流程，大模型帮助分析了 `kern_entry` 的执行顺序，确认了页表设置的时机

### 调试 2 系统调用以及返回

#### 2.1 ecall 和 sret 指令的 QEMU 处理流程

**调试结果**

1. 查看调用栈![alt text](<Pasted image 20251213182848.png>)
2. cause 为 8 表示是 ecall![alt text](<Pasted image 20251213183029.png>) 3.找到 sert 的地址![alt text](<Pasted image 20251213184201.png>)
3. 使用 hleper_sert 函数断点![alt text](<Pasted image 20251213184902.png>)

**ecall 指令的处理流程：**

1. **指令翻译阶段**（`target/riscv/insn_trans/trans_privileged.inc.c:21-28`）：

   - `trans_ecall` 函数识别 ecall 指令
   - 调用 `generate_exception(ctx, RISCV_EXCP_U_ECALL)` 生成异常
   - 标记为 `DISAS_NORETURN`，退出当前翻译块

2. **异常处理阶段**（`target/riscv/cpu_helper.c:507-591`）：
   在 `riscv_cpu_do_interrupt` 函数中：

   - **异常识别**（第 513-546 行）：

     - 从 `cs->exception_index` 提取异常原因 `cause = 8`（RISCV_EXCP_U_ECALL）
     - 根据当前特权模式映射到对应的 ecall 类型（用户态 ecall 保持为 8）

   - **保存上下文**（第 555-563 行）：

     ```c
     // 更新 mstatus：保存 SIE 到 SPIE，设置 SPP，清除 SIE
     s = set_field(s, MSTATUS_SPIE, get_field(s, MSTATUS_SIE));
     s = set_field(s, MSTATUS_SPP, env->priv);  // 保存当前特权模式（用户态=0）
     s = set_field(s, MSTATUS_SIE, 0);          // 禁用监督态中断

     // 设置异常相关 CSR
     env->scause = cause | (async << (TARGET_LONG_BITS - 1));  // scause = 8
     env->sepc = env->pc;                                       // 保存 ecall 指令地址
     env->sbadaddr = tval;                                      // 异常值（ecall 时为 0）
     ```

   - **跳转到异常处理入口**（第 564-566 行）：
     ```c
     env->pc = (env->stvec >> 2 << 2) +
               ((async && (env->stvec & 3) == 1) ? cause * 4 : 0);
     riscv_cpu_set_mode(env, PRV_S);  // 切换到监督态
     ```
     跳转到 `stvec` 指向的异常向量表入口（ucore 的 `__alltraps`）

**sret 指令的处理流程：**

1. **指令翻译阶段**（`target/riscv/insn_trans/trans_privileged.inc.c:43-59`）：

   - `trans_sret` 函数识别 sret 指令
   - 调用 `gen_helper_sret(cpu_pc, cpu_env, cpu_pc)` 生成 helper 调用
   - 标记为 `DISAS_NORETURN`，退出当前翻译块

2. **特权返回处理**（`target/riscv/op_helper.c:74-102`）：
   在 `helper_sret` 函数中：

   - **权限检查**（第 76-88 行）：

     - 检查当前是否在监督态或更高特权模式
     - 检查压缩指令支持（对齐检查）
     - 检查 TSR 位（是否允许 sret）

   - **恢复上下文**（第 90-99 行）：

     ```c
     // 从 mstatus 恢复状态
     target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);  // 获取之前特权模式（用户态=0）

     // 恢复 SIE 位（从 SPIE）
     mstatus = set_field(mstatus, MSTATUS_SIE, get_field(mstatus, MSTATUS_SPIE));

     // 清除 SPIE，设置 SPP = U（为下次做准备）
     mstatus = set_field(mstatus, MSTATUS_SPIE, 0);
     mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);

     // 切换回之前的特权模式
     riscv_cpu_set_mode(env, prev_priv);  // 从 PRV_S 切回 PRV_U
     ```

   - **返回用户空间**（第 101 行）：
     ```c
     return retpc;  // 返回 sepc（ecall 的下一条指令地址）
     ```

**关键观察点：**

- ecall 和 sret 都会触发翻译块退出（`DISAS_NORETURN`），因为它们修改了 PC 和特权模式
- QEMU 通过软件完全模拟了硬件的异常处理机制，包括 CSR 的读写和特权模式切换
- 调试中发现 `env->cause` 并不是直接存储的字段，而是从 `cs->exception_index` 临时提取的

#### 2.2 TCG Translation（指令翻译）功能

**TCG 的工作原理：**

QEMU 使用 TCG（Tiny Code Generator）进行动态二进制翻译：

1. **指令解码**：识别 RISC-V 指令（如 ecall、sret）
2. **TCG 代码生成**：将 guest 指令转换为 TCG 中间代码（IR）
3. **代码编译**：将 TCG IR 编译为 host 机器码（x86_64），存储在 `code_gen_buffer`
4. **执行**：直接执行生成的 host 代码

**与双重 GDB 调试实验的关系：**

在调试 1（页表查询过程）中，我们同样观察到了 TCG Translation 的作用：

- **访存指令的翻译**：当 ucore 执行访存指令（如 `sd`、`lw`）时，QEMU 将其翻译为 x86_64 指令序列
- **helper 函数调用**：翻译后的代码会调用 helper 函数（如 `helper_le_stq_mmu`、`store_helper`）来处理访存
- **TLB 查找和页表遍历**：这些 helper 函数实现了完整的地址翻译流程

**关键区别：**

- **普通指令**（如访存）：翻译后生成直接的 host 代码，可以链式执行（chaining）
- **特权指令**（如 ecall、sret）：标记为 `DISAS_NORETURN`，必须退出当前 TB，因为：
  - 修改了 PC（跳转到新的地址空间）
  - 修改了特权模式（改变 CPU 状态）
  - 可能触发异常处理流程

通过双重调试，我们能够观察到：

- 一条 RISC-V 指令如何被翻译为多条 x86_64 指令
- 软件如何完整模拟硬件的行为（异常处理、特权切换等）
- TCG 翻译和执行的具体实现细节

#### 2.3 调试过程中的有趣细节和学到的知识

**有趣的细节：**

1. **`env->cause` 不存在**：调试时发现 `print/x env->cause` 报错 "There is no member named cause"，原来 `cause` 是从 `cs->exception_index` 临时提取的局部变量，不是 CPU 状态的直接成员。

2. **siglongjmp 的使用**：执行 sret 后，QEMU 使用 `siglongjmp` 跳转回执行循环。这看起来像"异常跳转"，但实际上是 QEMU 正常的上下文切换机制，用于退出当前翻译块并重新进入执行循环。

3. **mstatus 的位操作**：观察到 QEMU 使用 `set_field` 和 `get_field` 宏来操作 mstatus 的各个位字段，这种设计使得代码更加清晰和安全。

4. **翻译块的退出机制**：发现 `DISAS_NORETURN` 标记的使用，这是 TCG 翻译中关键的设计，确保在某些指令（如特权指令、跳转指令）执行后，能够正确地退出当前翻译块。

**学到的知识：**

1. **软件模拟硬件的完整性**：QEMU 不仅模拟指令执行，还完整模拟了硬件的异常处理、特权切换、CSR 读写等机制。这种软件模拟使得我们能够在调试器中详细观察硬件行为。

2. **动态二进制翻译的效率**：TCG 通过翻译块（TB）的方式，将一段连续的 guest 代码翻译后缓存执行，避免了逐条指令解释的开销。只有当遇到跳转、异常等情况时才退出 TB。

3. **双重调试的价值**：通过同时调试 ucore 和 QEMU，我们能够从两个层面理解系统：

   - **ucore 层面**：看到操作系统如何处理异常和系统调用
   - **QEMU 层面**：看到模拟器如何模拟硬件行为

4. **TCG Helper 函数的作用**：复杂操作（如异常处理、特权返回）通过 helper 函数实现，这些函数在 C 代码中，可以被 GDB 调试，使得我们可以深入理解实现细节。

#### 2.4 通过大模型解决的问题

**问题 1：查找 QEMU 源码中 sret 的处理函数**

**情景**：需要找到 QEMU 中处理 sret 指令的关键代码位置，以便设置断点进行调试。

**思路**：知道 RISC-V 指令通常在 `target/riscv/` 目录下，但不确定具体文件和函数名。

**交互过程**：

1. 询问："qemu 的源码中是如何处理 ecall 指令的，给我找一下关键的代码和流程"
2. 大模型通过搜索找到了相关文件位置
3. 继续询问："查找专门的 sret helper 函数"，提供了具体的文件路径 `target/riscv/op_helper.c:74`

**解决结果**：

- 找到了 `helper_sret` 函数在 `target/riscv/op_helper.c:74`
- 找到了指令翻译函数 `trans_sret` 在 `target/riscv/insn_trans/trans_privileged.inc.c:43`
- 成功设置了断点并追踪了完整的 sret 处理流程

**问题 2：理解 siglongjmp 的作用**

**情景**：在执行 sret 后，GDB 进入了 `cpu_loop_exit` → `siglongjmp` 的调用链，看起来像是"异常跳转"，不确定这是否正常。

**思路**：这是 QEMU 的执行模型，但需要确认这是正常的执行流程而非错误。

**交互过程**：

1. 展示调试输出，询问："后面是怎么了"
2. 大模型解释了这是 QEMU TCG 执行模型的正常机制
3. 解释了为什么需要退出翻译块（因为 sret 修改了 PC 和特权模式）

**解决结果**：

- 理解了 `siglongjmp` 是 QEMU 正常的上下文切换机制
- 理解了翻译块（TB）的退出机制
- 学会了如何验证 sret 是否成功（查看 PC 和特权模式）

**问题 3：理解 ecall 处理中的 CSR 设置流程**

**情景**：在调试 `riscv_cpu_do_interrupt` 时，需要理解 QEMU 如何设置 `scause`、`sepc` 等 CSR 寄存器。

**交互过程**：

1. 展示了调试输出，询问："现在在做什么呢"
2. 大模型逐行解释了 ecall 处理的关键步骤
3. 指出了需要观察的关键状态（mstatus 的位字段、特权模式切换等）

**解决结果**：

- 理解了 ecall 处理中 CSR 设置的完整流程
- 学会了如何查看和验证关键寄存器状态
- 理解了特权模式从用户态切换到监督态的过程
