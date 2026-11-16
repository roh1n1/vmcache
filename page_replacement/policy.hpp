#pragma once
// Page-replacement policy interface for vmcache-n.
//
// Not standalone: this header is included from vmcache-n.cpp *after*
// PageState and ResidentPageSet are defined, and uses u64/PID/vector/function
// from that translation unit.
//
// Division of labour
//   Policy    decides WHICH resident pages are good eviction candidates.
//   Mechanism (BufferPool::evict in vmcache-n.cpp) does everything else:
//             takes the page locks, writes dirty pages back, and either
//             demotes the page to the next tier or evicts it to SSD.
//
// A policy never locks a page and never moves one. It only tracks residency
// and returns PIDs. The mechanism locks the returned PIDs itself and skips any
// page that is no longer evictable, so a policy may return stale candidates.
//
// One policy instance exists per BufferPool (i.e. per memory tier), so a
// policy only ever sees the pages resident in its own tier.
//
// To add a policy: create page_replacement/<name>.hpp, implement this
// interface, and register it in makeReplacementPolicy() in
// page_replacement.hpp. Select it at runtime with REPLACEMENT=<name>.

struct ReplacementPolicy {
   virtual ~ReplacementPolicy() {}

   // Name used by REPLACEMENT=<name> and in log output.
   virtual const char* name() const = 0;

   // A page became resident in this tier (new page, page fault, promotion
   // into this tier, or demotion into this tier).
   virtual void onInsert(PID pid) = 0;

   // A page left this tier (evicted to SSD, demoted, or promoted away).
   virtual void onRemove(PID pid) = 0;

   // Append up to n eviction candidates to `out`. May append fewer than n,
   // including none; the caller calls again until it has enough. The policy
   // may update its own bookkeeping while doing so (CLOCK marks pages here).
   virtual void selectVictims(u64 n, vector<PID>& out) = 0;

   // Can a page in this PageState::getState() value still be evicted? Checked
   // by the mechanism on every candidate, both when it is returned and again
   // just before the lock is taken. The default accepts any unpinned page.
   virtual bool canEvictInState(u64 state) const {
      return state == PageState::Unlocked || state == PageState::Marked;
   }

   // Call fn(pid) for resident pages, scanning a window of `batch` slots and
   // advancing the scan position. Used by the promotion path to find other
   // hot pages to migrate together with the one being promoted.
   // Phase 1: hotness is still decided by the caller (state == Unlocked).
   virtual void scanResident(u64 batch, const function<void(PID)>& fn) = 0;
};
