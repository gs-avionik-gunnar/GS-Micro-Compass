#include "Compass.h"

#include <math.h>

namespace compass {

float wrap360(float d) {
  d = fmodf(d, 360.0f);
  if (d < 0.0f) d += 360.0f;
  if (d >= 360.0f) d -= 360.0f;  // fmodf can return 360-eps rounded up
  return d;
}

float applyOffsets(float rawDeg, int declTenths, int offsetDeg) {
  return wrap360(rawDeg + (float)declTenths / 10.0f + (float)offsetDeg);
}

void Heading::configure(const Mount& m, int smoothPct) {
  m_ = m;
  if (smoothPct < 5) smoothPct = 5;
  if (smoothPct > 100) smoothPct = 100;
  alpha_ = (float)smoothPct / 100.0f;
}

void Heading::feed(int16_t rawX, int16_t rawY) {
  float x = ((float)rawX - cal_.offX) * cal_.scX;
  float y = ((float)rawY - cal_.offY) * cal_.scY;
  if (m_.swapXY) {
    float t = x;
    x = y;
    y = t;
  }
  if (m_.invX) x = -x;
  if (m_.invY) y = -y;
  if (!init_) {
    fx_ = x;
    fy_ = y;
    init_ = true;
  } else {
    fx_ += alpha_ * (x - fx_);
    fy_ += alpha_ * (y - fy_);
  }
}

float Heading::rawDeg() const { return wrap360(atan2f(fy_, fx_) * 57.29578f); }

void Calibrator::begin() {
  n_ = 0;
  minX_ = maxX_ = minY_ = maxY_ = 0.0f;
}

void Calibrator::feed(int16_t rawX, int16_t rawY) {
  float x = (float)rawX, y = (float)rawY;
  if (!n_) {
    minX_ = maxX_ = x;
    minY_ = maxY_ = y;
  } else {
    if (x < minX_) minX_ = x;
    if (x > maxX_) maxX_ = x;
    if (y < minY_) minY_ = y;
    if (y > maxY_) maxY_ = y;
  }
  ++n_;
}

bool Calibrator::finish(Cal& out) const {
  float rx = spanX(), ry = spanY();
  if (n_ < 10 || rx < kMinSpan || ry < kMinSpan) return false;
  float avg = (rx + ry) / 2.0f;
  float scx = avg / rx, scy = avg / ry;
  if (scx < 0.2f || scx > 5.0f || scy < 0.2f || scy > 5.0f) return false;
  out.offX = (maxX_ + minX_) / 2.0f;
  out.offY = (maxY_ + minY_) / 2.0f;
  out.scX = scx;
  out.scY = scy;
  out.valid = true;
  return true;
}

}  // namespace compass
