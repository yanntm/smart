
// $Id$

#ifndef DSM_H
#define DSM_H

#include "../Language/exprs.h"
#include "../States/stateheap.h"
#include "../Chains/procs.h"

/** @name dsm.h
    @type File
    @args \ 

  Discrete-state model.
  The meta model class used by solution engines.

  Hopefully this can avoid virtual functions for the critical operations.

  We will still use virtual functions and derived classes for:
	Showing states

*/

//@{

class reachset;  // defined in States/reachset.h

/** Meta-model class.
    
    For explicit models such as Markov chains, we supply full functionality
    for completeness, but some functions probably will never be used.
    E.g., the events correspond to the arcs in the chain; we can then produce
    expressions for the "next state function", but since the "underlying"
    Markov chain is already known, we should never need those expressions.
*/

class state_model {
  int events;
  const char* name; 
public:  // for now at least
  /// Type of the underlying process
  Process_type proctype;
  /// Reachable states.
  reachset *statespace; 
  // Reachability graph?

  // Expansion: either list the different types here, or enclose them
  // within a struct (e.g., "stochastic process")
  markov_chain *mc;

public:
  state_model(const char* name, int e);
  virtual ~state_model();

  inline int NumEvents() const { return events; }
  inline const char* Name() const { return name; }

  virtual void ShowState(OutputStream &s, const state &x) = 0;
  virtual void ShowEventName(OutputStream &s, int e) = 0;

  /** Returns the number of initial states for this model.
      For a Petri net, this should be 1.
      For a Markov chain, this can be greater than 1.
  */
  virtual int NumInitialStates() const = 0;

  /** Fills the nth initial state for this model.
      0 <= n < NumInitialStates()
  */
  virtual void GetInitialState(int n, state &s) const = 0;

  /** Build an expression that determines if a given event is enabled.
      The expression should be of type "proc bool".
  */
  virtual expr* EnabledExpr(int e) = 0;

  /** Build an expression that determines the next state when an event occurs.
      The expression should be of type "proc state".
      The expression may assume that the event is enabled.
      Note: if the occurrence of the event in a given state can lead
            to more than one new state, the expression ERROR should be
	    returned.
	    In this case, use a different function (below).
  */
  virtual expr* NextStateExpr(int e) = 0;

  /** Build an expression that determines the set of states that can
      be reached when an event occurs.
      The expression should be of type "proc stateset"
         (using an explicit stateset representation).
  */ 
  virtual expr* NextStatesExpr(int e) {
    Internal.Start(__FILE__, __LINE__);
    Internal << "NextStatesExpr is just an idea at this point, not implemented!\n"; 
    Internal.Stop();
    return ERROR;  // Keep compiler happy
  }

  /** Build an expression that determines an event's firing distribution.
      See EventDistributionType for the possible types of this expression.
      The expression may assume that the event is enabled.
      If the event has no assigned distribution (e.g., for model checking)
      	then NULL should be returned.
      Note: this effectively assumes that the occurrence of the event
            in a given state can lead to at most one new state;
	    if this is not the case, the expression ERROR should be
	    returned.
  */
  virtual expr* EventDistribution(int e) = 0;

  /** Just return the type of an event's firing distribution.
      (assuming the event is enabled)
      If the distribution depends on the state, then the type will be
      "proc T", otherwise it is simply "T", where T can be any of
         int, real, ph int, ph real, expo, rand int, rand real.
      If the event has no assigned distribution (e.g., for model checking)
      	then VOID should be returned.
      Note: this effectively assumes that the occurrence of the event
            in a given state can lead to at most one new state;
	    if this is not the case, then NO_SUCH_TYPE should be returned.
  */
  virtual type EventDistributionType(int e) = 0;

  void DetermineProcessType();
};

void Delete(state_model *x);

OutputStream& operator<< (OutputStream &s, state_model *m);

//@}

#endif

