#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *sm_reg;

typedef enum {
  SM_OP_LOAD_CONST,
  SM_OP_FS_CREATE,
  SM_OP_FS_DELETE,
  SM_OP_FS_COPY,
  SM_OP_FS_MOVE,
  SM_OP_FS_WRITE,
  SM_OP_FS_READ,
  SM_OP_FS_UNPACK,
  SM_OP_RETURN,
} sm_opcode;

typedef struct sm_instr {
  sm_opcode op;
  void *data;
  struct sm_instr *next;
} sm_instr;

/* Operation data structures */
typedef struct {
  int dest;
  const void *value;
} sm_load_const;

typedef struct {
  int dest;
  int path;
  int type;
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
  int dest;
  int path;
  int content;
  int mode;
} sm_fs_write;

typedef struct {
  int dest;
  int path;
} sm_fs_read;

typedef struct {
  int tar_path;
  int dest;
} sm_fs_unpack;

typedef struct {
  int value;
} sm_return;

typedef struct sm_ctx sm_ctx;

sm_ctx *sm_thread_start(void);
void sm_thread_stop(sm_ctx *ctx);
void sm_submit(sm_ctx *ctx, sm_instr *chain);
sm_reg sm_get_reg(sm_ctx *ctx, int idx);
void sm_wait(sm_ctx *ctx, int *value);

/* Existing executor for direct use */
typedef struct sm_vm sm_vm;
void sm_execute(sm_instr *head, sm_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
