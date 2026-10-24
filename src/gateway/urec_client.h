// UREC Live backend: read and set a machine's status by its code.
//   GET /api/machines/code/{code}          -> MachineDTO {"status": "Available" | "In Use" | ...}
//   PUT /api/machines/code/{code}/status   {"status": "In Use" | "Available"}
// Every PUT is saved, broadcast to /topic/machines and logged as CHECK_IN/CHECK_OUT, even if the
// status did not change, so main.cpp only PUTs on real transitions.
#pragma once

#include <Arduino.h>

class UrecClient {
 public:
  bool configured() const;

  // Returns the HTTP status (or a negative HTTPClient error); fills *status on 200.
  int getStatus(const char* code, String* status, String* name = nullptr);
  int putStatus(const char* code, const char* status);
};
