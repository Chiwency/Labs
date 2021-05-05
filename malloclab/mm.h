#include <stdio.h>




extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);

static void *coalesce(void *bp);
static void *extend_heap(size_t size);
void place(char *bp, size_t size);