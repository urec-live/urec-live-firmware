// Replays recorded sets (download_sets.py CSVs) through gym::RepValidator, on the PC.
//
//   run_replay.cmd <csv files or folders> [--truth=<file>]
//
// Per set it prints the dips the beacon counted then (old), what the validator counts now (new),
// and each dip's verdict with its rise/return in mm. With --truth (lines "<set number> <reps>"),
// it also prints the error per set and in total.
//
// Recordings made before the lift-start wake (no "# cal=" line) start at the first dip, mid-motion:
// they replay unanchored, with gravity from the rest after the set, and their first dip is
// "unchecked" (counted, as the beacon did then). Their truth is the reps after that first dip, and
// the error compares it with the dips counted after the first one. Newer recordings start at rest
// and list every dip in "# moves=".
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "rep_validator.h"

namespace fs = std::filesystem;

struct Recording {
  std::vector<uint32_t> t;
  std::vector<std::array<int16_t, 3>> acc;
  std::vector<uint32_t> dips;  // every dip the rep counter counted
  bool haveCal = false;
  bool wake = false;
  int16_t cal[3] = {0, 0, 0};
  int setSeq = -1;
};

static void parseList(const std::string& s, std::vector<uint32_t>* out) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty() && isdigit(static_cast<unsigned char>(item[0]))) out->push_back(std::stoul(item));
  }
}

static bool load(const fs::path& p, Recording* r) {
  std::ifstream f(p);
  if (!f) return false;
  std::string line;
  std::vector<uint32_t> counts, moves;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("# counts_ms=", 0) == 0) {
      parseList(line.substr(12), &counts);
    } else if (line.rfind("# moves=", 0) == 0) {
      parseList(line.substr(8), &moves);  // "t:verdict:rise:ret" items: the number before ':' is the time
    } else if (line.rfind("# cal=", 0) == 0) {
      int x, y, z, w = 0;
      if (sscanf(line.c_str(), "# cal=%d,%d,%d wake=%d", &x, &y, &z, &w) >= 3) {
        r->haveCal = true;
        r->cal[0] = static_cast<int16_t>(x);
        r->cal[1] = static_cast<int16_t>(y);
        r->cal[2] = static_cast<int16_t>(z);
        r->wake = w != 0;
      }
    } else if (line.rfind("# w8 set=", 0) == 0) {
      r->setSeq = atoi(line.c_str() + 9);
    } else if (!line.empty() && isdigit(static_cast<unsigned char>(line[0]))) {
      unsigned t;
      int ax, ay, az;
      if (sscanf(line.c_str(), "%u,%d,%d,%d", &t, &ax, &ay, &az) == 4) {
        r->t.push_back(t);
        r->acc.push_back({static_cast<int16_t>(ax), static_cast<int16_t>(ay), static_cast<int16_t>(az)});
      }
    }
  }
  r->dips = moves.empty() ? counts : moves;
  return !r->t.empty();
}

// The rest after the set: the mean of the last 2 s.
static void restAfter(const Recording& r, int16_t out[3]) {
  double s[3] = {0, 0, 0};
  int n = 0;
  for (size_t i = 0; i < r.t.size(); i++) {
    if (r.t[i] + 2000 <= r.t.back()) continue;
    for (int a = 0; a < 3; a++) s[a] += r.acc[i][a];
    n++;
  }
  for (int a = 0; a < 3; a++) out[a] = static_cast<int16_t>(lround(s[a] / (n > 0 ? n : 1)));
}

static std::string setKey(const fs::path& p) {  // "024_set201_boot0171.csv" -> "201"
  const std::string name = p.filename().string();
  const size_t at = name.find("set");
  if (at == std::string::npos) return name;
  size_t e = at + 3;
  while (e < name.size() && isdigit(static_cast<unsigned char>(name[e]))) e++;
  return name.substr(at + 3, e - at - 3);
}

static gym::RepValidator s_v;  // large: keep it off the stack

int main(int argc, char** argv) {
  std::vector<fs::path> files;
  std::map<std::string, int> truth;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a.rfind("--truth=", 0) == 0) {
      std::ifstream f(a.substr(8));
      std::string line;
      while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string key;
        int reps;
        if (ss >> key >> reps) truth[key] = reps;
      }
    } else if (fs::is_directory(a)) {
      for (const auto& e : fs::directory_iterator(a)) {
        if (e.path().extension() == ".csv") files.push_back(e.path());
      }
    } else {
      files.push_back(a);
    }
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    fprintf(stderr, "usage: replay_reps <csv files or folders> [--truth=<file>]\n");
    return 2;
  }

  int totalOld = 0, totalNew = 0, totalErr = 0, scored = 0;
  for (const fs::path& p : files) {
    Recording r;
    if (!load(p, &r)) {
      printf("%s: no samples\n", p.filename().string().c_str());
      continue;
    }
    int16_t rest[3];
    if (r.haveCal) {
      memcpy(rest, r.cal, sizeof(rest));
    } else {
      restAfter(r, rest);
    }
    const bool anchored = r.haveCal && r.wake;
    s_v.begin(r.t[0], rest, anchored);
    size_t d = 0;
    for (size_t i = 0; i < r.t.size(); i++) {
      s_v.addSample(r.t[i], r.acc[i].data());
      while (d < r.dips.size() && r.dips[d] <= r.t[i]) s_v.onCandidate(r.dips[d++]);
    }
    while (d < r.dips.size()) s_v.onCandidate(r.dips[d++]);
    s_v.endSession(r.t.back());

    int afterFirst = 0;  // counted, not counting an unchecked first dip
    for (size_t k = 0; k < s_v.moveCount(); k++) {
      const gym::MoveInfo m = s_v.move(k);
      if (gym::moveCounted(m.verdict) && m.verdict != gym::MoveVerdict::Unchecked) afterFirst++;
    }
    const int judged = anchored ? s_v.published() : afterFirst;
    totalOld += static_cast<int>(r.dips.size());
    totalNew += s_v.published();
    std::string mark;
    const auto it = truth.find(setKey(p));
    if (it != truth.end()) {
      const int e = judged - it->second;
      char buf[48];
      snprintf(buf, sizeof(buf), " truth=%d %s", it->second, e == 0 ? "OK" : "");
      mark = buf;
      if (e != 0) {
        snprintf(buf, sizeof(buf), "ERR %+d", e);
        mark += buf;
      }
      totalErr += abs(e);
      scored++;
    }
    printf("%-22s old=%2zu new=%2u%s%s", p.filename().string().c_str(), r.dips.size(), s_v.published(),
           anchored ? "" : " (unanchored)", mark.c_str());
    {
      printf(" |");
      for (size_t k = 0; k < s_v.moveCount(); k++) {
        const gym::MoveInfo m = s_v.move(k);
        printf(" %.1f:%s", (m.dipMs - r.t[0]) / 1000.0, gym::moveVerdictName(m.verdict));
        if (m.verdict == gym::MoveVerdict::Tilted) {
          printf("(%udeg)", m.tiltDeg);
        } else if (m.verdict != gym::MoveVerdict::Superseded && m.verdict != gym::MoveVerdict::Unchecked) {
          char a[8], b[8];
          if (m.riseMm == gym::RepValidator::kUnknownMm) strcpy(a, "?"); else snprintf(a, sizeof(a), "%d", m.riseMm);
          if (m.returnMm == gym::RepValidator::kUnknownMm) strcpy(b, "?"); else snprintf(b, sizeof(b), "%d", m.returnMm);
          printf("(%s/%s)", a, b);
        }
      }
    }
    printf("\n");
  }
  printf("%zu sets: %d dips counted then, %d now", files.size(), totalOld, totalNew);
  if (scored > 0) printf("; truth on %d sets, total |error| %d", scored, totalErr);
  printf("\n");
  return 0;
}
