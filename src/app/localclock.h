/// @file app/localclock.h
/// @brief The host's local time of day, for frontends that feed Session::setClockHour().

#pragma once
#include <ctime>

namespace dio {

/// Local time of day of the host computer in hours, 0 (inclusive) to 24 (exclusive), with the
/// minutes and seconds as a fraction. Lives with the frontends because the core reads no clock.
inline double localClockHour() {
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  return t ? t->tm_hour + t->tm_min / 60.0 + t->tm_sec / 3600.0 : 12.0;
}

}  // namespace dio
