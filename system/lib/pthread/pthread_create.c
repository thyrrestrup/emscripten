/*
 * Copyright 2021 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE
#include "pthread_impl.h"
#include "assert.h"
#include <pthread.h>
#include <stdbool.h>
#include <threads.h>

// See musl's pthread_create.c

extern int __cxa_thread_atexit(void (*)(void *), void *, void *);
extern int __pthread_create_js(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
extern void _emscripten_thread_init(int, int, int);
extern void __pthread_exit_run_handlers();
extern void __pthread_exit_done();
extern int8_t __dso_handle;

void __run_cleanup_handlers(void* _unused) {
  pthread_t self = __pthread_self();
  while (self->cancelbuf) {
    void (*f)(void *) = self->cancelbuf->__f;
    void *x = self->cancelbuf->__x;
    self->cancelbuf = self->cancelbuf->__next;
    f(x);
  }
}

void __do_cleanup_push(struct __ptcb *cb) {
  struct pthread *self = __pthread_self();
  cb->__next = self->cancelbuf;
  self->cancelbuf = cb;
  static thread_local bool registered = false;
  if (!registered) {
    __cxa_thread_atexit(__run_cleanup_handlers, NULL, &__dso_handle);
    registered = true;
  }
}

void __do_cleanup_pop(struct __ptcb *cb) {
  __pthread_self()->cancelbuf = cb->__next;
}

int __pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg) {
  return __pthread_create_js(thread, attr, start_routine, arg);
}

void _emscripten_thread_exit(void* result) {
  struct pthread *self = __pthread_self();
  assert(self);

  self->canceldisable = PTHREAD_CANCEL_DISABLE;
  self->cancelasync = PTHREAD_CANCEL_DEFERRED;
  self->result = result;

  __pthread_exit_run_handlers();

  if (self == emscripten_main_browser_thread_id()) {
    // FIXME(sbc): When pthread_exit causes the entire application to exit
    // we should be returning zero (according to the man page for pthread_exit).
    exit((intptr_t)result);
    return;
  }

  // Mark the thread as no longer running.
  // When we publish this, the main thread is free to deallocate the thread object and we are done.
  // Therefore set _pthread_self = 0; above to 'release' the object in this worker thread.
  self->threadStatus = 1;

  emscripten_futex_wake(&self->threadStatus, INT_MAX); // wake all threads

  // Not hosting a pthread anymore in this worker, reset the info structures to null.
  _emscripten_thread_init(0, 0, 0); // Unregister the thread block inside the wasm module.
  __pthread_exit_done();
}

// Mark as `no_sanitize("address"` since emscripten_pthread_exit destroys
// the current thread and runs its exit handlers.  Without this asan injects
// a call to __asan_handle_no_return before emscripten_unwind_to_js_event_loop
// which seem to cause a crash later down the line.
__attribute__((no_sanitize("address")))
_Noreturn void __pthread_exit(void* retval) {
  _emscripten_thread_exit(retval);
  emscripten_unwind_to_js_event_loop();
}

weak_alias(__pthread_create, emscripten_builtin_pthread_create);
weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
