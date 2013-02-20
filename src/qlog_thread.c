/*
 * See Copyright Notice in qnode.h
 */

#include <unistd.h>
#include <stdio.h>
#include "qassert.h"
#include "qconfig.h"
#include "qdefines.h"
#include "qengine.h"
#include "qlist.h"
#include "qlog.h"
#include "qlog_thread.h"
#include "qmailbox.h"
#include "qmalloc.h"
#include "qmsg.h"
#include "qserver.h"
#include "qsignal.h"
#include "qthread_log.h"

pthread_key_t g_thread_log_key = PTHREAD_ONCE_INIT;
qlog_thread_t *g_log_thread = NULL;

static void thread_log_box(int fd, int flags, void *data) {
  UNUSED(fd);
  UNUSED(flags);
  qsignal_t *signal = (qsignal_t*)data;
  int idx;
  int i = 0;
  for (i = 0; ; i++) {
    if (signal == g_log_thread->signals[i]) {
      idx = i;
      break;
    }
  }
  qlist_t *list = NULL;
  qlog_t *log;
  qthread_log_t *thread_log = g_server->thread_log[idx];
  qthread_log_fetch(thread_log, &list);
  /*
   * -1 means destroy the log thread
   */
  if (flags != -1) {
    qsignal_recv(signal);
    qsignal_active(signal, 0);
  }

  qlist_t *pos, *next;
  for (pos = list->next; pos != list; ) {
    log = qlist_entry(pos, qlog_t, entry);
    next = pos->next;
    qlist_del_init(&(log->entry));

    log->n += sprintf(log->buff + log->n, " %s:%d ", log->file, log->line);
    vsprintf(log->buff + log->n, log->format, log->args);
    printf("%s\n", log->buff);
    if (flags == -1) {
      /* do flush I/O work */
    }

    qfree(log);
    pos = next;
  }
}

static void* main_loop(void *arg) {
  qlog_thread_t *thread = (qlog_thread_t*)arg;
  thread->started = 1;
  while (thread->started && qengine_loop(thread->engine) == 0) {
  }
  return NULL;
}

void log_key_destroy(void *value) {
  UNUSED(value);
}

int qlog_thread_new(int thread_num) {
  g_log_thread = qalloc_type(qlog_thread_t);
  if (g_log_thread == NULL) {
    return -1;
  }
  if (pthread_key_create(&g_thread_log_key, log_key_destroy) < 0) {
    return -1;
  }
  g_log_thread->engine = qengine_new();
  if (g_log_thread->engine == NULL) {
    qfree(g_log_thread);
    return -1;
  }
  g_log_thread->thread_num = thread_num;
  g_log_thread->signals = (qsignal_t**)qmalloc(thread_num * sizeof(qsignal_t*));
  int i = 0;
  for (i = 0; i < thread_num; ++i) {
    g_log_thread->signals[i] = qsignal_new();
    int fd = qsignal_get_fd(g_log_thread->signals[i]);
    qengine_add_event(g_log_thread->engine, fd, QEVENT_READ, thread_log_box, g_log_thread->signals[i]);
  }
  g_log_thread->started = 0;
  int result;
  result = pthread_create(&g_log_thread->id, NULL, main_loop, g_log_thread);
  qassert(result == 0);
  /* ugly, but works */
  while (g_log_thread->started == 0) {
    usleep(100);
  }
  return 0;
}

void qlog_thread_destroy() {
  int i;
  for (i = 0; i < g_log_thread->thread_num; ++i) {
    qsignal_t *signal = g_log_thread->signals[i];
    thread_log_box(0, -1, signal);
    qsignal_destroy(signal);
  }
  qfree(g_log_thread->signals);
  qengine_destroy(g_log_thread->engine);
  qfree(g_log_thread);
  g_log_thread = NULL;
}

void qlog_thread_active(int idx) {
  if (qsignal_active(g_log_thread->signals[idx], 1) == 0) {
    qsignal_send(g_log_thread->signals[idx]);
  }
}
