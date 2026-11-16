#pragma once
// LeanEvict (LeanStore's replacement strategy).
//
//   hot pages     : untracked, nothing happens when they are accessed
//   cooling stage : a FIFO of randomly chosen pages, bounded to a percentage
//                   of the pool (LEAN_COOLING_PCT, default 30 as in the WATT
//                   paper). A page that reaches the front of the FIFO without
//                   having been touched is evicted; a touched one is hot again.
//
// "Touched while cooling" is detected with the page-state word: cooling a page
// means setting it to Marked, and any access (GuardO/fixS/fixX) clears Marked
// back to Unlocked. So, like CLOCK, this policy needs no onAccess hook and
// workers pay nothing for it. canEvictInState therefore requires Marked.
//
// Corresponds to leanEvict2 in itodnerd/WATT-simulate (the paper does not say
// which of the simulator's two LeanEvict variants it plots).
//
// The cooling stage is manipulated only while evicting, under one mutex.

#include <deque>
#include <mutex>
#include <unordered_set>

struct LeanEvictPolicy : ReplacementPolicy {
   ResidentPageSet residentSet;
   PageState* pageState;
   u64 coolingTarget;                    // pages the cooling stage should hold
   std::mutex mutex;                     // guards cooling + inCooling
   std::deque<PID> cooling;              // FIFO, front = cooled longest ago
   std::unordered_set<PID> inCooling;    // membership; entries not here are stale

   LeanEvictPolicy(u64 maxCount, PageState* pageState)
      : residentSet(maxCount), pageState(pageState) {
      const char* env = getenv("LEAN_COOLING_PCT");
      int pct = env ? atoi(env) : 30;
      if (pct < 1 || pct > 90) {
         cerr << "LEAN_COOLING_PCT must be between 1 and 90 (got " << pct << ")" << endl;
         exit(EXIT_FAILURE);
      }
      coolingTarget = maxCount * pct / 100;
   }

   const char* name() const override { return "leanevict"; }

   void onInsert(PID pid) override { residentSet.insert(pid); }

   void onRemove(PID pid) override {
      residentSet.remove(pid);
      std::lock_guard<std::mutex> g(mutex);
      inCooling.erase(pid);  // its FIFO entry, if any, is now stale and skipped
   }

   // Move one random hot page into the cooling stage. Caller holds mutex.
   // A page that is already Marked but not in the FIFO (a victim the mechanism
   // could not take earlier) is adopted again, otherwise it would stay Marked
   // forever without ever being offered.
   void coolRandomPage() {
      thread_local std::mt19937_64 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
      u64 pid = residentSet.ht[rng() & residentSet.mask].pid.load();
      if (pid == ResidentPageSet::empty || pid == ResidentPageSet::tombstone)
         return;
      if (inCooling.count(pid))
         return;
      PageState& ps = pageState[pid];
      u64 v = ps.stateAndVersion;
      u64 state = PageState::getState(v);
      if (state == PageState::Unlocked) {
         if (!ps.tryMark(v))
            return;
      } else if (state != PageState::Marked) {
         return;  // locked or shared: in use, leave it hot
      }
      cooling.push_back(pid);
      inCooling.insert(pid);
   }

   void selectVictims(u64 n, vector<PID>& out) override {
      std::lock_guard<std::mutex> g(mutex);

      // 1. Top the cooling stage up to its target size with random hot pages.
      //    Bounded, since the pool may hold fewer pages than the target.
      u64 missing = cooling.size() < coolingTarget ? coolingTarget - cooling.size() : 0;
      for (u64 tries = 0; cooling.size() < coolingTarget && tries < 64 * missing; tries++)
         coolRandomPage();

      // 2. Take pages from the front. Still Marked = not touched while cooling
      //    = victim. No longer Marked = touched, so it is hot again.
      u64 found = 0;
      while (found < n && !cooling.empty()) {
         PID pid = cooling.front();
         cooling.pop_front();
         if (inCooling.erase(pid) == 0)
            continue;  // stale: the page left this tier meanwhile
         if (PageState::getState(pageState[pid].stateAndVersion.load()) == PageState::Marked) {
            out.push_back(pid);
            found++;
         }
      }
   }

   // Only pages still Marked (untouched while cooling) may be evicted.
   bool canEvictInState(u64 state) const override {
      return state == PageState::Marked;
   }

   // Promotion scan: sequential walk. Hot (= Unlocked) pages are exactly the
   // ones outside the cooling stage, so the caller's "Unlocked means hot" holds.
   void scanResident(u64 batch, const function<void(PID)>& fn) override {
      residentSet.iterateClockBatch(batch, fn);
   }
};
