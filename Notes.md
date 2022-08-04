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
![3_0](/img/3_0.PNG)

XV6有一个叫做walk的函数，它在软件中实现了MMU硬件相同的功能，完成3级 page table的查找。
### 内核地址空间
![3_2](/img/3_2.PNG)

### 用户地址空间
![3_3](/img/3_3.PNG)

text区域是程序的指令，data区域存放的是初始化了的全局变量，BSS包含了未被初始化或者初始化为0的全局变量。之所以这些变量要单独列出来，是因为例如你在C语言中定义了一个大的矩阵作为全局变量，它的元素初始值都是0，为什么要为这个矩阵分配内存呢？其实只需要记住这个矩阵的内容是0就行。


## Chapter 4 : trap
### 前置知识
![4_1](/img/4_1.PNG)

Callee vs caller saved 的约定是约定谁负责在整个调用中保存和恢复寄存器中的值。

- Caller-saved register(又名易失性寄存器）用于保存不需要在各个调用之间保留的临时数据。

因此，如果要在过程调用后恢复该值，则调用方caller有责任将这些寄存器压入堆栈或将其复制到其他位置。不过，让调用销毁这些寄存器中的临时值是正常的。从被调用方的角度来看，您的函数可以自由覆盖（也就是破坏）这些寄存器，而无需保存/恢复。

- Callee-saved register（又称非易失性寄存器）用于保存应在每次调用中保留的长寿命值。

当调用者进行过程调用时，可以期望这些寄存器在被调用者返回后将保持相同的值，这使被调用者有责任在返回调用者之前保存它们并恢复它们, 还是不要碰它们。


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
ecall并不会切换page table，我们需要在user page table中的某个地方来执行最初的内核代码。而这就是trampoline page（PTE-U=0）。内核事先设置好了STVEC寄存器的内容为0x3ffffff000，这就是trampoline page的起始位置（也就是uservec函数的起始）。

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

## Chapter 6 : interrupt
当遇到中断，操作系统需要保存当前的工作，处理中断，处理完成之后再恢复之前的工作。系统调用，page fault，中断，都使用相同的机制。
### 设备驱动
1. 大部分驱动都分为两个部分：
- bottom部分通常是Interrupt handler。当一个中断送到了CPU，并且CPU设置接收这个中断，CPU会调用相应的Interrupt handler。

- top部分，是用户进程，或者内核的其他部分调用的接口。对于UART来说，这里有read/write接口，这些接口可以被更高层级的代码调用。

2. 如何对设备进行编程：

操作系统需要知道这些设备位于物理地址空间的具体位置（由主板制造商决定），然后再通过普通的load/store指令对这些地址进行编程。

通常情况下，驱动中会有一些队列（或者说buffer），top部分的代码会从队列中读写数据，而Interrupt handler（bottom部分）同时也会向队列中读写数据。这里的队列可以将并行运行的设备和CPU解耦开来。

### UART
当shell需要输出时会调用write系统调用最终走到uartwrite函数中，这个函数会在循环中将buf中的字符一个一个的向UART硬件写入。UART硬件一次只能接受一个字符的传输，可能会非常慢，或许每秒只能传输1000个字符，所以不能通过循环来等待UART完成字符传输。

UART硬件会在完成传输一个字符后，触发一个中断。所以UART驱动中除了uartwrite函数外，还有名为uartintr的中断处理程序。这个中断处理程序会在UART硬件触发中断时由trap.c代码调用，wake uartwrite中的sleep函数，使其恢复执行，并尝试发送一个新的字符。

sleep函数和wakeup函数都带有一个叫做sleep channel的参数，可以唤醒等待的特定事件的线程。


## Chapter 7 : Multithreading
V6内核共享了内存，对于每个用户进程都有一个内核线程来执行来自用户进程的系统调用。

每一个用户进程都有独立的内存地址空间，并且包含了一个线程，这个线程控制了用户进程代码指令的执行，不会共享内存。

1. 从一个用户进程切换到另一个用户进程，都需要从第一个用户进程接入到内核中，保存用户进程的状态并运行第一个用户进程的内核线程。（用户寄存器存在`trapframe`中，内核线程的寄存器存在`context`中）
2. 再从第一个用户进程的内核线程切换到第二个用户进程的内核线程。
3. 之后，第二个用户进程的内核线程暂停自己，并恢复第二个用户进程的用户寄存器。
4. 最后返回到第二个用户进程继续执行。

任何运行在CPU1上的进程，当它决定出让CPU，它都会切换到CPU1对应的调度器线程，并由调度器线程切换到下一个进程。

trapframe只包含进入和离开内核时的数据。而context结构体中包含的是在内核线程和调度器线程之间切换时，需要保存和恢复的数据。

内核会在两个场景下出让CPU。当定时器中断触发了，内核总是会让当前进程出让CPU，因为我们需要在定时器中断间隔的时间点上交织执行所有想要运行的进程。另一种场景就是任何时候一个进程调用了系统调用并等待I/O，例如等待你敲入下一个按键，在你还没有按下按键时，等待I/O的机制会触发出让CPU。

当发生定时器中断时，进入usertrap函数，之后调用devintr函数来判断是何种中断，之后返回到usertrap函数，如果是定时器中断则运行yield函数。

```
// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}
```
yield函数获得了锁，修改进程状态并进入sched函数。
sched函数基本没有干任何事情，只是做了一些合理性检查，比如进程是否持有除了进程锁以外的锁，之后进入swtch函数。

```
//sched
swtch(&p->context, &mycpu()->context);
```
```
#swtch
.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        ...

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ...        
        ret
```
swtch函数在保存完当前内核线程的内核寄存器之后`&p->context`，就会恢复当前CPU核的调度器线程的寄存器`&mycpu()->context`，并继续执行当前CPU核的调度器线程。调度器线程中的代码会返回到ra寄存器中的地址，也就是**scheduler函数中swtch函数返回的位置（之前调度器线程对于swtch函数的调用）**。虽然pid为3的spin进程也调用了swtch函数，但是那个swtch函数还没有返回，而是保存在了pid为3的栈和context对象中。
```
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    int nproc = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state != UNUSED) {
        nproc++;
      }
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    if(nproc <= 2) {   // only init and sh exist
      intr_on();
      asm volatile("wfi");
    }
  }
}
```
调度器线程进入scheduler函数，选择一个RUNABLE的进程B运行，调用swtch函数保留调度器线程的寄存器，并从B进程的swtch函数返回。

**swtch函数是线程切换的核心，但是swtch函数中只有保存寄存器，再加载寄存器的操作。** 线程除了寄存器以外的还有很多其他状态，它有变量，堆中的数据等等，但是所有的这些数据都在内存中，并且会保持不变。

## Chapter 8 : Locks
### lost awake
lost awake发生原因：sleep归还condition lock后还未设置进程状态为SLEEPING，立刻wakeup不能找到已SLEEP的进程，所以会丢失一次wake的机会。
解决方法：
- 调用sleep时需要持有condition lock，这样sleep函数才能知道相应的锁。
- sleep函数只有在获取到进程的锁p->lock之后，才能释放condition lock。
- wakeup需要同时持有两个锁（进程锁、condition锁）才能查看进程，唤醒sleep的进程。

对于sleep的调用都需要包装在一个循环中，这样从sleep中返回的时候才能够重新检查condition是否还符合，不符合可以重新进入sleep

### wait
进程要退出时进入exit函数，并释放绝大部分资源，但进程栈、trapframe、pagetable等在进程运行的时候释放是很奇怪的，所以这些资源都是由父进程释放的。如果一个进程exit了，并且它的父进程调用了wait系统调用，父进程的wait会返回，扫描进程表单，找到父进程是自己且状态是ZOMBIE的进程。之后由父进程调用freeproc函数，来完成释放进程资源的最后几个步骤。最后子进程的状态会被设置成UNUSED，fork系统调用才能重用进程在进程表单的位置。

## Chapter 9 : file system
![9_1](/img/9_1.PNG)
文件系统最好按照分层的方式进行理解:
- 在最底层是磁盘，也就是一些实际保存数据的存储设备，正是这些设备提供了持久化存储。
- 在这之上是buffer cache或者说block cache，这些cache可以避免频繁的读写磁盘。这里我们将磁盘中的数据保存在了内存中。
- 为了保证持久性，再往上通常会有一个logging层。许多文件系统都有某种形式的logging，我们下节课会讨论这部分内容，所以今天我就跳过它的介绍。
- 在logging层之上，XV6有inode cache，这主要是为了同步（synchronization），我们稍后会介绍。inode通常小于一个disk block，所以多个inode通常会打包存储在一个disk block中。为了向单个inode提供同步操作，XV6维护了inode cache。
- 再往上就是inode本身了。它实现了read/write。
- 再往上，就是文件名，和文件描述符操作。

### disk
xv6中，**block 1024字节**
![9_2](/img/9_2.PNG)
disk布局结构为：
- block0要么没有用，要么被用作boot sector来启动操作系统。
- block1通常被称为super block，它描述了文件系统。它可能包含磁盘上有多少个block共同构成了文件系统这样的信息。我们之后会看到XV6在里面会存更多的信息，你可以通过block1构造出大部分的文件系统信息。
- 在XV6中，log从block2开始，到block32结束。实际上log的大小可能不同，这里在super block中会定义log就是30个block。
- 接下来在block32到block45之间，XV6存储了inode。我之前说过多个inode会打包存在一个block中，一个inode是64字节。
- 之后是bitmap block，这是我们构建文件系统的默认方法，它只占据一个block。它记录了数据block是否空闲。
- 之后就全是数据block了，数据block存储了文件的内容和目录的内容。

### inode
**inode 64字节**
inode的结构：
- 通常来说它有一个type字段，表明inode是文件还是目录。
- nlink字段，也就是link计数器，用来跟踪究竟有多少文件名指向了当前的inode。
- size字段，表明了文件数据有多少个字节。
- 不同文件系统中的表达方式可能不一样，不过在XV6中接下来是一些block的编号，例如编号0，编号1，等等。XV6的inode中总共有12个block编号。这些被称为**direct block number**。这12个block编号指向了构成文件的前12个block。举个例子，如果文件只有2个字节，那么只会有一个block编号0，它包含的数字是磁盘上文件前2个字节的block的位置。
- 之后还有一个**indirect block number**，它对应了磁盘上一个block，这个block包含了256个block number，这256个block number包含了文件的数据。所以inode中block number 0到block number 11都是direct block number，而block number 12保存的indirect block number指向了另一个block。
  
XV6中文件最大的长度（256+12）*1024字节

**目录文件**
一个目录本质上是一个文件加上一些文件系统能够理解的结构。在XV6中，这里的结构极其简单。每一个目录包含了directory entries，每一条entry都有固定的格式：
- 前2个字节包含了目录中文件或者子目录的inode编号，
- 接下来的14个字节包含了文件或者子目录名。
所以每个entry总共是16个字节。

### 创建inode
- 分配inode发生在sys_open函数中，这个函数会负责创建文件。
- 在`sys_open`函数中，会调用create函数，解析路径名并找到最后一个目录，查看文件是否存在，如果存在的话会返回错误。

```
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}
```

- 之后就会调用`ialloc`（inode allocate），这个函数会为文件x分配inode，它查找所有inode的block，找到一个空闲的inode，将其type字段设置为文件，并标记已分配。ialloc函数位于fs.c文件中。ialloc函数中调用`bread`（block read）函数。
- bread函数首先会调用`bget`，bget会为我们从buffer cache中找到block的缓存。首先获得整个cache的锁，读取cache信息，如果存在cache，将block的引用计数refcnt加一，释放cache锁，最后对该block调用acquiresleep函数（sleep lock），使得其中一个进程回到ialloc函数中扫描该block是否有空闲的inode。ialloc对block读写完毕后调用`brelse`函数，对refcnt减一，并释放sleep lock。

与spinlock不同的是，可以在I/O操作的过程中持有sleep lock。

### echo "hi" > x
![9_3](/img/9_3.PNG)

创建一个文件涉及到了多个操作：
- 首先是分配inode，因为首先写的是block 33
- 之后inode被初始化，然后又写了一次block 33
- 之后是写block 46，是将文件x的inode编号写入到x所在目录的inode的data block中
- 之后是更新root inode，因为文件x创建在根目录，所以需要更新根目录的inode的size字段，以包含这里新创建的文件x
- 最后再次更新了文件x的inode

一旦成功的创建了文件x，之后会调用write系统调用，我们在上节课看到了write系统调用也执行了多个写磁盘的操作。
![9_4](/img/9_4.PNG)
- 首先会从bitmap block，也就是block 45中，分配data block，通过从bitmap中分配一个bit，来表明一个data block已被分配。
- 上一步分配的data block是block 595，这里将字符“h”写入到block 595。
- 将字符“i”写入到block 595。
- 最后更新文件夹x的inode来更新size字段。

### logging
logging是一种针对文件系统crash之后的问题的解决方案，它有一些好的属性：
- 确保文件系统的系统调用是原子性的
- 支持快速恢复（Fast Recovery）
- 原则上来说，它可以非常的高效

**（log write）** 任何一次写操作不是直接写入到block所在的位置，而总是先将写操作写入到log中。

**（commit op）** 文件系统的操作结束了，并且都存在于log中，我们会commit文件系统的操作。这意味着我们需要在log的某个位置记录属于同一个文件系统的操作的个数，例如5。

**（install log）** 当我们在log中存储了所有写block的内容时，如果我们要真正执行这些操作，只需要将数据从log写到block。

**（clean log）** 一旦完成了，就可以清除log。清除log实际上就是将属于同一个文件系统的操作的个数设置为0。

log结构：
最开始有一个header block，也就是我们的commit record，里面包含了：
- 数字n代表有效的log block的数量
- 每个log block的实际对应的block编号bn0，bn1...

之后就是log的数据，也就是每个block的数据，依次为bn0对应的block的数据...

**文件操作：** 

每个文件系统操作，都有begin_op和end_op分别表示事物的开始和结束。

任何一个文件系统调用的begin_op和end_op之间的写操作总是会走到log_write。任何文件系统调用，如果需要更新block或者说更新block cache中的block，都要调用`log_write`函数，将block编号加在log这个**内存数据** 中。
```
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);    //将block固定在buffer cache中，避免被cache撤回
    log.lh.n++;
  }
  release(&log.lock);
}

```
end_op时，我们会将数据写入到磁盘中的log中，之后再写入commit record或者log header。end_op调用了`commit`函数。
```
static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}
```
commit中首先有两个操作：
1. write_log()将内存中的log header中的block编号对应的block，从block cache写入到磁盘上的log区域
2. write_head()将内存中的log header写入到磁盘中。
```
// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block in cache
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);   //copy modified cache block to log block in cache
    bwrite(to);  // write the log block to disk
    brelse(from);
    brelse(to);
  }
}
```
之后commit调用install_trans函数，把log中的数据写入block。最后commit设置log header中的n为0并写回磁盘。

每次启动xv6都会read_head()获得log header的内容，如果n大于0，调用install_trans函数，完成磁盘写入后清除log。
