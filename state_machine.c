#define _GNU_SOURCE
#include "state_machine.h"
#include "fs_utils.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define SM_REG_COUNT 8

/* Generic VM context with a fixed-width register array */
typedef struct sm_vm {
  sm_reg regs[SM_REG_COUNT];
} sm_vm;

/* Helper to validate register indices */
static inline bool reg_valid(int idx) { return idx >= 0 && idx < SM_REG_COUNT; }

/* ----- State machine executor ----- */
void sm_execute(sm_instr *head, sm_vm *vm) {
  if (!vm)
    return;
  sm_instr *cur = head;
  while (cur) {
    switch (cur->op) {
    case SM_OP_LOAD_CONST: {
      sm_load_const *a = (sm_load_const *)cur->data;
      if (!a || !reg_valid(a->dest))
        break;
      vm->regs[a->dest] = (void *)a->value;
      break;
    }
    case SM_OP_FS_CREATE: {
      sm_fs_create *a = (sm_fs_create *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path) ||
          !reg_valid(a->type))
        break;
      const char *p = (const char *)vm->regs[a->path];
      const char *t = (const char *)vm->regs[a->type];
      bool ok = (p && t) ? fs_create(p, t) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_FS_DELETE: {
      sm_fs_delete *a = (sm_fs_delete *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path))
        break;
      const char *p = (const char *)vm->regs[a->path];
      bool ok = p ? fs_delete(p) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_FS_COPY: {
      sm_fs_copy *a = (sm_fs_copy *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->src) || !reg_valid(a->dst))
        break;
      const char *s = (const char *)vm->regs[a->src];
      const char *d = (const char *)vm->regs[a->dst];
      bool ok = (s && d) ? fs_copy(s, d) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_FS_MOVE: {
      sm_fs_move *a = (sm_fs_move *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->src) || !reg_valid(a->dst))
        break;
      const char *s = (const char *)vm->regs[a->src];
      const char *d = (const char *)vm->regs[a->dst];
      bool ok = (s && d) ? fs_move(s, d) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_FS_WRITE: {
      sm_fs_write *a = (sm_fs_write *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path) ||
          !reg_valid(a->content) || !reg_valid(a->mode))
        break;
      const char *p = (const char *)vm->regs[a->path];
      const char *c = (const char *)vm->regs[a->content];
      const char *m = (const char *)vm->regs[a->mode];
      bool ok = (p && c && m) ? fs_write(p, c, m) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_FS_READ: {
      sm_fs_read *a = (sm_fs_read *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path))
        break;
      const char *p = (const char *)vm->regs[a->path];
      char *buf = p ? fs_read(p) : NULL;
      vm->regs[a->dest] = buf;
      break;
    }
    case SM_OP_FS_UNPACK: {
      sm_fs_unpack *a = (sm_fs_unpack *)cur->data;
      if (!a || !reg_valid(a->tar_path) || !reg_valid(a->dest))
        break;
      const char *t = (const char *)vm->regs[a->tar_path];
      const char *d = (const char *)vm->regs[a->dest];
      if (t && d)
        fs_unpack(t, d);
      break;
    }
    default:
      /* Unknown opcode – ignore to allow graceful failure */
      break;
    }
    cur = cur->next;
  }
}

/* ----- Persistent executor thread ----- */

typedef struct sm_job {
  sm_instr *instr;
  struct sm_job *next;
} sm_job;

typedef struct sm_ctx {
  sm_vm vm;
  sm_job *head;
  sm_job *tail;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  pthread_t thread;
  bool running;
} sm_ctx;

static void *sm_worker(void *arg) {
  sm_ctx *ctx = arg;
  for (;;) {
    pthread_mutex_lock(&ctx->lock);
    while (ctx->running && ctx->head == NULL)
      pthread_cond_wait(&ctx->cond, &ctx->lock);
    if (!ctx->running && ctx->head == NULL) {
      pthread_mutex_unlock(&ctx->lock);
      break;
    }
    sm_job *j = ctx->head;
    ctx->head = j->next;
    if (!ctx->head)
      ctx->tail = NULL;
    pthread_mutex_unlock(&ctx->lock);

    sm_execute(j->instr, &ctx->vm);
    free(j);
  }
  return NULL;
}

sm_ctx *sm_thread_start(void) {
  sm_ctx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return NULL;
  pthread_mutex_init(&ctx->lock, NULL);
  pthread_cond_init(&ctx->cond, NULL);
  ctx->running = true;
  if (pthread_create(&ctx->thread, NULL, sm_worker, ctx) != 0) {
    ctx->running = false;
    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void sm_thread_stop(sm_ctx *ctx) {
  if (!ctx)
    return;
  pthread_mutex_lock(&ctx->lock);
  ctx->running = false;
  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
  pthread_join(ctx->thread, NULL);
  pthread_cond_destroy(&ctx->cond);
  pthread_mutex_destroy(&ctx->lock);
  free(ctx);
}

void sm_submit(sm_ctx *ctx, sm_instr *chain) {
  if (!ctx)
    return;
  sm_job *j = malloc(sizeof(*j));
  if (!j)
    return;
  j->instr = chain;
  j->next = NULL;
  pthread_mutex_lock(&ctx->lock);
  if (ctx->tail)
    ctx->tail->next = j;
  else
    ctx->head = j;
  ctx->tail = j;
  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
}

sm_reg sm_get_reg(sm_ctx *ctx, int idx) {
  if (!ctx || !reg_valid(idx))
    return NULL;
  pthread_mutex_lock(&ctx->lock);
  sm_reg val = ctx->vm.regs[idx];
  pthread_mutex_unlock(&ctx->lock);
  return val;
}
