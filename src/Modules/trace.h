
/** \file trace.h

    Module for traces.
    Defines a trace type, and appropriate operators.
*/

#ifndef TRACE_H
#define TRACE_H

#include "../include/shared.h"
#include "statesets.h"

// ******************************************************************
// *                                                                *
// *                          trace class                           *
// *                                                                *
// ******************************************************************

/**
    Represent a tree-like trace.
*/
class trace : public shared_object {
public:
  trace();

protected:
  virtual ~trace();

public:
  void setParent(const trace* t);

  /**
      The length of the trace, exluding the length of subtraces.
   */
  virtual int Length() const;

  /**
      The length of the entire trace, including the length of subtraces.
   */
  virtual int TotalLength() const;

  virtual void Append(shared_state* state);
  virtual const shared_state* getState(int i) const;
  virtual void Concatenate(int i, trace* subtrace);
  virtual const trace* getSubtrace(int i) const;

  virtual bool Print(OutputStream &s, int width) const;
  virtual bool Equals(const shared_object *o) const;

private:
  static exprman* em;

  const trace* parent;
  List<shared_state> states;
  List<trace> subtraces;

  friend class init_trace;
};

/**
    Store necessary information to generate a trace.
*/
class trace_data : public shared_object {
public:
  trace_data();

  virtual bool Print(OutputStream &s, int width) const;
  virtual bool Equals(const shared_object *o) const;
};

#endif
