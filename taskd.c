/*
 * taskd.c
 *
 * A very small Firecracker-friendly daemon that:
 *   • is started by root at boot     (e.g. from /etc/rc.local or a unit file)
 *   • double-forks to detach from tty and run in the background
 *   • listens on an AF_VSOCK stream socket
 *   • accepts one connection at a time and prints a greeting, then closes
 *
 * Build:   gcc -O2 -Wall -Wextra -pedantic -std=c11 taskd.c -o taskd
 * Run:     taskd <PORT>
 *
 * Tested on: Linux 5.10+ inside a Firecracker microVM
 *
 * © 2025 – public domain / CC0
 */
#define _GNU_SOURCE
// clang-format off
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>
#include <sys/stat.h>
#include <unistd.h>
// clang-format on

// Submodule libraries
#include "protocol.h"
#include "state_machine.h"
#include "xxhash.h"
#include <cJSON.h>

static void daemonize(void) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("first fork");
    exit(EXIT_FAILURE);
  }
  if (pid > 0) /* parent */
    exit(EXIT_SUCCESS);

  /* We are in the first child. Become session leader. */
  if (setsid() == -1) {
    perror("setsid");
    exit(EXIT_FAILURE);
  }

  /* Ensure we’ll never re-acquire a controlling terminal. */
  pid = fork();
  if (pid < 0) {
    perror("second fork");
    exit(EXIT_FAILURE);
  }
  if (pid > 0) /* first child */
    exit(EXIT_SUCCESS);

  /* second child continues */

  /* Clear file-mode creation mask and change directory to / */
  umask(0);
  if (chdir("/") == -1) {
    perror("chdir");
    /* not fatal, keep going */
  }

  /* Close inherited FDs */
  for (int fd = 0; fd < 3; ++fd)
    close(fd);

  /* Redirect stdio to /dev/null */
  int nullfd = open("/dev/null", O_RDWR);
  if (nullfd >= 0) {
    dup2(nullfd, STDIN_FILENO);
    dup2(nullfd, STDOUT_FILENO);
    dup2(nullfd, STDERR_FILENO);
    if (nullfd > 2)
      close(nullfd);
  }
}

static sm_ctx *g_sm_ctx = NULL;

/* Collect JSON messages into an array instead of writing immediately. */
static void report_collect_cb(const char *json, void *ud) {
  cJSON *arr = ud;
  if (!json || !arr)
    return;
  cJSON *obj = cJSON_Parse(json);
  if (obj)
    cJSON_AddItemToArray(arr, obj);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <vsock-port>\n", argv[0]);
    return EXIT_FAILURE;
  }

  unsigned int port = (unsigned int)strtoul(argv[1], NULL, 10);
  if (port == 0) {
    fprintf(stderr, "Invalid port\n");
    return EXIT_FAILURE;
  }

  /* Fork off and turn into a daemon immediately */
  daemonize();

  /* Start the persistent state machine thread */
  g_sm_ctx = sm_thread_start();

  /* Set up AF_VSOCK listener */
  int srv_fd = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (srv_fd == -1) {
    /* Cannot log; write to syslog in a fuller implementation */
    exit(EXIT_FAILURE);
  }

  struct sockaddr_vm sa = {0};
  sa.svm_family = AF_VSOCK;
  sa.svm_port = port;
  sa.svm_cid = VMADDR_CID_ANY; /* Listen on our own CID */

  if (bind(srv_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    close(srv_fd);
    exit(EXIT_FAILURE);
  }

  if (listen(srv_fd, 32) == -1) {
    close(srv_fd);
    exit(EXIT_FAILURE);
  }

  /* Simple service loop */
  for (;;) {
    int client_fd = accept(srv_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EINTR)
        continue;
      /* Permanent failure – just restart loop; could also exit */
      continue;
    }

    /* First message must be a handshake */
    char *msg = proto_recv_json(client_fd);
    int status_code = -1;
    bool handshake_ok = false;
    if (msg) {
      handshake_msg hs;
      handshake_ok = parse_handshake(msg, &hs);
      free(msg);
      status_code = handshake_ok ? 0 : -1;
    }
    char *status_msg = report_status(status_code);
    if (status_msg) {
      char final_msg[128];  // plenty for small JSON messages
      snprintf(final_msg, sizeof(final_msg), "%s\n%c", status_msg, '\0');
      send(client_fd, final_msg, strlen(final_msg) + 1, MSG_NOSIGNAL); // +1 to send the null
      free(status_msg);
    }
    if (!handshake_ok) {
      close(client_fd);
      continue;
    }

    /* Wait for recipe */
    msg = proto_recv_json(client_fd);
    if (msg) {
      sm_instr *recipe = proto_parse_recipe(msg);
      if (recipe) {
        cJSON *resp = cJSON_CreateArray();
        sm_set_report_cb(g_sm_ctx, report_collect_cb, resp);
        if (!sm_submit(g_sm_ctx, recipe)) {
          free(msg);
          sm_set_report_cb(g_sm_ctx, NULL, NULL);
          cJSON_Delete(resp);
          close(client_fd);
          continue;
        }
        free(msg);
        int ret = 0;
        sm_wait(g_sm_ctx, &ret);
        sm_set_report_cb(g_sm_ctx, NULL, NULL);
        char *done = report_status(0);
        if (done) {
          cJSON *obj = cJSON_Parse(done);
          if (obj)
            cJSON_AddItemToArray(resp, obj);
          free(done);
        }
        char *out = cJSON_PrintUnformatted(resp);
        if (out) {
          size_t len = strlen(out);
          char *tmp = realloc(out, len + 1);
          if (tmp) {
            out = tmp;
            out[len] = '\0';
            len += 1;
          }
          send(client_fd, out, len, MSG_NOSIGNAL);
          free(out);
        }
        cJSON_Delete(resp);
      } else {
        free(msg);
      }
    }
    close(client_fd);
  }

  /* never reached */
  return EXIT_SUCCESS;
}
