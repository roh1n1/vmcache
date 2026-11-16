#pragma once
// Umbrella header for the page-replacement policies. vmcache-n.cpp includes
// this one file; see policy.hpp for the interface and how to add a policy.

#include "policy.hpp"
#include "clock.hpp"
#include "random.hpp"
#include "lru.hpp"
#include "lean_evict.hpp"

// Build the policy named by REPLACEMENT (default "clock") for one tier.
// Exits with the list of valid names if the name is unknown.
static ReplacementPolicy* makeReplacementPolicy(u64 maxCount, PageState* pageState) {
   const char* env = getenv("REPLACEMENT");
   string name = env ? env : "clock";
   if (name == "clock")
      return new ClockPolicy(maxCount, pageState);
   if (name == "random")
      return new RandomPolicy(maxCount);
   if (name == "lru")
      return new LruPolicy();
   if (name == "leanevict")
      return new LeanEvictPolicy(maxCount, pageState);
   cerr << "unknown REPLACEMENT='" << name << "'; available: clock, random, lru, leanevict" << endl;
   exit(EXIT_FAILURE);
}
