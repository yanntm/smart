
#include "gen_meddly.h"

#include "../Options/options.h"
#include "../Options/optman.h"

#include "../ExprLib/startup.h"
#include "../Modules/glue_meddly.h"
#include "../Modules/expl_states.h"
#include "../Formlsms/dsde_hlm.h"
#include "../Formlsms/rss_meddly.h"
#include<map>
#include<vector>

// #define DUMP_MXD
// #define DUMP_MTMXD
// #define DEBUG_BUILD_SV
// #define DEBUG_SHAREDEDGE
// #define DEBUG_ENCODE
// #define DEBUG_EVENT_CONSTR
// #define DEBUG_EVENT_OVERALL
// #define DEBUG_NOCHANGE

// #define SHOW_SUBSTATES
// #define SHOW_MINTERMS

// #define SHORT_CIRCUIT_ENABLING

#define USING_MEDDLY_ADD_MINTERM
#define USE_FRMDD_FOR_BUILDING_POTENTIAL_DEADLOCK_STATES
// #define TEST_HYB

using namespace MEDDLY;

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

#ifdef USING_OLD_MEDDLY_STUFF

bool meddly_varoption::vars_named;

meddly_varoption::meddly_varoption(meddly_reachset &x, const dsde_hlm &p)
: parent(p), ms(x)
{
  mxd_wrap = 0;
  built_ok = true;
  event_enabling = new dd_edge*[parent.getNumEvents()];
  event_firing = new dd_edge*[parent.getNumEvents()];
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
  Delete(mxd_wrap);
}

void meddly_varoption::initializeVars()
{
  // We're good, just need to set the initial states

  // allocate what we need
  shared_state* st = new shared_state(&parent);
  int num_init = parent.NumInitialStates();
  int** mt = new int* [num_init];
#ifdef DEBUG_SHAREDEDGE
  fprintf(stderr, "initial: ");
#endif

  // Convert initial states to minterms
  for (int n=0; n<num_init; n++) {
    mt[n] = new int[ms.getNumLevels()];
    parent.GetInitialState(n, st);
    ms.MddState2Minterm(st, mt[n]);
  } // for n

  // Build DD from minterms
  shared_ddedge* initial = ms.newMddEdge();
  DCASSERT(initial);
  try {
    ms.createMinterms(mt, num_init, initial);
    ms.setInitial(initial);

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
  if (mxd_wrap) mxd_wrap->reportStats(out);
}

satotf_opname::otf_relation* meddly_varoption::buildNSF_OTF(debugging_msg &debug)
{
  return 0;
}

satimpl_opname::implicit_relation* meddly_varoption::buildNSF_IMPLICIT(debugging_msg &debug)
{
  return 0;
}

sathyb_opname::hybrid_relation* meddly_varoption::buildNSF_HYBRID(debugging_msg &debug)
{
  return 0;
}

MEDDLY::dd_edge meddly_varoption::buildPotentialDeadlockStates_IMPLICIT(debugging_msg &debug)
{
  MEDDLY::dd_edge result(getMddForest());
  return result;
}

// **************************************************************************
// *                                                                        *
// *                         bounded_encoder  class                         *
// *                                                                        *
// **************************************************************************

class bounded_encoder : public meddly_encoder {
  long* terms;
  int maxbound;
  long lastcomputed;
  traverse_data tdx;
  result ans;
  const hldsm &parent;
  shared_state* expl_state;
public:
  bounded_encoder(const char* n, forest* f, const hldsm &p, int max_var_size);
protected:
  virtual ~bounded_encoder();
public:
  virtual bool arePrimedVarsSeparate() const { return false; }
  virtual int getNumDDVars() const { return parent.getPartInfo().num_levels; }
  virtual void buildSymbolicSV(const symbol* sv, bool primed,
                                expr *f, shared_object* answer);

  virtual void state2minterm(const shared_state* s, int* mt) const;
  virtual void minterm2state(const int* mt, shared_state *s) const;

  virtual meddly_encoder* copyWithDifferentForest(const char*, forest*) const;
protected:
  void FillTerms(const model_statevar* sv, int p, int &i, expr* f);
};

// **************************************************************************
// *                                                                        *
// *                        bounded_encoder  methods                        *
// *                                                                        *
// **************************************************************************

bounded_encoder
::bounded_encoder(const char* n, forest* f, const hldsm &p, int max_var_size)
: meddly_encoder(n, f), tdx(traverse_data::Compute), parent(p)
{
  DCASSERT(parent.hasPartInfo());
  maxbound = MAX(parent.getPartInfo().num_levels+1, max_var_size);
  terms = new long[maxbound];
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
bounded_encoder::copyWithDifferentForest(const char* n, forest* nf) const
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
protected:
  meddly_encoder* mtmxd_wrap;
public:
  bounded_varoption(meddly_reachset &x, const dsde_hlm &p, const exprman* em,
    const meddly_procgen &pg);

  virtual ~bounded_varoption();

  virtual void initializeEvents(debugging_msg &d);

  virtual void updateEvents(debugging_msg &d, bool* cl);

  virtual bool hasChangedLevels(const dd_edge &s, bool* cl);

  virtual void reportStats(DisplayStream &out) const;

protected: // in the following, dd is an mxd edge.
  void encodeExpr(expr* e, dd_edge &dd, const char *what, const char* who);

  void buildNoChange(const model_event &e, dd_edge &dd);

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
::bounded_varoption(meddly_reachset &x, const dsde_hlm &p,
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

void bounded_varoption::initializeEvents(debugging_msg &d)
{
  // Nothing to do?
}

void bounded_varoption::updateEvents(debugging_msg &d, bool* cl)
{
  DCASSERT(built_ok);
  DCASSERT(event_enabling);
  DCASSERT(event_firing);
  forest* f = get_mxd_forest();
  DCASSERT(f);
  dd_edge mask(f);

  // For now, don't bother checking cl; just build everything

  for (int i=0; i<getParent().getNumEvents(); i++) {
    const model_event* e = getParent().readEvent(i);
    DCASSERT(e);

    //
    // Build enabling for this event
    //

    if (d.startReport()) {
      d.report() << "Building enabling   DD for event ";
      d.report() << e->Name() << "\n";
      d.stopIO();
    }

    if (0==event_enabling[i]) event_enabling[i] = new dd_edge(f);
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

    if (0==event_firing[i])   event_firing[i]   = new dd_edge(f);
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

bool bounded_varoption::hasChangedLevels(const dd_edge &s, bool* cl)
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
::encodeExpr(expr* e, dd_edge &dd, const char* what, const char* who)
{
  DCASSERT(mtmxd_wrap);
  if (0==e) {
    forest* f = dd.getForest();
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
    if (getParent().StartError(0)) {
      getParent().SendError("Got null result for ");
      getParent().SendError(what);
      getParent().SendError(who);
      getParent().DoneError();
    }
    throw subengine::Engine_Failed;
  }
  shared_ddedge* me = smart_cast <shared_ddedge*>(foo.getPtr());
  DCASSERT(me);

#ifdef DEBUG_ENCODE
  getParent().getEM()->cout() << "MTMXD encoding for expr: ";
  e->Print(getParent().getEM()->cout(), 0);
  getParent().getEM()->cout() << " is node " << me->E.getNode() << "\n";
  getParent().getEM()->cout().flush();
  me->E.show(stdout, 2);
#endif

  // now, copy it into the MXD
  try {
    apply(COPY, me->E, dd);
    return;
  }
  catch (error ce) {
    // An error occurred, report it...
    if (getParent().StartError(0)) {
      getParent().SendError("Meddly error ");
      getParent().SendError(ce.getName());
      getParent().SendError(" for ");
      getParent().SendError(what);
      getParent().SendError(who);
      getParent().DoneError();
    }

    // ...and figure out which one
    if (error::INSUFFICIENT_MEMORY == ce.getCode())
      throw subengine::Out_Of_Memory;

    throw subengine::Engine_Failed;
  }
}

void bounded_varoption::buildNoChange(const model_event &e, dd_edge &dd)
{
  DCASSERT(minterm);
  DCASSERT(mtmxd_wrap);
  const hldsm::partinfo &part = getParent().getPartInfo();

  // "don't change" levels
  for (int k=1; k<=part.num_levels; k++) {
    minterm[k] = DONT_CARE;
    minprim[k] = (e.nextstateDependsOnLevel(k))
        ? DONT_CARE
        : DONT_CHANGE;
  }
  DCASSERT(dd.getForest());
  try {
    dd.getForest()->createEdge(&minterm, &minprim, 1, dd);
  }
  catch (error fe) {
    if (error::INSUFFICIENT_MEMORY == fe.getCode())
      throw subengine::Out_Of_Memory;
    throw subengine::Engine_Failed;
  }

  // Now, "and" in the "don't change" variables
  // that are associated with "do change" levels, if any

  shared_ddedge* x = smart_cast <shared_ddedge*>(mtmxd_wrap->makeEdge(0));
  DCASSERT(x);
  shared_ddedge* xp = smart_cast <shared_ddedge*>(mtmxd_wrap->makeEdge(0));
  DCASSERT(xp);
  dd_edge mask(x->E.getForest());
  mask.getForest()->createEdge(long(1), mask);
  dd_edge noch(x->E.getForest());

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

        apply(EQUAL, x->E, xp->E, noch);

        mask *= noch;
      } // for p
    } // for k that e depends on

    // cleanup
    Delete(x);
    Delete(xp);
    x=0;
    xp=0;

    // copy mask into the right forest
    dd_edge mask2(dd.getForest());
    apply(COPY, mask, mask2);

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
  catch (error e) {
    // cleanup, just in case
    Delete(x);
    Delete(xp);

    // An error occurred, report it...
    if (getParent().StartError(0)) {
      getParent().SendError("Meddly error ");
      getParent().SendError(e.getName());
      getParent().SendError(" in buildNoChange");
      getParent().DoneError();
    }
    // ...and figure out which one
    if (error::INSUFFICIENT_MEMORY == e.getCode()) {
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
  for (int i=0; i<getParent().getNumStateVars(); i++) {
    const model_statevar* sv = getParent().readStateVar(i);
    if (sv->HasBounds()) continue;

    if (built_ok) {
      built_ok = false;
      if (!getParent().StartError(0)) return;
      em->cerr() << "BOUNDED setting requires bounds for state variables.";
      em->newLine();
      em->cerr() << "The following state variables do not have declared bounds:";
      em->newLine();
      em->cerr() << sv->Name();
    } else {
      em->cerr() << ", " << sv->Name();
    }
  } // for i
  if (!built_ok) getParent().DoneError();
}

int bounded_varoption::initDomain(const exprman* em)
{
  //
  // Build the domain
  //
  if (!built_ok) return 0;

  const hldsm::partinfo& part = getParent().getPartInfo();
  minterm = new int[part.num_levels+1];
  minprim = new int[part.num_levels+1];
  minterm[0] = 1;
  minprim[0] = 1;
  int maxbound = 0;
  variable** vars = new variable*[part.num_levels+1];
  vars[0] = 0;
  for (int k=part.num_levels; k; k--) {
    int bnd = 1;
    for (int i=part.pointer[k]; i>part.pointer[k-1]; i--) {
      DCASSERT(part.variable[i]);
      int nv = part.variable[i]->NumPossibleValues();
      int newbnd = bnd * nv;
      if (newbnd < 1 || newbnd / nv != bnd) {
        if (getParent().StartError(0)) {
          em->cerr() << "Overflow in size of level " << k << " for MDD";
          getParent().DoneError();
        }
        built_ok = false;
        return 0;
      }
      bnd = newbnd;
    } // for p
    vars[k] = createVariable(bnd, buildVarName(part, k));
    maxbound = MAX(bnd, maxbound);

  } // for k
  if (!ms.createVars(vars, part.num_levels)) {
    built_ok = false;
    return 0;
  }
  return maxbound;
}

void bounded_varoption::initEncoders(int maxbound, const meddly_procgen &pg)
{
  if (!built_ok) return;
  //
  // Initialize MDD forest
  //
  forest* mdd = ms.createForest(
    false, forest::BOOLEAN, forest::MULTI_TERMINAL,
    pg.buildRSSPolicies()
  );
  DCASSERT(mdd);
  //
  // Initialize MxD forest
  //
  forest* mxd = ms.createForest(
    true, forest::BOOLEAN, forest::MULTI_TERMINAL,
    pg.buildNSFPolicies()
  );
  DCASSERT(mxd);
  //
  // Initialize MTMxD forest
  //
  forest* mtmxd = ms.createForest(
    true, forest::INTEGER, forest::MULTI_TERMINAL,
    pg.buildNSFPolicies()
  );
  DCASSERT(mtmxd);
  //
  // Build encoders
  //
  ms.setMddWrap(new bounded_encoder("MDD", mdd, getParent(), maxbound));
  set_mxd_wrap(new bounded_encoder("MxD", mxd, getParent(), maxbound));
  mtmxd_wrap =  new bounded_encoder("MTMxD", mtmxd, getParent(), maxbound);
}


// **************************************************************************
// *                                                                        *
// *                         substate_encoder class                         *
// *                                                                        *
// **************************************************************************

class substate_encoder : public meddly_encoder {
  substate_colls* colls;
  const hldsm &parent;

  long* terms;
  int maxbound;
  // long lastcomputed;
  traverse_data tdx;
  result ans;
  shared_state* expl_state;

public:
  substate_encoder(const char* n, forest* f, const hldsm &p, substate_colls* c);
protected:
  virtual ~substate_encoder();
public:
  virtual bool arePrimedVarsSeparate() const { return false; }
  virtual int getNumDDVars() const { return parent.getPartInfo().num_levels; }
  virtual void buildSymbolicSV(const symbol* sv, bool primed, expr *f, shared_object* answer);

  virtual void state2minterm(const shared_state* s, int* mt) const;
  virtual void minterm2state(const int* mt, shared_state *s) const;

  virtual meddly_encoder* copyWithDifferentForest(const char* n, forest*) const;

protected:
  void FillTerms(const model_statevar* sv, expr* f);
};

// **************************************************************************
// *                                                                        *
// *                        substate_encoder methods                        *
// *                                                                        *
// **************************************************************************

substate_encoder
::substate_encoder(const char* n, forest* f, const hldsm &p, substate_colls* c)
: meddly_encoder(n, f), parent(p), tdx(traverse_data::Compute)
{
  colls = c;
  DCASSERT(parent.hasPartInfo());

  maxbound = MAX(parent.getPartInfo().num_levels+1, 10000);
  terms = new long[maxbound];
  expl_state = new shared_state(&p);
  tdx.answer = &ans;
  tdx.current_state = expl_state;
}

substate_encoder::~substate_encoder()
{
  Delete(colls);
  delete[] terms;
  Delete(expl_state);
}

void substate_encoder
::buildSymbolicSV(const symbol* sv, bool primed, expr* f, shared_object* answer)
{
  //
  // TODO: The behavior can be undefined when the nodes are extensible
  //

  if (0==sv) throw Failed;
  shared_ddedge* dd = dynamic_cast<shared_ddedge*> (answer);
  if (0==dd) throw Invalid_Edge;
#ifdef DEVELOPMENT_CODE
  if (dd->numRefs()>1) throw Shared_Output_Edge;
#endif
  DCASSERT(dd);

  const model_statevar* mv = dynamic_cast<const model_statevar*> (sv);
  DCASSERT(mv);
  FillTerms(mv, f);

  try {
    int level = mv->GetPart();
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


void
substate_encoder::state2minterm(const shared_state* s, int* mt) const
{
  DCASSERT(colls);
  if (0==s || 0==mt) throw Failed;
  for (int k=parent.getPartInfo().num_levels; k; k--) {
    int ssz = s->readSubstateSize(k);
    long sk = colls->addSubstate(k, s->readSubstate(k), ssz);//
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
substate_encoder::copyWithDifferentForest(const char* n, forest* nf) const
{
  return new substate_encoder(n, nf, parent, Share(colls));
}

void substate_encoder::FillTerms(const model_statevar* sv, expr* f)
{
  int level = sv->GetPart();
  CHECK_RANGE(1, level, 1+parent.getPartInfo().num_levels);

  DCASSERT(parent.getPartInfo().pointer[0] == -1);
  // Number of state variables merged into the variable at level
  int num_sv = parent.getPartInfo().pointer[level] - parent.getPartInfo().pointer[level - 1];
  int* minterm_sv = new int[num_sv];
  // Index of state variable sv in minterm_sv
  int index_sv = 0;
  while (parent.getPartInfo().variable[parent.getPartInfo().pointer[level] - index_sv] != sv) {
    index_sv++;
  }

  // The bound of variable at level
  long bound = colls->getMaxIndex(level);
  DCASSERT(bound <= maxbound);
  for (long i = 0; i < bound; i++) {
    colls->getSubstate(level, i, minterm_sv, num_sv);
    if (f) {
      sv->GetValueNumber(minterm_sv[index_sv], ans);
      sv->SetNextState(tdx, expl_state, ans.getInt());
      f->Compute(tdx);
    } else {
      sv->GetValueNumber(minterm_sv[index_sv], ans);
    }
    terms[i] = ans.getInt();
  }

  delete[] minterm_sv;
}

// **************************************************************************
// *                                                                        *
// *                    enabling_subevent class for hybrid                  *
// *                                                                        *
// **************************************************************************

// TBD - move this somewhere better
//
class enabling_subeventI : public sathyb_opname::subevent {
public:
  // TBD - clean up this constructor!
  enabling_subeventI(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls *c, intset event_deps, expr* chunk, forest* f, int* v, int nv);
  virtual ~enabling_subeventI();

protected:
  virtual void confirm(sathyb_opname::hybrid_relation &rel, int v, int index);

private: // helpers
#ifndef USING_MEDDLY_ADD_MINTERM
  // returns true on success
  bool addMinterm(const int* from, const int* to);
#endif

  void exploreEnabling(sathyb_opname::hybrid_relation &rel, int dpth);


  inline bool maybeEnabled() {
#ifdef SHORT_CIRCUIT_ENABLING
    DCASSERT(is_enabled);
    is_enabled->Compute(td);
    if (td.answer->isNormal()) {
      return td.answer->getBool();
    } else if (td.answer->isUnknown()) {
      return true;
    } else {
      // null?  not sure what to do here
      return false;
    }
#else
    return true;
#endif
  }

private:
  expr* is_enabled;
  int** mt_from;
  int** mt_to;
  int mt_alloc;
  int mt_used;
  int num_levels;
  // some of this stuff could be shared, one per model,
  // at the cost of re-initializing stuff every time we need to explore
  // TBD - option to select between "optimize for speed" and "optimize for memory"
  // (two different classes?  or make this a template <bool> class?)
  traverse_data td;
  shared_state* tdcurr;
  shared_state* tdnext;
  int* from_minterm;
  int* to_minterm;

  // used by exploreFiring
  int changed_k;
  int new_index;

  substate_colls* colls;

  // used only for debug info?
  const model_event* E;

  // TBD - this should be static or accessed via a parent class
  debugging_msg &debug;
};

// **************************************************************************
// *                        enabling_subeventI  methods                        *
// **************************************************************************

enabling_subeventI::enabling_subeventI(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls* c, intset event_deps, expr* chunk, forest* f, int* v, int nv)
: sathyb_opname::subevent(f, v, nv, false), td(traverse_data::Compute), debug(d)
{
  E = Ev;
  is_enabled = chunk;
  colls = c;

  mt_from = 0;
  mt_to = 0;
  mt_alloc = mt_used = 0;
  num_levels = p.getPartInfo().num_levels;

  td.answer = new result;
  tdcurr = new shared_state(&p);
  tdnext = new shared_state(&p);
  td.current_state = tdcurr;
  td.next_state = tdnext;
  from_minterm = new int[1+num_levels];
  to_minterm = new int[1+num_levels];

  from_minterm[0] = DONT_CARE;
  to_minterm[0] = DONT_CARE;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = DONT_CARE;
    if (event_deps.contains(k)) {
      to_minterm[k] = DONT_CARE;
    } else {
      to_minterm[k] = DONT_CARE;
    }
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
  }
}

enabling_subeventI::~enabling_subeventI()
{
  Delete(is_enabled);
  for (int i=0; i<mt_used; i++) {
    delete[] mt_from[i];
    delete[] mt_to[i];
  }
  free(mt_from);
  free(mt_to);

  delete td.answer;
  Delete(tdcurr);
  Delete(tdnext);
  delete[] from_minterm;
  delete[] to_minterm;
}

void enabling_subeventI::confirm(sathyb_opname::hybrid_relation &rel, int k, int index)
{
  DCASSERT(E);
  if (debug.startReport()) {
    debug.report() << "confirming level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    is_enabled->Print(debug.report(), 0);
    debug.report() << "\n";
    debug.stopIO();
  }

  changed_k = k;
  new_index = index;

  // Explicitly explore new states and discover minterms to add
  exploreEnabling(rel, getNumVars());

#ifndef USING_MEDDLY_ADD_MINTERM
  if (0==mt_used) return;

  if (debug.startReport()) {
    for (int i = 0; i < mt_used; i++) {
      debug.report() << "\nFiring:\n";
      debug.report() << "\nfrom[" << i << "]: [";
      debug.report().PutArray(mt_from[i]+1, num_levels);
      debug.report() << "]\nto[" << i << "]: [";
      debug.report().PutArray(mt_to[i]+1, num_levels);
      debug.report() << "]\n";
      debug.report() << "\n\n";
    }
    debug.stopIO();
  }

  // Add those minterms
  dd_edge add_to_root(getForest());
  getForest()->createEdge(mt_from, mt_to, mt_used, add_to_root);
  mt_used = 0;

  add_to_root += getRoot();
  setRoot(add_to_root);

  if (debug.startReport()) {
    debug.report() << "confirmed  level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    is_enabled->Print(debug.report(), 0);
    debug.newLine();
    debug.report() << "New root: " << add_to_root.getNode() << "\n";
    debug.stopIO();
  }
#endif
}

#ifndef USING_MEDDLY_ADD_MINTERM
bool enabling_subeventI::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    int old_alloc = mt_alloc;
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
    if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
    for (int i=old_alloc; i<mt_alloc; i++) {
      mt_from[i] = 0;
      mt_to[i] = 0;
    }
  }
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[1+num_levels];
  }
  if (0==mt_to[mt_used]) {
    mt_to[mt_used] = new int[1+num_levels];
  }
  memcpy(mt_from[mt_used], from, (1+num_levels) * sizeof(int));
  memcpy(mt_to[mt_used], to, (1+num_levels) * sizeof(int));
  mt_used++;
  return true;
}
#endif

void enabling_subeventI::exploreEnabling(sathyb_opname::hybrid_relation &rel, int dpth)
{
  //
  // Are we at the bottom?
  //
  if (0==dpth) {
    bool start_d = debug.startReport();
    if (start_d) {
      debug.report() << "enabled?\n\tstate ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tfrom minterm [";
      debug.report().PutArray(from_minterm+1, num_levels);
      debug.report() << "]\n\t";
      is_enabled->Print(debug.report(), 0);
      debug.report() << " : ";
    }
#ifdef SHORT_CIRCUIT_ENABLING
    td.answer->setBool(true);
#else
    DCASSERT(is_enabled);
    is_enabled->Compute(td);
#endif
    if (start_d) {
      if (td.answer->isNormal()) {
        if (td.answer->getBool())
          debug.report() << "true";
        else
          debug.report() << "false";
      } else if (td.answer->isUnknown())
        debug.report() << "?";
      else
        debug.report() << "null";
      debug.report() << "\n";
      debug.stopIO();
    }

    DCASSERT(td.answer->isNormal());
    if (false == td.answer->getBool()) return;

    //
    // next state -> minterm
    //
    /*
     // ASM - this should already be set!

     for (int dd=0; dd<getNumVars(); dd++) {
     int kk = getVars()[dd];
     to_minterm[kk] = DONT_CARE;
     }
     */

    //
    // More debug info
    //
    if (debug.startReport()) {
      debug.report() << "enabled\n\tstate ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tto minterm [";
      debug.report().PutArray(to_minterm+1, num_levels);
      debug.report() << "]\n";
      debug.stopIO();
    }

    addMinterm(from_minterm, to_minterm);

    return;
  }

  //
  // Are we at the level being confirmed?
  //
  const int k = getVars()[dpth-1];
  if (k == changed_k) {
    tdcurr->set_substate_known(changed_k);
    int ssz = tdcurr->readSubstateSize(changed_k);
    int foo = colls->getSubstate(changed_k, new_index, tdcurr->writeSubstate(changed_k), ssz);
    if (foo<0) throw subengine::Engine_Failed;

    //
    // check expression and short-circuit if definitely false
    // tbd - what about definitely true?
    //

    if (maybeEnabled()) {
      from_minterm[changed_k] = new_index;
      exploreEnabling(rel, dpth-1);
    }

    from_minterm[changed_k] = DONT_CARE;
    tdcurr->set_substate_unknown(changed_k);
    return;
  }

  //
  // Regular level - explore all confirmed
  //
  tdcurr->set_substate_known(k);
  const int localsize = rel.getHybridForest()->getLevelSize(k);
  for (int i = 0; i<localsize; i++) {
    if (!rel.isConfirmedState(k, i)) continue; // skip unconfirmed

    int ssz = tdcurr->readSubstateSize(k);
    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;

    //
    // check expression and short-circuit if definitely false
    //

    if (maybeEnabled()) {
      from_minterm[k] = i;
      exploreEnabling(rel, dpth-1);
    }
  } // for i
  from_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
}

// **************************************************************************
// *                                                                        *
// *                    firing_subevent class for hybrid                    *
// *                                                                        *
// **************************************************************************
class firing_subeventI : public sathyb_opname::subevent {
public:
  // TBD - clean up this constructor!
  firing_subeventI(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls *c, intset event_deps, expr* chunk, forest* f, int* v, int nv);
  virtual ~firing_subeventI();

protected:
  virtual void confirm(sathyb_opname::hybrid_relation &rel, int v, int index);

private: // helpers
#ifndef USING_MEDDLY_ADD_MINTERM
  // returns true on success
  bool addMinterm(const int* from, const int* to);
#endif

  void exploreFiring(sathyb_opname::hybrid_relation &rel, int dpth);

private:
  expr* fire_expr;
  int** mt_from;
  int** mt_to;
  int mt_alloc;
  int mt_used;
  int num_levels;
  // some of this stuff could be shared, one per model,
  // at the cost of re-initializing stuff every time we need to explore
  // TBD - option to select between "optimize for speed" and "optimize for memory"
  // (two different classes?  or make this a template <bool> class?)
  traverse_data td;
  shared_state* tdcurr;
  shared_state* tdnext;
  int* from_minterm;
  int* to_minterm;

  // used by exploreFiring
  int changed_k;
  int new_index;

  substate_colls* colls;

  // used only for debug info?
  const model_event* E;

  // TBD - this should be static or accessed via a parent class
  debugging_msg &debug;
};

// **************************************************************************
// *                        firing_subeventI  methods                        *
// **************************************************************************

firing_subeventI::firing_subeventI(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls* c, intset event_deps, expr* chunk, forest* f, int* v, int nv)
: sathyb_opname::subevent(f, v, nv, true), td(traverse_data::Compute), debug(d)
{
  E = Ev;
  fire_expr = chunk;
  colls = c;

  mt_from = 0;
  mt_to = 0;
  mt_alloc = mt_used = 0;
  num_levels = p.getPartInfo().num_levels;

  td.answer = new result;
  tdcurr = new shared_state(&p);
  tdnext = new shared_state(&p);
  td.current_state = tdcurr;
  td.next_state = tdnext;
  from_minterm = new int[1+num_levels];
  to_minterm = new int[1+num_levels];

  from_minterm[0] = DONT_CARE;
  to_minterm[0] = DONT_CARE;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = DONT_CARE;
    if (event_deps.contains(k)) {
      to_minterm[k] = DONT_CARE;
    } else {
      to_minterm[k] = DONT_CARE;
    }
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
  }
}

firing_subeventI::~firing_subeventI()
{
  Delete(fire_expr);
  for (int i=0; i<mt_used; i++) {
    delete[] mt_from[i];
    delete[] mt_to[i];
  }
  free(mt_from);
  free(mt_to);

  delete td.answer;
  Delete(tdcurr);
  Delete(tdnext);
  delete[] from_minterm;
  delete[] to_minterm;
}

void firing_subeventI::confirm(sathyb_opname::hybrid_relation &rel, int k, int index)
{
  DCASSERT(E);
  if (debug.startReport()) {
    debug.report() << "confirming level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    fire_expr->Print(debug.report(), 0);
    debug.report() << "\n";
    debug.stopIO();
  }

  changed_k = k;
  new_index = index;

  // Explicitly explore new states and discover minterms to add
  exploreFiring(rel, getNumVars());

#ifndef USING_MEDDLY_ADD_MINTERM
  if (0==mt_used) return;

  if (debug.startReport()) {
    for (int i = 0; i < mt_used; i++) {
      debug.report() << "\nFiring:\n";
      debug.report() << "\nfrom[" << i << "]: [";
      debug.report().PutArray(mt_from[i]+1, num_levels);
      debug.report() << "]\nto[" << i << "]: [";
      debug.report().PutArray(mt_to[i]+1, num_levels);
      debug.report() << "]\n";
      debug.report() << "\n\n";
    }
    debug.stopIO();
  }

  // Add those minterms
  dd_edge add_to_root(getForest());
  getForest()->createEdge(mt_from, mt_to, mt_used, add_to_root);
  mt_used = 0;

  add_to_root += getRoot();
  setRoot(add_to_root);

  if (debug.startReport()) {
    debug.report() << "confirmed  level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    fire_expr->Print(debug.report(), 0);
    debug.newLine();
    debug.report() << "New root: " << add_to_root.getNode() << "\n";
    debug.stopIO();
  }
#endif
}

#ifndef USING_MEDDLY_ADD_MINTERM
bool firing_subeventI::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    int old_alloc = mt_alloc;
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
    if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
    for (int i=old_alloc; i<mt_alloc; i++) {
      mt_from[i] = 0;
      mt_to[i] = 0;
    }
  }
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[1+num_levels];
  }
  if (0==mt_to[mt_used]) {
    mt_to[mt_used] = new int[1+num_levels];
  }
  memcpy(mt_from[mt_used], from, (1+num_levels) * sizeof(int));
  memcpy(mt_to[mt_used], to, (1+num_levels) * sizeof(int));
  mt_used++;
  return true;
}
#endif

void firing_subeventI::exploreFiring(sathyb_opname::hybrid_relation &rel, int dpth)
{
  //
  // Are we at the bottom?
  //
  if (0==dpth) {
    bool start_d = debug.startReport();
    if (start_d) {
      debug.report() << "firing?\n\tfrom state ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tfrom minterm [";
      debug.report().PutArray(from_minterm+1, num_levels);
      debug.report() << "]\n\t";
      fire_expr->Print(debug.report(), 0);
      debug.report() << " : ";
    }
    DCASSERT(fire_expr);
    td.answer->setBool(true);
    fire_expr->Compute(td);
    if (start_d) {
      if (td.answer->isNormal())
        debug.report() << "ok";
      else if (td.answer->isUnknown())
        debug.report() << "?";
      else if (td.answer->isOutOfBounds()) {
        td.current_state->Print(debug.report(), 0);
        debug.report() << "out of bounds";
      }
      else
        debug.report() << "null";
      debug.report() << "\n";
      debug.stopIO();
    }

    if (!td.answer->isNormal()) return; // not enabled after all

    //
    // next state -> minterm
    for (int dd=0; dd<getNumVars(); dd++) {
      int kk = getVars()[dd];
      to_minterm[kk] = colls->addSubstate(kk,
                                          tdnext->readSubstate(kk), tdnext->readSubstateSize(kk)
                                          );

      if (to_minterm[kk]>=0) continue;
      if (-2==to_minterm[kk]) throw subengine::Out_Of_Memory;
      throw subengine::Engine_Failed;

    }

    //
    // More debug info
    //
    if (debug.startReport()) {
      debug.report() << "firing\n\tto state ";
      tdnext->Print(debug.report(), 0);
      debug.report() << "\n\tto minterm [";
      debug.report().PutArray(to_minterm+1, num_levels);
      debug.report() << "]\n";
      debug.stopIO();
    }

    // add minterm to queue
    //
    if (!addMinterm(from_minterm, to_minterm))
      throw subengine::Out_Of_Memory;

    return;
  }

  //
  // Are we at the level being confirmed?
  //
  const int k = getVars()[dpth-1];
  if (k == changed_k) {
    tdcurr->set_substate_known(changed_k);
    tdnext->set_substate_known(changed_k);
    int ssz = tdcurr->readSubstateSize(changed_k);
    int foo = colls->getSubstate(changed_k, new_index, tdcurr->writeSubstate(changed_k), ssz);
    if (foo<0) throw subengine::Engine_Failed;
    colls->getSubstate(changed_k, new_index, tdnext->writeSubstate(changed_k), ssz);

    //
    // tbd - short circuit?
    //

    from_minterm[changed_k] = new_index;
    exploreFiring(rel, dpth-1);

    from_minterm[changed_k] = DONT_CARE;
    to_minterm[changed_k] = DONT_CARE;
    tdcurr->set_substate_unknown(changed_k);
    tdnext->set_substate_unknown(changed_k);
    return;
  }

  //
  // Regular level - explore all confirmed
  //
  tdcurr->set_substate_known(k);
  tdnext->set_substate_known(k);
  const int localsize = rel.getHybridForest()->getLevelSize(k);
  for (int i = 0; i<localsize; i++) {
    if (!rel.isConfirmedState(k, i)) continue; // skip unconfirmed

    int ssz = tdcurr->readSubstateSize(k);
    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;
    colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

    //
    // tbd - short circuit?
    //

    from_minterm[k] = i;
    exploreFiring(rel, dpth-1);
  } // for i
  from_minterm[k] = DONT_CARE;
  to_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
  tdnext->set_substate_unknown(k);
}

// **************************************************************************
// *                                                                        *
// *                        enabling_subevent  class                        *
// *                                                                        *
// **************************************************************************

// TBD - move this somewhere better
//
class enabling_subevent : public satotf_opname::subevent {
  public:
    // TBD - clean up this constructor!
    // enabling_subevent(debugging_msg &d, const dsde_hlm &p, substate_colls *c, intset event_deps, expr* chunk, int* v, int nv);
    enabling_subevent(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls *c, intset event_deps, expr* chunk, forest* f, int* v, int nv);
    virtual ~enabling_subevent();

  protected:
    virtual void confirm(satotf_opname::otf_relation &rel, int v, int index);

  private: // helpers
#ifndef USING_MEDDLY_ADD_MINTERM
    // returns true on success
    bool addMinterm(const int* from, const int* to);
#endif

    void exploreEnabling(satotf_opname::otf_relation &rel, int dpth);

    inline bool maybeEnabled() {
#ifdef SHORT_CIRCUIT_ENABLING
      DCASSERT(is_enabled);
      is_enabled->Compute(td);
      if (td.answer->isNormal()) {
        return td.answer->getBool();
      } else if (td.answer->isUnknown()) {
        return true;
      } else {
        // null?  not sure what to do here
        return false;
      }
#else
      return true;
#endif
    }

  private:
    expr* is_enabled;
    int** mt_from;
    int** mt_to;
    int mt_alloc;
    int mt_used;
    int num_levels;
    // some of this stuff could be shared, one per model,
    // at the cost of re-initializing stuff every time we need to explore
    // TBD - option to select between "optimize for speed" and "optimize for memory"
    // (two different classes?  or make this a template <bool> class?)
    traverse_data td;
    shared_state* tdcurr;
    int* from_minterm;
    int* to_minterm;

    // used by exploreEnabling
    int changed_k;
    int new_index;

    substate_colls* colls;

    // used only for debug info?
    const model_event* E;

    // TBD - this should be static or accessed via a parent class
    debugging_msg &debug;
};

// **************************************************************************
// *                       enabling_subevent  methods                       *
// **************************************************************************

// enabling_subevent::enabling_subevent(debugging_msg &d, const dsde_hlm &p, substate_colls* c, intset event_deps, expr* chunk, int* v, int nv)
enabling_subevent::enabling_subevent(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls* c, intset event_deps, expr* chunk, forest* f, int* v, int nv)
 : satotf_opname::subevent(f, v, nv, false), td(traverse_data::Compute), debug(d)
{
  E = Ev;
  is_enabled = chunk;
  colls = c;

  mt_from = 0;
  mt_to = 0;
  mt_alloc = mt_used = 0;
  num_levels = p.getPartInfo().num_levels;

  td.answer = new result;
  tdcurr = new shared_state(&p);
  td.current_state = tdcurr;
  from_minterm = new int[1+num_levels];
  to_minterm = new int[1+num_levels];

  from_minterm[0] = DONT_CARE;
  to_minterm[0] = DONT_CHANGE;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = DONT_CARE;
    if (event_deps.contains(k)) {
      to_minterm[k] = DONT_CARE;
    } else {
      to_minterm[k] = DONT_CHANGE;
    }
    tdcurr->set_substate_unknown(k);
  }
}

enabling_subevent::~enabling_subevent()
{
  Delete(is_enabled);
  for (int i=0; i<mt_used; i++) {
    delete[] mt_from[i];
    delete[] mt_to[i];
  }
  free(mt_from);
  free(mt_to);

  delete td.answer;
  Delete(tdcurr);
  delete[] from_minterm;
  delete[] to_minterm;
}

void enabling_subevent::confirm(satotf_opname::otf_relation &rel, int k, int index)
{
  DCASSERT(E);
  if (debug.startReport()) {
    debug.report() << "confirming level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " enabling ";
    is_enabled->Print(debug.report(), 0);
    debug.report() << "\n";
    debug.stopIO();
  }

  changed_k = k;
  new_index = index;

  // Explicitly explore new states and discover minterms to add

  exploreEnabling(rel, getNumVars());

#ifndef USING_MEDDLY_ADD_MINTERM
  if (0==mt_used) return;

  // Add those minterms
  dd_edge add_to_root(getForest());
  DCASSERT(mt_from);
  DCASSERT(mt_to);

  if (debug.startReport()) {
    for (int i = 0; i < mt_used; i++) {
      debug.report() << "\nEnabling\n";
      debug.report() << "\nfrom[" << i << "]: [";
      debug.report().PutArray(mt_from[i]+1, num_levels);
      debug.report() << "]\nto[" << i << "]: [";
      debug.report().PutArray(mt_to[i]+1, num_levels);
      debug.report() << "]\n";
      debug.report() << "\n\n";
    }
    debug.stopIO();
  }

  getForest()->createEdge(mt_from, mt_to, mt_used, add_to_root);
  mt_used = 0;

  add_to_root += getRoot();
  setRoot(add_to_root);

  if (debug.startReport()) {
    debug.report() << "confirmed  level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " enabling ";
    is_enabled->Print(debug.report(), 0);
    debug.newLine();
    debug.report() << "New root: " << add_to_root.getNode() << "\n";
    debug.stopIO();
  }
#endif
}

#ifndef USING_MEDDLY_ADD_MINTERM
bool enabling_subevent::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    int old_alloc = mt_alloc;
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
    if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
    for (int i=old_alloc; i<mt_alloc; i++) {
      mt_from[i] = 0;
      mt_to[i] = 0;
    }
  }
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[1+num_levels];
    DCASSERT(0==mt_to[mt_used]);
    mt_to[mt_used] = new int[1+num_levels];
  }
  memcpy(mt_from[mt_used], from, (1+num_levels) * sizeof(int));
  memcpy(mt_to[mt_used], to, (1+num_levels) * sizeof(int));
  mt_used++;
  return true;
}
#endif

void enabling_subevent::exploreEnabling(satotf_opname::otf_relation &rel, int dpth)
{
  //
  // Are we at the bottom?
  //
  if (0==dpth) {
    bool start_d = debug.startReport();
    if (start_d) {
      debug.report() << "enabled?\n\tstate ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tfrom minterm [";
      debug.report().PutArray(from_minterm+1, num_levels);
      debug.report() << "]\n\t";
      is_enabled->Print(debug.report(), 0);
      debug.report() << " : ";
    }
#ifdef SHORT_CIRCUIT_ENABLING
    td.answer->setBool(true);
#else
    DCASSERT(is_enabled);
    is_enabled->Compute(td);
#endif
    if (start_d) {
      if (td.answer->isNormal()) {
        if (td.answer->getBool())
          debug.report() << "true";
        else
          debug.report() << "false";
      } else if (td.answer->isUnknown())
        debug.report() << "?";
      else
        debug.report() << "null";
      debug.report() << "\n";
      debug.stopIO();
    }

    DCASSERT(td.answer->isNormal());
    if (false == td.answer->getBool()) return;

    //
    // next state -> minterm
    //
    /*
        // ASM - this should already be set!

    for (int dd=0; dd<getNumVars(); dd++) {
      int kk = getVars()[dd];
      to_minterm[kk] = DONT_CARE;
    }
    */

    //
    // More debug info
    //
    if (debug.startReport()) {
      debug.report() << "enabled\n\tstate ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tto minterm [";
      debug.report().PutArray(to_minterm+1, num_levels);
      debug.report() << "]\n";
      debug.stopIO();
    }

    addMinterm(from_minterm, to_minterm);

    return;
  }

  //
  // Are we at the level being confirmed?
  //
  const int k = getVars()[dpth-1];
  if (k == changed_k) {
      tdcurr->set_substate_known(changed_k);
      int ssz = tdcurr->readSubstateSize(changed_k);
      int foo = colls->getSubstate(changed_k, new_index, tdcurr->writeSubstate(changed_k), ssz);
      if (foo<0) throw subengine::Engine_Failed;

      //
      // check expression and short-circuit if definitely false
      // tbd - what about definitely true?
      //

      if (maybeEnabled()) {
        from_minterm[changed_k] = new_index;
        exploreEnabling(rel, dpth-1);
      }

      from_minterm[changed_k] = DONT_CARE;
      tdcurr->set_substate_unknown(changed_k);
      return;
  }

  //
  // Regular level - explore all confirmed
  //
  tdcurr->set_substate_known(k);
  const int localsize = rel.getRelForest()->getLevelSize(k);
  for (int i = 0; i<localsize; i++) {
      if (!rel.isConfirmed(k, i)) continue; // skip unconfirmed

      int ssz = tdcurr->readSubstateSize(k);
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;

      //
      // check expression and short-circuit if definitely false
      //

      if (maybeEnabled()) {
        from_minterm[k] = i;
        exploreEnabling(rel, dpth-1);
      }
  } // for i
  from_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
}


// **************************************************************************
// *                                                                        *
// *                         firing_subevent  class                         *
// *                                                                        *
// **************************************************************************

// TBD - move this somewhere better
//
class firing_subevent : public satotf_opname::subevent {
  public:
    // TBD - clean up this constructor!
    firing_subevent(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls *c, intset event_deps, expr* chunk, forest* f, int* v, int nv);
    virtual ~firing_subevent();

  protected:
    virtual void confirm(satotf_opname::otf_relation &rel, int v, int index);

  private: // helpers
#ifndef USING_MEDDLY_ADD_MINTERM
    // returns true on success
    bool addMinterm(const int* from, const int* to);
#endif

    void exploreFiring(satotf_opname::otf_relation &rel, int dpth);

  private:
    expr* fire_expr;
    int** mt_from;
    int** mt_to;
    int mt_alloc;
    int mt_used;
    int num_levels;
    // some of this stuff could be shared, one per model,
    // at the cost of re-initializing stuff every time we need to explore
    // TBD - option to select between "optimize for speed" and "optimize for memory"
    // (two different classes?  or make this a template <bool> class?)
    traverse_data td;
    shared_state* tdcurr;
    shared_state* tdnext;
    int* from_minterm;
    int* to_minterm;

    // used by exploreFiring
    int changed_k;
    int new_index;

    substate_colls* colls;

    // used only for debug info?
    const model_event* E;

    // TBD - this should be static or accessed via a parent class
    debugging_msg &debug;
};

// **************************************************************************
// *                        firing_subevent  methods                        *
// **************************************************************************

firing_subevent::firing_subevent(debugging_msg &d, const dsde_hlm &p, const model_event* Ev, substate_colls* c, intset event_deps, expr* chunk, forest* f, int* v, int nv)
 : satotf_opname::subevent(f, v, nv, true), td(traverse_data::Compute), debug(d)
{
  E = Ev;
  fire_expr = chunk;
  colls = c;

  mt_from = 0;
  mt_to = 0;
  mt_alloc = mt_used = 0;
  num_levels = p.getPartInfo().num_levels;

  td.answer = new result;
  tdcurr = new shared_state(&p);
  tdnext = new shared_state(&p);
  td.current_state = tdcurr;
  td.next_state = tdnext;
  from_minterm = new int[1+num_levels];
  to_minterm = new int[1+num_levels];

  from_minterm[0] = DONT_CARE;
  to_minterm[0] = DONT_CARE;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = DONT_CARE;
    if (event_deps.contains(k)) {
      to_minterm[k] = DONT_CARE;
    } else {
      to_minterm[k] = DONT_CHANGE;
    }
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
  }
}

firing_subevent::~firing_subevent()
{
  Delete(fire_expr);
  for (int i=0; i<mt_used; i++) {
    delete[] mt_from[i];
    delete[] mt_to[i];
  }
  free(mt_from);
  free(mt_to);

  delete td.answer;
  Delete(tdcurr);
  Delete(tdnext);
  delete[] from_minterm;
  delete[] to_minterm;
}

void firing_subevent::confirm(satotf_opname::otf_relation &rel, int k, int index)
{
  DCASSERT(E);
  if (debug.startReport()) {
    debug.report() << "confirming level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    fire_expr->Print(debug.report(), 0);
    debug.report() << "\n";
    debug.stopIO();
  }

  changed_k = k;
  new_index = index;

  // Explicitly explore new states and discover minterms to add

  exploreFiring(rel, getNumVars());

#ifndef USING_MEDDLY_ADD_MINTERM
  if (0==mt_used) return;

  if (debug.startReport()) {
    for (int i = 0; i < mt_used; i++) {
      debug.report() << "\nFiring:\n";
      debug.report() << "\nfrom[" << i << "]: [";
      debug.report().PutArray(mt_from[i]+1, num_levels);
      debug.report() << "]\nto[" << i << "]: [";
      debug.report().PutArray(mt_to[i]+1, num_levels);
      debug.report() << "]\n";
      debug.report() << "\n\n";
    }
    debug.stopIO();
  }

  // Add those minterms
  dd_edge add_to_root(getForest());
  getForest()->createEdge(mt_from, mt_to, mt_used, add_to_root);
  mt_used = 0;

  add_to_root += getRoot();
  setRoot(add_to_root);

  if (debug.startReport()) {
    debug.report() << "confirmed  level " << k << " index " << index;
    debug.report() << " event " << E->Name() << " firing ";
    fire_expr->Print(debug.report(), 0);
    debug.newLine();
    debug.report() << "New root: " << add_to_root.getNode() << "\n";
    debug.stopIO();
  }
#endif
}

#ifndef USING_MEDDLY_ADD_MINTERM
bool firing_subevent::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    int old_alloc = mt_alloc;
    if (0==mt_alloc) {
      mt_alloc = 8;
      mt_from = (int**) malloc(mt_alloc * sizeof(int**));
      mt_to = (int**) malloc(mt_alloc * sizeof(int**));
    } else {
      mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
      mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
      mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
    }
    if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
    for (int i=old_alloc; i<mt_alloc; i++) {
      mt_from[i] = 0;
      mt_to[i] = 0;
    }
  }
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[1+num_levels];
  }
  if (0==mt_to[mt_used]) {
    mt_to[mt_used] = new int[1+num_levels];
  }
  memcpy(mt_from[mt_used], from, (1+num_levels) * sizeof(int));
  memcpy(mt_to[mt_used], to, (1+num_levels) * sizeof(int));
  mt_used++;
  return true;
}
#endif

void firing_subevent::exploreFiring(satotf_opname::otf_relation &rel, int dpth)
{
  //
  // Are we at the bottom?
  //
  if (0==dpth) {
    bool start_d = debug.startReport();
    if (start_d) {
      debug.report() << "firing?\n\tfrom state ";
      tdcurr->Print(debug.report(), 0);
      debug.report() << "\n\tfrom minterm [";
      debug.report().PutArray(from_minterm+1, num_levels);
      debug.report() << "]\n\t";
      fire_expr->Print(debug.report(), 0);
      debug.report() << " : ";
    }
    DCASSERT(fire_expr);
    td.answer->setBool(true);
    fire_expr->Compute(td);
    if (start_d) {
      if (td.answer->isNormal())
        debug.report() << "ok";
      else if (td.answer->isUnknown())
        debug.report() << "?";
      else if (td.answer->isOutOfBounds())
        debug.report() << "out of bounds";
      else
        debug.report() << "null";
      debug.report() << "\n";
      debug.stopIO();
    }

    if (!td.answer->isNormal()) return; // not enabled after all

    //
    // next state -> minterm
    for (int dd=0; dd<getNumVars(); dd++) {
      int kk = getVars()[dd];
      to_minterm[kk] = colls->addSubstate(kk,
          tdnext->readSubstate(kk), tdnext->readSubstateSize(kk)
      );

      if (to_minterm[kk]>=0) continue;
      if (-2==to_minterm[kk]) throw subengine::Out_Of_Memory;
      throw subengine::Engine_Failed;
    }

    //
    // More debug info
    //
    if (debug.startReport()) {
      debug.report() << "firing\n\tto state ";
      tdnext->Print(debug.report(), 0);
      debug.report() << "\n\tto minterm [";
      debug.report().PutArray(to_minterm+1, num_levels);
      debug.report() << "]\n";
      debug.stopIO();
    }

    // add minterm to queue
    //
    if (!addMinterm(from_minterm, to_minterm))
      throw subengine::Out_Of_Memory;

    return;
  }

  //
  // Are we at the level being confirmed?
  //
  const int k = getVars()[dpth-1];
  if (k == changed_k) {
      tdcurr->set_substate_known(changed_k);
      tdnext->set_substate_known(changed_k);
      int ssz = tdcurr->readSubstateSize(changed_k);
      int foo = colls->getSubstate(changed_k, new_index, tdcurr->writeSubstate(changed_k), ssz);
      if (foo<0) throw subengine::Engine_Failed;
      colls->getSubstate(changed_k, new_index, tdnext->writeSubstate(changed_k), ssz);

      //
      // tbd - short circuit?
      //

      from_minterm[changed_k] = new_index;
      exploreFiring(rel, dpth-1);

      from_minterm[changed_k] = DONT_CARE;
      to_minterm[changed_k] = DONT_CARE;
      tdcurr->set_substate_unknown(changed_k);
      tdnext->set_substate_unknown(changed_k);
      return;
  }

  //
  // Regular level - explore all confirmed
  //
  tdcurr->set_substate_known(k);
  tdnext->set_substate_known(k);
  const int localsize = rel.getRelForest()->getLevelSize(k);
  for (int i = 0; i<localsize; i++) {
      if (!rel.isConfirmed(k, i)) continue; // skip unconfirmed

      int ssz = tdcurr->readSubstateSize(k);
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;
      colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

      //
      // tbd - short circuit?
      //

      from_minterm[k] = i;
      exploreFiring(rel, dpth-1);
  } // for i
  from_minterm[k] = DONT_CARE;
  to_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
  tdnext->set_substate_unknown(k);
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
    expr_node* termlist;
    deplist* next;
    // mxd
    // dd_edge* dd;
    // explicit storage of minterms
  private:
    intset level_deps;
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

    inline int getLevelAbove(int k) const {
      return level_deps.getSmallestAfter(k);
    }
    inline bool sameDeps(const intset& deps) const {
      return level_deps == deps;
    }
    inline bool intersectDepsExist(const intset& deps) const {
      intset result_deps = level_deps * deps;
      return !result_deps.isEmpty();
    }
    inline void unionWithDeps(const intset& deps) {
      level_deps += deps;
    }
    inline void addToDeps(intset& deps) const {
      deps += level_deps;
    }
    inline int countDeps() const {
      return level_deps.cardinality();
    }
    inline bool singleDeps() const {
      return level_deps.isSingleCardinality();
    }


    /**
        Given a list of levels, build the intersection
        with the set of levels that we depend on.
        This is unsafe - no bound checking on the arrays - so make sure
        the arrays are large enough :^)

          @param  levels_in   Zero-terminated array of levels, as input.
          @param  levels_out  Resulting list stored here, as a
                              zero-terminated array of levels.
    */
    inline void intersectLevels(const int* levels_in, int* levels_out) const {
      while (*levels_in) {
        if (level_deps.contains(*levels_in)) {
          *levels_out = *levels_in;
          levels_out++;
        }
        levels_in++;
      }
      *levels_out = 0;
    }
  private:
    void expandLists();
  };
protected:
  deplist** enable_deps;
  deplist** fire_deps;
  substate_colls* colls;
  intset* confirmed;    // substates that are confirmed
  intset* toBeExplored; // substates to explore next round
  int num_levels;
private:
  traverse_data td;
  shared_state* tdcurr;
  shared_state* tdnext;
  int* from_minterm;
  int* to_minterm;
  int* tmpLevels;   // array of size num_levels+1
public:
  substate_varoption(meddly_reachset &x, const dsde_hlm &p,
    const exprman* em, const meddly_procgen &pg);
  virtual ~substate_varoption();
  virtual void initializeVars();
  virtual void initializeEvents(debugging_msg &d);
  virtual void reportStats(DisplayStream &out) const;

  //
  // TBD - for now
  //

  virtual satotf_opname::otf_relation* buildNSF_OTF(debugging_msg &debug);
  virtual satimpl_opname::implicit_relation* buildNSF_IMPLICIT(debugging_msg &debug);
  virtual sathyb_opname::hybrid_relation* buildNSF_HYBRID(debugging_msg &debug);
  virtual MEDDLY::dd_edge buildPotentialDeadlockStates_IMPLICIT(debugging_msg &debug);
  virtual substate_colls* getSubstateStorage() { return colls; }

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

  /**
      Add minterms for enabling expression.
        @param  d         Debugging stream
        @param  dl        Chunk of function.
        @param  k         Current level
        @param  changed   0, if we have already seen an unexplored local.
                          Otherwise, zero-terminated list of levels with
                          unexplored locals to pull from.
  */
  void exploreEnabling(debugging_msg &d, deplist &dl, int k, const int* changed);


  /**
      Add minterms for next-state expression.
        @param  d         Debugging stream
        @param  dl        Chunk of function.
        @param  k         Current level
        @param  changed   0, if we have already seen an unexplored local.
                          Otherwise, zero-terminated list of levels with
                          unexplored locals to pull from.
  */
  void exploreNextstate(debugging_msg &d, deplist &dl, int k, const int* changed);

protected:
  /**
      Update all enabling and next-state functions for the given list of
      modified (i.e., new local states to explore) levels.
      The unexplored locals at those levels are then marked as explored.
        @param  d         Debugging stream
        @param  levels    Zero-terminated list of levels, from
                          lowest to highest.
  */
  void updateLevels(debugging_msg &d, const int* levels);

  /**
      Build the list of levels that have confirmed, unexplored locals.
        @param  levels    Array of dimension num_levels+1 or larger.
                          On output - a zero terminated list of levels.
  */
  inline void buildLevelsToExplore(int* levels) {
    for (int k=1; k<=num_levels; k++) {
      int first = toBeExplored[k].getSmallestAfter(-1);
      if (first<0) continue;
      *levels = k;
      levels++;
    }
    *levels = 0;
  }

#ifdef SHOW_SUBSTATES
  void show_substates(OutputStream &s);
#endif
};

// **************************************************************************
// *                                                                        *
// *                        relation_node class                             *
// *                                                                        *
// **************************************************************************

//TBD - Move to someplace better
class derive_relation_node : public relation_node {
public:
  // TBD - clean up this constructor!
  derive_relation_node(debugging_msg &dm, substate_colls* c, forest* fst, int lvl, long e, long f, long inh);
  virtual ~derive_relation_node();
  virtual long nextOf(long i) override;

private:
  long e_delta;
  long f_delta;
  long i_const;
  substate_colls* colls;

  // debugging_msg &debug;
};


// **************************************************************************
// *                                                                        *
// *                        relation_node methods                           *
// *                                                                        *
// **************************************************************************


derive_relation_node::derive_relation_node(debugging_msg &dm, substate_colls* c, forest* fst, int lvl, long e, long f, long inh):relation_node(141010, fst, lvl, -1, e, f, inh){
  e_delta = e;
  f_delta = f;
  i_const = inh;
  colls = c;
}

derive_relation_node::~derive_relation_node()
{
  //TBD
}

//Take the index : find token : update the token : return a new index
long derive_relation_node::nextOf(long i)
{
  if(i>=relation_node::getPieceSize())
    relation_node::expandTokenUpdate(i);

  if(relation_node::getTokenUpdate()[i]==-2)
    {
    int sz = 1;
    int chunk[sz];
    int chunk_updated[sz];
    // int curr = 0;
    colls->getSubstate(this->getLevel(), i, chunk, sz);

    //Token update is calculated
    chunk_updated[0] = -1;

    if (i_const!=-1)
      {
        if ((chunk[0]>=e_delta) && (chunk[0]<i_const))  chunk_updated[0] = chunk[0] + f_delta;
      }
    else
      {
        if (chunk[0]>=e_delta)  chunk_updated[0] = chunk[0] + f_delta;
      }

    if(chunk_updated[0]<0) return -1;

    //New index is received
    long j = colls->addSubstate(this->getLevel(), chunk_updated, sz);
    relation_node::setTokenUpdateAtIndex(i,j);
    }

  return relation_node::getTokenUpdate()[i];

}


// **************************************************************************
// *                                                                        *
// *                  substate_varoption::deplist  methods                  *
// *                                                                        *
// **************************************************************************

substate_varoption::deplist
::deplist(const intset &x, expr_node* tl, deplist* n, int d) : level_deps(x)
{
  termlist = tl;
  // dd = 0;
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
    expandLists();
  }
  if (0==mt_from) return false; // malloc or realloc failed
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[depth];
  }
  memcpy(mt_from[mt_used], from, depth * sizeof(int));
  if (0==mt_to[mt_used]) {
    mt_to[mt_used] = new int[depth];
  }
  for (int i=0; i<depth; i++) {
    mt_to[mt_used][i] = DONT_CARE;
  }
  mt_used++;
  return true;
}

bool substate_varoption::deplist::addMinterm(const int* from, const int* to)
{
  if (mt_used >= mt_alloc) {
    expandLists();
  }
  if (0==mt_from || 0==mt_to) return false; // malloc or realloc failed
  if (0==mt_from[mt_used]) {
    mt_from[mt_used] = new int[depth];
  }
  memcpy(mt_from[mt_used], from, depth * sizeof(int));
  if (0==mt_to[mt_used]) {
    mt_to[mt_used] = new int[depth];
  }
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
  if (next) {
    s.Pad(' ', pad);
    s << "(synch)\n";
    next->showMinterms(s, pad);
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
  if (next) {
    s.Pad(' ', pad);
    s << "(synch)\n";
    next->showMintermPairs(s, pad);
  }
}

void substate_varoption::deplist::expandLists()
{
  // resize arrays...
  int old_alloc = mt_alloc;
  if (0==mt_alloc) {
    mt_alloc = 8;
    mt_from = (int**) malloc(mt_alloc * sizeof(int**));
    mt_to = (int**) malloc(mt_alloc * sizeof(int**));
  } else {
    mt_alloc = MIN(2*mt_alloc, 256 + mt_alloc);
    mt_from = (int**) realloc(mt_from, mt_alloc * sizeof(int**));
    mt_to = (int**) realloc(mt_to, mt_alloc * sizeof(int**));
  }
  for (int i=old_alloc; i<mt_alloc; i++) {
    mt_from[i] = 0;
    mt_to[i] = 0;
  }
}

// **************************************************************************
// *                                                                        *
// *                       substate_varoption methods                       *
// *                                                                        *
// **************************************************************************

substate_varoption
::substate_varoption(meddly_reachset &x, const dsde_hlm &p,
  const exprman* em, const meddly_procgen &pg)
: meddly_varoption(x, p), td(traverse_data::Compute)
{
  colls = 0;
  enable_deps = 0;
  fire_deps = 0;
  confirmed = 0;
  toBeExplored = 0;
  td.answer = new result;
  tdcurr = new shared_state(&p);
  tdnext = new shared_state(&p);
  td.current_state = tdcurr;
  td.next_state = tdnext;
  from_minterm = 0;
  to_minterm = 0;
  initDomain(em);
  initEncoders(pg);
  tmpLevels = 0;
}

substate_varoption::~substate_varoption()
{
  Delete(colls);
  delete[] confirmed;
  delete[] toBeExplored;
  delete[] from_minterm;
  delete[] to_minterm;
  Delete(tdcurr);
  Delete(tdnext);
  delete td.answer;
  delete[] tmpLevels;
}

void substate_varoption::initializeVars()
{
  meddly_varoption::initializeVars();
  // confirm the initial substates
  for (int k=num_levels; k; k--) {
    int N = colls->getMaxIndex(k);
    enlarge(confirmed[k], N);
    confirmed[k].addRange(0, N-1);
    enlarge(toBeExplored[k], N);
    toBeExplored[k].addRange(0, N-1);
  } // for k
  // make scratch space
  tmpLevels = new int[num_levels+1];
}

void
substate_varoption::initializeEvents(debugging_msg &d)
{
  if (d.startReport()) {
    d.report() << "preprocessing event expressions\n";
    d.stopIO();
  }
  enable_deps = new deplist*[getParent().getNumEvents()];
  fire_deps = new deplist*[getParent().getNumEvents()];
  for (int i=0; i<getParent().getNumEvents(); i++) {
    const model_event* e = getParent().readEvent(i);
    enable_deps[i] = getExprDeps(e->getEnabling(), num_levels);
    fire_deps[i] = getExprDeps(e->getNextstate(), num_levels);
#ifdef DEVELOPMENT_CODE
    if (enable_deps[i]) {
      DCASSERT(enable_deps[i]->termlist);
    }
    if (fire_deps[i]) {
      DCASSERT(fire_deps[i]->termlist);
    }
#endif
  } // for i

  if (!d.startReport()) return;
  d.report() << "done preprocessing event expressions\n";

  for (int i=0; i<getParent().getNumEvents(); i++) {

    d.report() << "\t" << getParent().readEvent(i)->Name() << " enabling:\n";
    for (deplist *DL = enable_deps[i]; DL; DL=DL->next) {
      d.report() << "\t\tlevels ";
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
        d.report() << k << ", ";
      } // for k
      d.report() << "\n\t\t";
      for (expr_node* term = DL->termlist; term; term=term->next) {
        term->term->Print(d.report(), 0);
        d.report() << ", ";
      } // for term
      d.report() << "\n";
    } // for DL

    d.report() << "\t" << getParent().readEvent(i)->Name() << " firing:\n";
    for (deplist *DL = fire_deps[i]; DL; DL=DL->next) {
      d.report() << "\t\tlevels ";
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
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
}


satotf_opname::otf_relation* substate_varoption::buildNSF_OTF(debugging_msg &debug)
{
  using namespace MEDDLY;

  exprman* em = getExpressionManager();
  DCASSERT(em);

  std::vector<satotf_opname::event*> otf_events;

  //
  // For each event
  //
  intset depends(num_levels+1);
  for (int i=0; i<getParent().getNumEvents(); i++) {
    //
    // union the enabling and firing dependencies to get all levels
    // that this event depends on

    depends.removeAll();

    int num_enabling = 0;
    for (deplist* ptr = enable_deps[i]; ptr; ptr=ptr->next, num_enabling++) {
      ptr->addToDeps(depends);
    };

    int num_firing = 0;
    for (deplist* ptr = fire_deps[i]; ptr; ptr=ptr->next, num_firing++) {
      ptr->addToDeps(depends);
    }

    // DCASSERT(num_enabling + num_firing > 0);
    if (num_enabling + num_firing == 0) continue;

    //
    // Build subevents
    //

    satotf_opname::subevent** subevents = new
      satotf_opname::subevent* [num_enabling + num_firing];
    int se = 0;

    //
    // Build enabling subevents
    //
    for (deplist* ptr = enable_deps[i]; ptr; ptr=ptr->next, se++) {
      //
      // build enabling expression from list
      //
      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
      }
      DCASSERT(length>0);
      expr* chunk = 0;
      if (1==length) {
        //
        // awesomesauce
        //
        chunk = Share(ptr->termlist->term);
      } else {
        //
        // Build conjunction
        //
        expr** terms = new expr*[length];
        int ti = 0;
        for (expr_node* t = ptr->termlist; t; t=t->next, ti++) {
          terms[ti] = Share(t->term);
        }
        chunk = em->makeAssocOp(location::NOWHERE(), exprman::aop_and, terms, 0, length);
      }

      // build list of variables this piece depends on
      int nv = ptr->countDeps();
      int* v = new int[nv];
      int k = 0;
      for (int vi = 0; vi<nv; vi++) {
        k = ptr->getLevelAbove(k);
        DCASSERT(k>0);
        v[vi] = k;
      }

      // Ok, build the enabling subevent
      const model_event* e = getParent().readEvent(i);
      subevents[se] = new enabling_subevent(debug, getParent(), e, colls, depends, chunk, get_mxd_forest(), v, nv);
    }


    //
    // Build firing subevents
    //
    for (deplist* ptr = fire_deps[i]; ptr; ptr=ptr->next, se++) {
      //
      // build firing expression from list
      //
      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
      }
      DCASSERT(length>0);
      expr* chunk = 0;
      if (1==length) {
        //
        // awesomesauce
        //
        chunk = Share(ptr->termlist->term);
      } else {
        //
        // Build conjunction
        //
        expr** terms = new expr*[length];
        int ti = 0;
        for (expr_node* t = ptr->termlist; t; t=t->next, ti++) {
          terms[ti] = Share(t->term);
        }
        chunk = em->makeAssocOp(location::NOWHERE(), exprman::aop_semi, terms, 0, length);
      }

      // build list of variables this piece depends on
      int nv = ptr->countDeps();

      int* v = new int[nv];
      int k = 0;
      for (int vi = 0; vi<nv; vi++) {
        k = ptr->getLevelAbove(k);
        DCASSERT(k>0);
        v[vi] = k;
      }

      // Ok, build the enabling subevent
      const model_event* e = getParent().readEvent(i);
      subevents[se] = new firing_subevent(debug, getParent(), e, colls, depends, chunk, get_mxd_forest(), v, nv);
    }


    //
    // Pull these together into the event
    //
    otf_events.push_back(new satotf_opname::event(subevents, num_enabling + num_firing));
  } // for i

  //
  // Build overall otf relation
  //
  return new satotf_opname::otf_relation(
    ms.getMddForest(), get_mxd_forest(), ms.getMddForest(),
    otf_events.data(), (int)otf_events.size()
  );
}

satimpl_opname::implicit_relation* substate_varoption::buildNSF_IMPLICIT(debugging_msg &debug)
{
  using namespace MEDDLY;
  // exprman* em = getExpressionManager();
  // DCASSERT(em);
  substate_colls* c_pass = this->getSubstateStorage();

  satimpl_opname::implicit_relation* T = new satimpl_opname::implicit_relation(ms.getMddForest(), ms.getMddForest(), ms.getMddForest());

  int max_node_count = 10;
  int nEvents = getParent().getNumEvents();
  // int nPlaces = getParent().getNumStateVars();


  int* tops_of_events = (int*)malloc(nEvents*sizeof(int));
  // int* place_count_in_event = (int*)malloc(nEvents*sizeof(int));
  std::vector<std::map<int, std::pair<long,long>>> event_table;
  event_table.resize(nEvents);
  //Holds the variables affected in the events from bottom to top
  // int** v_all_fire = (int**)malloc(nEvents*sizeof(int*));
  //Holds the constant delta of event on certain variable
  // long** v_delta_fire = (long**)malloc(nEvents*sizeof(int*));

  for(int i = 0; i<nEvents; i++) {
    if(enable_deps[i])
    for (deplist *DL = enable_deps[i]; DL; DL=DL->next) {
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
        std::pair<long,long> enable_alone = std::make_pair(DL->termlist->term->getLower(),0);
        event_table[i].insert(std::pair<int, std::pair<long,long>>(k,enable_alone));
      } // for k
    } // for enable_deps

    if(fire_deps[i])
    for (deplist *DL = fire_deps[i]; DL; DL=DL->next) {
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
        long fst = 0;
        std::map<int, std::pair<long,long>>::iterator fit = event_table[i].find(k);
        if (fit != event_table[i].end()) fst = fit->second.first;
        std::pair<long,long> fire_alone = std::make_pair(fst,DL->termlist->term->getDelta());
        event_table[i].erase(k);
        event_table[i].insert(std::pair<int, std::pair<long,long>>(k,fire_alone));
      } // for k
    } // for fire_deps

    max_node_count+=event_table[i].size();
    if(event_table[i].size()>0)
    {
      std::map<int, std::pair<long,long>>::reverse_iterator rit = event_table[i].rbegin();
      tops_of_events[i] = rit->first;
    }
  }// for Events



  derive_relation_node** rNode = (derive_relation_node**)malloc(max_node_count*sizeof(derive_relation_node*));
  int rCtr = 0;
  // int avbl = 0;

  for(int i = 0; i < nEvents; i++)
    {
    unsigned long sign = 0;
    int previous_node_handle = 1;
    std::map<int, std::pair<long,long>>::iterator e_it = event_table[i].begin();
    if(event_table[i].size()>0)
    while(e_it!=event_table[i].end()){

      int uniq = (e_it->first)*100 + (e_it->second.first)*10 + (e_it->second.first+e_it->second.second);
      sign = sign*10 + uniq;
      rNode[rCtr] = new derive_relation_node(debug, c_pass, get_mxd_forest(), e_it->first, e_it->second.first, e_it->second.second,-1);
      //rNode[rCtr] = new derive_relation_node(debug,e_it->second.first,e_it->second.second,c_pass,sign,e_it->first,previous_node_handle);
      previous_node_handle = T->registerNode((e_it->first==tops_of_events[i]),rNode[rCtr]);
      rCtr ++;
      e_it++;
    }
    }

  //
  // Return overall implicit relation
  //
  return T;
}

sathyb_opname::hybrid_relation* substate_varoption::buildNSF_HYBRID(debugging_msg &debug)
{
   using namespace MEDDLY;
  exprman* em = getExpressionManager();
  DCASSERT(em);
  substate_colls* c_pass = this->getSubstateStorage();

  int max_node_count = 10;
  int nEvents = getParent().getNumEvents();
  // int nPlaces = getParent().getNumStateVars();

  //forest* mxdRel = ms.createForest(true, forest::BOOLEAN, forest::MULTI_TERMINAL);

  int* tops_of_events = (int*)malloc(nEvents*sizeof(int));
  // int* place_count_in_event = (int*)malloc(nEvents*sizeof(int));
  std::vector<std::map<int, std::pair<long,long>>> event_table;
  event_table.resize(nEvents);
  //Holds the variables affected in the events from bottom to top
  // int** v_all_fire = (int**)malloc(nEvents*sizeof(int*));
  //Holds the constant delta of event on certain variable
  // long** v_delta_fire = (long**)malloc(nEvents*sizeof(int*));

  sathyb_opname::event** T = (sathyb_opname::event**)malloc(nEvents*sizeof(sathyb_opname::event*));

#if 1

  intset depends(num_levels+1);


  for (int i=0; i<getParent().getNumEvents(); i++) {


    std::map<int, std::vector<long>> rnmap_level_to_enable_inh_del;

    depends.removeAll();

    unsigned num_subevents = 0;
    unsigned num_relnodes = 0;

    for (deplist* ptr = enable_deps[i]; ptr; ptr=ptr->next) {
      ptr->addToDeps(depends);
      if (ptr->singleDeps()) num_relnodes++;
       else num_subevents++;
    };

    for (deplist* ptr = fire_deps[i]; ptr; ptr=ptr->next) {
      ptr->addToDeps(depends);
      if (ptr->singleDeps()) num_relnodes++;
      else num_subevents++;
    }

    printf("\n Initial analysis says se: %d",num_subevents);

    // DCASSERT(num_enabling + num_firing > 0);
    if (2*num_subevents + num_relnodes == 0) continue;

    //
    // Build subevents or implicit nodes
    //

    sathyb_opname::subevent** subevents = new sathyb_opname::subevent* [num_subevents];
    relation_node** relnodes = new relation_node* [num_relnodes];

    int se = 0;
    int rn = 0;

    /*
    * 1. Build firing mxd
    * 2. Build enabling mxd if v is in firing mxd
    * 3. Clear them from enable_deps and fire_deps
    * 4. Build rel from remaining
    */

     std::set<int> mxd_vars;

     for (deplist* ptr = fire_deps[i]; ptr; ptr=ptr->next) {

      //
      // build firing expression from list
      //
      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
      }

      DCASSERT(length>0); // Multiple expressions are combined. For eg. p1' = p1 + 1 ; p2' = p2 + tk(p1)  -> This gives length 2
      expr* chunk = 0;
      if ((length > 1) || !ptr->singleDeps())
      {
        //
        // Build conjunction
        //
        expr** terms = new expr*[length];
        int ti = 0;
        for (expr_node* t = ptr->termlist; t; t=t->next, ti++) {
          terms[ti] = Share(t->term);
        }
        chunk = em->makeAssocOp(location::NOWHERE(), exprman::aop_semi, terms, 0, length);

      // build list of variables this piece depends on
      int nv = ptr->countDeps();

      int* v = new int[nv];
      int k = 0;
      for (int vi = 0; vi<nv; vi++) {
        k = ptr->getLevelAbove(k);
        DCASSERT(k>0);
        v[vi] = k;
        printf(":%d",k);
        mxd_vars.insert(k);
      }

      // Ok, build the firing subevent
      const model_event* e = getParent().readEvent(i);
      subevents[se] = new firing_subeventI(debug, getParent(), e, c_pass, depends, chunk, get_mxd_forest(), v, nv);
      printf("\n Multi variable  firing mxd for levels %d",nv);
      se++;
      }
    }

    //
    // build enabling expression
    //

    #if 0
    for (deplist* ptr = enable_deps[i]; ptr; ptr=ptr->next) {

      //
      // build firing expression from list
      //
      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
      }

      DCASSERT(length>0);
      expr* chunk = 0;

      if (1==length) {
        //
        // awesomesauce
        //
       chunk = Share(ptr->termlist->term);

        // build list of variables this piece depends on
        int nv = ptr->countDeps();
        printf("\n Length == 1: No. of variables it depends = %d",nv);

        int* v = new int[nv];
        int k = 0;
        k = ptr->getLevelAbove(k);
        v[0] = k;
        if(mxd_vars.find(k)!=mxd_vars.end())
         {
           printf("\n Single variable enabling mxd for level %d",v[0]);
           const model_event* e = getParent().readEvent(i);
           subevents[se] = new enabling_subeventI(debug, getParent(), e, c_pass, depends, chunk, get_mxd_forest(), v, nv);
           se++;
         }


        } else {
        //
        // Build conjunction
        //
        expr** terms = new expr*[length];
        int ti = 0;
        for (expr_node* t = ptr->termlist; t; t=t->next, ti++) {
          terms[ti] = Share(t->term);
        }
        chunk = em->makeAssocOp(location::NOWHERE(), exprman::aop_and, terms, 0, length);
        if (chunk==0) continue;

        // build list of variables this piece depends on
        int nv = ptr->countDeps();
        printf("\n No. of variables it depends = %d",nv);

        int* v = new int[nv];
        int k = 0;
        printf("\n{");
        for (int vi = 0; vi<nv; vi++) {
          k = ptr->getLevelAbove(k);
          DCASSERT(k>0);
          v[vi] = k;
          printf("%d,",k);
        }
        printf("}");
        const model_event* e = getParent().readEvent(i);
        subevents[se] = new enabling_subeventI(debug, getParent(), e, c_pass, depends, chunk, get_mxd_forest(), v, nv);
        se++;
        printf("\n Enabling Subevent se[%d] with %d variables",se, nv);
      }
    }
    #endif


    //
    // Get enabling conditions for relation_nodes
    //

    for (deplist* ptr = enable_deps[i]; ptr; ptr=ptr->next) {

      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
        }

      if  ((length==1) && (ptr->singleDeps()))  {
          int k = ptr->getLevelAbove(0);
          if(mxd_vars.find(k) != mxd_vars.end()) { }
          else {
          long upper = ptr->termlist->term->getUpper()>0?ptr->termlist->term->getUpper():-1;
          std::vector<long> effects;
          effects.push_back(ptr->termlist->term->getLower());
          effects.push_back(upper);
          rnmap_level_to_enable_inh_del.insert(std::make_pair(k,effects));
          }
        }
    }

    //
    // Get firing conditions for relation_nodes
    //
    for (deplist* ptr = fire_deps[i]; ptr; ptr=ptr->next) {

      //
      // build firing expression from list
      //
      int length = 0;
      for (expr_node* t = ptr->termlist; t; t=t->next) {
        length++;
      }

      DCASSERT(length>0);
      expr* chunk = 0;
      if (1==length) {
        //
        // Can implicit relation node be built?
        //
        if (ptr->singleDeps())
          {
          int k = ptr->getLevelAbove(0);
          if(mxd_vars.find(k) != mxd_vars.end())
          { } else {
          long delta = ptr->termlist->term->getDelta();
          long en = ptr->termlist->term->getLower();
          long in = ptr->termlist->term->getUpper();
          if (in==0) in = -1;
            if(rnmap_level_to_enable_inh_del.find(k)!=rnmap_level_to_enable_inh_del.end()){
                std::vector<long> effects;
                for(int dh = 0; dh<rnmap_level_to_enable_inh_del[k].size();dh++)
                effects.push_back(rnmap_level_to_enable_inh_del[k][dh]);
                effects.push_back(delta);
                rnmap_level_to_enable_inh_del[k] = effects;
            } else {
              std::vector<long> effects;
              effects.push_back(en);
              effects.push_back(in);
              effects.push_back(delta);
              rnmap_level_to_enable_inh_del.insert(std::make_pair(k,effects));
            }
          }
          }
        //
        // awesomesauce
        //
        chunk = Share(ptr->termlist->term);
      }
    }


    //
    // Build relation_nodes
    //
    for (auto impl_it = rnmap_level_to_enable_inh_del.begin(); impl_it != rnmap_level_to_enable_inh_del.end(); impl_it++ )
      {
        int k = impl_it->first;
        long en,in,fr;
        int sz = impl_it->second.size();

        switch(sz) {
            case 1:   en = 0; in = -1; fr = impl_it->second[0]; break;
            case 2:   en = impl_it->second[0]; in = impl_it->second[1]; fr = 0; break;
            case 3:   en = impl_it->second[0]; in = impl_it->second[1]; fr = impl_it->second[2]; break;
            default:  en = 0; in = -1; fr = 0; break;
        }
        printf("\n level %d: rn = %d, %d, %d", k, en, fr, in);

	      if(!(en == 0 && in == -1 && fr == 0))
          {
            relnodes[rn] = new derive_relation_node(debug, c_pass, get_mxd_forest(), k, en, fr, in);
            rn++;
          }



      }

    /*auto impl_it = rnmap_level_to_enable_inh_del.begin();
    while(impl_it!=rnmap_level_to_enable_inh_del.end())
      {
        impl_it = rnmap_level_to_enable_inh_del.erase(impl_it);
      }*/




    //
    // Pull these together into the event
    //

    printf("\n Created %d rn and %d se", rn,se);
    if ((rn > 0) && (se > 0))
      T[i] = new sathyb_opname::event((sathyb_opname::subevent**)(&subevents[0]), se, (relation_node**)(&relnodes[0]), rn);
    else if (rn > 0)
      T[i] = new sathyb_opname::event(NULL, se, (relation_node**)(&relnodes[0]), rn);
    else
      T[i] = new sathyb_opname::event((sathyb_opname::subevent**)(&subevents[0]), se, NULL, rn);

  } // for i
  #endif
  sathyb_opname::hybrid_relation* HYB = new sathyb_opname::hybrid_relation(ms.getMddForest(), get_mxd_forest(), ms.getMddForest(), &T[0], nEvents);

  //
  // Return overall hybrid relation
  //
  printf("\n Hybrid IS READY\n");
  return HYB;
}

struct int_pair {
  int level;
  int value;
  int_pair(int k, int v) : level(k), value(v) {}
};


int getIndexOf(substate_colls* c_pass, int level, int tokens)
{
  DCASSERT(c_pass);
  const int sz = 1;
  int chunk[sz];
  chunk[0] = tokens;
  return c_pass->addSubstate(level, chunk, sz);
}

// disabling expressions for transitions:
// {
// (k_a:v_a, k_b:v_b, ...), // t1: k_a < v_a | k_b < v_b | ...
// (k_c:v_c, k_d:v_d, ...), // t2: k_c < v_c | k_d < v_d | ...
// ...
// }
//
// potential deadlock states = conjunction of transition disabling expressions
#ifdef USE_FRMDD_FOR_BUILDING_POTENTIAL_DEADLOCK_STATES
MEDDLY::dd_edge substate_varoption::buildPotentialDeadlockStates_IMPLICIT(debugging_msg &debug)
{
  using namespace MEDDLY;
  // exprman* em = getExpressionManager();
  // DCASSERT(em);
  substate_colls* c_pass = this->getSubstateStorage();
  DCASSERT(c_pass);
  forest* qrmdd = getMddForest();
  DCASSERT(qrmdd);

  forest::policies frmdd_policies(false);
  frmdd_policies.setFullyReduced();
  forest* frmdd = qrmdd->useDomain()->createForest(
      false,
      forest::BOOLEAN,
      forest::MULTI_TERMINAL,
      frmdd_policies);
  DCASSERT(frmdd);

  // for each level, k
  //    for each event, e, that starts at level k
  //      traverse relation nodes and build disabling expression for e
  //      add disabling expression to vector of disabling expressions
  //    end for
  //  end for
  //  call buildPotentialDeadlockStates()

  std::vector<std::vector<int_pair>> disabling_expressions;
  const int nEvents = getParent().getNumEvents();

  for (int i = 0; i < nEvents; i++) {
    if (!enable_deps[i]) continue;
    std::vector<int_pair> t;
    for (deplist *DL = enable_deps[i]; DL; DL=DL->next) {
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
        long enabling_tokens = DL->termlist->term->getLower();
        t.emplace_back(k, enabling_tokens);
      } // for k
    } // for enable_deps
    disabling_expressions.push_back(t);
  }

  dd_edge fr_result(frmdd);
  if (disabling_expressions.size() != 0) {

    // Build disabling XDDs per event
    std::map<int, std::vector<dd_edge>> disabling_ddedges;
    for (auto& i : disabling_expressions) {

      // i_union accumlates the disabling XDD for event i
      dd_edge i_union(frmdd);

      // for each variable j build a disabling XDD
      for (auto& j : i) {

        if (j.value == 0) continue;

        // Build one minterm for each value in [0, j.value)
        // Note: map each value to its index in the local state space of
        //        variable j.level
        const int nVars = frmdd->useDomain()->getNumVariables();
        int** minterms = new int*[j.value];
        for (int m = 0; m < j.value; m++) {
          minterms[m] = new int[nVars+1];
          for (int n = 0; n <= nVars; n++) {
            minterms[m][n] = MEDDLY::DONT_CARE;
          }
          minterms[m][j.level] = getIndexOf(c_pass, j.level, m);
        }

        // Combine minterms for this variable
        dd_edge expr(frmdd);
        frmdd->createEdge(minterms, j.value, expr);

#ifdef DEBUG_IS_REACHABLE
        std::cout << "\nExpr: 0 <= L" << j.level << " < " << j.value << ":\n";
        ostream_output s(std::cout);
        expr.show(s, 2);
        std::cout.flush();
#endif

        // Add to event's XDD
        i_union += expr;

        // Cleanup
        for (int m = 0; m < j.value; m++) delete [] minterms[m];
        delete [] minterms;

      } // per variable, j

#ifdef DEBUG_IS_REACHABLE
      std::cout << "\nDisabling dd_edge:\n";
      ostream_output s(std::cout);
      i_union.show(s, 2);
      std::cout.flush();
#endif

      // Save disabling XDD for event i (to be processed later)
      (disabling_ddedges[i_union.getLevel()]).push_back(i_union);

    } // per event, i

#if 1
#if 0
    bool first_time = true;
    for (auto& i : disabling_ddedges) {
      fprintf(stdout, "Level %d, Size %d\n", i.first, i.second.size());
      for (auto& j : i.second) {
        if (first_time) {
          fr_result = j;
          first_time = false;
        } else {
          fr_result *= j;
        }
      }
    }
#else
    for (auto& i : disabling_ddedges) {
      if (i.second.size() == 0) continue;
      fprintf(stdout, "Level %d, Size %lu\n", i.first, i.second.size());
      dd_edge conjunction = i.second[0];
      int counter = 1;
      for (auto& j : i.second) {
        fprintf(stdout, "\rProcessing edge %d / %lu", counter++, i.second.size());
        conjunction *= j;
      }
      fprintf(stdout, "\n");
      i.second.clear();
      i.second.push_back(conjunction);
    }
    bool first_time = true;
    int counter = 1;
    fprintf(stdout, "\nDone with intersections at each level. Combining %lu levels\n", disabling_ddedges.size());
    for (auto& i : disabling_ddedges) {
      if (i.second.size() == 0) continue;
      fprintf(stdout, "\rProcessing edge %d / %ld", counter++, disabling_ddedges.size());
      if (first_time) {
        first_time = false;
        fr_result = i.second[0];
      } else {
        fr_result *= i.second[0];
      }
    }
#endif
#else
    std::vector<dd_edge> v_disabling_edges;
    for (auto& i : disabling_ddedges) {
      for (auto& j : i.second) {
        v_disabling_edges.push_back(j);
      }
    }
    const int n_batches = 16;
    const int batch_size = v_disabling_edges.size() / n_batches;
    fprintf(stdout, "Batches %d, Batch size %d\n", n_batches, batch_size);
    std::vector<dd_edge> batch_result;
    bool is_zero = false;
    for (int i = 0; i < n_batches; i++) {
      fprintf(stdout, "Processing batch %d", i+1);
      const int start = i * batch_size;
      const int stop = (i+1) * batch_size;
      dd_edge i_result = v_disabling_edges[start];
      for (int j = start+1; j < stop; j++) {
        fprintf(stdout, "\rProcessing batch %d: %d / %d", i+1, j-start, batch_size);
        i_result *= v_disabling_edges[j];
      }
      fprintf(stdout, "\n");
      if (i_result.getNode() == 0) { fr_result = i_result; is_zero = true; break; }
      batch_result.push_back(i_result);
    }
    if (!is_zero) {
      const int start = n_batches * batch_size;
      if (start < v_disabling_edges.size()) {
        fprintf(stdout, "Processing remaining ddedges");
        dd_edge remaining_ddedges = v_disabling_edges[start];
        for (int j = start+1; j < v_disabling_edges.size(); j++) {
          fprintf(stdout, "\rProcessing remaining ddedges: %d / %d",
              j-start, v_disabling_edges.size()-start);
          remaining_ddedges *= v_disabling_edges[j];
        }
        batch_result[n_batches-1] *= remaining_ddedges;
        fprintf(stdout, "\n");
      }
      if (batch_result[n_batches-1] == 0) {
        fr_result = batch_result[n_batches-1]; is_zero = true;
      } else {
        fprintf(stdout, "Processing intermediates:");
        fr_result = batch_result[0];
        for (int i = 1; i < n_batches; i++) {
          fprintf(stdout, "\rProcessing intermediates: %d / %d", i, n_batches);
          fr_result *= batch_result[i];
        }
        fprintf(stdout, "\n");
      }
    }
#endif
  }

  // Return XDD representing the potential deadlock states
  dd_edge qr_result(qrmdd);
  apply(COPY, fr_result, qr_result);
  return qr_result;
}
#else
MEDDLY::dd_edge substate_varoption::buildPotentialDeadlockStates_IMPLICIT(debugging_msg &debug)
{
  using namespace MEDDLY;
  exprman* em = getExpressionManager();
  DCASSERT(em);
  substate_colls* c_pass = this->getSubstateStorage();
  DCASSERT(c_pass);
  forest* mdd = getMddForest();
  DCASSERT(mdd);

  // for each level, k
  //    for each event, e, that starts at level k
  //      traverse relation nodes and build disabling expression for e
  //      add disabling expression to vector of disabling expressions
  //    end for
  //  end for
  //  call buildPotentialDeadlockStates()

  std::vector<std::vector<int_pair>> disabling_expressions;
  const int nEvents = getParent().getNumEvents();

  for (int i = 0; i < nEvents; i++) {
    if (!enable_deps[i]) continue;
    std::vector<int_pair> t;
    for (deplist *DL = enable_deps[i]; DL; DL=DL->next) {
      int k = DL->getLevelAbove(0);
      for ( ; k>0; k=DL->getLevelAbove(k)) {
        long enabling_tokens = DL->termlist->term->getLower();
        t.emplace_back(k, enabling_tokens);
      } // for k
    } // for enable_deps
    disabling_expressions.push_back(t);
  }

  dd_edge result(mdd);
  if (disabling_expressions.size() != 0) {

    // Build disabling XDDs per event
    std::map<int, std::vector<dd_edge>> disabling_ddedges;
    for (auto& i : disabling_expressions) {

      // i_union accumlates the disabling XDD for event i
      dd_edge i_union(mdd);

      // for each variable j build a disabling XDD
      for (auto& j : i) {

        if (j.value == 0) continue;

        // Build one minterm for each value in [0, j.value)
        // Note: map each value to its index in the local state space of
        //        variable j.level
        const int nVars = mdd->useDomain()->getNumVariables();
        int** minterms = new int*[j.value];
        for (int m = 0; m < j.value; m++) {
          minterms[m] = new int[nVars+1];
          for (int n = 0; n <= nVars; n++) {
            minterms[m][n] = MEDDLY::DONT_CARE;
          }
          minterms[m][j.level] = getIndexOf(c_pass, j.level, m);
        }

        // Combine minterms for this variable
        dd_edge expr(mdd);
        mdd->createEdge(minterms, j.value, expr);

#ifdef DEBUG_IS_REACHABLE
        std::cout << "\nExpr: 0 <= L" << j.level << " < " << j.value << ":\n";
        ostream_output s(std::cout);
        expr.show(s, 2);
        std::cout.flush();
#endif

        // Add to event's XDD
        i_union += expr;

        // Cleanup
        for (int m = 0; m < j.value; m++) delete [] minterms[m];
        delete [] minterms;

      } // per variable, j

#ifdef DEBUG_IS_REACHABLE
      std::cout << "\nDisabling dd_edge:\n";
      ostream_output s(std::cout);
      i_union.show(s, 2);
      std::cout.flush();
#endif

      // Save disabling XDD for event i (to be processed later)
      (disabling_ddedges[i_union.getLevel()]).push_back(i_union);

    } // per event, i

    bool first_time = true;
    for (auto& i : disabling_ddedges) {
      // fprintf(stdout, "Level %d, Size %d\n", i.first, i.second.size());
      for (auto& j : i.second) {
        if (first_time) {
          result = j;
          first_time = false;
        } else {
          result *= j;
        }
      }
    }
  }

  // Return XDD representing the potential deadlock states
  return result;
}
#endif



void substate_varoption::initDomain(const exprman* em)
{
  //
  // Build the domain
  //
  if (!built_ok) return;

  const hldsm::partinfo& part = getParent().getPartInfo();
  num_levels = part.num_levels;
  variable** vars = new variable*[num_levels+1];
  vars[0] = 0;
  const int initial_var_bound = meddly_procgen::usesXdds()? -1 : 1 ;
  for (int k=num_levels; k; k--) {
    vars[k] = createVariable(initial_var_bound, buildVarName(part, k));
  } // for k
  if (!ms.createVars(vars, num_levels)) {
    built_ok = false;
    return;
  }

  from_minterm = new int[num_levels+1];
  to_minterm = new int[num_levels+1];
  from_minterm[0] = DONT_CARE;
  to_minterm[0] = DONT_CHANGE;
  for (int k=1; k<=num_levels; k++) {
    from_minterm[k] = DONT_CARE;
    to_minterm[k] = DONT_CHANGE;
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
  confirmed = new intset[num_levels+1];
  toBeExplored = new intset[num_levels+1];
}

void substate_varoption::initEncoders(const meddly_procgen &pg)
{
  if (!built_ok) return;

  //
  // Initialize MDD forest
  //
  forest* mdd = ms.createForest(
    false, forest::BOOLEAN, forest::MULTI_TERMINAL,
    pg.buildRSSPolicies()
  );
  DCASSERT(mdd);
  //
  // Initialize MxD forest
  //
  forest::policies frmxd_policies = pg.buildNSFPolicies();
  #ifndef TEST_HYB
  //frmxd_policies.setIdentityReduced();
  #else
    frmxd_policies.setFullyReduced();
  #endif
  forest* mxd = ms.createForest(
    true, forest::BOOLEAN, forest::MULTI_TERMINAL,
    frmxd_policies //pg.buildNSFPolicies()
  );
  DCASSERT(mxd);

  //
  // Build encoders
  //
  ms.setMddWrap(new substate_encoder("MDD", mdd, getParent(), Share(colls)));
  set_mxd_wrap(new substate_encoder("MxD", mxd, getParent(), Share(colls)));
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
      #ifndef TEST_HYB
      if (find->sameDeps(deps)) break;
      #else
      //check deplist for non-null intersecting dependencies to group subevents
      if (find->intersectDepsExist(deps))
        {
          find->unionWithDeps(deps);
          break;
        }
      #endif

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
::exploreEnabling(debugging_msg &d, deplist &dl, int k, const int* changed)
{
  DCASSERT(k>0);
  int ssz = tdcurr->readSubstateSize(k);
  int next_k = dl.getLevelAbove(k);

  //
  // Are we obligated to visit unexplored states only?
  //
  intset* toVisit = &(confirmed[k]);
  const int* next_changed = 0;
  if (changed) {
    if (k == changed[0]) {
      next_changed = changed+1;
    } else {
      next_changed = changed;
    }
    if (0==next_changed[0]) {
      // visit unexplored only
      toVisit = &(toBeExplored[k]);
    }
  }
  DCASSERT(toVisit);

  //
  // Start exploring.
  // Two cases - bottom level, and not there yet.
  //

  if (next_k > 0) {
    //
    // Not the bottom level; recurse
    //
    tdcurr->set_substate_known(k);
    for (int i = toVisit->getSmallestAfter(-1);
         i>=0;
         i = toVisit->getSmallestAfter(i))
    {
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;

      //
      // option for short circuiting here...
      //

      from_minterm[k] = i;
      exploreEnabling(d, dl, next_k, toBeExplored[k].contains(i) ? 0 : next_changed);
    } // for i
    from_minterm[k] = DONT_CARE;
    tdcurr->set_substate_unknown(k);
    return;
  }

  //
  // Bottom level
  // Explore as usual, but instead of recursing, add the minterm.
  //
  tdcurr->set_substate_known(k);
  for (int i = toVisit->getSmallestAfter(-1);
       i>=0;
       i = toVisit->getSmallestAfter(i))
  {
    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;

    from_minterm[k] = i;

    bool start_d = d.startReport();
    if (start_d) {
      d.report() << "enabled?\n\tstate ";
      tdcurr->Print(d.report(), 0);
      d.report() << "\n\tminterm [";
      d.report().PutArray(from_minterm+1, num_levels);
      d.report() << "]\n";
    }

    bool is_enabled = true;
    for (expr_node* n = dl.termlist; n; n=n->next) {
      n->term->Compute(td);

      if (start_d) {
        d.report() << "\t";
        n->term->Print(d.report(), 0);
        d.report() << " : ";
        if (td.answer->isNormal()) {
          if (td.answer->getBool())
            d.report() << "true";
          else
            d.report() << "false";
        } else if (td.answer->isUnknown())
          d.report() << "?";
        else
          d.report() << "null";
        d.report() << "\n";
        d.stopIO();
      }

      DCASSERT(td.answer->isNormal());
      if (td.answer->getBool()) continue;
      is_enabled = false;
      break;
    } // for n
    if (!is_enabled) continue;

    // enabled; add minterm to buffer
    if (!dl.addMinterm(from_minterm)) throw subengine::Out_Of_Memory;

  } // for i
  from_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
}


void substate_varoption
::exploreNextstate(debugging_msg &d, deplist &dl, int k, const int* changed)
{
  DCASSERT(k>0);
  int ssz = tdcurr->readSubstateSize(k);
  int next_k = dl.getLevelAbove(k);

  //
  // Are we obligated to visit unexplored states only?
  //
  intset* toVisit = &(confirmed[k]);
  const int* next_changed = 0;
  if (changed) {
    if (k == changed[0]) {
      next_changed = changed+1;
    } else {
      next_changed = changed;
    }
    if (0==next_changed[0]) {
      // visit unexplored only
      toVisit = &(toBeExplored[k]);
    }
  }
  DCASSERT(toVisit);

  //
  // Start exploring.
  // Two cases - bottom level, and not there yet.
  //

  if (next_k > 0) {
    //
    // Not the bottom level; recurse
    //
    tdcurr->set_substate_known(k);
    tdnext->set_substate_known(k);
    for (int i = toVisit->getSmallestAfter(-1);
         i>=0;
         i = toVisit->getSmallestAfter(i))
    {
      int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
      if (foo<0) throw subengine::Engine_Failed;
      colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

      //
      // option for short circuiting here...
      //

      from_minterm[k] = i;
      exploreNextstate(d, dl, next_k, toBeExplored[k].contains(i) ? 0 : next_changed);
    } // for i
    from_minterm[k] = DONT_CARE;
    tdcurr->set_substate_unknown(k);
    tdnext->set_substate_unknown(k);
    return;
  }

  //
  // Bottom level
  // Explore as usual, but instead of recursing, add the minterm.
  //

  tdcurr->set_substate_known(k);
  tdnext->set_substate_known(k);
  for (int i = toVisit->getSmallestAfter(-1);
       i>=0;
       i = toVisit->getSmallestAfter(i))
  {
    int foo = colls->getSubstate(k, i, tdcurr->writeSubstate(k), ssz);
    if (foo<0) throw subengine::Engine_Failed;
    colls->getSubstate(k, i, tdnext->writeSubstate(k), ssz);

    from_minterm[k] = i;

    bool start_d = d.startReport();
    if (start_d) {
      d.report() << "firing?\n\tstate ";
      tdcurr->Print(d.report(), 0);
      d.report() << "\n\tminterm [";
      d.report().PutArray(from_minterm+1, num_levels);
      d.report() << "]\n";
    }

    bool is_enabled = true;
    for (expr_node* n = dl.termlist; n; n=n->next) {
      n->term->Compute(td);

      if (start_d) {
        d.report() << "\t";
        n->term->Print(d.report(), 0);
        d.report() << " : ";
        if (td.answer->isNormal())
          d.report() << "ok";
        else if (td.answer->isUnknown())
          d.report() << "?";
        else if (td.answer->isOutOfBounds())
          d.report() << "out of bounds";
        else
          d.report() << "null";
        d.report() << "\n";
        d.stopIO();
      }

      if (td.answer->isNormal()) continue;
      is_enabled = false;
      break;
    } // for n
    if (!is_enabled) continue;

    // next state -> minterm
    //
    for (int kk = dl.getLevelAbove(0);
        kk>=0; kk = dl.getLevelAbove(kk)) {

      to_minterm[kk] = colls->addSubstate(kk,
          tdnext->readSubstate(kk), tdnext->readSubstateSize(kk)
      );

      if (to_minterm[kk]>=0) continue;
      if (-2==to_minterm[kk]) throw subengine::Out_Of_Memory;
      throw subengine::Engine_Failed;
    } // for kk

    //
    // Reporting
    //
    if (d.startReport()) {
      d.report() << "firing\n\tto state ";
      tdnext->Print(d.report(), 0);
      d.report() << "\n\tto minterm [";
      d.report().PutArray(to_minterm+1, num_levels);
      d.report() << "]\n";
      d.stopIO();
    }

    // add minterm to queue
    //
    if (!dl.addMinterm(from_minterm, to_minterm))
      throw subengine::Out_Of_Memory;

    // clear out minterm
    //
    for (int kk = dl.getLevelAbove(0);
        kk>=0; kk = dl.getLevelAbove(kk)) {

      to_minterm[kk] = DONT_CHANGE;
    } // for kk

  } // for i
  from_minterm[k] = DONT_CARE;
  tdcurr->set_substate_unknown(k);
  tdnext->set_substate_unknown(k);
  return;
}



void substate_varoption::updateLevels(debugging_msg &d, const int* levels)
{
  DCASSERT(tmpLevels);
  if (d.startReport()) {
    d.report() << "updating for levels: ";
    for (int p=0; levels[p]; p++) {
      if (p) {
        d.report() << ", ";
      }
      d.report() << levels[p];
    }
    d.report() << "\n";
    d.stopIO();
  }
  for (int i=0; i<getParent().getNumEvents(); i++) {
    const model_event* e = getParent().readEvent(i);
    if (d.startReport()) {
      d.report() << "updating event " << e->Name() << "\n";
      d.stopIO();
    }

    //
    // Enablings
    //
    bool new_enablings = false;
    for (deplist* ed = enable_deps[i]; ed; ed = ed->next) {
      ed->intersectLevels(levels, tmpLevels);
      if (0==tmpLevels[0]) continue;
      // need to explore these
      new_enablings = true;
      exploreEnabling(d, *ed, ed->getLevelAbove(0), tmpLevels);
    }

    //
    // Next state
    //
    bool new_firings = false;
    for (deplist* fd = fire_deps[i]; fd; fd = fd->next) {
      fd->intersectLevels(levels, tmpLevels);
      if (0==tmpLevels[0]) continue;
      // need to explore these
      new_firings = true;
      exploreNextstate(d, *fd, fd->getLevelAbove(0), tmpLevels);
    }

    if (d.startReport()) {
      if (new_firings || new_enablings) {
        d.report() << "event " << e->Name() << " has changed\n";
      } else {
        d.report() << "event " << e->Name() << " is unchanged\n";
      }
      d.stopIO();
    }

    if (!(new_firings || new_enablings)) continue;

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

    // TBD: update the edges from the minterms

  } // for i

  //
  // Done exploring.
  // Mark everything in these levels as explored.
  //
  for (int p=0; levels[p]; p++) {
    int k = levels[p];
    enlarge(toBeExplored[k], colls->getMaxIndex(k));
    toBeExplored[k].removeAll();
    enlarge(confirmed[k], colls->getMaxIndex(k));
  } // for k

#ifdef SHOW_SUBSTATES
  if (d.startReport()) {
    d.report() << "Done updating.  Current substates:\n";
    show_substates(d.report());
    d.stopIO();
  }
#endif
}

#ifdef SHOW_SUBSTATES
void substate_varoption::show_substates(OutputStream &s)
{
  for (int k=num_levels; k>0; k--) {
    s << "    Level " << k << " substates:\n";
    int ssz = tdcurr->readSubstateSize(k);
    tdcurr->set_substate_known(k);
    for (int i=0; i<colls->getMaxIndex(k); i++) {
      s << "\t";
      s.Put(long(i), 3);
      s.Put(confirmed[k].contains(i) ? 'c' : 'u');
      s.Put(toBeExplored[k].contains(i) ? '!' : ' ');
      s << ": ";
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
  pregen_varoption(meddly_reachset &x, const dsde_hlm &p,
    const exprman* em, const meddly_procgen &pg);

  virtual void updateEvents(debugging_msg &d, bool* cl);

  virtual bool hasChangedLevels(const dd_edge &s, bool* cl);
};

// **************************************************************************
// *                                                                        *
// *                        pregen_varoption methods                        *
// *                                                                        *
// **************************************************************************

pregen_varoption
::pregen_varoption(meddly_reachset &x, const dsde_hlm &p,
  const exprman* em, const meddly_procgen &pg)
: substate_varoption(x, p, em, pg)
{
}

void
pregen_varoption::updateEvents(debugging_msg &d, bool* cl)
{
  int* updated = new int[num_levels+1];

  for (int iter=1; ; iter++) {
    if (d.startReport()) {
      d.report() << "Pregenerating locals, iteration " << iter << "\n";
#ifdef SHOW_SUBSTATES
      d.report() << "Current substates:\n";
      show_substates(d.report());
#endif
      d.stopIO();
    }

    // What levels have been updated
    buildLevelsToExplore(updated);
    if (0==updated[0]) break;   // nothing

    // Adjust enablings, next-state functions for those levels
    updateLevels(d, updated);

    // set unconfirmed to be explored,
    // and confirm everything
    for (int k=num_levels; k>0; k--) {
      toBeExplored[k].addRange(0, colls->getMaxIndex(k)-1);
      toBeExplored[k] -= confirmed[k];
      confirmed[k].addRange(0, colls->getMaxIndex(k)-1);
    }
  }

  // Cleanup
  delete[] updated;
  throw subengine::Engine_Failed;
}

bool pregen_varoption::hasChangedLevels(const dd_edge &s, bool* cl)
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
  onthefly_varoption(meddly_reachset &x, const dsde_hlm &p,
    const exprman* em, const meddly_procgen &pg);

  virtual void updateEvents(debugging_msg &d, bool* cl);

  virtual bool hasChangedLevels(const dd_edge &s, bool* cl);
};

// **************************************************************************
// *                                                                        *
// *                       onthefly_varoption methods                       *
// *                                                                        *
// **************************************************************************

onthefly_varoption
::onthefly_varoption(meddly_reachset &x, const dsde_hlm &p,
  const exprman* em, const meddly_procgen &pg)
 : substate_varoption(x, p, em, pg)
{
}

void onthefly_varoption::updateEvents(debugging_msg &d, bool* cl)
{
  // throw subengine::Engine_Failed;
}

bool onthefly_varoption::hasChangedLevels(const dd_edge &s, bool* cl)
{
  return false;  // for now...
}

// **************************************************************************
// *                                                                        *
// *                ontheflyimplicit_varoption class                        *
// *                                                                        *
// **************************************************************************

class ontheflyimplicit_varoption : public substate_varoption {
public:
  ontheflyimplicit_varoption(meddly_reachset &x, const dsde_hlm &p,
                             const exprman* em, const meddly_procgen &pg);

  virtual void updateEvents(debugging_msg &d, bool* cl);

  virtual bool hasChangedLevels(const dd_edge &s, bool* cl);
};

// **************************************************************************
// *                                                                        *
// *                 ontheflyimplicit_varoption methods                       *
// *                                                                        *
// **************************************************************************

ontheflyimplicit_varoption
::ontheflyimplicit_varoption(meddly_reachset &x, const dsde_hlm &p,
                             const exprman* em, const meddly_procgen &pg)
: substate_varoption(x, p, em, pg)
{
}

void ontheflyimplicit_varoption::updateEvents(debugging_msg &d, bool* cl)
{
  // throw subengine::Engine_Failed;
}

bool ontheflyimplicit_varoption::hasChangedLevels(const dd_edge &s, bool* cl)
{
  return false;  // for now...
}

// **************************************************************************
// *                                                                        *
// *                         meddly_procgen methods                         *
// *                                                                        *
// **************************************************************************

unsigned meddly_procgen::proc_storage;
unsigned meddly_procgen::edge_style;
unsigned meddly_procgen::var_type;
unsigned meddly_procgen::nsf_ndp;
unsigned meddly_procgen::rss_ndp;
bool meddly_procgen::uses_xdds;

meddly_procgen::meddly_procgen()
{
  // Anything?
}

meddly_procgen::~meddly_procgen()
{
}

meddly_varoption*
meddly_procgen::makeBounded(const dsde_hlm &m, meddly_reachset &ms) const
{
  bounded_varoption *mvo = new bounded_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}


meddly_varoption*
meddly_procgen::makeExpanding(const dsde_hlm &m, meddly_reachset &ms) const
{
  // TBD
  return 0;
}

meddly_varoption*
meddly_procgen::makeOnTheFly(const dsde_hlm &m, meddly_reachset &ms) const
{
  onthefly_varoption *mvo = new onthefly_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}


meddly_varoption*
meddly_procgen::makePregen(const dsde_hlm &m, meddly_reachset &ms) const
{
  pregen_varoption *mvo = new pregen_varoption(ms, m, em, *this);
  if (!mvo->wasBuiltOK()) {
    delete mvo;
    return 0;
  }
  return mvo;
}

forest::policies
meddly_procgen::buildNSFPolicies() const
{
  forest::policies p(true);
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

forest::policies
meddly_procgen::buildRSSPolicies() const
{
  forest::policies p(false);
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


// ******************************************************************
// *                                                                *
// *                                                                *
// *                         Initialization                         *
// *                                                                *
// *                                                                *
// ******************************************************************

class init_genmeddly : public initializer {
  public:
    init_genmeddly();
    virtual bool execute();

  private:
    unsigned numNDPButtons();
    void addNDPButtons(option* o);
};
init_genmeddly the_genmeddly_initializer;

init_genmeddly::init_genmeddly() : initializer("init_genmeddly")
{
  usesResource("em");
  usesResource("engtypes");
  buildsResource("meddlyprocgen");
}

bool init_genmeddly::execute()
{
  if (0==em) return false;

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

  //
  // Option defaults
  meddly_procgen::proc_storage = meddly_procgen::MTMXD;
  meddly_procgen::edge_style = meddly_procgen::POTENTIAL;
  meddly_procgen::var_type = meddly_procgen::BOUNDED;
  meddly_procgen::nsf_ndp = meddly_procgen::PESSIMISTIC;
  meddly_procgen::rss_ndp = meddly_procgen::PESSIMISTIC;
  meddly_varoption::vars_named = false;

  // Build options
  if (em->OptMan()) {
    //
    // EVMxD vs MTMxD option
    option* mps = em->OptMan()->addRadioOption(
      "MeddlyProcessStorage",
      "Type of forest to use for process storage, using Meddly",
      2, meddly_procgen::proc_storage
    );

    mps->addRadioButton(
      "MTMXD",
      "Multi-terminal MDD for matrices",
      meddly_procgen::MTMXD
    );
    mps->addRadioButton(
      "EVMXD",
      "Edge-valued (using *) MDD for matrices",
      meddly_procgen::EVMXD
    );

    //
    // potential vs. actual edge option
    option* mpes = em->OptMan()->addRadioOption(
      "MeddlyProcessEdgeStyle",
      "Style for representing the underlying process, using Meddly",
      2, meddly_procgen::edge_style
    );

    mpes->addRadioButton(
      "ACTUAL",
      "Outgoing edges only for reachable states; unreachable states have no outgoing edges",
      meddly_procgen::ACTUAL
    );
    mpes->addRadioButton(
      "POTENTIAL",
      "Unreachable states may have outgoing edges",
      meddly_procgen::POTENTIAL
    );

    //
    // variable type option
    option* mvs = em->OptMan()->addRadioOption(
      "MeddlyVariableStyle",
      "Method for determining sizes of state variables, for Meddly-based process generation",
      4, meddly_procgen::var_type
    );
    mvs->addRadioButton(
      "BOUNDED",
      "All state variables have declared bounds",
      meddly_procgen::BOUNDED
    );
    mvs->addRadioButton(
      "EXPANDING",
      "State variable bounds are discovered during generation",
      meddly_procgen::EXPANDING
    );
    mvs->addRadioButton(
      "ON_THE_FLY",
      "Local state spaces are discovered during generation",
      meddly_procgen::ON_THE_FLY
    );
    mvs->addRadioButton(
      "PREGEN",
      "Local state spaces are generated in before generating reachability set",
      meddly_procgen::PREGEN
    );

    //
    // Node deletion policy options

    addNDPButtons(
      em->OptMan()->addRadioOption(
        "MeddlyNSFNodeDeletion",
        "Node deletion policy to use for next-state function forests in Meddly",
        numNDPButtons(), meddly_procgen::nsf_ndp
      )
    );

    addNDPButtons(
      em->OptMan()->addRadioOption(
        "MeddlyRSSNodeDeletion",
        "Node deletion policy to use for reachable state space forests in Meddly",
        numNDPButtons(), meddly_procgen::rss_ndp
      )
    );

    //
    // Variable names
    em->OptMan()->addBoolOption(
      "MeddlyVarsAreNamed",
      "Should the variables internal to MEDDLY be named appropriately.  If true, when MDDs and MxDs are displayed (typically for debugging), more useful information is shown for the level names.  Should be set to false for speed if possible.",
      meddly_varoption::vars_named
    );
  }

  return true;
}


unsigned init_genmeddly::numNDPButtons()
{
    return 3;
}

void init_genmeddly::addNDPButtons(option* o)
{
  o->addRadioButton(
    "NEVER",
    "Never delete nodes; useful for debugging",
    meddly_procgen::NEVER
  );
  o->addRadioButton(
    "OPTIMISTIC",
    "Nodes are marked for deletion and are recycled once they do not appear in the compute table; useful when nodes are likely to be re-used",
    meddly_procgen::OPTIMISTIC
  );
  o->addRadioButton(
    "PESSIMISTIC",
    "Nodes are recycled as soon as possible; useful when nodes are unlikely to be re-used, or to reduce memory",
    meddly_procgen::PESSIMISTIC
  );
}


//==========================================================================================
#else // not USING_OLD_MEDDLY_STUFF
//==========================================================================================

//==========================================================================================
#endif
//==========================================================================================

