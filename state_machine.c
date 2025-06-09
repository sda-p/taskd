#define _GNU_SOURCE
#include "fs_utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define SM_REG_COUNT 8

typedef void *sm_reg;

typedef enum {
  SM_OP_LOAD_CONST,
  SM_OP_FS_CREATE,
  SM_OP_FS_DELETE,
  SM_OP_FS_COPY,
  SM_OP_FS_MOVE,
  SM_OP_FS_WRITE,
  SM_OP_FS_READ,
} sm_opcode;

struct sm_instr;

typedef struct sm_instr {
  sm_opcode op;          /* operation to perform */
  void *data;            /* operation-specific data */
  struct sm_instr *next; /* next step */
} sm_instr;

/* Generic VM context with a fixed-width register array */
typedef struct {
  sm_reg regs[SM_REG_COUNT];
} sm_vm;

/* Helper to validate register indices */
static inline bool reg_valid(int idx) { return idx >= 0 && idx < SM_REG_COUNT; }

/* ----- Operation data structures ----- */
typedef struct {
  int dest; /* register index to store the constant */
  const void *value;
} sm_load_const;

typedef struct {
  int dest; /* store bool result */
  int path; /* register index with const char *path */
  int type; /* register index with const char *"dir" or "file" */
} sm_fs_create;

typedef struct {
  int dest;
  int path;
} sm_fs_delete;

typedef struct {
  int dest;
  int src;
  int dst;
} sm_fs_copy;

typedef struct {
  int dest;
  int src;
  int dst;
} sm_fs_move;

typedef struct {
  int dest;    /* bool result */
  int path;    /* char * */
  int content; /* char * */
  int mode;    /* char * */
} sm_fs_write;

typedef struct {
  int dest; /* store char* (fs_read allocates) */
  int path;
} sm_fs_read;

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
    default:
      /* Unknown opcode – ignore to allow graceful failure */
      break;
    }
    cur = cur->next;
  }
}
