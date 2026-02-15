#include <stdio.h>
#include "values.h"

int main(void) {
  char buf[256];
  int i;

  for (i = 0; i < NVALUES; i++) {
    snprintf(buf, sizeof(buf), "%.17g", kValues[i]);
    puts(buf);
  }
  return 0;
}
