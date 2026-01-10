#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "common.h"
#include "vm.h"
#include "lx/lxlx.h"

int main(int argc, const char* argv[]) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  srand(ts.tv_sec * 1000000000 + ts.tv_nsec);

  LX_ARGC = argc;
  LX_ARGV = argv;

  initVM();

  InterpretResult r = interpret((uint8_t*)lxlx_bytecode);
  freeVM();

  if (r == INTERPRET_RUNTIME_ERROR) return 70;
  if (r == INTERPRET_COMPILE_ERROR) return 65;
  return 0;
}
