#ifdef RYU64_WASI_BUILD
#include "wasi_io.h"
#else
#include <stdio.h>
#endif

#include "ryu64.h"
#include "values.h"

int main(void) {
  char buf[256];
  size_t len;
  ryu_printf_spec spec;
  int i;

  spec.kind = RYU_FMT_G;
  spec.precision = 17;
  spec.uppercase = 0;
  spec.alternate_form = 0;
  spec.always_sign = 0;
  spec.space_sign = 0;

  for (i = 0; i < NVALUES; i++) {
    ryu64_to_printf(buf, sizeof(buf), kValues[i], &spec, &len);
    buf[len] = '\0';
#ifdef RYU64_WASI_BUILD
    write_line(buf, len);
#else
    puts(buf);
#endif
  }
  return 0;
}
