#include <emmintrin.h>
__m128d test(__m128d a, __m128d b) {
  return _mm_sub_pd(a,b);
}
