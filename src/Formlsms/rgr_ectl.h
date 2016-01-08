
// $Id$

#ifndef RGR_ECTL_H
#define RGR_ECTL_H

#include "graph_llm.h"

/**
    Abstract base class, for reachgraphs that use explicit CTL stuff.
*/
class ectl_reachgraph : public graph_lldsm::reachgraph {

  public:
    ectl_reachgraph();

  protected:
    virtual ~ectl_reachgraph();
    virtual const char* getClassName() const { return "ectl_reachgraph"; }

  protected:
    // Go forward from p, one step, store result in r.
    // If p and r refer to the same thing, might go more than one step.
    // Returns true if some state was added to r.
    virtual bool forward(const intset& p, intset &r) const = 0;
    virtual bool backward(const intset& p, intset &r) const = 0;
    
    // Build set of absorbing states (no outgoing edges)
    // Default: builds emptyset
    virtual void absorbing(intset &r) const;

    // Build set of source (initial) states
    // Default: builds emptyset
    virtual void source(intset &r) const;

    /** Determine TSCCs satisfying a property (absorbing don't count).
        This is done "in place".  Should only be called for "fair" models;
        behavior is undefined if the model is not fair.
        Default behavior here is to empty p.
          @param  p   On input: property p.
                      On output: states are removed if
                      they are not in a TSCC, or if
                      not all states in the TSCC satisfy p.
    */
    virtual void getTSCCsSatisfying(intset &p) const;

    // TBD - there will be others

  public:
    virtual stateset* EX(bool revTime, const stateset* p) const;
    virtual stateset* EU(bool revTime, const stateset* p, const stateset* q) const;
    virtual stateset* unfairEG(bool revTime, const stateset* p) const;
    virtual stateset* fairEG(bool revTime, const stateset* p) const;
    /*
    virtual stateset* unfairAEF(bool revTime, const stateset* p, const stateset* q) const;
    */

  private:
    long _EU(bool rt, const intset& p, const intset& q, intset &r, intset &tmp) const;
    long unfair_EG(bool rt, const intset &p, intset &r, intset &tmp) const;
    long fair_EG(bool rt, const intset &p, intset &r, intset &tmp) const;

    // TBD - there will be others
};

#endif

