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

/* Current context for return signalling */
static __thread struct sm_ctx *current_ctx = NULL;

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
  pthread_cond_t done_cond;
  bool job_done;
  int job_value;
  pthread_t thread;
  bool running;
} sm_ctx;

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
    case SM_OP_FS_HASH: {
      sm_fs_hash *a = (sm_fs_hash *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path))
        break;
      const char *p = (const char *)vm->regs[a->path];
      char *h = p ? fs_hash(p) : NULL;
      vm->regs[a->dest] = h;
      break;
    }
    case SM_OP_FS_LIST: {
      sm_fs_list *a = (sm_fs_list *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->path))
        break;
      const char *p = (const char *)vm->regs[a->path];
      char *list = p ? fs_list_dir(p) : NULL;
      vm->regs[a->dest] = list;
      break;
    }
    case SM_OP_EQ: {
      sm_eq *a = (sm_eq *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->lhs) || !reg_valid(a->rhs))
        break;
      uintptr_t l = (uintptr_t)vm->regs[a->lhs];
      uintptr_t r = (uintptr_t)vm->regs[a->rhs];
      bool eq = l == r;
      vm->regs[a->dest] = (void *)(uintptr_t)eq;
      break;
    }
    case SM_OP_NOT: {
      sm_not *a = (sm_not *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->src))
        break;
      bool v = (uintptr_t)vm->regs[a->src] != 0;
      vm->regs[a->dest] = (void *)(uintptr_t)(!v);
      break;
    }
    case SM_OP_AND: {
      sm_and *a = (sm_and *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->lhs) || !reg_valid(a->rhs))
        break;
      bool l = (uintptr_t)vm->regs[a->lhs] != 0;
      bool r = (uintptr_t)vm->regs[a->rhs] != 0;
      vm->regs[a->dest] = (void *)(uintptr_t)(l && r);
      break;
    }
    case SM_OP_OR: {
      sm_or *a = (sm_or *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->lhs) || !reg_valid(a->rhs))
        break;
      bool l = (uintptr_t)vm->regs[a->lhs] != 0;
      bool r = (uintptr_t)vm->regs[a->rhs] != 0;
      vm->regs[a->dest] = (void *)(uintptr_t)(l || r);
      break;
    }
    case SM_OP_INDEX_SELECT: {
      sm_index_select *a = (sm_index_select *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->list) ||
          !reg_valid(a->index))
        break;
      const char *list = (const char *)vm->regs[a->list];
      size_t idx = (size_t)(uintptr_t)vm->regs[a->index];
      char *out = NULL;
      if (list) {
        out = list_index(list, idx);
      }
      vm->regs[a->dest] = out;
      break;
    }
    case SM_OP_RANDOM_RANGE: {
      sm_random_range *a = (sm_random_range *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->min) || !reg_valid(a->max))
        break;
      long min = (long)(uintptr_t)vm->regs[a->min];
      long max = (long)(uintptr_t)vm->regs[a->max];
      long val = rand_range(min, max);
      vm->regs[a->dest] = (void *)(uintptr_t)val;
      break;
    }
    case SM_OP_PATH_JOIN: {
      sm_path_join *a = (sm_path_join *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->base) ||
          !reg_valid(a->name))
        break;
      const char *base = (const char *)vm->regs[a->base];
      const char *name = (const char *)vm->regs[a->name];
      char *out = (base && name) ? path_join(base, name) : NULL;
      vm->regs[a->dest] = out;
      break;
    }
    case SM_OP_RANDOM_WALK: {
      sm_random_walk *a = (sm_random_walk *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->root) ||
          !reg_valid(a->depth))
        break;
      const char *root = (const char *)vm->regs[a->root];
      int depth = (int)(uintptr_t)vm->regs[a->depth];
      char *out = root ? fs_random_walk(root, depth) : NULL;
      vm->regs[a->dest] = out;
      break;
    }
    case SM_OP_DIR_CONTAINS: {
      sm_dir_contains *a = (sm_dir_contains *)cur->data;
      if (!a || !reg_valid(a->dest) || !reg_valid(a->dir_a) ||
          !reg_valid(a->dir_b))
        break;
      const char *ap = (const char *)vm->regs[a->dir_a];
      const char *bp = (const char *)vm->regs[a->dir_b];
      bool ok = (ap && bp) ? fs_dir_contains(ap, bp) : false;
      vm->regs[a->dest] = (void *)(uintptr_t)ok;
      break;
    }
    case SM_OP_RETURN: {
      sm_return *a = (sm_return *)cur->data;
      int val = a ? a->value : 0;
      if (current_ctx) {
        pthread_mutex_lock(&current_ctx->lock);
        current_ctx->job_value = val;
        current_ctx->job_done = true;
        pthread_cond_signal(&current_ctx->done_cond);
        pthread_mutex_unlock(&current_ctx->lock);
      }
      cur = NULL;
      continue;
    }
    default:
      /* Unknown opcode – ignore to allow graceful failure */
      break;
    }
    cur = cur->next;
  }
}

/* ----- Persistent executor thread ----- */

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
    ctx->job_done = false;
    pthread_mutex_unlock(&ctx->lock);

    current_ctx = ctx;
    sm_execute(j->instr, &ctx->vm);
    current_ctx = NULL;

    pthread_mutex_lock(&ctx->lock);
    if (!ctx->job_done) {
      ctx->job_value = 0;
      ctx->job_done = true;
      pthread_cond_signal(&ctx->done_cond);
    }
    pthread_mutex_unlock(&ctx->lock);
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
  pthread_cond_init(&ctx->done_cond, NULL);
  ctx->running = true;
  if (pthread_create(&ctx->thread, NULL, sm_worker, ctx) != 0) {
    ctx->running = false;
    pthread_cond_destroy(&ctx->cond);
    pthread_cond_destroy(&ctx->done_cond);
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
  pthread_cond_destroy(&ctx->done_cond);
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
  ctx->job_done = false;
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

void sm_wait(sm_ctx *ctx, int *value) {
  if (!ctx)
    return;
  pthread_mutex_lock(&ctx->lock);
  while (!ctx->job_done)
    pthread_cond_wait(&ctx->done_cond, &ctx->lock);
  if (value)
    *value = ctx->job_value;
  pthread_mutex_unlock(&ctx->lock);
}
