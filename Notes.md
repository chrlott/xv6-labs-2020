# MIT 6.S081笔记
## Chapter 2 : syscall
### 操作系统防御性
硬件隔离：
1. 内核/用户模式
2. page table（虚拟内存Virtual Memory）

### 内核/用户模式
1. kernel mode->user mode：

用户侧调用系统调用。指令ECALL（risc-v中）传入一个参数表示该系统调用。每一次应用程序执行ECALL指令，应用程序都会通过这个唯一的接入点进入到内核中。内核侧调用syscall函数检查ECALL的参数，调用相应的系统调用。


## Chapter 3 : page tables
### 页表
虚拟内存的意义是为了实现内存的**隔离**，防止进程破坏别的进程。

一旦MMU打开了，CPU执行的每一条指令中的地址都是虚拟地址。MMU使用虚拟与物理内存对应的表单（页表）对地址进行映射，但页表是存在内存中的。寄存器SATP会保存页表的地址。

为了实现进程间的隔离，每个进程都有独立的页表。相同的虚拟内存地址，就可以翻译到不同的物理内存地址。

关于页表的设计有两个问题：
1. 但是如果表单中的条目记录的是虚拟地址，那么表单会非常巨大。事实上，表单中记录的不是地址而是page，RISC-V中，一个page是4KB，也就是4096Bytes。
2. 如果每个进程都保存2^27个条目的页表，将消耗大量的内存。因此采用三层页表检索的方法，每个页表含2^9个条目。这样做的好处是，使用一个page时只需要3 * 512个PTE，而不是2^27个PTE。

XV6有一个叫做walk的函数，它在软件中实现了MMU硬件相同的功能，完成3级 page table的查找。
### 内核地址空间
![3_2](/img/3_2.PNG)

### 用户地址空间
![3_3](/img/3_3.PNG)

text区域是程序的指令，data区域存放的是初始化了的全局变量，BSS包含了未被初始化或者初始化为0的全局变量。之所以这些变量要单独列出来，是因为例如你在C语言中定义了一个大的矩阵作为全局变量，它的元素初始值都是0，为什么要为这个矩阵分配内存呢？其实只需要记住这个矩阵的内容是0就行。


## Chapter 4 : trap
### 前置知识
![4_1](/img/4_1.PNG)

caller: not preserved across funcall

callee: preserved across funcall
### Trap
trap发生时需要：
- 保存程序计数器PC
- mode改为supervisor mode
- 保存32个用户寄存器，以便之后恢复
- SATP寄存器指向kernel page table
- 堆栈寄存器指向内核的一个地址，来调用内核的C函数

supervisor mode拥有的特权：使用SATP等寄存器，访问PTE-U=0的页表，仅此而已。

write系统调用的过程
![4_2](/img/4_2.PNG)

### ECALL函数
ecall并不会切换page table，我们需要在user page table中的某个地方来执行最初的内核代码。而这个trampoline page（PTE-U=0）。内核事先设置好了STVEC寄存器的内容为0x3ffffff000，这就是trampoline page的起始位置，也是uservec函数的起始。

ecall做的事：
1. 将代码从user mode改到supervisor mode
2. 将程序计数器的值保存在了SEPC寄存器
3. 跳转到STVEC寄存器指向的指令

### uservec函数（trampoline.S）

``` 
# save the user a0 in p->trapframe->a0
csrr t0, sscratch
sd t0, 112(a0)
```
将用户寄存器保存到内存（trapframe）。寄存器SSCRATCH保存了用户页表中trapframe的地址。将SSCRATCH寄存器与a0寄存器内容交换，将所有寄存器保存到偏移量（trapframe地址）+a0的位置。

```
# restore kernel stack pointer from p->trapframe->kernel_sp
ld sp, 8(a0)
```
初始化Stack Pointer指向这个进程的kernel stack的最顶端
```
# make tp hold the current hartid, from p->trapframe->kernel_hartid
ld tp, 32(a0)
```
将CPU核的编号也就是hartid保存在tp寄存器
```
# load the address of usertrap(), p->trapframe->kernel_trap
ld t0, 16(a0)
```
向t0寄存器写入将要执行的第一个C函数的指针，也就是函数usertrap的指针。我们在后面会使用这个指针。
```
# restore kernel page table from p->trapframe->kernel_satp
ld t1, 0(a0)
csrw satp, t1
sfence.vma zero, zero
```
向t1寄存器写入kernel page table的地址，并把satp和t1交换。切换到kernel page table。

trampoline代码在用户空间和内核空间都映射到了同一个地址，因此切换page table时，寻址的结果不会改变，我们实际上就可以继续在同一个代码序列中执行程序而不崩溃。

### usertrap函数（trap.c）
usertrap某种程度上存储并恢复硬件状态，但是它也需要检查触发trap的原因（查看SCAUSE寄存器），以确定相应的处理方式

如果是系统调用触发trap，则进入syscall函数，根据trapframe中保存的a7寄存器的值进入指定的系统调用函数，并把返回值写入trapframe中a0。进入syscall和出来后都需要检查进程是否被杀掉了。

最后，usertrap调用了一个函数usertrapret。

### 返回user mode
usertrapret函数设置好返回到用户空间之前内核要做的工作，最后调用trampoline中的sret函数，sret是我们在kernel中的最后一条指令，当执行完这条指令：

- 程序会切换回user mode
- SEPC寄存器的数值会被拷贝到PC寄存器（程序计数器）
- 重新打开中断

系统调用被刻意设计的看起来像是函数调用，但是背后的user/kernel转换比函数调用要复杂的多。之所以这么复杂，很大一部分原因是要保持user/kernel之间的隔离性，内核不能信任来自用户空间的任何内容。

## Chapter 5 : lazy page allocation
### 前置知识
page fault触发的trap机制并且进入到内核空间后，可以得到以下的信息：
- 引起page fault的内存地址（STVAL寄存器）
- 引起page fault的原因类型（SCAUSE寄存器）
- 引起page fault时的程序计数器值，这表明了page fault在用户空间发生的位置，以便重新执行对应的指令（SEPC寄存器中，并同时会保存在trapframe->epc中）
### Lazy Page Allocation
sbrk是XV6提供的系统调用，它使得用户应用程序能扩大自己的heap。sbrk指向的是heap的最底端，同时也是stack的最顶端，该位置以进程中的sz字段_p->sz表示。
![5_1](/img/5_1.PNG)

为了避免内存浪费，采用lazy page allocation。sbrk系统调用基本上不做任何事情，唯一需要做的事情就是提升_p->sz_，将_p->sz_增加n，其中n是需要新分配的内存page数量。但是内核在这个时间点并不会分配任何物理内存。之后应用程序如果使用到了新申请的那部分内存，这时会触发page fault，因为我们还没有将新的内存映射到page table。所以，如果我们解析一个大于旧的_p->sz_，但是又小于新的_p->sz（注，也就是旧的p->sz + n）_的虚拟地址，我们希望内核能够分配一个内存page，并且重新执行指令。

### Zero-Fill-On-Demand
地址空间的BSS段是未初始化的全局变量，有如此多的内容全是0的page，在物理内存中，我只需要分配一个page，这个page的内容全是0。然后将所有虚拟地址空间的全0的page都map到这一个物理page上。这样至少在程序启动的时候能节省大量的物理内存分配。这部分PTE都是只读的，发生page fault以后创建一个新的page，将其内容设置为0，并重新执行指令。

### Copy On Write Fork（COW）
当我们创建子进程时，与其创建，分配并拷贝内容到新的物理内存，其实我们可以直接共享父进程的物理内存page。所以这里，我们可以设置子进程的PTE指向父进程对应的物理内存page。为了确保进程间的隔离性，我们可以将这里的父进程和子进程的PTE的标志位都设置成只读的。

### Demand Paging
在lazy allocation中，如果内存耗尽了该如何办？

可以撤回并释放page，那么你就有了一个新的空闲的page。有很多种策略来选择撤回哪一个page，包括LRU，还会参考是否是dirty page、page的access标识（是否被读或写）。该标识会定期被操作系统清零。
