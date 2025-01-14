
#include "mod_inst.h"
#include "mod_vars.h"
#include "mod_def.h"
#include "../Utils/strings.h"
#include "../Streams/streams.h"
#include "../Options/options.h"
#include "exprman.h"
#include "arrays.h"
#include "measures.h"
#include "engine.h"

// #define ARRAY_TRACE
// #define DEBUG_PARTINFO

// ******************************************************************
// *                                                                *
// *                         lldsm  methods                         *
// *                                                                *
// ******************************************************************

const exprman* lldsm::em = 0;

lldsm::lldsm(model_type t)
 : shared_object()
{
  type = t;
  parent = 0;
  next_phase = 0;
}

lldsm::~lldsm()
{
}

void lldsm::initOptions(exprman* om)
{
  em = om;
  if (0==om) return;

}

const char* lldsm::getNameOf(model_type t)
{
  switch (t) {
    case Unknown: return "Unknown";
    case Error:   return "Error";
    case RSS:     return "RSS";
    case FSM:     return "FSM";
    case DTMC:    return "DTMC";
    case CTMC:    return "CTMC";
    default:      return "unknown type code!";
  }
  // keep old compilers happy
  return "unknown type code!";
}

bool lldsm::Print(OutputStream &s, int) const
{
  s << "low-level model";
  return true;
}

bool lldsm::Equals(const shared_object* ptr) const
{
  return (ptr == this);
}

void lldsm::reportMemUsage(exprman* em, const char* prefix) const
{
}

long lldsm::bailOut(const char* sfile, unsigned sline, const char* why) const
{
  if (em->startInternal(sfile, sline)) {
    em->causedBy(0);
    em->internal() << why << " for low level model: ";
    em->internal() << getNameOf(Type());
    const char* cn = getClassName();
    if (cn) {
      em->internal() << " (class " << cn << ")";
    }
    em->stopIO();
  }
  return -2;
}

// ******************************************************************
// *                                                                *
// *                         hldsm  methods                         *
// *                                                                *
// ******************************************************************

const exprman* hldsm::em = 0;

hldsm::hldsm(model_type t) : shared_object()
{
  type = t;
  parent = 0;
  process = 0;
  part = 0;
}

hldsm::~hldsm()
{
  Delete(process);
  delete part;
}

void hldsm::initOptions(exprman* om)
{
  em = om;
}

bool hldsm::buildPartInfo()
{
  if (part) return true;
  engtype* VarOrder = em->findEngineType("VariableOrdering");
  if (0==VarOrder) return false;
  result dummy;
  VarOrder->runEngine(this, dummy);
  return part;
}

bool hldsm::Print(OutputStream &s, int) const
{
  s << "high-level model";
  return true;
}

bool hldsm::Equals(const shared_object* ptr) const
{
  return (ptr == this);
}

bool hldsm::StartWarning(const warning_msg &who, const expr* cause) const
{
    if (cause) {
        return who.startWarning(cause->Where());
    } else {
        return who.startWarning(location::NOWHERE());
    }
}

void hldsm::DoneWarning() const
{
  em->newLine();
  em->warn() << "within model ";
  if (Name()) em->warn() << Name();
  else        em->warn() << "(no name)";
  if (parent) {
    em->warn() << " instantiated " << parent->Where();
  }
  em->stopIO();
}

bool hldsm::StartError(const expr* cause) const
{
  if (!em->startError())  return false;
  em->causedBy(cause);
  return true;
}

void hldsm::OutOfBoundsError(const result &x) const
{
  if (!x.isOutOfBounds()) return;
  const model_statevar* mv
    = smart_cast <const model_statevar*> (x.getOutOfBounds());
  DCASSERT(mv);
  em->cerr() << ":";
  em->newLine();
  mv->printBoundsError(x);
}

void hldsm::SendError(const char* s) const
{
  em->cerr() << s;
}

void hldsm::SendRealError(const result &x) const
{
  DCASSERT(em->REAL);
  em->REAL->print(em->cerr(), x);
}

void hldsm::DoneError() const
{
  em->newLine();
  em->cerr() << "within model ";
  if (Name()) em->cerr() << Name();
  else        em->cerr() << "(no name)";
  if (parent) {
    em->cerr() << " instantiated " << parent->Where();
  }
  em->stopIO();
}

void hldsm::bailOut(const char* sfile, unsigned sline, const char* why) const
{
  if (em->startInternal(sfile, sline)) {
    em->causedBy(0);
    em->internal() << why << " for high level model ";
    if (parent) em->internal() << parent->Name();
    em->stopIO();
  }
}

// ******************************************************************
// *                        partinfo  methods                       *
// ******************************************************************

hldsm::partinfo::partinfo(model_statevar** vars, int NV)
{
  num_vars = NV;
  sort(vars);
}

hldsm::partinfo::~partinfo()
{
  delete[] pointer;
  delete[] variable;
}

void hldsm::partinfo::sort(model_statevar** vars)
{
  DCASSERT(vars);

#ifdef DEBUG_PARTINFO
  printf("Building partition info on input:\n");
  for (int i=0; i<num_vars; i++) {
    printf("\t%s index %d part %d\n", vars[i]->Name(), vars[i]->GetIndex(), vars[i]->GetPart());
  }
#endif

  // Determine number of levels
  num_levels = 1;
  for (int i=0; i<num_vars; i++) {
    num_levels = MAX(num_levels, vars[i]->GetPart());
  }

  pointer = new int[num_levels+1];
  variable = new model_statevar* [num_vars];

  // count variables per level
  for (int i=0; i<=num_levels; i++) pointer[i] = 0;
  for (int i=0; i<num_vars; i++) {
    DCASSERT(vars[i]);
    int lvl = vars[i]->GetPart();
    CHECK_RANGE(1, lvl, 1+num_levels);
    pointer[lvl]++;
  }
  // Accumulate
  for (int i=0; i<num_levels; i++) pointer[i+1] += pointer[i];
  DCASSERT(pointer[num_levels] == num_vars);

  // Fill ordered array of variables
  for (int i=0; i<num_vars; i++) variable[i] = 0;
  for (int i=0; i<num_vars; i++) {
    int lvl = vars[i]->GetPart();
    pointer[lvl]--;
    variable[pointer[lvl]] = vars[i];
  }

  // ok, now reset the pointers
  pointer[0] = -1;
  for (int i=0; i<num_vars; i++) {
    DCASSERT(variable[i]);
    int lvl = variable[i]->GetPart();
    CHECK_RANGE(1, lvl, num_levels+1);
    pointer[lvl] = i;
#ifdef DEVELOPMENT_CODE
    if (0==i) continue;
    if (variable[i]->GetPart() == variable[i-1]->GetPart())   continue;
    if (variable[i]->GetPart() == 1+variable[i-1]->GetPart()) continue;
    DCASSERT(0);
#endif
  }

  // set state variable index order according to partition order
  // such that top level variables have low index
  int inum = 0;
  for (int k=num_levels; k; k--) {
    for (int p=pointer[k]; p>pointer[k-1]; p--) {
      DCASSERT(k==variable[p]->GetPart());
      variable[p]->SetIndex(inum);
      inum++;
    }
  } // for k

  for (int i=0; i<num_vars; i++) {
    vars[i] = variable[i];
  }

#ifdef DEBUG_PARTINFO
  printf("Done; new parition info:\n");
  for (int i=0; i<num_vars; i++) {
    printf("\t%s index %d part %d\n", variable[i]->Name(), variable[i]->GetIndex(), variable[i]->GetPart());
  }
  printf("Raw pointer array: [%d", pointer[0]);
  for (int i=1; i<=num_levels; i++) printf(", %d", pointer[i]);
  printf("]\n");
#endif

}


// ******************************************************************
// *                                                                *
// *                     model_instance methods                     *
// *                                                                *
// ******************************************************************

model_instance::model_instance(const location &W, const model_def* dfn)
 : symbol(W, em->MODEL, 0)
{
  Rename(dfn->SharedName());
  SetModelType(dfn);

  state = Constructing;

  compiled =0;

  if (dfn) {
    num_symbols = dfn->NumSlots();
  } else {
    num_symbols = 0;
  }
  if (num_symbols>0)  stab = new symbol* [num_symbols];
  else                stab = 0;
  for (int i=0; i<num_symbols; i++)  stab[i] = 0;

  slist = 0;

  // Initialize the groups of measures
  num_groups = em->getNumEngineTypes();
  mgroups = new set_of_measures* [num_groups];
  for (int i=0; i<num_groups; i++) mgroups[i] = 0;
  for (int i=0; i<num_groups; i++) {
    const engtype* et = em->getEngineTypeNumber(i);
    int j = et->getIndex();
    CHECK_RANGE(0, j, num_groups);
    DCASSERT(0==mgroups[j]);
    mgroups[j] = et->makeMeasureSet();
  }

  num_accepted_msrs = 0;
}

model_instance::~model_instance()
{
  Deconstruct();
}

bool model_instance::StartWarning(const warning_msg &who, const expr* cause) const
{
    if (cause) {
        return who.startWarning(cause->Where());
    } else {
        return who.startWarning(location::NOWHERE());
    }
}

void model_instance::DoneWarning() const
{
  em->newLine();
  em->warn() << "within model " << Name() << " instantiated " << Where();
  em->stopIO();
}

bool model_instance::StartError(const expr* cause) const
{
  if (!em->startError())  return false;
  em->causedBy(cause);
  return true;
}

void model_instance::DoneError() const
{
  em->newLine();
  em->cerr() << "within model " << Name() << " instantiated " << Where();
  em->stopIO();
}


void model_instance::AcceptSymbolOwnership(symbol* a)
{
  DCASSERT(state == Constructing);
  DCASSERT(a);
  a->LinkTo(slist);
  slist = a;
  if (model_debug.startReport()) {
    model_debug.report() << "Model ";
    if (Name()) model_debug.report() << Name() << " ";
    model_debug.report() << "accepted symbol ";
    if (a->Name()) model_debug.report() << a->Name();
    model_debug.report() << "\n";
    model_debug.stopIO();
  }
}

void model_instance::AcceptExternalSymbol(int slot, symbol* s)
{
  DCASSERT(state == Constructing);
  CHECK_RANGE(0, slot, num_symbols);
  DCASSERT(0 == stab[slot]);
  stab[slot] = s;
}

void model_instance::AcceptMeasure(measure* m)
{
  if (0==m) return;

  ++num_accepted_msrs;
  m->SetOwner(this);

  StringStream cruft;
  cruft << Name() << "." << num_accepted_msrs;
  m->Rename(new shared_string(cruft.GetString()));

  if (model_debug.startReport()) {
    model_debug.report() << "Model ";
    if (Name()) model_debug.report() << Name() << " ";
    model_debug.report() << "accepted measure ";
    if (m->Name()) model_debug.report() << m->Name();
    model_debug.report() << "\n";
    model_debug.stopIO();
  }
}

void model_instance::GroupMeasure(measure* m)
{
  if (0==m)  return;
  if (model_debug.startReport()) {
    model_debug.report() << "Grouping measure ";
    if (m->Name()) model_debug.report() << m->Name() << " ";
    model_debug.report() << "within model ";
    if (Name()) model_debug.report() << Name();
    model_debug.report() << "\n";
    model_debug.stopIO();
  }
  // check status of m
  m->notifyFrom(0);

  // Put each measure into the appropriate group
  engtype* t = m->EngineType();
  DCASSERT(t);
  int et = t->getIndex();
  CHECK_RANGE(0, et, num_groups);
  if (0==mgroups[et])  return;  // no grouping
  mgroups[et]->addMeasure(m);
}

void model_instance::SolveMeasure(traverse_data &x, measure* m)
{
  DCASSERT(m);
  DCASSERT(em);
  DCASSERT(m->isReady());
  engtype* et = m->EngineType();
  DCASSERT(et);
  // handled by measure::Solve() directly:
  try {
    switch (et->getForm()) {
      case engtype::Single:
          if (model_debug.startReport()) {
            model_debug.report() << "Solving measure ";
            if (m->Name()) model_debug.report() << m->Name();
            model_debug.report() << "\n";
            model_debug.stopIO();
          }
          et->solveMeasure(compiled, m);
          return;

      case engtype::Grouped: {
          if (model_debug.startReport()) {
            model_debug.report() << "Solving group of measures";
            if (m->Name()) model_debug.report() << ", triggered by " << m->Name();
            model_debug.report() << "\n";
            model_debug.stopIO();
          }
          DCASSERT(mgroups[et->getIndex()]);
          et->solveMeasures(compiled, mgroups[et->getIndex()]);
          return;
      }

      default:
        // any other cases should already have been handled, or is an error
        DCASSERT(0);
        return;

    } // switch et->getForm()
  } // try
  catch (subengine::error ee) {
    switch (ee) {
      case subengine::Engine_Failed:
          if (!m->isComputed())  m->SetNull();
          break;  // any errors should have been reported already

      case subengine::No_Engine:
          m->SetNull();
          if (StartError(x.parent)) {
            em->cerr() << "Measure " << m->Name();
            em->cerr() << " could not be solved, no solution engine available";
            DoneError();
          }
        break;

      default:
          m->SetNull();
          if (em->startInternal(__FILE__, __LINE__)) {
            em->causedBy(0);
            em->internal() << subengine::getNameOfError(ee);
            em->internal() << " on measure ";
            em->internal() << m->Name();
            em->stopIO();
          }
    } // switch
  } // catch
}

void model_instance::SetConstructionSuccess(hldsm* model)
{
  DCASSERT(Constructing == state);

  compiled = model;
  compiled->SetParent(this);

  state = Ready;
}

void model_instance::SetConstructionError()
{
  state = Error;

  // cleanup?
}

void model_instance::Deconstruct()
{
  if (Deleted == state)  return;

  if (model_debug.startReport()) {
    model_debug.report() << "Deleting model ";
    const char* n = Name();
    if (n)  model_debug.report() << n << "\n";
    else    model_debug.report() << "null-name\n";
    model_debug.stopIO();
  }

  state = Deleted;

  for (int i=0; i<num_symbols; i++)
    Delete(stab[i]);
  delete[] stab;
  stab = 0;
  num_symbols = 0;

  while (slist) {
    symbol* next = slist->Next();
    Delete(next);
    slist = next;
  }

  Delete(compiled);
  compiled = 0;

  for (int i=0; i<num_groups; i++) delete mgroups[i];
  delete[] mgroups;
  num_groups = 0;
}

bool model_instance
::NotProperInstance(const expr* call, const char* who) const
{
  switch (state) {
    case Ready:
        return false;

    case Constructing:
        if (em->startError()) {
          em->causedBy(call);
          em->cerr() << "Model ";
          if (Name())  em->cerr() << Name() << " ";
          em->cerr() << "is still under construction";
          if (who)  em->cerr() << "; cannot compute " << who;
          em->stopIO();
        }
        return true;

    case Error:
        return true;

    case Deleted:
        if (em->startError()) {
          em->causedBy(call);
          em->cerr() << "Model ";
          if (Name())  em->cerr() << Name() << " ";
          em->cerr() << "has been deleted";
          if (who)  em->cerr() << "; cannot compute " << who;
          em->stopIO();
        }
        return true;
  } // switch
  if (em->startInternal(__FILE__, __LINE__)) {
    em->causedBy(call);
    em->internal() << "Bad state for model " << Name();
    em->stopIO();
  }
  return true;  // Just in case
}

//
// HIDDEN STUFF
//

// ******************************************************************
// *                                                                *
// *                       error_lldsm  class                       *
// *                                                                *
// ******************************************************************

class error_lldsm : public lldsm {
public:
  error_lldsm();
protected:
  virtual ~error_lldsm();
  const char* getClassName() const { return "error_lldsm"; }
};

error_lldsm::error_lldsm() : lldsm(Error)
{
}

error_lldsm::~error_lldsm()
{
}

// ******************************************************************
// *                                                                *
// *                         mi_call  class                         *
// *                                                                *
// ******************************************************************

/**  An expression to compute a model call.
     That is, this computes expressions of the type
        m.measure;
     where m is an expression of type MODEL.
*/

class mi_call : public expr {
protected:
  const model_def* parent;
  expr* mdl;
  int msr_slot;
public:
  mi_call(const location &W, const model_def* p, expr* m, int slot);
  virtual ~mi_call();

  virtual void Compute(traverse_data &x);
  virtual void Traverse(traverse_data &x);
  virtual bool Print(OutputStream &s, int) const;
};

mi_call::mi_call(const location &W, const model_def* p, expr* m, int slot)
  : expr (W, (typelist*) 0)
{
  parent = p;
  mdl = m;
  msr_slot = slot;
  const symbol* msr = parent->GetSymbol(msr_slot);
  SetType(em->SafeType(msr));
}

mi_call::~mi_call()
{
}

void mi_call::Compute(traverse_data &x)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  const expr* oldp = x.parent;
  x.parent = this;

  // Instantiate model as necessary
  SafeCompute(mdl, x);
  model_instance* mi = smart_cast <model_instance*> (x.answer->getPtr());
  symbol* find;
  if (0==mi) {
    find = 0;
  } else {
    if (mi->NotProperInstance(this, 0)) {
      find = 0;
    } else {
      find = mi->FindExternalSymbol(msr_slot);
    }
  }

  SafeCompute(find, x);
  x.parent = oldp;
}

void mi_call::Traverse(traverse_data &x)
{
  expr* newmdl;
  switch (x.which) {
    case traverse_data::Substitute:
        DCASSERT(x.answer);
        if (0==mdl) {
          x.answer->setNull();
          return;
        }
        mdl->Traverse(x);
        newmdl = smart_cast <expr*> (Share(x.answer->getPtr()));
        if (newmdl != mdl) {
          x.answer->setPtr(new mi_call(Where(), parent,
          newmdl, msr_slot));
        } else {
          x.answer->setPtr(Share(this));
        }
        return;

    default:
        expr::Traverse(x);
  }
}

bool mi_call::Print(OutputStream &s, int) const
{
  DCASSERT(mdl);
  if (!mdl->Print(s, 0)) {
    DCASSERT(0);
    return false;
  }
  s << ".";
  const symbol* msr = parent->GetSymbol(msr_slot);
  DCASSERT(msr);
  s << msr->Name();
  return true;
}


// ******************************************************************
// *                                                                *
// *                         mi_acall class                         *
// *                                                                *
// ******************************************************************

/**  An expression to compute a model call for an array measure.
     That is, this computes expressions of the type
        m.measure[i][j];
     where m is an expression of type MODEL.
*/

class mi_acall : public expr {
protected:
  // model call part
  const model_def* parent;
  expr* mdl;
  // measure call part
  int msr_slot;
  expr **indx;
  int numindx;
public:
  mi_acall(const location &W, const model_def* p, expr* m,
    int slot, expr **i, int ni);
  virtual ~mi_acall();

  virtual void Compute(traverse_data &x);
  virtual void Traverse(traverse_data &x);
  virtual bool Print(OutputStream &s, int) const;
};

mi_acall::mi_acall(const location &W, const model_def* p,
      expr* m, int slot, expr **i, int ni)
  : expr (W, (typelist*) 0)
{
  parent = p;
  mdl = m;
  msr_slot = slot;
  indx = i;
  numindx = ni;
  const symbol* msr = parent->GetSymbol(msr_slot);
  SetType(em->SafeType(msr));
}

mi_acall::~mi_acall()
{
  for (int i=0; i<numindx; i++)  Delete(indx[i]);
  delete[] indx;
}

void mi_acall::Compute(traverse_data &x)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  const expr* oldp = x.parent;
  x.parent = this;

  // Instantiate model as necessary
  SafeCompute(mdl, x);
  model_instance* mi = smart_cast <model_instance*> (x.answer->getPtr());
  array_item* elem;
  if (0==mi) {
    elem = 0;
  } else {
    if (mi->NotProperInstance(this, 0)) {
      elem = 0;
    } else {
      // find the measure array
      symbol* find = mi->FindExternalSymbol(msr_slot);
      if (find) {
        array* rewards = smart_cast <array*> (find);
        DCASSERT(rewards);
        // compute the array element.
        elem = rewards->GetItem(indx, *x.answer);
      } else {
        elem = 0;
      }
    }
  }
  if (elem) elem->Compute(x, false);
  else      x.answer->setNull();

  x.parent = oldp;
}

void mi_acall::Traverse(traverse_data &x)
{
  DCASSERT(mdl);
  switch (x.which) {
    case traverse_data::Substitute: {
        DCASSERT(x.answer);
        // build new model call part
        if (0==mdl) {
          x.answer->setNull();
          return;
        }
        mdl->Traverse(x);
        expr* newmdl = smart_cast <expr*> (Share(x.answer->getPtr()));
        bool notequal = (newmdl != mdl);

        // build new list of array indexes
        expr** newindx = new expr*[numindx];
        for (int i=0; i<numindx; i++) {
          if (indx[i]) {
            indx[i]->Traverse(x);
            newindx[i] = smart_cast <expr*> (Share(x.answer->getPtr()));
           } else {
            newindx[i] = 0;
          }
          if (newindx[i] != indx[i])  notequal = true;
         } // for i

        // build a new call
        if (notequal) {
          x.answer->setPtr(new mi_acall(Where(), parent,
                            newmdl, msr_slot, newindx, numindx));
        } else {
          for (int i=0; i<numindx; i++)  Delete(newindx[i]);
          delete[] newindx;
          x.answer->setPtr(Share(this));
        }
        return;
    } // Substitute

    default:
      expr::Traverse(x);
  }
}


bool mi_acall::Print(OutputStream &s, int) const
{
  DCASSERT(mdl);
  if (!mdl->Print(s, 0)) {
    DCASSERT(0);
    return false;
  }
  s << ".";
  const symbol* msr = parent->GetSymbol(msr_slot);
  DCASSERT(msr);
  s << msr->Name();
  for (int n=0; n<numindx; n++) {
    s << "[";
    if (indx[n])  indx[n]->Print(s, 0);
    else          s << "null";
    s << indx[n] << "]";
  }
  return true;
}


// ******************************************************************
// *                                                                *
// *                        exprman  methods                        *
// *                                                                *
// ******************************************************************

inline expr* Bailout(expr* ret, expr** p, int np)
{
  if (p) {
    for (int i=0; i<np; i++)  Delete(p[i]);
    delete[] p;
  }
  return ret;
}

const model_def* GrabModelType(const exprman* em, const location &W,
        bool want_array, const symbol* mi)
{
  if (0==mi) return 0;
  // first, check array status
  const array* foo = dynamic_cast <const array*> (mi);
  if (want_array != (foo != 0)) {
    if (em->startError()) {
      em->causedBy(W);
      em->cerr() << mi->Name();
      if (foo)  em->cerr() << " is an array";
      else      em->cerr() << " is not an array";
      em->stopIO();
    }
    return 0;
  }
  const model_def* p = mi->GetModelType();
  if (0==p) if (em->startError()) {
    em->causedBy(W);
    em->cerr() << mi->Name();
    if (foo)  em->cerr() << " does not appear to be an array of models";
    else      em->cerr() << " does not appear to be a model";
    em->stopIO();
  }
  return p;
}

int GrabMsrSlot(const exprman* em, const location &W, const model_def* p,
    symbol* mi, bool want_array, const char* msr_name)
{
  if (0==p || 0==msr_name) return -1;
  int slot = p->FindVisible(msr_name);
  if (slot < 0) {
    if (em->startError()) {
      em->causedBy(W);
      em->cerr() << "Measure " << msr_name;
      const array* foo = dynamic_cast <const array*> (mi);
      if (foo)  em->cerr() << " does not exist in model array ";
      else      em->cerr() << " does not exist in model ";
      em->cerr() << mi->Name();
      em->stopIO();
    }
    return slot;
  }

  const symbol* msr = p->GetSymbol(slot);
  DCASSERT(msr);
  const array* foo = dynamic_cast <const array*> (msr);
  bool is_array = (foo != 0);

  if (want_array != is_array) {
    if (em->startError()) {
      em->causedBy(W);
      em->cerr() << "Measure " << msr_name;
      const array* foo = dynamic_cast <const array*> (mi);
      if (foo)  em->cerr() << " in model array ";
      else      em->cerr() << " in model ";
      em->cerr() << mi->Name() << " is";
      if (is_array) em->cerr() << " is an array";
      else          em->cerr() << " is not an array";
      em->stopIO();
    }
    return -1;
  }
  return slot;
}

bool OkMsrArrayCall(const exprman* em, const location &W,
      const model_def* p, int slot, expr** pass, int np)
{
  if (0==p || slot<0) {
    if (pass) {
      for (int i=0; i<np; i++) Delete(pass[i]);
      delete[] pass;
    }
    return false;
  }
  const symbol* foo = p->GetSymbol(slot);
  const array* msr = smart_cast <const array*> (foo);
  DCASSERT(msr);
  bool ok = msr->checkArrayCall(W, pass, np);
  if (ok) return true;
  if (pass) {
    for (int i=0; i<np; i++) Delete(pass[i]);
    delete[] pass;
  }
  return false;
}


expr* exprman::makeMeasureCall(const location &W,
      symbol* mi, const char* msr_name) const
{
  const model_def* p = GrabModelType(this, W, false, mi);
  int slot = GrabMsrSlot(this, W, p, mi, false, msr_name);
  if (slot<0) return makeError();
  return new mi_call(W, p, mi, slot);
}


expr* exprman::makeMeasureCall(const location &W,
      symbol* mi, expr** indexes, int ni, const char* msr_name) const
{
  const model_def* p = GrabModelType(this, W, true, mi);
  int slot = GrabMsrSlot(this, W, p, mi, false, msr_name);
  expr* m;
  if (slot >= 0)  m = makeArrayCall(W, mi, indexes, ni);
  else            m = Bailout(makeError(), indexes, ni);
  if (!isOrdinary(m))  return m;
  return new mi_call(W, p, m, slot);
}


expr* exprman::makeMeasureCall(const location &W,
      symbol* mi, const char* msr_name, expr** indexes, int ni) const
{
  const model_def* p = GrabModelType(this, W, false, mi);
  int slot = GrabMsrSlot(this, W, p, mi, true, msr_name);
  if (OkMsrArrayCall(this, W, p, slot, indexes, ni))
    return new mi_acall(W, p, mi, slot, indexes, ni);
  else
    return makeError();
}

expr* exprman::makeMeasureCall(const location &W,
      symbol* mi, expr** i, int ni,
      const char* msr_name, expr** j, int nj) const
{
  const model_def* p = GrabModelType(this, W, true, mi);
  int slot = GrabMsrSlot(this, W, p, mi, true, msr_name);
  expr* m;
  if (slot >= 0)  m = makeArrayCall(W, mi, i, ni);
  else            m = Bailout(makeError(), i, ni);
  if (!isOrdinary(m))  return Bailout(m, j, nj);
  if (OkMsrArrayCall(this, W, p, slot, j, nj))
    return new mi_acall(W, p, m, slot, j, nj);
  else
    return makeError();
}


// ******************************************************************
// *                                                                *
// *                           Front  end                           *
// *                                                                *
// ******************************************************************

lldsm* MakeErrorModel()
{
  return new error_lldsm();
}

void InitLLM(exprman* om)
{
  lldsm::initOptions(om);
  hldsm::initOptions(om);
}
