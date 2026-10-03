#pragma once
#include <math.h>
#include <stdint.h>
#include <time.h>

namespace solarphase {
enum class Phase { Unknown, Day, Night, Sunrise, Sunset };
struct State {
  Phase phase = Phase::Unknown;
  bool day = false;
};
inline const char* name(Phase phase) {
  switch (phase) {
    case Phase::Day: return "day";
    case Phase::Night: return "night";
    case Phase::Sunrise: return "sunrise";
    case Phase::Sunset: return "sunset";
    default: return "unknown";
  }
}
// NOAA fractional-year solar-position approximation, evaluated in UTC.
// https://gml.noaa.gov/grad/solcalc/solareqns.PDF
// Icons mark a twilight band (-6..+6 degrees), not exact event times.
inline State calculate(time_t current, float latitude, float longitude) {
  if (current <= 1700000000 || !isfinite(latitude) || !isfinite(longitude) ||
      fabsf(latitude) > 90 || fabsf(longitude) > 180) return {};
  struct tm utc = {};
#ifdef _WIN32
  if (gmtime_s(&utc, &current)) return {};
#else
  if (!gmtime_r(&current, &utc)) return {};
#endif
  const int year = utc.tm_year + 1900;
  const bool leap = year%4 == 0 && (year%100 != 0 || year%400 == 0);
  constexpr float rad = 0.01745329252f;
  const float hours = utc.tm_hour + utc.tm_min/60.0f + utc.tm_sec/3600.0f;
  const float gamma = 6.28318530718f / (leap ? 366 : 365) * (utc.tm_yday + (hours - 12)/24);
  const float equation = 229.18f * (0.000075f + 0.001868f*cosf(gamma) - 0.032077f*sinf(gamma)
      - 0.014615f*cosf(2*gamma) - 0.040849f*sinf(2*gamma));
  const float declination = 0.006918f - 0.399912f*cosf(gamma) + 0.070257f*sinf(gamma)
      - 0.006758f*cosf(2*gamma) + 0.000907f*sinf(2*gamma)
      - 0.002697f*cosf(3*gamma) + 0.00148f*sinf(3*gamma);
  float solarMinutes = fmodf(hours*60 + equation + 4*longitude, 1440);
  if (solarMinutes < 0) solarMinutes += 1440;
  const float hourAngle = solarMinutes/4 - 180;
  const float sine = sinf(latitude*rad)*sinf(declination)
      + cosf(latitude*rad)*cosf(declination)*cosf(hourAngle*rad);
  const float elevation = asinf(fmaxf(-1, fminf(1, sine))) / rad;
  State result;
  result.day = elevation >= -0.833f;
  result.phase = elevation >= 6 ? Phase::Day : elevation <= -6 ? Phase::Night
      : hourAngle < 0 ? Phase::Sunrise : Phase::Sunset;
  return result;
}
} // namespace solarphase
