#pragma once
// LRU: evict the page that was used longest ago.
//
// Same structure as LRU2b in the WATT simulator (itodnerd/WATT-simulate):
// a list ordered by recency plus a hash map from PID to its list node.
//   access: move the page's node to the back (most recent)
//   evict : take pages from the front (least recent)
//
// One global list per tier behind one mutex, i.e. exactly the shared-list
// design the WATT paper calls non-scalable (Table 1). Expect the lock to show
// up in multi-threaded throughput; the hit rate is what LRU is being measured
// for here. Memory is roughly 100 bytes per resident page (list node + map).
//
// A returned victim stays in the list until the mechanism really evicts or
// demotes it (onRemove). A candidate the mechanism skips therefore just stays
// near the front and may be offered again later.

#include <list>
#include <mutex>
#include <unordered_map>

struct LruPolicy : ReplacementPolicy {
   std::mutex mutex;
   std::list<PID> order;  // front = least recently used, back = most recent
   std::unordered_map<PID, std::list<PID>::iterator> where;

   const char* name() const override { return "lru"; }

   // Make pid the most recent page (inserting it if new). Caller holds mutex.
   void touch(PID pid) {
      auto it = where.find(pid);
      if (it == where.end()) {
         order.push_back(pid);
         where[pid] = std::prev(order.end());
      } else {
         order.splice(order.end(), order, it->second);
      }
   }

   void onInsert(PID pid) override {
      std::lock_guard<std::mutex> g(mutex);
      touch(pid);
   }

   void onRemove(PID pid) override {
      std::lock_guard<std::mutex> g(mutex);
      auto it = where.find(pid);
      if (it == where.end())
         return;
      order.erase(it->second);
      where.erase(it);
   }

   bool needsOnAccess() const override { return true; }

   void onAccess(PID pid, bool) override {
      std::lock_guard<std::mutex> g(mutex);
      auto it = where.find(pid);
      if (it == where.end())
         return;  // not in this tier any more
      order.splice(order.end(), order, it->second);
   }

   // Oldest pages first. `out` already holds what earlier calls of this
   // eviction offered, so continue after those; wrap around if we ran out.
   void selectVictims(u64 n, vector<PID>& out) override {
      std::lock_guard<std::mutex> g(mutex);
      u64 skip = out.size() < order.size() ? out.size() : 0;
      auto it = order.begin();
      std::advance(it, skip);
      for (u64 i = 0; i < n && it != order.end(); ++i, ++it)
         out.push_back(*it);
   }

   // Promotion scan: the most recently used pages are the hot ones.
   void scanResident(u64 batch, const function<void(PID)>& fn) override {
      vector<PID> hot;
      {
         std::lock_guard<std::mutex> g(mutex);
         for (auto it = order.rbegin(); it != order.rend() && hot.size() < batch; ++it)
            hot.push_back(*it);
      }
      for (PID pid : hot)  // outside the lock: fn must be free to take page locks
         fn(pid);
   }
};
