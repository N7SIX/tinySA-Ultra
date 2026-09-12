// host_test/test_metrology.c
//
// Host-side unit tests for tinySA-Ultra metrology-critical pure functions.
// The code under test is extracted VERBATIM from the firmware sources by
// run_tests.sh — see the .inc includes below. Nothing here duplicates the
// firmware math; if the firmware changes, the next run tests the new code.
//
// Scope (honest): validates pure logic only — interpolation/search/clamp,
// unit-conversion formulas, checksum properties, RSSI scaling round-trips.
// It does NOT validate RF calibration constants, DSP, or hardware behavior.
//
// Build+run:  ./host_test/run_tests.sh
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
  printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float)(a)-(float)(b)) <= (tol), \
  "%.6f vs %.6f (tol %.6f)", (double)(a), (double)(b), (double)(tol))

/* ---- real firmware definitions (extracted verbatim by run_tests.sh) ---- */
#include "defs.inc"   /* freq_t, pureRSSI_t, FREQ_SCALE_FACTOR, SCALE_FACTOR,
                         CORRECTION_POINTS/SIZE (TINYSA4), U_* enum, PURE macros */

/* __ROR is an ARM intrinsic; equivalent rotate-right for host gcc x86. */
#define __ROR(v, n) (((uint32_t)(v) >> (n)) | ((uint32_t)(v) << (32 - (n))))

/* ---- real checksum() from flash.c (type line included by extractor) ---- */
#include "checksum_extract.inc"

/* ---- real value()/to_dBm() from plot.c --------------------------------- */
/* plot.c puts the return type on its own line; the extractor includes it. */
struct { int unit; } setting;      /* value()/to_dBm() read setting.unit only */
#include "value_extract.inc"
#include "to_dBm_extract.inc"

/* ---- real calculate_correction() + interpolation core from sa_core.c --- */
struct {
  freq_t correction_frequency[CORRECTION_SIZE][CORRECTION_POINTS];
  float  correction_value[CORRECTION_SIZE][CORRECTION_POINTS];
} config;   /* only these two fields are referenced by the extracted code */

int32_t scaled_correction_multi[CORRECTION_SIZE][CORRECTION_POINTS];
int32_t scaled_correction_value[CORRECTION_SIZE][CORRECTION_POINTS];
#include "calc_extract.inc"

/* Wrapper mirrors the tail of get_frequency_correction(): the extracted
 * core contains its own `done:` label, and the `disabled` parameter stands
 * in for firmware's `setting.disable_correction` early-exit. */
pureRSSI_t test_interp(freq_t f, int c, pureRSSI_t cv_in, int disabled)
{
  pureRSSI_t cv = cv_in;
  if (disabled) goto done;
#include "interp_extract.inc"
}

static float interp_db(freq_t f, int c)
{
  return PURE_TO_float(test_interp(f, c, 0, 0));
}

/* ======================================================================== */
static void test_checksum(void)
{
  /* all-zero input -> zero */
  uint32_t z[4] = {0, 0, 0, 0};
  CHECK(checksum(z, sizeof z) == 0, "all-zero must checksum to 0");

  /* rotation actually happens: {0x40000000,0x80000000} -> 0 only if the
   * first word is rotated right by 31 before the second is added. */
  uint32_t r[2] = {0x40000000, 0x80000000};
  CHECK(checksum(r, sizeof r) == 0, "ROR(31)+add rotation property");

  /* order sensitivity (a naive sum would be equal here) */
  uint32_t a[2] = {1, 0}, b[2] = {0, 1};
  CHECK(checksum(a, sizeof a) != checksum(b, sizeof b), "order-sensitive");

  /* round-trip property config_recall() depends on */
  struct { uint32_t magic; uint32_t data[7]; uint32_t checksum; } cfg = {
    .magic = 0x8CD2915B,
    .data  = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00,
              0x0F1E2D3C, 0x4B5A6978, 0x8796A5B4},
  };
  cfg.checksum = checksum(&cfg, sizeof cfg - sizeof cfg.checksum);
  CHECK(cfg.checksum != 0, "nonzero checksum for real payload");
  CHECK(checksum(&cfg, sizeof cfg - sizeof cfg.checksum) == cfg.checksum,
        "round-trip matches");

  /* any bit flip in the covered area must invalidate the checksum */
  uint32_t saved = cfg.data[3];
  cfg.data[3] = saved ^ 0x00000055;
  CHECK(checksum(&cfg, sizeof cfg - sizeof cfg.checksum) != cfg.checksum,
        "bit flip detected");
  cfg.data[3] = saved;
  CHECK(checksum(&cfg, sizeof cfg - sizeof cfg.checksum) == cfg.checksum,
        "restored payload verifies again");
}

static void test_rssi_scaling(void)
{
  /* pureRSSI is dB*32: round-trip must be exact (dyadic /32) */
  for (float v = -160.0f; v <= -10.0f; v += 0.5f)
    CHECK(PURE_TO_float(float_TO_PURE_RSSI(v)) == v, "PURE round-trip %.1f", v);
}

static void test_correction_interp(void)
{
  /* synthetic table, c=0: freq[i]=1MHz*(i+1), value[i]=2*i dB.
   * scaled = dB << SCALE_FACTOR(5), i.e. pureRSSI LSB = 1/32 dB. */
  for (int i = 0; i < CORRECTION_POINTS; i++) {
    config.correction_frequency[0][i] = (freq_t)1000000ULL * (i + 1);
    config.correction_value[0][i]     = 2.0f * i;
  }
  /* intentional duplicate frequency at [3] to exercise the divider==0 guard */
  config.correction_frequency[0][3] = 4000000ULL;
  calculate_correction();

  const float LSB = 1.0f / 32.0f;

  /* below the first table entry: clamped to value[0] */
  CHECK_NEAR(interp_db(500000, 0), 0.0f, LSB);

  /* exact table hit */
  CHECK_NEAR(interp_db(1000000, 0), 2.0f, LSB);

  /* midpoint between 1MHz(2dB) and 2MHz(4dB) */
  CHECK_NEAR(interp_db(1500000, 0), 3.0f, LSB);

  /* generic interior point (3.9MHz, between 3M=4dB and 4M=6dB) */
  CHECK_NEAR(interp_db(3900000, 0), 5.8f, 2 * LSB);

  /* divider==0 guard (duplicate frequency, f inside the segment):
   * must return the left value, not divide by zero */
  CHECK_NEAR(interp_db(4500000, 0), 4.0f, LSB);

  /* above the last entry and exact last entry: clamped to value[19]=38dB */
  CHECK_NEAR(interp_db(1000000000ULL, 0), 38.0f, LSB);
  CHECK_NEAR(interp_db(20000000, 0), 38.0f, LSB);

  /* monotonicity inside a rising segment (3.0M..4.0M, 4dB..6dB) */
  float prev = interp_db(3000000, 0);
  for (freq_t f = 3000000; f <= 4000000; f += 12345) {
    float v = interp_db(f, 0);
    CHECK(v >= prev - 1e-6f, "monotonic at %llu", (unsigned long long)f);
    prev = v;
  }

  /* disabled correction: identity early-exit (firmware's
   * setting.disable_correction path) */
  CHECK(test_interp(3900000, 0, float_TO_PURE_RSSI(-5.5f), 1)
        == float_TO_PURE_RSSI(-5.5f), "disabled -> passthrough cv");
}

static void test_units(void)
{
  /* identity units */
  for (float x = -90.0f; x <= 10.0f; x += 10.0f) {
    setting.unit = U_DBM; CHECK(value(x) == x && to_dBm(x) == x, "DBM identity");
    setting.unit = U_RAW; CHECK(value(x) == x && to_dBm(x) == x, "RAW identity");
  }

  /* reference points for a 50-ohm system */
  setting.unit = U_WATT; CHECK_NEAR(value(0.0f), 0.001f, 1e-9);     /* 0dBm = 1mW */
  setting.unit = U_WATT; CHECK_NEAR(to_dBm(0.001f), 0.0f, 1e-4);
  setting.unit = U_DBUV; CHECK_NEAR(value(0.0f), 106.9897f, 0.01f); /* 0dBm = 107dBuV */
  setting.unit = U_DBMV; CHECK_NEAR(value(0.0f), 46.9897f, 0.01f);  /* 0dBm = 47dBmV */
  setting.unit = U_DBV;  CHECK_NEAR(value(0.0f), -13.0103f, 0.01f);
  setting.unit = U_VOLT; CHECK_NEAR(value(0.0f), 0.2236068f, 1e-3); /* 0dBm = 224mV */
  setting.unit = U_VPP;  CHECK_NEAR(value(0.0f), 0.6324555f, 2e-4); /* fw uses 2.828 */

  /* round-trip through every converted unit */
  const int units[] = {U_DBMV, U_DBUV, U_DBV, U_VOLT, U_VPP, U_WATT};
  const float levels[] = {-90.0f, -50.0f, -20.0f, 0.0f, 10.0f};
  for (unsigned u = 0; u < sizeof units / sizeof units[0]; u++) {
    setting.unit = units[u];
    for (unsigned l = 0; l < sizeof levels / sizeof levels[0]; l++) {
      float back = to_dBm(value(levels[l]));
      CHECK_NEAR(back, levels[l], 0.02f);
    }
  }
}

int main(void)
{
  test_checksum();
  test_rssi_scaling();
  test_correction_interp();
  test_units();
  printf("%d checks, %d failures - %s\n", checks, failures,
         failures ? "FAILED" : "ALL TESTS PASSED");
  return failures ? 1 : 0;
}
