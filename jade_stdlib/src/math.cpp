#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <algorithm>

extern "C" {

// ── jade.stdlib.math ──────────────────────────────────────────────────────────

double jade_math_sqrt (double x)         { return sqrt(x);  }
double jade_math_abs_f(double x)         { return fabs(x);  }
int32_t jade_math_abs_i(int32_t x)       { return abs(x);   }
double jade_math_pow  (double b, double e){ return pow(b,e); }
double jade_math_floor(double x)         { return floor(x); }
double jade_math_ceil (double x)         { return ceil(x);  }
double jade_math_round(double x)         { return round(x); }
double jade_math_sin  (double x)         { return sin(x);   }
double jade_math_cos  (double x)         { return cos(x);   }
double jade_math_tan  (double x)         { return tan(x);   }
double jade_math_asin (double x)         { return asin(x);  }
double jade_math_acos (double x)         { return acos(x);  }
double jade_math_atan2(double y, double x){ return atan2(y,x);}
double jade_math_log  (double x)         { return log(x);   }
double jade_math_log2 (double x)         { return log2(x);  }
double jade_math_log10(double x)         { return log10(x); }
double jade_math_exp  (double x)         { return exp(x);   }

double jade_math_min_f(double a, double b)  { return a < b ? a : b; }
double jade_math_max_f(double a, double b)  { return a > b ? a : b; }
int32_t jade_math_min_i(int32_t a, int32_t b){ return a < b ? a : b; }
int32_t jade_math_max_i(int32_t a, int32_t b){ return a > b ? a : b; }

double jade_math_clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// constants (accessed as values, not functions)
double jade_math_PI()  { return 3.14159265358979323846; }
double jade_math_E()   { return 2.71828182845904523536; }
double jade_math_INF() { return INFINITY; }
double jade_math_NAN_val() { return NAN; }
int    jade_math_is_nan(double x) { return std::isnan(x) ? 1 : 0; }
int    jade_math_is_inf(double x) { return std::isinf(x) ? 1 : 0; }

// random
void   jade_math_srand(uint32_t seed) { srand(seed); }
double jade_math_random()   { return (double)rand() / RAND_MAX; }
int32_t jade_math_rand_int(int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo);
}

} // extern "C"
