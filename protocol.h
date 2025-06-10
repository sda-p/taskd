#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "state_machine.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char command[32];
  char value[128];
} proto_msg;

typedef struct {
  char greeting[32];
  int version;
} handshake_msg;

static inline bool parse_handshake(const char *json, handshake_msg *out) {
  if (!json || !out)
    return false;
  cJSON *root = cJSON_Parse(json);
  if (!root)
    return false;
  cJSON *g = cJSON_GetObjectItemCaseSensitive(root, "hello");
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "version");
  bool ok = cJSON_IsString(g) && cJSON_IsNumber(v);
  if (ok) {
    strncpy(out->greeting, g->valuestring, sizeof(out->greeting) - 1);
    out->greeting[sizeof(out->greeting) - 1] = '\0';
    out->version = v->valueint;
  }
  cJSON_Delete(root);
  return ok;
}

static inline char *report_status(int status) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;
  cJSON_AddNumberToObject(root, "status", status);
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

static inline bool proto_parse(const char *json, proto_msg *out) {
  if (!json || !out)
    return false;
  cJSON *root = cJSON_Parse(json);
  if (!root)
    return false;
  cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "command");
  cJSON *val = cJSON_GetObjectItemCaseSensitive(root, "value");
  bool ok = cJSON_IsString(cmd) && cJSON_IsString(val);
  if (ok) {
    strncpy(out->command, cmd->valuestring, sizeof(out->command) - 1);
    out->command[sizeof(out->command) - 1] = '\0';
    strncpy(out->value, val->valuestring, sizeof(out->value) - 1);
    out->value[sizeof(out->value) - 1] = '\0';
  }
  cJSON_Delete(root);
  return ok;
}

static inline char *proto_build(const proto_msg *msg) {
  if (!msg)
    return NULL;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;
  cJSON_AddStringToObject(root, "command", msg->command);
  cJSON_AddStringToObject(root, "value", msg->value);
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out; /* caller must free */
}

static inline bool proto_recv(int fd, proto_msg *out) {
  char buf[256];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0)
    return false;
  buf[n] = '\0';
  return proto_parse(buf, out);
}

static inline bool proto_send(int fd, const proto_msg *msg) {
  char *json = proto_build(msg);
  if (!json)
    return false;
  size_t len = strlen(json);
  ssize_t n = send(fd, json, len, MSG_NOSIGNAL);
  free(json);
  return n == (ssize_t)len;
}

/* Receive raw JSON string (caller must free) */
static inline char *proto_recv_json(int fd) {
  char buf[512];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0)
    return NULL;
  buf[n] = '\0';
  char *out = malloc((size_t)n + 1);
  if (!out)
    return NULL;
  memcpy(out, buf, (size_t)n + 1);
  return out;
}

/* Mapping from opcode string to enum */
static inline bool opcode_from_string(const char *s, sm_opcode *out) {
  if (!s || !out)
    return false;
  struct {
    const char *name;
    sm_opcode code;
  } map[] = {
      {"SM_OP_LOAD_CONST", SM_OP_LOAD_CONST},
      {"SM_OP_FS_CREATE", SM_OP_FS_CREATE},
      {"SM_OP_FS_DELETE", SM_OP_FS_DELETE},
      {"SM_OP_FS_COPY", SM_OP_FS_COPY},
      {"SM_OP_FS_MOVE", SM_OP_FS_MOVE},
      {"SM_OP_FS_WRITE", SM_OP_FS_WRITE},
      {"SM_OP_FS_READ", SM_OP_FS_READ},
      {"SM_OP_FS_UNPACK", SM_OP_FS_UNPACK},
      {"SM_OP_RETURN", SM_OP_RETURN},
  };
  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
    if (strcmp(s, map[i].name) == 0) {
      *out = map[i].code;
      return true;
    }
  }
  return false;
}

/* Parse JSON recipe into instruction list */
static inline sm_instr *proto_parse_recipe(const char *json) {
  if (!json)
    return NULL;
  cJSON *root = cJSON_Parse(json);
  if (!root || !cJSON_IsArray(root)) {
    cJSON_Delete(root);
    return NULL;
  }
  sm_instr *head = NULL, *tail = NULL;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    if (!cJSON_IsObject(item))
      continue;
    cJSON *op = cJSON_GetObjectItemCaseSensitive(item, "op");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(item, "data");
    if (!cJSON_IsString(op) || !cJSON_IsObject(data))
      continue;
    sm_opcode code;
    if (!opcode_from_string(op->valuestring, &code))
      continue;
    sm_instr *ins = calloc(1, sizeof(*ins));
    if (!ins)
      continue;
    ins->op = code;
    switch (code) {
    case SM_OP_LOAD_CONST: {
      sm_load_const *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *val = cJSON_GetObjectItemCaseSensitive(data, "value");
      if (!cJSON_IsNumber(dest) || !cJSON_IsString(val)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->value = strdup(val->valuestring);
      ins->data = d;
      break;
    }
    case SM_OP_FS_CREATE: {
      sm_fs_create *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *path = cJSON_GetObjectItemCaseSensitive(data, "path");
      cJSON *type = cJSON_GetObjectItemCaseSensitive(data, "type");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(path) ||
          !cJSON_IsNumber(type)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->path = path->valueint;
      d->type = type->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_DELETE: {
      sm_fs_delete *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *path = cJSON_GetObjectItemCaseSensitive(data, "path");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(path)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->path = path->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_COPY: {
      sm_fs_copy *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *src = cJSON_GetObjectItemCaseSensitive(data, "src");
      cJSON *dst = cJSON_GetObjectItemCaseSensitive(data, "dst");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(src) ||
          !cJSON_IsNumber(dst)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->src = src->valueint;
      d->dst = dst->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_MOVE: {
      sm_fs_move *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *src = cJSON_GetObjectItemCaseSensitive(data, "src");
      cJSON *dst = cJSON_GetObjectItemCaseSensitive(data, "dst");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(src) ||
          !cJSON_IsNumber(dst)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->src = src->valueint;
      d->dst = dst->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_WRITE: {
      sm_fs_write *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *path = cJSON_GetObjectItemCaseSensitive(data, "path");
      cJSON *content = cJSON_GetObjectItemCaseSensitive(data, "content");
      cJSON *mode = cJSON_GetObjectItemCaseSensitive(data, "mode");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(path) ||
          !cJSON_IsNumber(content) || !cJSON_IsNumber(mode)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->path = path->valueint;
      d->content = content->valueint;
      d->mode = mode->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_READ: {
      sm_fs_read *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      cJSON *path = cJSON_GetObjectItemCaseSensitive(data, "path");
      if (!cJSON_IsNumber(dest) || !cJSON_IsNumber(path)) {
        free(d);
        break;
      }
      d->dest = dest->valueint;
      d->path = path->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_FS_UNPACK: {
      sm_fs_unpack *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *tar_path = cJSON_GetObjectItemCaseSensitive(data, "tar_path");
      cJSON *dest = cJSON_GetObjectItemCaseSensitive(data, "dest");
      if (!cJSON_IsNumber(tar_path) || !cJSON_IsNumber(dest)) {
        free(d);
        break;
      }
      d->tar_path = tar_path->valueint;
      d->dest = dest->valueint;
      ins->data = d;
      break;
    }
    case SM_OP_RETURN: {
      sm_return *d = malloc(sizeof(*d));
      if (!d)
        break;
      cJSON *val = cJSON_GetObjectItemCaseSensitive(data, "value");
      if (!cJSON_IsNumber(val)) {
        free(d);
        break;
      }
      d->value = val->valueint;
      ins->data = d;
      break;
    }
    default:
      free(ins);
      ins = NULL;
      break;
    }
    if (!ins)
      continue;
    if (!head)
      head = ins;
    else
      tail->next = ins;
    tail = ins;
  }
  cJSON_Delete(root);
  return head;
}

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
