#ifndef SHOOTOUT_VALUES_H
#define SHOOTOUT_VALUES_H

static volatile double kValues[] = {
    -1.0,
    0.0,
    2.2250738585072014e-308,
    2.22507386e-308,
    1.32624736e-315,
    1.32624737e-315,
    1e-6,
    1.0,
    2.7182818284590452353602874713526,
    3.1415926535897932384626433832795,
    1e20,
    1.79769313e308,
};
#define NVALUES ((int)(sizeof(kValues) / sizeof(kValues[0])))

#endif
