#pragma once
// random: evict a uniformly random resident page. No per-page state, so it
// is the lower bound the other policies should beat. It never uses Marked.
//
// Pages are sampled by probing random slots of the resident hash table and
// skipping empty/tombstone slots. The table is sized for 1.5x the pool, so a
// probe hits a resident page with probability >= ~1/3 when the pool is full.

struct RandomPolicy : ReplacementPolicy {
   ResidentPageSet residentSet;

   explicit RandomPolicy(u64 maxCount) : residentSet(maxCount) {}

   const char* name() const override { return "random"; }

   void onInsert(PID pid) override { residentSet.insert(pid); }
   void onRemove(PID pid) override { residentSet.remove(pid); }

   void selectVictims(u64 n, vector<PID>& out) override {
      thread_local std::mt19937_64 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
      u64 found = 0;
      // Bounded: an almost-empty pool would otherwise spin forever here.
      // The caller retries if we return fewer than n.
      for (u64 tries = 0; found < n && tries < 64 * n; tries++) {
         u64 pid = residentSet.ht[rng() & residentSet.mask].pid.load();
         if (pid == ResidentPageSet::empty || pid == ResidentPageSet::tombstone)
            continue;
         out.push_back(pid);
         found++;
      }
   }

   // Promotion scan: sequential walk, same as CLOCK's.
   void scanResident(u64 batch, const function<void(PID)>& fn) override {
      residentSet.iterateClockBatch(batch, fn);
   }
};
