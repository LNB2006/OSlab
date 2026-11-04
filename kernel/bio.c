// Buffer cache.
//
// Each block maps to one bucket. The bucket lock protects its list,
// block identities, and reference counts. Each buffer's sleeplock
// protects its data. The miss lock serializes allocation and replacement.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

static struct {
  struct bucket buckets[NBUCKET];
  struct buf buf[NBUF];

  struct spinlock misslock;
  int used;   // Number of buffers assigned an identity at least once.
  int hand;   // Next replacement candidate in the fixed buffer array.
} bcache;

static struct bucket *
bucketof(uint blockno)
{
  return &bcache.buckets[blockno % NBUCKET];
}

void
binit(void)
{
  initlock(&bcache.misslock, "bcache.miss");

  for(int i = 0; i < NBUCKET; i++)
    initlock(&bcache.buckets[i].lock, "bcache.bucket");

  for(int i = 0; i < NBUF; i++)
    initsleeplock(&bcache.buf[i].lock, "buffer");
}

// Find an existing block and take a reference before releasing the lock.
// A positive reference prevents replacement while the caller waits for
// the buffer's sleeplock.
static struct buf *
lookup(struct bucket *bucket, uint dev, uint blockno)
{
  acquire(&bucket->lock);

  for(struct buf *b = bucket->head; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      return b;
    }
  }

  release(&bucket->lock);
  return 0;
}

// Caller holds misslock. Return an unused buffer or detach a recyclable
// buffer from its old bucket. The returned buffer is not on any list.
static struct buf *
reserve(void)
{
  // Prefer a buffer that has never been used.
  if(bcache.used < NBUF)
    return &bcache.buf[bcache.used++];

  // All buffers have been used. Scan them in round-robin order.
  for(int n = 0; n < NBUF; n++){
    struct buf *b = &bcache.buf[bcache.hand];
    bcache.hand = (bcache.hand + 1) % NBUF;

    // misslock prevents another replacement from changing blockno.
    struct bucket *old = bucketof(b->blockno);
    acquire(&old->lock);

    // A concurrent hit can change refcnt, so check it under old->lock.
    if(b->refcnt == 0){
      if(b->prev != 0)
        b->prev->next = b->next;
      else
        old->head = b->next;

      if(b->next != 0)
        b->next->prev = b->prev;

      release(&old->lock);
      return b;
    }

    release(&old->lock);
  }

  panic("bget: no buffers");
}

// Return the buffer for (dev, blockno) with its sleeplock held.
static struct buf *
bget(uint dev, uint blockno)
{
  struct bucket *dst = bucketof(blockno);

  // Fast path: hits need only the destination bucket.
  struct buf *b = lookup(dst, dev, blockno);

  if(b == 0){
    acquire(&bcache.misslock);

    // Another CPU may have installed this block since the first lookup.
    b = lookup(dst, dev, blockno);

    if(b == 0){
      b = reserve();

      acquire(&dst->lock);

      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      // Insert at the front of the destination bucket.
      b->prev = 0;
      b->next = dst->head;
      if(b->next != 0)
        b->next->prev = b;
      dst->head = b;

      release(&dst->lock);
    }

    release(&bcache.misslock);
  }

  // No spinlock may be held while waiting for a sleeplock.
  acquiresleep(&b->lock);
  return b;
}

struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b = bget(dev, blockno);

  if(!b->valid){
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }

  return b;
}

void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");

  virtio_disk_rw(b, 1);
}

void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // Our reference still keeps the block in this bucket.
  struct bucket *bucket = bucketof(b->blockno);

  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}

void
bpin(struct buf *b)
{
  struct bucket *bucket = bucketof(b->blockno);

  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b)
{
  struct bucket *bucket = bucketof(b->blockno);

  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}