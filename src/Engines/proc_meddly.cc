
// $Id$

#define PROC_MEDDLY_DETAILS
#include "proc_meddly.h"

#include "../Options/options.h"

#include "../Modules/glue_meddly.h"
#include "../Modules/expl_states.h"
#define DSDE_HLM_DETAILS
#include "../Formlsms/dsde_hlm.h"
#include "../Formlsms/rss_mdd.h"

// #define DUMP_MXD
// #define DUMP_MTMXD
// #define DEBUG_BUILD_SV
// #define DEBUG_SHAREDEDGE
// #define DEBUG_ENCODE
// #define DEBUG_EVENT_CONSTR
// #define DEBUG_EVENT_OVERALL
// #define DEBUG_NOCHANGE

#define SHOW_SUBSTATES
#define DEBUG_EXPLORE_ENABLING
#define DEBUG_EXPLORE_FIRING
#define SHOW_MINTERMS

// **************************************************************************
// *                                                                        *
// *                          minterm_pool methods                          *
// *                                                                        *
// **************************************************************************

minterm_pool::minterm_pool(int max_minterms, int depth)
{
  term_depth = depth;
  alloc = max_minterms+1;
  free_list = 0;
  minterms = new int*[alloc];
  for (int i=0; i<alloc; i++) minterms[i] = 0;
  used = 1;
}

minterm_pool::~minterm_pool()
{
  for (int i=1; i<used; i++) delete[] minterms[i];
  delete[] minterms;
}

int* minterm_pool::allocMinterm()
{
  CHECK_RANGE(0, used, alloc);
  int* answer = new int[term_depth+2];
  answer[term_depth] = 1;
  answer[term_depth+1] = used;
  minterms[used] = answer;
  used++;
  return answer;
}

void minterm_pool::reportStats(DisplayStream &out) const
{
  out << "\t" << used << " minterms used, required ";
  size_t batchmem = (term_depth+2)*sizeof(int)*used + alloc*sizeof(int*);
  out.PutMemoryCount(batchmem, 3);
  out << "\n";
}

// **************************************************************************
// *                                                                        *
// *                        meddly_varoption methods                        *
// *                                                                        *
// **************************************************************************

bool meddly_varoption::vars_named;

meddly_varoption::meddly_varoption(meddly_states &x, const dsde_hlm &p)
: ms(x), parent(p)
{
  built_ok = true;
  event_enabling = new MEDDLY::dd_edge*[parent.getNumEvents()];
  event_firing = new MEDDLY::dd_edge*[parent.getNumEvents()];
  for (int i=0; i<parent.getNumEvents(); i++) {
    event_enabling[i] = event_firing[i] = 0;
  }
}

meddly_varoption::~meddly_varoption()
{
  if (event_enabling) {
    for (int i=0; i<parent.getNumEvents(); i++) delete event_enabling[i];
    delete[] event_enabling;
  }
  if (event_firing) {
    for (int i=0; i<parent.getNumEvents(); i++) delete event_firing[i];
    delete[] event_firing;
  }
}

void meddly_varoption::initializeVars()
{
  // We're good, just need to set the initial states
  DCASSERT(ms.mdd_wrap);

  // allocate what we need
  shared_state* st = new shared_state(&parent);
  int num_init = parent.NumInitialStates();
  int** mt = new int* [num_init];
#ifdef DEBUG_SHAREDEDGE
  fprintf(stderr, "initial: ");
#endif

  // Convert initial states to minterms
  for (int n=0; n<num_init; n++) {
    mt[n] = new int[ms.getNumVars()+1];
    parent.GetInitialState(n, st);
    ms.mdd_wrap->state2minterm(st, mt[n]);
  } // for n

  // Build DD from minterms
  ms.initial = smart_cast <shared_ddedge*> (ms.mdd_wrap->makeEdge(0));
  DCASSERT(ms.initial);
  try {
    ms.mdd_wrap->createMinterms(mt, num_init, ms.initial);
    // Cleanup
    for (int n=0; n<num_init; n++)  delete[] mt[n];
    delete[] mt;
    Delete(st);
    return;
  }
  catch (sv_encoder::error e) {
    // Cleanup
    for (int n=0; n<num_init; n++)  delete[] mt[n];
    delete[] mt;
    Delete(st);
    switch (e) {
      case sv_encoder::Out_Of_Memory:   throw subengine::Out_Of_Memory;
      default:                          throw subengine::Engine_Failed;
    }
  }
}

char* meddly_varoption::buildVarName(const hldsm::partinfo &part, int k)
{
  if (!vars_named) return 0;
  StringStream s;
  bool printed = false;
  for (int p=part.pointer[k]; p>part.pointer[k-1]; p--) {
     if (printed)  s << ":";
     else          printed = true;
     s << part.variable[p]->Name();
  }  // for p
  return s.GetString();
}

void meddly_varoption::reportStats(DisplayStream &out) const
{
  ms.reportStats(out);
}

// **************************************************************************
// *                                                                        *
// *                         bounded_encoder  class                         *
// *                                                                        *
// **************************************************************************

class bounded_encoder : public meddly_encoder {
  int* terms;
  int maxbound;
  long lastcomputed;
  traverse_data tdx;
  result ans;
  const hldsm &parent;
  shared_state* expl_state;
public:
  bounded_encoder(const char* n, MEDDLY::forest* f, const hldsm &p, int max_var_size);
protected:
  virtual ~bounded_encoder();
public:
  virtual bool arePrimedVarsSeparate() const { return false; }
  virtual int getNumDDVars() const { return parent.getPartInfo().num_levels; }
  virtual void buildSymbolicSV(const symbol* sv, bool primed, 
                                expr *f, shared_object* answer);

  virtual void state2minterm(const shared_state* s, int* mt) const;
  virtual void minterm2state(const int* mt, shared_state *s) const;

  virtual meddly_encoder* copyWithDifferentForest(const char*, MEDDLY::forest*) const;
protected:
  void FillTerms(const model_statevar* sv, int p, int &i, expr* f);
};

// **************************************************************************
// *                                                                        *
// *                        bounded_encoder  methods                        *
// *                                                                        *
// **************************************************************************

bounded_encoder
::bounded_encoder(const char* n, MEDDLY::forest* f, const hldsm &p, int max_var_size)
: meddly_encoder(n, f), tdx(traverse_data::Compute), parent(p)
{
  DCASSERT(parent.hasPartInfo());
  maxbound = MAX(parent.getPartInfo().num_levels+1, max_var_size);
  terms = new int[maxbound];
  expl_state = new shared_state(&p);
  tdx.answer = &ans;
  tdx.current_state = expl_state;
}

bounded_encoder::~bounded_encoder()
{
  delete[] terms;
  Delete(expl_state);
}

void bounded_encoder
::buildSymbolicSV(const symbol* sv, bool primed, expr* f, shared_object* answer)
{
  if (0==sv) throw Failed;
  shared_ddedge* dd = dynamic_cast<shared_ddedge*> (answer);
  if (0==dd) throw Invalid_Edge;
#ifdef DEVELOPMENT_CODE
  if (dd->numRefs()>1) throw Shared_Output_Edge;
#endif
  DCASSERT(dd);

  const model_statevar* mv = dynamic_cast<const model_statevar*> (sv);
  DCASSERT(mv);

  int level = mv->GetPart();
  CHECK_RANGE(1, level, 1+parent.getPartInfo().num_levels);

  int i = 0;
  FillTerms(mv, parent.getPartInfo().pointer[level], i, f);

  try {
    F->createEdgeForVar(level, primed, terms, dd->E);

#ifdef DEBUG_BUILD_SV
    fprintf(stderr, "%#010lx: ", (unsigned long)answer);
    fprintf(stderr, " built variable %s", mv->Name());
    if (primed) fprintf(stderr, "'");
    fprintf(stderr, " (level %d", level);
    if (primed) fprintf(stderr, "'");
    fprintf(stderr, "): ");
    dd->E.show(stderr, 0);
    fprintf(stderr, "\n");
#endif
  }
  catch (MEDDLY::error e) {
    convert(e);
  }
}

void bounded_encoder
::FillTerms(const model_statevar* sv, int p, int &i, expr* f)
{
  DCASSERT(sv);
  if (parent.getPartInfo().pointer[sv->GetPart()-1] >= p) {
    CHECK_RANGE(0, i, maxbound);
    terms[i] = lastcomputed;
    i++;
    return;
  }
  long stop = sv->NumPossibleValues();
  if (parent.getPartInfo().variable[p] != sv) {
    for (long v=0; v<stop; v++) {
      FillTerms(sv, p-1, i, f);
    } // for v
    return;
  }
  for (long v=0; v<stop; v++) {
    if (f) {
      sv->GetValueNumber(v, ans);
      sv->SetNextState(tdx, expl_state, ans.getInt());
      f->Compute(tdx);
    } else {
      sv->GetValueNumber(v, ans);
    }
    lastcomputed = ans.getInt();
    FillTerms(sv, p-1, i, f);
  } // for v
}

void 
bounded_encoder::state2minterm(const shared_state* s, int* mt) const
{
  if (0==s || 0==mt) throw Failed;

  const hldsm::partinfo &part = parent.getPartInfo();
  int p = part.pointer[part.num_levels];
  for (int k=part.num_levels; k; k--) {
    int stop = part.pointer[k-1];
    mt[k] = 0;
    for (; p> stop; p--) {
      mt[k] *= part.variable[p]->NumPossibleValues();
      mt[k] += s->get(part.variable[p]->GetIndex());
    } // for p
  } // for k
}

void
bounded_encoder::minterm2state(const int* mt, shared_state *s) const
{
  if (0==s || 0==mt) throw Failed;

  int p = 0;
  const hldsm::partinfo &part = parent.getPartInfo();
  for (int k=1; k<= part.num_levels; k++) {
    int stop = part.pointer[k];
    int digit = mt[k];
    for (; p <= stop; p++) {
      int b = part.variable[p]->NumPossibleValues();
      s->set(part.variable[p]->GetIndex(), digit % b);
      digit /= b;
    } // for p
  } // for k
}

meddly_encoder* 
bounded_encoder::copyWithDifferentForest(const char* n, MEDDLY::forest* nf) const
{
  return new bounded_encoder(n, nf, parent, maxbound);
}


// **************************************************************************
// *                                                                        *
// *                        bounded_varoption  class                        *
// *                                                                        *
// **************************************************************************

class bounded_varoption : public meddly_varoption {
  int* minterm; // unprimed minterm
  int* minprim; // primed minterm
public:
  meddly_encoder* mtmxd_wrap;
public:
  bounded_varoption(meddly_states &x, const dsde_hlm &p, const exprman* em,
    const meddly_procgen &pg);

  virtual ~bounded_varoption();

  virtual void initializeEvents(named_msg &d);

  virtual void updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl);

  virtual bool hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl);

  virtual void reportStats(DisplayStream &out) const;

protected: // in the following, dd is an mxd edge.
  void encodeExpr(expr* e, MEDDLY::dd_edge &dd, const char *what, const char* who);

  void buildNoChange(const model_event &e, MEDDLY::dd_edge &dd);

private:
  void checkBounds(const exprman* em);
  int  initDomain(const exprman* em);
  void initEncoders(int maxbound, const meddly_procgen &pg);

};

// **************************************************************************
// *                                                                        *
// *                       bounded_varoption  methods                       *
// *                                                                        *
// **************************************************************************

bounded_varoption
::bounded_varoption(meddly_states &x, const dsde_hlm &p, 
  const exprman* em, const meddly_procgen &pg) : meddly_varoption(x, p)
{
  minterm = 0;
  minprim = 0;
  mtmxd_wrap = 0;

  checkBounds(em);
  int maxbounds = initDomain(em);
  initEncoders(maxbounds, pg);
}

bounded_varoption::~bounded_varoption()
{
  delete[] minterm;
  delete[] minprim;
  Delete(mtmxd_wrap);
}

void bounded_varoption::initializeEvents(named_msg &d)
{
  // Nothing to do?
}

void bounded_varoption::updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl)
{
  DCASSERT(built_ok);
  DCASSERT(nsf);
  DCASSERT(event_enabling);
  DCASSERT(event_firing);
  MEDDLY::forest* f = nsf->getForest();
  DCASSERT(f);
  MEDDLY::dd_edge mask(f);

  // For now, don't bother checking cl; just build everything

  for (int i=0; i<parent.getNumEvents(); i++) {
    const model_event* e = parent.readEvent(i);
    DCASSERT(e);

    //
    // Build enabling for this event
    //

    if (d.startReport()) {
      d.report() << "Building enabling   DD for event ";
      d.report() << e->Name() << "\n";
      d.stopIO();
    }

    if (0==event_enabling[i]) event_enabling[i] = new MEDDLY::dd_edge(f);
    encodeExpr(
      e->getEnabling(), *event_enabling[i], "enabling of event", e->Name()
    );

    //
    // Build firing for this event
    //

    if (d.startReport()) {
      d.report() << "Building next-state DD for event ";
      d.report() << e->Name() << "\n";
      d.stopIO();
    }

    if (0==event_firing[i])   event_firing[i]   = new MEDDLY::dd_edge(f);
    encodeExpr(
      e->getNextstate(), *event_firing[i], "firing of event", e->Name()
    );

    // Build "don't change" mask for this event; "AND" it with firing
    buildNoChange(*e, mask);

#ifdef DEBUG_EVENT_CONSTR
    printf("Don't change mask for event %s (#%d):\n", e->Name(), i);
    mask.show(stdout, 2);
    printf("Raw next state for event %s:\n", e->Name());
    event_firing[i]->show(stdout, 2);
#endif

    (*event_firing[i]) *= mask;

#ifdef DEBUG_EVENT_OVERALL
    printf("Final next state for event %s:\n", e->Name());
    event_firing[i]->show(stdout, 2);
#endif
  } // for i
}

bool bounded_varoption::hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl)
{
  return false;
}




void bounded_varoption::reportStats(DisplayStream &out) const
{
  meddly_varoption::reportStats(out);
  if (mtmxd_wrap) mtmxd_wrap->reportStats(out);
#ifdef DUMP_MXD
  out << "NSF root in MXD: " << ms.nsf->E.getNode() << "\n";
  out << "MXD forest:\n";
  out.flush();
  ms.mxd_wrap->getForest()->showInfo(out.getDisplay(), 1);
  fflush(out.getDisplay());
#endif
#ifdef DUMP_MTMXD
  out << "MTMXD forest:\n";
  out.flush();
  mtmxd_wrap->getForest()->showInfo(out.getDisplay(), 1);
  fflush(out.getDisplay());
#endif

}

void bounded_varoption
::encodeExpr(expr* e, MEDDLY::dd_edge &dd, const char* what, const char* who)
{
  DCASSERT(mtmxd_wrap);
  if (0==e) {
    MEDDLY::forest* f = dd.getForest();
    f->createEdge(true, dd);
    return;
  }
  DCASSERT(e);
  result foo;
  traverse_data x(traverse_data::BuildDD);
  x.answer = &foo;
  x.ddlib = mtmxd_wrap;

  // First, build the expr as an MTMXD
  e->Traverse(x);
  if (foo.isNull()) {
    if (parent.StartError(0)) {
      parent.SendError("Got null result for ");
      parent.SendError(what);
      parent.SendError(who);
      parent.DoneError();
    }
    throw subengine::Engine_Failed;
  }
  shared_ddedge* me = smart_cast <shared_ddedge*>(foo.getPtr());
  DCASSERT(me);

#ifdef DEBUG_ENCODE
  parent.getEM()->cout() << "MTMXD encoding for expr: ";
  e->Print(parent.getEM()->cout(), 0);
  parent.getEM()->cout() << " is node " << me->E.getNode() << "\n";
  parent.getEM()->cout().flush();
  me->E.show(stdout, 2);
#endif

  // now, copy it into the MXD
  try {
    MEDDLY::apply(MEDDLY::COPY, me->E, dd);
    return;
  }
  catch (MEDDLY::error ce) {
    // An error occurred, report it...
    if (parent.StartError(0)) {
      parent.SendError("Meddly error ");
      parent.SendError(ce.getName());
      parent.SendError(" for ");
      parent.SendError(what);
      parent.SendError(who);
      parent.DoneError();
    }

    // ...and figure out which one
    if (MEDDLY::error::INSUFFICIENT_MEMORY == ce.getCode())
      throw subengine::Out_Of_Memory;

    throw subengine::Engine_Failed;
  }
}

void bounded_varoption::buildNoChange(const model_event &e, MEDDLY::dd_edge &dd)
{
  DCASSERT(minterm);
  DCASSERT(mtmxd_wrap);
  const hldsm::partinfo &part = parent.getPartInfo();

  // "don't change" levels
  for (int k=1; k<=part.num_levels; k++) {
    minterm[k] = MEDDLY::DONT_CARE;
    minprim[k] = (e.nextstateDependsOnLevel(k)) 
        ? MEDDLY::DONT_CARE 
        : MEDDLY::DONT_CHANGE;
  }
  DCASSERT(dd.getForest());
  try {
    dd.getForest()->createEdge(&minterm, &minprim, 1, dd);
  }
  catch (MEDDLY::error fe) {
    if (MEDDLY::error::INSUFFICIENT_MEMORY == fe.getCode()) 
      throw subengine::Out_Of_Memory;
    throw subengine::Engine_Failed;
  }

  // Now, "and" in the "don't change" variables
  // that are associated with "do change" levels, if any

  shared_ddedge* x = smart_cast <shared_ddedge*>(mtmxd_wrap->makeEdge(0));
  DCASSERT(x);
  shared_ddedge* xp = smart_cast <shared_ddedge*>(mtmxd_wrap->makeEdge(0));
  DCASSERT(xp);
  MEDDLY::dd_edge mask(x->E.getForest());
  mask.getForest()->createEdge(1, mask);
  MEDDLY::dd_edge noch(x->E.getForest());

  try {
    // IMPORTANT: iterate from bottom level up, this helps
    // to minimize the number of temporary nodes in the MDD
    for (int k=1; k<=part.num_levels; k++) if (e.nextstateDependsOnLevel(k)) {
      // check variables at this level
      for (int p=part.pointer[k-1]+1; p<=part.pointer[k]; p++) {
        const model_statevar* sv = part.variable[p];
        if (e.nextstateDependsOnVar(sv->GetIndex())) continue;
        // still here?
        // event e depends on level k, but not on state variable sv.
        mtmxd_wrap->buildSymbolicSV(sv, false, 0, x);
        mtmxd_wrap->buildSymbolicSV(sv, true, 0, xp);

        MEDDLY::apply(MEDDLY::EQUAL, x->E, xp->E, noch);

        mask *= noch;
      } // for p
    } // for k that e depends on

    // cleanup
    Delete(x);
    Delete(xp);
    x=0;
    xp=0;

    // copy mask into the right forest
    MEDDLY::dd_edge mask2(dd.getForest());
    MEDDLY::apply(MEDDLY::COPY, mask, mask2);

#ifdef DEBUG_NOCHANGE
    printf("mask2:\n");
    mask2.show(stdout, 2);
    printf("dd initially:\n");
    dd.show(stdout, 2);
#endif
    dd *= mask2;
#ifdef DEBUG_NOCHANGE
    printf("final dd:\n");
    dd.show(stdout, 2);
#endif

    return;
  } // try
  catch (MEDDLY::error e) {
    // cleanup, just in case
    Delete(x);
    Delete(xp);

    // An error occurred, report it...
    if (parent.StartError(0)) {
      parent.SendError("Meddly error ");
      parent.SendError(e.getName());
      parent.SendError(" in buildNoChange");
      parent.DoneError();
    }
    // ...and figure out which one
    if (MEDDLY::error::INSUFFICIENT_MEMORY == e.getCode()) {
      throw subengine::Out_Of_Memory;
    } else {
      throw subengine::Engine_Failed;
    }
  } // catch
}






void bounded_varoption::checkBounds(const exprman* em)
{
  //
  // Check that all state variables are bounded
  //
  if (!built_ok) return;
  for (int i=0; i<parent.getNumStateVars(); i++) {
    const model_statevar* sv = parent.readStateVar(i);
    if (sv->HasBounds()) continue;

    if (built_ok) {
      built_ok = false;
      if (!parent.StartError(0)) return;
      em->cerr() << "BOUNDED setting requires bounds for state variables.";
      em->newLine();
      em->cerr() << "The following state variables do not have declared bounds:";
      em->newLine();
      em->cerr() << sv->Name();
    } else {
      em->cerr() << ", " << sv->Name();
    }
  } // for i
  if (!built_ok) parent.DoneError();
}

int bounded_varoption::initDomain(const exprman* em)
{
  //
  // Build the domain
  //
  if (!built_ok) return 0;
  
  const hldsm::partinfo& part = parent.getPartInfo();
  minterm = new int[part.num_levels+1];
  minprim = new int[part.num_levels+1];
  minterm[0] = 1;
  minprim[0] = 1;
  int maxbound = 0;
  MEDDLY::variable** vars = new MEDDLY::variable*[part.num_levels+1];
  vars[0] = 0;
  for (int k=part.num_levels; k; k--) {
    int bnd = 1;
    for (int i=part.pointer[k]; i>part.pointer[k-1]; i--) {
      DCASSERT(part.variable[i]);
      int nv = part.variable[i]->NumPossibleValues();
      int newbnd = bnd * nv;
      if (newbnd < 1 || newbnd / nv != bnd) {
        if (parent.StartError(0)) {
          em->cerr() << "Overflow in size of level " << k << " for MDD";
          parent.DoneError();
        }
        built_ok = false;
        return 0;
      }
      bnd = newbnd;
    } // for p
    vars[k] = MEDDLY::createVariable(bnd, buildVarName(part, k));
    maxbound = MAX(bnd, maxbound);

  } // for k
  if (!ms.createVars(parent, vars, part.num_levels)) {
    built_ok = false;
    return 0;
  }
  DCASSERT(ms.vars);
  return maxbound;
}

void bounded_varoption::initEncoders(int maxbound, const meddly_procgen &pg)
{
  if (!built_ok) return;
  //
  // Initialize MDD forest
  //
  MEDDLY::forest* mdd = ms.vars->createForest(
    false, MEDDLY::forest::BOOLEAN, MEDDLY::forest::MULTI_TERMINAL, 
    pg.buildRSSPolicies()
  );
  DCASSERT(mdd);
  //
  // Initialize MxD forest
  //
  MEDDLY::forest* mxd = ms.vars->createForest(
    true, MEDDLY::forest::BOOLEAN, MEDDLY::forest::MULTI_TERMINAL,
    pg.buildNSFPolicies()
  );
  DCASSERT(mxd);
  //
  // Initialize MTMxD forest
  //
  MEDDLY::forest* mtmxd = ms.vars->createForest(
    true, MEDDLY::forest::INTEGER, MEDDLY::forest::MULTI_TERMINAL,
    pg.buildNSFPolicies()
  );
  DCASSERT(mtmxd);
  //
  // Build encoders
  //
  ms.mdd_wrap = new bounded_encoder("MDD", mdd, parent, maxbound);
  ms.mxd_wrap = new bounded_encoder("MxD", mxd, parent, maxbound);
  mtmxd_wrap =  new bounded_encoder("MTMxD", mtmxd, parent, maxbound);
}


// **************************************************************************
// *                                                                        *
// *                         substate_encoder class                         *
// *                                                                        *
// **************************************************************************

class substate_encoder : public meddly_encoder {
  substate_colls* colls;
  const hldsm &parent;
public:
  substate_encoder(const char* n, MEDDLY::forest* f, const hldsm &p, substate_colls* c);
protected:
  virtual ~substate_encoder();
public:
  virtual bool arePrimedVarsSeparate() const { return false; }
  virtual int getNumDDVars() const { return parent.getPartInfo().num_levels; }
  virtual void buildSymbolicSV(const symbol* sv, bool primed, 
                                expr *f, shared_object* answer);

  virtual void state2minterm(const shared_state* s, int* mt) const;
  virtual void minterm2state(const int* mt, shared_state *s) const;

  virtual meddly_encoder* copyWithDifferentForest(const char* n, MEDDLY::forest*) const;
};

// **************************************************************************
// *                                                                        *
// *                        substate_encoder methods                        *
// *                                                                        *
// **************************************************************************

substate_encoder
::substate_encoder(const char* n, MEDDLY::forest* f, const hldsm &p, substate_colls* c)
: meddly_encoder(n, f), parent(p)
{
  colls = c;
  DCASSERT(parent.hasPartInfo());
}

substate_encoder::~substate_encoder()
{
  Delete(colls);
}

void substate_encoder
::buildSymbolicSV(const symbol* sv, bool primed, expr* f, shared_object* answer)
{
  throw Failed;
}

void 
substate_encoder::state2minterm(const shared_state* s, int* mt) const
{
  DCASSERT(colls);
  if (0==s || 0==mt) throw Failed;

  for (int k=parent.getPartInfo().num_levels; k; k--) {
    int ssz = s->readSubstateSize(k);
    long sk = colls->addSubstate(k, s->readSubstate(k), ssz);
    if (sk<0) {
      if (-2 == sk) throw Out_Of_Memory;
      throw Failed;
    }
    mt[k] = sk;
    // check overflow
    if (sk != mt[k]) throw Failed;
    // TBD: Should we print an error for that one?
  } // for k
}

void
substate_encoder::minterm2state(const int* mt, shared_state *s) const
{
  DCASSERT(colls);
  if (0==s || 0==mt) throw Failed;

  for (int k=parent.getPartInfo().num_levels; k; k--) {
    int ssz = s->readSubstateSize(k);
    int foo = colls->getSubstate(k, mt[k], s->writeSubstate(k), ssz);
    if (foo<0) throw Failed;
  } // for k
}

meddly_encoder* 
substate_encoder::copyWithDifferentForest(const char* n, MEDDLY::forest* nf) const
{
  return new substate_encoder(n, nf, parent, Share(colls));
}

// **************************************************************************
// *                                                                        *
// *                        substate_varoption class                        *
// *                                                                        *
// **************************************************************************

// Abstract base class
class substate_varoption : public meddly_varoption {
protected:
  struct expr_node {
    expr* term;
    expr_node* next;
  };
  struct deplist {
    intset level_deps;
    expr_node* termlist;
    deplist* next;
    // mxd 
    MEDDLY::dd_edge* dd;
    // explicit storage of minterms
    int** mt_from;
    int** mt_to;
    int mt_alloc;
    int mt_used;
    int depth;
  public:
    deplist(const intset &x, expr_node* tl, deplist* n, int d);
    ~deplist();
    // returns true on success
    bool addMinterm(const int* from);
    // returns true on success
    bool addMinterm(const int* from, const int* to);
    // show minterms
    void showMinterms(OutputStream &s, int pad);
    void showMintermPairs(OutputStream &s, int pad);
  };
protected:
  deplist** enable_deps;
  deplist** fire_deps;
  substate_colls* colls;
  intset* explored;     // substates that are already explored
  intset* confirmed;    // substates that are confirmed (explored or not)
  int num_levels;
private:
  traverse_data td;
  shared_state* tdcurr;
  shared_state* tdnext;
  int* from_minterm;
  int* to_minterm;
public:
  substate_varoption(meddly_states &x, const dsde_hlm &p, 
    const exprman* em, const meddly_procgen &pg);
  virtual ~substate_varoption();
  virtual void initializeVars();
  virtual void initializeEvents(named_msg &d);
  virtual void reportStats(DisplayStream &out) const;
private:
  void initDomain(const exprman* em);
  void initEncoders(const meddly_procgen &pg);
  static deplist* getExprDeps(expr *x, int numlevels);
  static void clearList(deplist* &L);
  inline void enlarge(intset &set, int N) {
    int oldsz = set.getSize();
    if (oldsz >= N) return;
    int newsz = oldsz;
    while (newsz < N) newsz += 256;
    set.resetSize(newsz);
    set.removeRange(oldsz, newsz-1);
  }
protected:
  void exploreEnabling(deplist &dl, int k, bool has_unexp);
  void exploreNextstate(deplist &dl, int k, bool has_unexp);

  inline void exploreEnabling(deplist *dl) {
    DCASSERT(dl);
    return exploreEnabling(*dl, dl->level_deps.getSmallestAfter(0), false);
  }
  inline void exploreNextstate(deplist *dl) {
    DCASSERT(dl);
    return exploreNextstate(*dl, dl->level_deps.getSmallestAfter(0), false);
  }
#ifdef SHOW_SUBSTATES
  void show_substates(OutputStream &s);
#endif
};

// **************************************************************************
// *                                                                        *
// *                  substate_varoption::deplist  methods                  *
// *                                                                        *
// **************************************************************************

substate_varoption::deplist
::deplist(const intset &x, expr_node* tl, deplist* n, int d) : level_deps(x) 
{ 
  termlist = tl;
  dd = 0;
  next = n;
  mt_from = mt_to = 0;
  mt_alloc = mt_used = 0;
  depth = d;
}

substate_varoption::deplist::~deplist()
{
  for (int i=0; i<mt_used; i++) {
    delete[] mt_from[i];
    delete[] mt_to[i];
  }
  free(mt_from);
  free(mt_to);
}

bool substate_varoption::deplist::addMinterm(const int* from)
{
  if (mt_used >= mt_alloc) {
    // resize arrays...
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
  }
  if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
  mt_from[mt_used] = new int[depth];
  mt_to[mt_used] = new int[depth];
  memcpy(mt_from[mt_used], from, depth * sizeof(int));
  for (int i=depth; i>=0; i--) {
    mt_to[mt_used][i] = -1;
  }
  /*
  // Nice, but not portable:
  static int negone = -1;
  if (4==sizeof(int)) {
    memset_pattern4(mt_to, &negone, depth * sizeof(int));
  } else if (8==sizeof(int)) {
    memset_pattern8(mt_to, &negone, depth * sizeof(int));
  } else {
      DCASSERT(0);  // FAIL
  }
  */
  mt_used++;
  return true;
}

bool substate_varoption::deplist::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    // resize arrays...
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
  }
  if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
  mt_from[mt_used] = new int[depth];
  mt_to[mt_used] = new int[depth];
  memcpy(mt_from[mt_used], from, depth * sizeof(int));
  memcpy(mt_to[mt_used], to, depth * sizeof(int));
  mt_used++;
  return true;
}

void substate_varoption::deplist::showMinterms(OutputStream &s, int pad)
{
  for (int i=0; i<mt_used; i++) {
    s.Pad(' ', pad);
    s.Put('[');
    s.PutArray(mt_from[i]+1, depth-1);
    s << "]\n";
  }
}

void substate_varoption::deplist::showMintermPairs(OutputStream &s, int pad)
{
  for (int i=0; i<mt_used; i++) {
    s.Pad(' ', pad);
    s.Put('[');
    s.PutArray(mt_from[i]+1, depth-1);
    s << "], [";
    s.PutArray(mt_to[i]+1, depth-1);
    s << "]\n";
  }
}


// **************************************************************************
// *                                                                        *
// *                       substate_varoption methods                       *
// *                                                                        *
// **************************************************************************

substate_varoption
::substate_varoption(meddly_states &x, const dsde_hlm &p, 
  const exprman* em, const meddly_procgen &pg) 
: meddly_varoption(x, p), td(traverse_data::Compute)
{
  colls = 0;
  enable_deps = 0;
  fire_deps = 0;
  explored = 0;
  confirmed = 0;
  td.answer = new result;
  tdcurr = new shared_state(&p);
  tdnext = new shared_state(&p);
  td.current_state = tdcurr;
  td.next_state = tdnext;
  from_minterm = 0;
  to_minterm = 0;
  initDomain(em);
  initEncoders(pg);
}

substate_varoption::~substate_varoption()
{
  Delete(colls);
  delete[] explored;
  delete[] confirmed;
  delete[] from_minterm;
  delete[] to_minterm;
  Delete(tdcurr);
  Delete(tdnext);
  delete td.answer;
}

void substate_varoption::initializeVars()
{
  meddly_varoption::initializeVars();
  // confirm the initial substates
  for (int k=num_levels; k; k--) {
    int N = colls->getMaxIndex(k);
    enlarge(confirmed[k], N);
    confirmed[k].addRange(0, N-1);
  } // for k
}
  
void 
substate_varoption::initializeEvents(named_msg &d)
{
  if (d.startReport()) {
    d.report() << "preprocessing event expressions\n";
    d.stopIO();
  }
  enable_deps = new deplist*[parent.getNumEvents()];
  fire_deps = new deplist*[parent.getNumEvents()];
  for (int i=0; i<parent.getNumEvents(); i++) {
    const model_event* e = parent.readEvent(i);
    enable_deps[i] = getExprDeps(e->getEnabling(), num_levels);
    fire_deps[i] = getExprDeps(e->getNextstate(), num_levels);
    DCASSERT(enable_deps[i]);
    DCASSERT(enable_deps[i]->termlist);
    DCASSERT(fire_deps[i]);
    DCASSERT(fire_deps[i]->termlist);
  } // for i

  if (!d.startReport()) return;
  d.report() << "done preprocessing event expressions\n";

  for (int i=0; i<parent.getNumEvents(); i++) {

    d.report() << "\t" << parent.readEvent(i)->Name() << " enabling:\n";
    for (deplist *DL = enable_deps[i]; DL; DL=DL->next) {
      d.report() << "\t\tlevels ";
      int k = DL->level_deps.getSmallestAfter(0);
      for ( ; k>0; k=DL->level_deps.getSmallestAfter(k)) {
        d.report() << k << ", ";
      } // for k
      d.report() << "\n\t\t";
      for (expr_node* term = DL->termlist; term; term=term->next) {
        term->term->Print(d.report(), 0);
        d.report() << ", ";
      } // for term
      d.report() << "\n";
    } // for DL

    d.report() << "\t" << parent.readEvent(i)->Name() << " firing:\n";
    for (deplist *DL = fire_deps[i]; DL; DL=DL->next) {
      d.report() << "\t\tlevels ";
      int k = DL->level_deps.getSmallestAfter(0);
      for ( ; k>0; k=DL->level_deps.getSmallestAfter(k)) {
        d.report() << k << ", ";
      } // for k
      d.report() << "\n\t\t";
      for (expr_node* term = DL->termlist; term; term=term->next) {
        term->term->Print(d.report(), 0);
        d.report() << ", ";
      } // for term
      d.report() << "\n";
    } // for DL
  } // for i

  d.stopIO();
}

void substate_varoption::reportStats(DisplayStream &out) const
{
  meddly_varoption::reportStats(out);
  DCASSERT(colls);
  colls->Report(out);
#ifdef DUMP_MXD
  out << "NSF root in MXD: " << ms.nsf->E.getNode() << "\n";
  out << "MXD forest:\n";
  out.flush();
  ms.mxd_wrap->getForest()->showInfo(out.getDisplay(), 1);
  fflush(out.getDisplay());
#endif
}

void substate_varoption::initDomain(const exprman* em)
{
  //
  // Build the domain
  //
  if (!built_ok) return;
  
  const hldsm::partinfo& part = parent.getPartInfo();
  num_levels = part.num_levels;
  MEDDLY::variable** vars = new MEDDLY::variable*[num_levels+1];
  vars[0] = 0;
  for (int k=num_levels; k; k--) {
    vars[k] = MEDDLY::createVariable(1, buildVarName(part, k));
  } // for k
  if (!ms.createVars(parent, vars, num_levels)) {
    built_ok = false;
    return;
  }
  DCASSERT(ms.vars);
  
  from_minterm = new int[num_levels+1];
  to_minterm = new int[num_levels+1];
  from_minterm[0] = -1;
  to_minterm[0] = -2;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = -1;
    to_minterm[k] = -2;
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
  }

  //
  // Initialize substate collections
  //
  const exp_state_lib* esl = InitExplicitStateStorage(0);
  DCASSERT(esl);
  colls = esl->createSubstateDBs(num_levels, false);
  DCASSERT(colls);

  //
  // Keep track of which substates are explored, confirmed
  //
  explored = new intset[num_levels+1];
  confirmed = new intset[num_levels+1];
}

void substate_varoption::initEncoders(const meddly_procgen &pg)
{
  if (!built_ok) return;

  //
  // Initialize MDD forest
  //
  MEDDLY::forest* mdd = ms.vars->createForest(
    false, MEDDLY::forest::BOOLEAN, MEDDLY::forest::MULTI_TERMINAL, 
    pg.buildRSSPolicies()
  );
  DCASSERT(mdd);
  //
  // Initialize MxD forest
  //
  MEDDLY::forest* mxd = ms.vars->createForest(
    true, MEDDLY::forest::BOOLEAN, MEDDLY::forest::MULTI_TERMINAL, 
    pg.buildNSFPolicies()
  );
  DCASSERT(mxd);

  //
  // Build encoders
  //
  ms.mdd_wrap = new substate_encoder("MDD", mdd, parent, Share(colls));
  ms.mxd_wrap = new substate_encoder("MxD", mxd, parent, Share(colls));
}

substate_varoption::deplist*
substate_varoption::getExprDeps(expr* x, int numlevels)
{
  if (0==x) return 0;

  // first, build list of terms in product
  static List <expr> PL;
  PL.Clear();
  x->BuildExprList(traverse_data::GetProducts, 0, &PL);
  expr_node* termlist = 0;
  for (int i=0; i<PL.Length(); i++) {
    expr_node* link = new expr_node;
    link->term = PL.Item(i);
    link->next = termlist;
    termlist = link;
  } // for i

  // now, merge any with the same level dependencies 
  intset deps(numlevels+1);
  static List <symbol> SL;
  deplist* DL = 0;
  while (termlist) {
    expr_node* curr = termlist;
    termlist = curr->next;
    
    // get level dependencies
    deps.removeAll();
    SL.Clear();
    curr->term->BuildSymbolList(traverse_data::GetSymbols, 0, &SL);
    for (int i=0; i<SL.Length(); i++) {
      model_statevar* mv = dynamic_cast <model_statevar*> (SL.Item(i));
      if (0==mv) continue;
      CHECK_RANGE(1, mv->GetPart(), numlevels+1);
      deps.addElement(mv->GetPart());
    } // for i
    
    // check deplist for any with same dependencies
    deplist* find;
    for (find = DL; find; find=find->next) {
      if (find->level_deps == deps) break;
    } // for find

    if (find) {
      // add current term to this group
      curr->next = find->termlist;
      find->termlist = curr;
    } else {
      // no match; start a new group
      find = new deplist(deps, curr, DL, numlevels+1);
      curr->next = 0;
      DL = find;
    }

  } // while termlist

  return DL;
}

void
substate_varoption::clearList(deplist* &L)
{
  while (L) {
    while (L->termlist) {
      expr_node* tl = L->termlist;
      L->termlist = tl->next;
      delete tl;
    }
    deplist *tmp = L;
    L = L->next;
    delete tmp;
  }
  L=0;
}


void substate_varoption
::exploreEnabling(deplist &dl, int k, bool has_unexp)
{
  DCASSERT(k>0);
  int ssz = tdcurr->readSubstateSize(k);
  int next_k = dl.level_deps.getSmallestAfter(k);
  if (next_k > 0) {
    // Not the bottom level; recurse
    tdcurr->set_substate_known(k);
    for (int i = confirmed[k].getSmallestAfter(-1);
         i>=0;
         i = confirmed[k].getSmallestAfter(i)) 
    {
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;

      // 
      // option for short circuiting here...
      //
      
      from_minterm[k] = i;
      exploreEnabling(dl, next_k, has_unexp || !explored[k].contains(i));
    } // for i
    from_minterm[k] = -1;
    tdcurr->set_substate_unknown(k);
    return;
  } 

  // this is the "bottom level"
  tdcurr->set_substate_known(k);
  for (int i = confirmed[k].getSmallestAfter(-1);
       i>=0;
       i = confirmed[k].getSmallestAfter(i)) 
  {
    // skip if we've examined this (global) state already:
    if (!has_unexp && explored[k].contains(i)) continue;

    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;

    from_minterm[k] = i;

#ifdef DEBUG_EXPLORE_ENABLING
    DisplayStream dump(stderr);
    dump << "enabled?\n\tstate ";
    tdcurr->Print(dump, 0);
    dump << "\n\tminterm [";
    dump.PutArray(from_minterm+1, num_levels);
    dump << "]\n";
    dump.flush();
#endif
    
    bool is_enabled = true;
    for (expr_node* n = dl.termlist; n; n=n->next) {
#ifdef DEBUG_EXPLORE_ENABLING
      dump << "\t";
      n->term->Print(dump, 0);
      dump << " : ";
#endif
      n->term->Compute(td);
#ifdef DEBUG_EXPLORE_ENABLING
      if (td.answer->isNormal()) {
        if (td.answer->getBool())
          dump << "true";
        else
          dump << "false";
      } else if (td.answer->isUnknown())
        dump << "?";
      else
        dump << "null";
      dump << "\n";
      dump.flush();
#endif
      DCASSERT(td.answer->isNormal());
      if (td.answer->getBool()) continue;
      is_enabled = false;
      break;
    } // for n
    if (!is_enabled) continue;

    // enabled; add minterm to buffer
    if (!dl.addMinterm(from_minterm)) throw subengine::Out_Of_Memory;

  } // for i
  from_minterm[k] = -1;
  tdcurr->set_substate_unknown(k);
}

void substate_varoption
::exploreNextstate(deplist &dl, int k, bool has_unexp)
{
  DCASSERT(k>0);
  int ssz = tdcurr->readSubstateSize(k);
  int next_k = dl.level_deps.getSmallestAfter(k);
  if (next_k > 0) {
    // Not the bottom level; recurse
    tdcurr->set_substate_known(k);
    tdnext->set_substate_known(k);
    for (int i = confirmed[k].getSmallestAfter(-1);
         i>=0;
         i = confirmed[k].getSmallestAfter(i)) 
    {
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;
      colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

      // 
      // option for short circuiting here...
      //
      
      from_minterm[k] = i;
      exploreNextstate(dl, next_k, has_unexp || !explored[k].contains(i));
    } // for i
    from_minterm[k] = -1;
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
    return;
  } 

  // this is the "bottom level"
  tdcurr->set_substate_known(k);
  tdnext->set_substate_known(k);
  for (int i = confirmed[k].getSmallestAfter(-1);
       i>=0;
       i = confirmed[k].getSmallestAfter(i)) 
  {
    // skip if we've examined this (global) state already:
    if (!has_unexp && explored[k].contains(i)) continue;

    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;
    colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

    from_minterm[k] = i;

#ifdef DEBUG_EXPLORE_FIRING
    DisplayStream dump(stderr);
    dump << "firing?\n\tstate ";
    tdcurr->Print(dump, 0);
    dump << "\n\tminterm [";
    dump.PutArray(from_minterm+1, num_levels);
    dump << "]\n";
    dump.flush();
#endif
    
    bool is_enabled = true;
    for (expr_node* n = dl.termlist; n; n=n->next) {
#ifdef DEBUG_EXPLORE_FIRING
      dump << "\t";
      n->term->Print(dump, 0);
      dump << " : ";
#endif
      n->term->Compute(td);
#ifdef DEBUG_EXPLORE_FIRING
      if (td.answer->isNormal()) 
        dump << "ok";
      else if (td.answer->isUnknown())
        dump << "?";
      else if (td.answer->isOutOfBounds())
        dump << "out of bounds";
      else
        dump << "null";
      dump << "\n";
      dump.flush();
#endif
      if (td.answer->isNormal()) continue;
      is_enabled = false;
      break;
    } // for n
    if (!is_enabled) continue;

    // next state -> minterm
    //
    for (int kk = dl.level_deps.getSmallestAfter(0); 
        kk>=0; kk = dl.level_deps.getSmallestAfter(kk)) {

      to_minterm[kk] = colls->addSubstate(kk, 
          tdnext->readSubstate(kk), tdnext->readSubstateSize(kk)
      );
    
      if (to_minterm[kk]>=0) continue;
      if (-2==to_minterm[kk]) throw subengine::Out_Of_Memory;
      throw subengine::Engine_Failed;
    } // for kk
#ifdef DEBUG_EXPLORE_FIRING
    dump << "firing\n\tto state ";
    tdnext->Print(dump, 0);
    dump << "\n\tto minterm [";
    dump.PutArray(to_minterm+1, num_levels);
    dump << "]\n";
    dump.flush();
#endif

    // add minterm to queue
    //
    if (!dl.addMinterm(from_minterm, to_minterm)) 
      throw subengine::Out_Of_Memory;

    // clear out minterm
    //
    for (int kk = dl.level_deps.getSmallestAfter(0); 
        kk>=0; kk = dl.level_deps.getSmallestAfter(kk)) {

      to_minterm[kk] = -2;
    } // for kk

  } // for i
  from_minterm[k] = -1;
  tdcurr->set_substate_unknown(k);
  tdnext->set_substate_unknown(k);
  return;
}

#ifdef SHOW_SUBSTATES
void substate_varoption::show_substates(OutputStream &s)
{
  for (int k=num_levels; k>0; k--) {
    s << "Level " << k << " substates:\n";
    int ssz = tdcurr->readSubstateSize(k);
    tdcurr->set_substate_known(k);
    for (int i=0; i<colls->getMaxIndex(k); i++) {
      s << "\t";
      s.Put(long(i), 3);
      if (!confirmed[k].contains(i)) s << "u: "; 
      else if (explored[k].contains(i)) s << " : "; else s << "e: ";
      colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      tdcurr->Print(s, 0);
      s << "\n";
    } // for i
    tdcurr->set_substate_unknown(k);
  } // for k
}
#endif

// **************************************************************************
// *                                                                        *
// *                         pregen_varoption class                         *
// *                                                                        *
// **************************************************************************

class pregen_varoption : public substate_varoption {
public:
  pregen_varoption(meddly_states &x, const dsde_hlm &p, 
    const exprman* em, const meddly_procgen &pg);

  virtual void updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl);

  virtual bool hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl);
};

// **************************************************************************
// *                                                                        *
// *                        pregen_varoption methods                        *
// *                                                                        *
// **************************************************************************

pregen_varoption
::pregen_varoption(meddly_states &x, const dsde_hlm &p, 
  const exprman* em, const meddly_procgen &pg)
: substate_varoption(x, p, em, pg)
{
}

void 
pregen_varoption::updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl)
{
  MEDDLY::forest* f = nsf->getForest();
  DCASSERT(f);
  MEDDLY::dd_edge tmp(f);

  for (int i=0; i<parent.getNumEvents(); i++) {
    const model_event* e = parent.readEvent(i);
    if (d.startReport()) {
      d.report() << "updating event " << e->Name() << "\n";
      d.stopIO();
    }
    exploreEnabling(enable_deps[i]);
    exploreNextstate(fire_deps[i]);
#ifdef SHOW_MINTERMS
    if (d.startReport()) {
      d.report() << "event " << e->Name() << ":\n";
      d.report() << "\tenabling:\n";
      enable_deps[i]->showMinterms(d.report(), 12);
      d.report() << "\tfiring:\n";
      fire_deps[i]->showMintermPairs(d.report(), 12);
      d.stopIO();
    }
#endif
    // TBD: update the edge from the minterms

  } // for i

#ifdef SHOW_SUBSTATES
  if (d.startReport()) {
    d.report() << "Current substates:\n";
    show_substates(d.report());
    d.stopIO();
  }
#endif

  
  throw subengine::Engine_Failed;
}

bool pregen_varoption::hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl)
{
  return false;  
}


// **************************************************************************
// *                                                                        *
// *                        onthefly_varoption class                        *
// *                                                                        *
// **************************************************************************

class onthefly_varoption : public substate_varoption {
public:
  onthefly_varoption(meddly_states &x, const dsde_hlm &p, 
    const exprman* em, const meddly_procgen &pg);

  virtual void updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl);

  virtual bool hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl);
};

// **************************************************************************
// *                                                                        *
// *                       onthefly_varoption methods                       *
// *                                                                        *
// **************************************************************************

onthefly_varoption
::onthefly_varoption(meddly_states &x, const dsde_hlm &p, 
  const exprman* em, const meddly_procgen &pg) 
 : substate_varoption(x, p, em, pg)
{
}

void onthefly_varoption::updateEvents(named_msg &d, meddly_encoder* nsf, bool* cl)
{
  throw subengine::Engine_Failed;
}

bool onthefly_varoption::hasChangedLevels(const MEDDLY::dd_edge &s, bool* cl)
{
  return false;  // for now...
}


// **************************************************************************
// *                                                                        *
// *                         meddly_procgen methods                         *
// *                                                                        *
// **************************************************************************

int meddly_procgen::proc_storage;
int meddly_procgen::edge_style;
int meddly_procgen::var_type;
int meddly_procgen::nsf_ndp;
int meddly_procgen::rss_ndp;

meddly_procgen::meddly_procgen()
{
  // Anything?
}

meddly_procgen::~meddly_procgen()
{
}

meddly_varoption* 
meddly_procgen::makeBounded(const dsde_hlm &m, meddly_states &ms) const
{
  bounded_varoption *mvo = new bounded_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}


meddly_varoption*
meddly_procgen::makeExpanding(const dsde_hlm &m, meddly_states &ms) const
{
  // TBD
  return 0;
}

meddly_varoption*
meddly_procgen::makeOnTheFly(const dsde_hlm &m, meddly_states &ms) const
{
  onthefly_varoption *mvo = new onthefly_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}

meddly_varoption*
meddly_procgen::makePregen(const dsde_hlm &m, meddly_states &ms) const
{
  pregen_varoption *mvo = new pregen_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}

MEDDLY::forest::policies 
meddly_procgen::buildNSFPolicies() const
{
  MEDDLY::forest::policies p(true);
  switch (nsf_ndp) {
    case NEVER:
      p.setNeverDelete(); 
      break;

    case OPTIMISTIC:
      p.setOptimistic();
      break;

    case PESSIMISTIC:
      p.setPessimistic();
      break;
  } // switch

  return p;
}

MEDDLY::forest::policies 
meddly_procgen::buildRSSPolicies() const
{
  MEDDLY::forest::policies p(false);
  p.setQuasiReduced();
  switch (rss_ndp) {
    case NEVER:
      p.setNeverDelete(); 
      break;

    case OPTIMISTIC:
      p.setOptimistic();
      break;

    case PESSIMISTIC:
      p.setPessimistic();
      break;
  } // switch

  return p;
}


// **************************************************************************
// *                                                                        *
// *                               Front  end                               *
// *                                                                        *
// **************************************************************************

radio_button** makeNDPButtons(int &num_buttons)
{
  num_buttons = 3;
  radio_button** ndps = new radio_button*[num_buttons];
  ndps[meddly_procgen::NEVER] = new radio_button(
    "NEVER",
    "Never delete nodes; useful for debugging",
    meddly_procgen::NEVER
  );
  ndps[meddly_procgen::OPTIMISTIC] = new radio_button(
    "OPTIMISTIC",
    "Nodes are marked for deletion and are recycled once they do not appear in the compute table; useful when nodes are likely to be re-used",
    meddly_procgen::OPTIMISTIC
  );
  ndps[meddly_procgen::PESSIMISTIC] = new radio_button(
    "PESSIMISTIC",
    "Nodes are recycled as soon as possible; useful when nodes are unlikely to be re-used, or to reduce memory",
    meddly_procgen::PESSIMISTIC
  );
  return ndps;
}



void InitializeProcGenMeddly(exprman* em)
{
  if (0==em) return;

  engtype* rsgen = MakeEngineType(em,
    "MeddlyProcessGeneration",
    "The algorithm to use to generate the underlying process, with Meddly",
    engtype::Model
  );
  engine* meddlygen = MakeRedirectionEngine(
    "MEDDLY",
    "Generate and store the underlying process using MDDs with Meddly; for details see option MeddlyProcessGeneration",
    rsgen
  );
  RegisterEngine(em, "ProcessGeneration", meddlygen);

  // EVMxD vs MTMxD option
  meddly_procgen::proc_storage = meddly_procgen::MTMXD;
  radio_button** styles = new radio_button*[2];
  styles[meddly_procgen::MTMXD] = new radio_button(
    "MTMXD",
    "Multi-terminal MDD for matrices",
    meddly_procgen::MTMXD
  );
  styles[meddly_procgen::EVMXD] = new radio_button(
    "EVMXD",
    "Edge-valued (using *) MDD for matrices",
    meddly_procgen::EVMXD
  );
  em->addOption(
    MakeRadioOption(
      "MeddlyProcessStorage",
      "Type of forest to use for process storage, using Meddly",
      styles, 2, meddly_procgen::proc_storage
    )
  );

  // potential vs. actual edge option
  meddly_procgen::edge_style = meddly_procgen::POTENTIAL;
  styles = new radio_button*[2];
  styles[meddly_procgen::ACTUAL] = new radio_button(
    "ACTUAL",
    "Outgoing edges only for reachable states; unreachable states have no outgoing edges",
    meddly_procgen::ACTUAL
  );
  styles[meddly_procgen::POTENTIAL] = new radio_button(
    "POTENTIAL",
    "Unreachable states may have outgoing edges",
    meddly_procgen::POTENTIAL
  );
  em->addOption(
    MakeRadioOption(
      "MeddlyProcessEdgeStyle",
      "Style for representing the underlying process, using Meddly",
      styles, 2, meddly_procgen::edge_style
    )
  );

  // variable type option
  styles = new radio_button*[4];
  styles[meddly_procgen::BOUNDED] = new radio_button(
    "BOUNDED", 
    "All state variables have declared bounds",
    meddly_procgen::BOUNDED
  );
  styles[meddly_procgen::EXPANDING] = new radio_button(
    "EXPANDING", 
    "State variable bounds are discovered during generation",
    meddly_procgen::EXPANDING
  );
  styles[meddly_procgen::ON_THE_FLY] = new radio_button(
    "ON_THE_FLY", 
    "Local state spaces are discovered during generation",
    meddly_procgen::ON_THE_FLY
  );
  styles[meddly_procgen::PREGEN] = new radio_button(
    "PREGEN", 
    "Local state spaces are generated in before generating reachability set",
    meddly_procgen::PREGEN
  );
  meddly_procgen::var_type = meddly_procgen::BOUNDED;  
  em->addOption(
    MakeRadioOption(
      "MeddlyVariableStyle",
      "Method for determining sizes of state variables, for Meddly-based process generation",
      styles, 4, meddly_procgen::var_type
    )
  );

  // Node deletion policy options
  radio_button** ndps;
  int num_ndps;
  ndps = makeNDPButtons(num_ndps);
  meddly_procgen::nsf_ndp = meddly_procgen::PESSIMISTIC;
  em->addOption(
    MakeRadioOption(
      "MeddlyNSFNodeDeletion",
      "Node deletion policy to use for next-state function forests in Meddly",
      ndps, num_ndps, meddly_procgen::nsf_ndp
    )
  );

  ndps = makeNDPButtons(num_ndps);
  meddly_procgen::rss_ndp = meddly_procgen::PESSIMISTIC;
  em->addOption(
    MakeRadioOption(
      "MeddlyRSSNodeDeletion",
      "Node deletion policy to use for reachable state space forests in Meddly",
      ndps, num_ndps, meddly_procgen::rss_ndp
    )
  );

  meddly_varoption::vars_named = false;
  em->addOption(
    MakeBoolOption(
      "MeddlyVarsAreNamed",
      "Should the variables internal to MEDDLY be named appropriately.  If true, when MDDs and MxDs are displayed (typically for debugging), more useful information is shown for the level names.  Should be set to false for speed if possible.",
      meddly_varoption::vars_named
    )
  );

}
