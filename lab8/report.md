# 练习

## 练习 0：填写已有实验

本实验依赖实验 2/3/4/5/6/7。请把你做的实验 2/3/4/5/6/7 的代码填入本实验中代码中有“LAB2”/“LAB3”/“LAB4”/“LAB5”/“LAB6” /“LAB7”的注释相应部分。并确保编译通过。注意：为了能够正确执行 lab8 的测试应用程序，可能需对已完成的实验 2/3/4/5/6/7 的代码进行进一步改进。
lab2:best_fit_pmm[ch]
lab3:无
lab4:update proc_run,do_fork
lab5:do_fork;pmm.c;
lab6:update alloc_proc

## 练习 1: 完成读文件操作的实现（需要编码）

首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在 kern/fs/sfs/sfs_inode.c 中 的 sfs_io_nolock()函数，实现读文件中数据的代码。

### 实现思路

`sfs_io_nolock` 函数是 SFS 文件系统中文件 I/O 操作的核心函数，负责在指定偏移位置读写文件内容。由于文件数据在磁盘上以块为单位存储，而读写请求可能跨越多个块且不一定对齐，因此需要分三种情况处理：

1. **首块未对齐部分**：如果起始偏移 `offset` 不在块边界上，需要处理第一个块中从 `offset` 到块末尾的数据
2. **中间完整块**：处理所有完整的块（块对齐的数据）
3. **末块未对齐部分**：如果结束位置 `endpos` 不在块边界上，需要处理最后一个块中从块开始到 `endpos` 的数据

### 代码实现

```c
blkoff = offset % SFS_BLKSIZE;

// (1) 处理首块未对齐部分
if (blkoff != 0) {
    size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset);
    if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
        goto out;
    }
    if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
        goto out;
    }
    alen += size;
    if (nblks == 0) {
        goto out;
    }
    buf += size;
    blkno++;
    nblks--;
}

// (2) 处理中间完整块
while (nblks > 0) {
    if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
        goto out;
    }
    if ((ret = sfs_block_op(sfs, buf, ino, 1)) != 0) {
        goto out;
    }
    alen += SFS_BLKSIZE;
    buf += SFS_BLKSIZE;
    blkno++;
    nblks--;
}

// (3) 处理末块未对齐部分
size = endpos % SFS_BLKSIZE;
if (size != 0) {
    if ((ret = sfs_bmap_load_nolock(sfs, sin, endpos / SFS_BLKSIZE, &ino)) != 0) {
        goto out;
    }
    if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
        goto out;
    }
    alen += size;
}
```

### 实现细节说明

#### 第一部分：首块未对齐处理

- 计算 `blkoff = offset % SFS_BLKSIZE`，确定在块内的偏移
- 如果 `blkoff != 0`，说明起始位置不在块边界上
- 计算需要读写的大小：
  - 如果还有后续块（`nblks != 0`），则读取从 `blkoff` 到块末尾的数据（`SFS_BLKSIZE - blkoff`）
  - 如果没有后续块（`nblks == 0`），则读取从 `offset` 到 `endpos` 的所有数据（`endpos - offset`）
- 使用 `sfs_bmap_load_nolock` 将逻辑块号转换为物理块号
- 使用 `sfs_buf_op`（`sfs_rbuf` 或 `sfs_wbuf`）进行部分块的读写操作
- 更新缓冲区指针和块号，为后续处理做准备

#### 第二部分：中间完整块处理

- 使用 `while` 循环逐个处理每个完整的块
- 由于逻辑块到物理块的映射可能不连续，需要逐个处理而不能批量处理
- 对每个块：
  - 使用 `sfs_bmap_load_nolock` 获取物理块号
  - 使用 `sfs_block_op`（`sfs_rblock` 或 `sfs_wblock`）进行整块读写
  - 更新缓冲区指针、逻辑块号和剩余块数

#### 第三部分：末块未对齐处理

- 计算 `size = endpos % SFS_BLKSIZE`，确定最后一个块需要读写的字节数
- 如果 `size != 0`，说明结束位置不在块边界上
- 使用 `sfs_bmap_load_nolock` 获取最后一个块的物理块号
- 使用 `sfs_buf_op` 从块开始位置（偏移 0）读取 `size` 字节的数据

### 关键函数说明

- **`sfs_bmap_load_nolock`**：将文件内的逻辑块号转换为磁盘上的物理块号，如果块不存在且是写操作，会自动分配新块
- **`sfs_buf_op`**：用于读写块内的部分数据，使用文件系统的缓冲区进行非对齐 I/O
- **`sfs_block_op`**：用于批量读写完整的块，效率更高

### 设计要点

1. **处理未对齐情况**：文件读写请求可能不按块对齐，需要分别处理首尾部分
2. **逐个处理块**：由于逻辑块到物理块的映射可能不连续，中间完整块需要逐个处理
3. **动态扩展**：写操作时，如果访问的块不存在，`sfs_bmap_load_nolock` 会自动分配新块
4. **错误处理**：每个步骤都检查返回值，出错时通过 `goto out` 跳转到错误处理代码

## 练习 2: 完成基于文件系统的执行程序机制的实现（需要编码）

改写 proc.c 中的 load_icode 函数和其他相关函数，实现基于文件系统的执行程序机制。执行：make qemu。如果能看看到 sh 用户程序的执行界面，则基本成功了。如果在 sh 用户界面上可以执行`exit`, `hello`（更多用户程序放在`user`目录下）等其他放置在`sfs`文件系统中的其他执行程序，则可以认为本实验基本成功。

### 练习 2 实现思路

`load_icode` 函数负责从文件系统中加载 ELF 格式的可执行文件到进程的内存空间，并设置好进程的执行环境。与 lab5 不同的是，lab8 中需要从文件描述符读取文件内容，而不是从内存中的二进制数据加载。此外，还需要在用户栈上设置 `argc` 和 `argv` 参数。

### 实现步骤

#### 1. 创建新的内存管理结构

```c
// (1) create a new mm for current process
if ((mm = mm_create()) == NULL) {
    goto bad_mm;
}

// (2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
if (setup_pgdir(mm) != 0) {
    goto bad_pgdir_cleanup_mm;
}
```

首先创建新的内存管理结构 `mm_struct`，然后设置页目录表。

#### 2. 读取并验证 ELF 文件头

```c
// (3.1) read raw data content in file and resolve elfhdr
if ((ret = load_icode_read(fd, &elf, sizeof(struct elfhdr), 0)) != 0) {
    goto bad_elf_cleanup_pgdir;
}

// (3.3) This program is valid?
if (elf.e_magic != ELF_MAGIC) {
    ret = -E_INVAL_ELF;
    goto bad_elf_cleanup_pgdir;
}
```

使用 `load_icode_read` 函数从文件描述符读取 ELF 文件头，并验证魔数是否正确。

#### 3. 加载程序段（TEXT/DATA/BSS）

对每个程序段（program header）：

- 读取程序头信息
- 使用 `mm_map` 建立虚拟内存映射
- 使用 `pgdir_alloc_page` 分配物理页面
- 使用 `load_icode_read` 从文件读取数据到内存
- 对于 BSS 段，使用 `memset` 清零

关键代码：

```c
// 读取程序头
if ((ret = load_icode_read(fd, &ph, sizeof(struct proghdr), phoff + i * sizeof(struct proghdr))) != 0) {
    goto bad_cleanup_mmap;
}

// 建立虚拟内存映射
if ((ret = mm_map(mm, ph.p_va, ph.p_memsz, vm_flags, NULL)) != 0) {
    goto bad_cleanup_mmap;
}

// 读取文件内容到内存
if ((ret = load_icode_read(fd, page2kva(page) + off, size, ph.p_offset + (start - ph.p_va))) != 0) {
    goto bad_cleanup_mmap;
}
```

#### 4. 设置用户栈

```c
// (4) call mm_map to setup user stack
vm_flags = VM_READ | VM_WRITE | VM_STACK;
if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
    goto bad_cleanup_mmap;
}
// 分配栈页面
assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER) != NULL);
// ... 分配更多栈页面
```

建立用户栈的虚拟内存映射，并分配初始栈页面。

#### 5. 设置进程的内存管理结构

```c
// (5) setup current process's mm, cr3, reset pgidr (using lsatp MARCO)
mm_count_inc(mm);
current->mm = mm;
current->pgdir = PADDR(mm->pgdir);
lsatp(PADDR(mm->pgdir));
```

将新创建的内存管理结构关联到当前进程，并更新页表基址寄存器。

#### 6. 在用户栈上设置 argc 和 argv

这是 lab8 相比 lab5 新增的重要功能。需要将命令行参数放置到用户栈上，格式如下（从高地址到低地址）：

- 参数字符串（argv[0], argv[1], ...）
- argv 指针数组（包含 argc+1 个元素，最后一个为 NULL）
- argc（整数）

实现步骤：

1. 计算所需的总空间大小
2. 从栈顶向下分配所有需要的页面
3. 复制参数字符串到栈上
4. 设置 argv 指针数组，指向对应的字符串
5. 设置 argc 值
6. 更新栈指针

关键代码：

```c
// 计算总大小
size_t string_size = 0;
for (i = 0; i < argc; i++) {
    string_size += strlen(kargv[i]) + 1;
}
size_t argv_array_size = sizeof(uintptr_t) * (argc + 1);
size_t argc_size = sizeof(int);
size_t total_size = string_size + argv_array_size + argc_size;

// 分配页面
uintptr_t argv_bottom = stacktop - total_size;
uintptr_t va = argv_bottom;
while (va < stacktop) {
    uintptr_t la = ROUNDDOWN(va, PGSIZE);
    if (pgdir_alloc_page(mm->pgdir, la, PTE_USER) == NULL) {
        goto bad_cleanup_mmap;
    }
    va = la + PGSIZE;
}

// 复制字符串并设置 argv 数组
// ... (详细实现见代码)
```

#### 7. 设置 trapframe

```c
// (7) setup trapframe for user environment
struct trapframe *tf = current->tf;
uintptr_t sstatus = tf->status;
memset(tf, 0, sizeof(struct trapframe));

// 设置用户栈指针
tf->gpr.sp = stacktop;
// 设置程序入口点
tf->epc = elf.e_entry;
// 设置状态寄存器：用户模式
tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
```

设置 trapframe，包括：

- `gpr.sp`：用户栈指针，指向 argc 的位置
- `epc`：程序入口地址（ELF 文件头中的 `e_entry`）
- `status`：状态寄存器，清除 SPP 位（表示之前是用户模式），设置 SPIE 位（允许用户模式中断）

### 练习 2 关键函数说明

- **`load_icode_read`**：从文件描述符读取指定偏移和长度的数据
- **`mm_map`**：建立虚拟内存映射
- **`pgdir_alloc_page`**：分配物理页面并建立页表项
- **`get_page`**：根据虚拟地址获取对应的 Page 结构

### 练习 2 与 lab5 的主要区别

1. **数据来源**：lab5 从内存中的二进制数据加载，lab8 从文件描述符读取
2. **参数传递**：lab8 需要在用户栈上设置 `argc` 和 `argv`
3. **文件操作**：使用 `load_icode_read` 替代直接的内存访问

### 练习 2 设计要点

1. **错误处理**：每个步骤都检查返回值，失败时通过 `goto` 跳转到相应的清理代码
2. **内存对齐**：argv 数组需要按指针大小对齐
3. **页面分配**：先分配所有需要的页面，再复制数据，避免跨页访问问题
4. **栈布局**：按照标准 C 程序参数传递约定布局栈空间

## 扩展练习 Challenge1：完成基于“UNIX 的 PIPE 机制”的设计方案

如果要在 ucore 里加入 UNIX 的管道（Pipe）机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的 C 语言 struct 定义。在网络上查找相关的 Linux 资料和实现，请在实验报告中给出设计实现”UNIX 的 PIPE 机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）

## 扩展练习 Challenge2：完成基于“UNIX 的软连接和硬连接机制”的设计方案

如果要在 ucore 里加入 UNIX 的软连接和硬连接机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的 C 语言 struct 定义。在网络上查找相关的 Linux 资料和实现，请在实验报告中给出设计实现”UNIX 的软连接和硬连接机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）
