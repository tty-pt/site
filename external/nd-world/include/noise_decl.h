/* nd-world's noise declarations now come from the shared library header
 * (external/libchaff, <ttypt/noise.h>): the server and the client use one
 * lattice/hash/stepper implementation. The former private copy
 * (src/noise_impl.h) was byte-for-byte the same algorithm; the worldgen
 * golden (tests/unit/data/worldgen_golden.txt) pins that the switch kept
 * every existing world bit-identical. */
#ifndef NOISE_H
#define NOISE_H

#include <ttypt/noise.h>

#endif /* NOISE_H */
