// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
  struct spinlock steal_lock;
} bcache;

int 
hashFunction(int blockno){
  return blockno%NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  //initlock(&bcache.lock, "bcache");
  for(int i = 0 ;i<NBUCKETS ; i++){
    //初始化
    initlock(&bcache.lock[i], "bcache");
    // Create linked list of buffers
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int hash_num = hashFunction(b->blockno);
    b->next = bcache.hashbucket[hash_num].next;
    b->prev = &bcache.hashbucket[hash_num];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[hash_num].next->prev = b;
    bcache.hashbucket[hash_num].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  //获取哈希值
  int hash_num = hashFunction(blockno);

  acquire(&bcache.lock[hash_num]);

  // Is the block already cached?
  for(b = bcache.hashbucket[hash_num].next; b != &bcache.hashbucket[hash_num]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hash_num]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.hashbucket[hash_num].prev; b != &bcache.hashbucket[hash_num]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[hash_num]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //窃取
  for(int i = (hash_num+1)%NBUCKETS ; i != hash_num;i=(i+1)%NBUCKETS){
    acquire(&bcache.lock[i]);
    for(b = bcache.hashbucket[i].prev ; b != &bcache.hashbucket[i];b = b->prev){
      if(b->refcnt == 0){//未被引用
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.lock[i]);
        //移除被窃取块，避免其他线程同时访问
        b->next = bcache.hashbucket[hash_num].next;
        b->prev = &bcache.hashbucket[hash_num];
        //将被窃取的缓存块插入到目标哈希队列的最前面
        bcache.hashbucket[hash_num].next->prev = b;
        bcache.hashbucket[hash_num].next=b;
        release(&bcache.lock[hash_num]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  int hash_num = hashFunction(b->blockno);

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock[hash_num]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[hash_num].next;
    b->prev = &bcache.hashbucket[hash_num];
    bcache.hashbucket[hash_num].next->prev = b;
    bcache.hashbucket[hash_num].next = b;
  }
  
  release(&bcache.lock[hash_num]);

}

void
bpin(struct buf *b) {
  int hash_num = hashFunction(b->blockno);
  acquire(&bcache.lock[hash_num]);
  b->refcnt++;
  release(&bcache.lock[hash_num]);
}

void
bunpin(struct buf *b) {
  int hash_num = hashFunction(b->blockno);
  acquire(&bcache.lock[hash_num]);
  b->refcnt--;
  release(&bcache.lock[hash_num]);
}


