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
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");

  freerange(end, (void*)PHYSTOP);
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

   push_off();
  int cpu = cpuid();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
static struct run *
borrow(int cpu)
{
  for(int offset = 1; offset < NCPU; offset++){
    int donor = (cpu + offset) % NCPU;

    acquire(&kmem[donor].lock);

    struct run *first = kmem[donor].freelist;
    if(first == 0){
      release(&kmem[donor].lock);
      continue;
    }

    // Detach up to 32 pages from the donor.
    struct run *last = first;
    for(int count = 1; count < 32 && last->next != 0; count++)
      last = last->next;

    kmem[donor].freelist = last->next;
    last->next = 0;

    release(&kmem[donor].lock);

    // The detached chain is private now.
    // Return its first page and put the rest on the local free list.
    if(first->next != 0){
      acquire(&kmem[cpu].lock);
      last->next = kmem[cpu].freelist;
      kmem[cpu].freelist = first->next;
      release(&kmem[cpu].lock);
    }

    return first;
  }

  return 0;
}

void *
kalloc(void)
{
  push_off();
  int cpu = cpuid();

  acquire(&kmem[cpu].lock);

  struct run *r = kmem[cpu].freelist;
  if(r != 0)
    kmem[cpu].freelist = r->next;

  release(&kmem[cpu].lock);

  if(r == 0)
    r = borrow(cpu);

  pop_off();

  if(r != 0)
    memset(r, 5, PGSIZE);

  return r;
}
