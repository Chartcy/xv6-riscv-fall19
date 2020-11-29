// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];//定义一个结构体数组

int getcpu(){//获取CPU信息
  push_off();
  int cpu=cpuid();
  pop_off();
  return cpu;
}

void
kinit()//初始化NCPU个锁
{
  for(int i=0;i<NCPU;i++){
    initlock(&kmem[i].lock,"kmem");
  }
  freerange(end, (void*)PHYSTOP);//freerange将全部内存分配给调用它的cpu
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int hart = getcpu();

  acquire(&kmem[hart].lock);
  r->next = kmem[hart].freelist;
  kmem[hart].freelist = r;
  release(&kmem[hart].lock);
}

void *
steal(int skip){
  // printf("cpu id %d\n",getcpu());
  struct run * rs=0;
  for(int i=0;i<NCPU;i++){
    // 当前cpu的锁已经在外面获取了，这里为了避免死锁，需要跳过
    if(holding(&kmem[i].lock)){
      continue;
    }
    acquire(&kmem[i].lock);//锁住
    if(kmem[i].freelist!=0){//当前有空闲
      rs=kmem[i].freelist;//窃取
      kmem[i].freelist=rs->next;//调整
      release(&kmem[i].lock);//释放锁
      return (void *)rs;
    }
    release(&kmem[i].lock);
  }
  // 不管还有没有，都直接返回，不用panic
  return (void *)rs;
}
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  int hart=getcpu();

  acquire(&kmem[hart].lock);
  r = kmem[hart].freelist;
  if(r)
    kmem[hart].freelist = r->next;
  release(&kmem[hart].lock);
  if(!r)
  {
    //borrow from other CPU's freelist
    r = steal(hart);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

