
#ifndef RNG_H
#define RNG_H

#include "mtwist.h"

/** Random number generator.
    A very thin wrapper around Geoffrey Kuenning's 
    implementation of the Mersenne-Twist pseudo-RNG.
*/
class Rng : public mt_prng {
public:
  Rng(unsigned long seed) : mt_prng(seed) { }

  /// Return a uniform (0,1).  Values 0 and 1 are NOT possible.
  inline double uniform() {  
    register unsigned long u;
    do {  // This loop should almost always require exactly 1 iteration.
      u = lrand();
    } while (0==u);  
    return u * (1.0 / 4294967296.0);
  }
};

#endif
