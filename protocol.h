#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char command[32];
    char value[128];
} proto_msg;

static inline bool proto_parse(const char *json, proto_msg *out)
{
    if (!json || !out) return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "command");
    cJSON *val = cJSON_GetObjectItemCaseSensitive(root, "value");
    bool ok = cJSON_IsString(cmd) && cJSON_IsString(val);
    if (ok) {
        strncpy(out->command, cmd->valuestring, sizeof(out->command) - 1);
        out->command[sizeof(out->command)-1] = '\0';
        strncpy(out->value, val->valuestring, sizeof(out->value) - 1);
        out->value[sizeof(out->value)-1] = '\0';
    }
    cJSON_Delete(root);
    return ok;
}

static inline char *proto_build(const proto_msg *msg)
{
    if (!msg) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "command", msg->command);
    cJSON_AddStringToObject(root, "value", msg->value);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out; /* caller must free */
}

static inline bool proto_recv(int fd, proto_msg *out)
{
    char buf[256];
    ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) return false;
    buf[n] = '\0';
    return proto_parse(buf, out);
}

static inline bool proto_send(int fd, const proto_msg *msg)
{
    char *json = proto_build(msg);
    if (!json) return false;
    size_t len = strlen(json);
    ssize_t n = send(fd, json, len, MSG_NOSIGNAL);
    free(json);
    return n == (ssize_t)len;
}

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
