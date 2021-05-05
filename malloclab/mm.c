
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define CHUNKSIZE (1 << 12)
#define ALIGNMENT 8
#define WSIZE 4                                         // 单字大小
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 满足对齐要求的分配

#define PACK(size, flag) ((size) | (flag)) // 设置头部和尾部的值，flag：1表示已分配，0表示未分配

// 爬了一个坑  下面的 p 不用括号括起来的话会出问题，，，，淦
#define GET(p) (*(unsigned int *)(p))              // 读指针指向的 4字节(unsigned int)内存的数
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // 往指针指向的4字节内存写值,都是针对内存块的头部和脚部而言

#define GET_SIZE(P) (GET(P) & ~0x7) // 读头部或脚部的块大小信息
#define GET_ALLOC(P) (GET(P) & 0x1) // 读块的分配与否信息

#define HEADER(bp) ((char *)(bp)-WSIZE)                              // 获取头部指针 改成指向char类型的是为了指针操做时一个单位表示1字节
#define FOOTER(bp) ((char *)(bp) + GET_SIZE(HEADER(bp)) - ALIGNMENT) // 获取脚部指针

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))   // 获取下一内存块的指针
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-ALIGNMENT))) // 获取前一内存块的指针

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_low;
static char *heap_high;
static char *heap_bp = 0;
int mm_init(void)
{
    mem_init();
    if ((heap_low = mem_sbrk(4 * WSIZE)) == (void *)-1)
    {
        return -1;
    }

    // 序言块记得要设置成已分配，不然会被当成空闲块被合并，然后指针越界，擦，踩坑了
    PUT(heap_low, 0);
    PUT((heap_low + WSIZE), PACK(ALIGNMENT, 1));     // 序言块头部
    PUT((heap_low + 2 * WSIZE), PACK(ALIGNMENT, 1)); // 序言块脚部
    PUT((heap_low + 3 * WSIZE), PACK(0, 1));         // 结尾块头部，结尾没有尾部

    heap_bp = heap_low + 2 * WSIZE;
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;

    return 0;
}

static void *extend_heap(size_t size)
{
    char *bp;
    size = ALIGN(size); // 满足对齐要求
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    }
    PUT(HEADER(bp), PACK(size, 0));
    PUT(FOOTER(bp), PACK(size, 0));
    PUT(HEADER(NEXT_BLKP(bp)), PACK(0, 1)); // 新设置的堆结尾， 之前的尾部当成了新分配块的头部

    return coalesce(bp);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FOOTER(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HEADER(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HEADER(bp));

    // 如果前后都是已分配的块
    if (prev_alloc && next_alloc)
    {
        return bp;
    }

    // 如果前面分配了，后面没分配
    if (prev_alloc && (!next_alloc))
    {
        size += GET_SIZE(HEADER(NEXT_BLKP(bp)));
        PUT(HEADER(bp), PACK(size, 0));
        PUT(FOOTER(bp), PACK(size, 0));
        return bp;
    }

    // 如果前面未分配，后面分配了
    if ((!prev_alloc) && next_alloc)
    {
        size += GET_SIZE(HEADER(PREV_BLKP(bp)));
        PUT(FOOTER(bp), PACK(size, 0));
        PUT(HEADER(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        return bp;
    }

    // 或者前后都未分配
    size += GET_SIZE(HEADER(PREV_BLKP(bp))) + GET_SIZE(FOOTER(NEXT_BLKP(bp)));
    PUT(HEADER(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FOOTER(NEXT_BLKP(bp)), PACK(size, 0));
    return PREV_BLKP(bp);
}

void *mm_malloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    if (heap_bp == 0)
    {
        mm_init();
    }

    int newsize = ALIGN(size + SIZE_T_SIZE);

    char *bp = heap_bp;
    // 每次都从头开始遍历 找到合适的空闲块
    while (GET_SIZE(HEADER(bp)) != 0)
    {
        if (GET_ALLOC(HEADER(bp)) || GET_SIZE(HEADER(bp)) < newsize)
        {
            bp = NEXT_BLKP(bp);
        }
        else
        {
            place(bp, newsize);
            return bp;
        }
    }
    size_t extend_size = MAX(newsize, CHUNKSIZE);
    if ((bp = extend_heap(extend_size)) == NULL)
        return NULL;
    place(bp, newsize);
    return bp;
}

// 分配时要分割空闲块，所以还是按书上的把place函数单独抽取出来
void place(char *bp, size_t size)
{
    size_t fsize = GET_SIZE(HEADER(bp));
    PUT(HEADER(bp), PACK(size, 1));
    PUT(FOOTER(bp), PACK(size, 1));
    // 下面由其他分配函数保证剩余的空闲块是双字以上的,至少可以安放头部和脚部
    if (fsize > size)
    {
        PUT(HEADER(NEXT_BLKP(bp)), PACK(fsize - size, 0));
        PUT(FOOTER(NEXT_BLKP(bp)), PACK(fsize - size, 0));
    }

    return;
}

void mm_free(void *ptr)
{
    if (ptr == 0)
    {
        return;
    }
    if (heap_bp == 0)
    {
        mm_init();
    }
    size_t size = GET_SIZE(HEADER(ptr)); // 获取该块的大小
    PUT(HEADER(ptr), PACK(size, 0));     // 把该块标记为未分配
    PUT(FOOTER(ptr), PACK(size, 0));
    coalesce(ptr); // 执行每次 free() 后合并空闲块的策略
}

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    // 满足一下Lab对该函数的要求
    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }
    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = GET_SIZE(HEADER(ptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
