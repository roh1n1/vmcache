#pragma once
// CLOCK (second chance), the replacement policy vmcache-n has always used.
//
// The reference bit is the page-state word itself: a page that has been
// swept once is set to Marked, and any access (GuardO/fixS/fixX) turns it back
// into Unlocked. A page found Marked on the next sweep was not touched in
// between and is returned as a victim. So Marked is CLOCK's private bit; other
// policies never need it.
//
// The clock hand lives in ResidentPageSet, which this policy owns.

struct ClockPolicy : ReplacementPolicy {
   ResidentPageSet residentSet;
   PageState* pageState;

   ClockPolicy(u64 maxCount, PageState* pageState)
      : residentSet(maxCount), pageState(pageState) {}

   const char* name() const override { return "clock"; }

   void onInsert(PID pid) override { residentSet.insert(pid); }
   void onRemove(PID pid) override { residentSet.remove(pid); }

   // One sweep over `n` slots of the clock.
   void selectVictims(u64 n, vector<PID>& out) override {
      residentSet.iterateClockBatch(n, [&](PID pid) {
         PageState& ps = pageState[pid];
         u64 v = ps.stateAndVersion;
         switch (PageState::getState(v)) {
            case PageState::Marked:
               out.push_back(pid);  // untouched since last sweep: victim
               break;
            case PageState::Unlocked:
               ps.tryMark(v);       // first encounter: give a second chance
               break;
            default:
               break;               // locked / shared: skip
         }
      });
   }

   // Only pages still Marked have used up their second chance. A page that
   // was touched after being selected is Unlocked again and must be kept.
   bool canEvictInState(u64 state) const override {
      return state == PageState::Marked;
   }

   void scanResident(u64 batch, const function<void(PID)>& fn) override {
      residentSet.iterateClockBatch(batch, fn);
   }
};
