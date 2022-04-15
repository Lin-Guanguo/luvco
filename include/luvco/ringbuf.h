#pragma once
#include <stdlib.h>

typedef struct luvco_ringbuf luvco_ringbuf;
typedef struct luvco_ringbuf2 luvco_ringbuf2;

size_t luvco_ringbuf_sizeof (int len);
void luvco_ringbuf_init (luvco_ringbuf* r, int len);
int luvco_ringbuf_push (luvco_ringbuf* r, void* data);
#define luvco_ringbuf_spinpush(r, data) while (luvco_ringbuf_push((r), (data)) != 0);
int luvco_ringbuf_unlockpush (luvco_ringbuf* r, void* data);
int luvco_ringbuf_pop (luvco_ringbuf* r, void** data);
int luvco_ringbuf_unlockpop (luvco_ringbuf* r, void** data);


size_t luvco_ringbuf2_sizeof (int len);
void luvco_ringbuf2_init (luvco_ringbuf2* r, int len, int firstbufsize);
int luvco_ringbuf2_push (luvco_ringbuf2* r, void* data);
#define luvco_ringbuf2_spinpush(r, data) while (luvco_ringbuf2_push((r), (data)) != 0);
int luvco_ringbuf2_unlockpush (luvco_ringbuf2* r, void* data);
int luvco_ringbuf2_pop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_unlockpop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_delete (luvco_ringbuf2* r);


