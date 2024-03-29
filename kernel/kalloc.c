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
} kmem;

struct {
    struct spinlock lock;
    uint cnt[(PHYSTOP - KERNBASE) / PGSIZE];
} refcnt;

uint
pgindex(uint64 pa) {
    return (pa - KERNBASE) / PGSIZE;
}

void
increment_refcnt(uint64 pa, int n) {
    acquire(&refcnt.lock);
    refcnt.cnt[pgindex(pa)] += n;
    release(&refcnt.lock);
}

int
get_refcnt(uint64 pa) {
    return refcnt.cnt[pgindex(pa)];
}

void
set_refcnt(uint64 pa, int n) {
    refcnt.cnt[pgindex(pa)] = n;
}

inline void
acquire_refcnt() {
    acquire(&refcnt.lock);
}

inline void
release_refcnt() {
    release(&refcnt.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcnt.lock, "refcnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
      acquire_refcnt();
      set_refcnt((uint64)p, 1);
      release_refcnt();
      kfree(p);
  }
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

  increment_refcnt((uint64)pa, -1);
  if (get_refcnt((uint64)pa) != 0)
      return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
      memset((char *) r, 5, PGSIZE); // fill with junk
      increment_refcnt((uint64) r, 1);
  }
  return (void*)r;
}
