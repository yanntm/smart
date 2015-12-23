
// $Id$

#include "rss_enum.h"
#include "../ExprLib/mod_vars.h"
#include "../ExprLib/exprman.h"

#include "../Modules/statesets.h"

// External libs
#include "lslib.h"    // for LS_Vector
#include "intset.h"   // for intset

// ******************************************************************
// *                                                                *
// *                     enum_reachset  methods                     *
// *                                                                *
// ******************************************************************

enum_reachset::enum_reachset(model_enum* ss)
{
  states = ss;
  natorder = 0;
  lexorder = 0;

  // keep track of which state has each index
  state_handle = new long[states->NumValues()];
  for (long j=0; j<states->NumValues(); j++) {
    const model_enum_value* st = states->ReadValue(j);
    CHECK_RANGE(0, st->GetIndex(), states->NumValues());
    state_handle[st->GetIndex()] = j;
  }

  // clear the initial vector
  initial.size = 0;
  initial.index = 0;
  initial.d_value = 0;
  initial.f_value = 0;
}

enum_reachset::~enum_reachset()
{
  Delete(states);
  delete natorder;
  delete lexorder;
  delete[] state_handle;
  delete[] initial.index;
}

void enum_reachset::getNumStates(long &ns) const
{
  ns = states->NumValues();
}

void enum_reachset::showInternal(OutputStream &os) const
{
  for (long i=0; i<states->NumValues(); i++) {
    const model_enum_value* st = states->ReadValue(i);
    os << "State " << i << " internal: (index " << st->GetIndex() << ") ";
    os.Put(st->Name());
    os << "\n";
    os.flush();
  } // for i
}

void enum_reachset::showState(OutputStream &os, const shared_state* st) const
{
  long i = st->get(states->GetIndex());
  const model_enum_value* mev = states->ReadValue(state_handle[i]);
  os.Put(mev->Name());
}

reachset::iterator& enum_reachset::iteratorForOrder(int display_order)
{
  switch (display_order) {

    case lldsm::LEXICAL:
        if (0==lexorder) lexorder = new lexical_iter(states);
        return *lexorder;

    case lldsm::DISCOVERY:
    case lldsm::NATURAL:
    default:
        if (0==natorder) natorder = new natural_iter(states);
        return *natorder;
  }
}

void enum_reachset::getReachable(result &rs) const
{
  if (0==states || 0==states->NumValues()) {
    rs.setNull();
    return;
  }
  intset* all = new intset(states->NumValues());
  all->addAll();
  rs.setPtr(new stateset(getParent(), all));
}

void enum_reachset::getPotential(expr* p, result &x) const
{
  // TBD!
  DCASSERT(0);
  x.setNull();
}

void enum_reachset::getInitialStates(result &x) const
{
  if (0==states || 0==states->NumValues()) {
    x.setNull();
    return;
  }
  intset* initss = new intset(states->NumValues());
  initss->removeAll();
  
  if (initial.index) {
    for (long z=0; z<initial.size; z++)
      initss->addElement(initial.index[z]);
  } 

  x.setPtr(new stateset(getParent(), initss));
}

void enum_reachset::setInitial(LS_Vector &init)
{
  DCASSERT(0==init.d_value);
  DCASSERT(0==init.f_value);
  initial = init;
}

// ******************************************************************
// *                                                                *
// *              enum_reachset::natural_iter  methods              *
// *                                                                *
// ******************************************************************

enum_reachset::natural_iter::natural_iter(model_enum* ss) 
{
  states = ss;
  i = states->NumValues();
}

enum_reachset::natural_iter::~natural_iter()
{
}

void enum_reachset::natural_iter::start()
{
  i = 0;
}

void enum_reachset::natural_iter::operator++(int)
{
  i++;
}

enum_reachset::natural_iter::operator bool() const
{
  return i < states->NumValues();
}

long enum_reachset::natural_iter::index() const
{
  return i;
}

void enum_reachset::natural_iter::copyState(shared_state* st) const
{
  st->set(states->GetIndex(), i);
}

// ******************************************************************
// *                                                                *
// *              enum_reachset::lexical_iter  methods              *
// *                                                                *
// ******************************************************************

enum_reachset::lexical_iter::lexical_iter(model_enum* ss) 
{
  states = ss;
  i = states->NumValues();
  map = new long[states->NumValues()];
  states->MakeSortedMap(map);
}

enum_reachset::lexical_iter::~lexical_iter()
{
  delete[] map;
}

void enum_reachset::lexical_iter::start()
{
  i = 0;
}

void enum_reachset::lexical_iter::operator++(int)
{
  i++;
}

enum_reachset::lexical_iter::operator bool() const
{
  return i < states->NumValues();
}

long enum_reachset::lexical_iter::index() const
{
  return map[i];
}

void enum_reachset::lexical_iter::copyState(shared_state* st) const
{
  st->set(states->GetIndex(), map[i]);
}

