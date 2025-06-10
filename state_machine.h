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
} sm_opcode;

typedef struct sm_instr {
  sm_opcode op;
  void *data;
  struct sm_instr *next;
} sm_instr;

typedef struct sm_ctx sm_ctx;

sm_ctx *sm_thread_start(void);
void sm_thread_stop(sm_ctx *ctx);
void sm_submit(sm_ctx *ctx, sm_instr *chain);
sm_reg sm_get_reg(sm_ctx *ctx, int idx);

/* Existing executor for direct use */
typedef struct sm_vm sm_vm;
void sm_execute(sm_instr *head, sm_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
