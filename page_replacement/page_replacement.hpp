#pragma once
// Umbrella header for the page-replacement policies. vmcache-n.cpp includes
// this one file; see policy.hpp for the interface and how to add a policy.

#include "policy.hpp"
#include "clock.hpp"
#include "random.hpp"

// Build the policy named by REPLACEMENT (default "clock") for one tier.
// Exits with the list of valid names if the name is unknown.
static ReplacementPolicy* makeReplacementPolicy(u64 maxCount, PageState* pageState) {
   const char* env = getenv("REPLACEMENT");
   string name = env ? env : "clock";
   if (name == "clock")
      return new ClockPolicy(maxCount, pageState);
   if (name == "random")
      return new RandomPolicy(maxCount);
   cerr << "unknown REPLACEMENT='" << name << "'; available: clock, random" << endl;
   exit(EXIT_FAILURE);
}
