
#include <limits.h>
#include "pn_form.h"
#include "../ExprLib/startup.h"
#include "../ExprLib/exprman.h"
#include "../Options/options.h"
#include "../ExprLib/formalism.h"

#include "../ExprLib/sets.h"
#include "../ExprLib/intervals.h"
#include "../ExprLib/mod_def.h"
#include "../ExprLib/mod_vars.h"
#include "../ExprLib/dd_front.h"
#include "../ExprLib/measures.h"

#include "dsde_hlm.h"

#include "../Formlsms/rss_meddly.h"

#include "basic_msr.h"


#include "../include/splay.h"

// #define DEBUG_PNS


// **************************************************************************
// *                                                                        *
// *                             place_sv class                             *
// *                                                                        *
// **************************************************************************

class place_sv : public model_statevar {
  long init;
  long upper;
public:
  place_sv(const symbol* w, const model_instance* pn);
protected:
  virtual ~place_sv();
public:
  inline long hasInit() const { return init > 0; }
  inline long hasUpper() const { return upper >= 0; }

  inline long getInit() const { return init; }

  inline void addInit(long i) { 
    DCASSERT(i>=0);
    init += i;
  }
  inline void addUpper(long u) {
    DCASSERT(u>=0);
    if (upper < 0)  upper = u;
    else            upper = MIN(upper, u);
  }

  /// Done making changes to this place.
  void Affix();
};

place_sv::place_sv(const symbol* w, const model_instance* pn)
 : model_statevar(w, pn, 0)
{
  init = 0;
  upper = -1;
}

place_sv::~place_sv()
{
}

void place_sv::Affix()
{
  if (upper > 0) SetBounds(MakeRangeSet(0, upper, 1));
  // TBD: check init <= upper
}

// **************************************************************************
// *                                                                        *
// *                            arc_entry  class                            *
// *                                                                        *
// **************************************************************************

/// Each transition maintains a collection of these.
class arc_entry {
  model_var* place;

  // for single arcs
  expr* input;
  expr* output;
  expr* inhibit;

  // for multiple arcs
  List <expr> *inputs;
  List <expr> *outputs;
  List <expr> *inhibits;

  // after we are "Compiled"
  expr* enabling;
  expr* firing;

  bool is_compiled;
public:
  arc_entry();
  ~arc_entry();

  inline void setPlace(model_var* p) {
    DCASSERT(0==input);
    DCASSERT(0==output);
    DCASSERT(0==inhibit);
    place = Share(p);
  }

  inline bool addInput(expr* x)   { return addWhere(x, input, inputs); }
  inline bool addOutput(expr* x)  { return addWhere(x, output, outputs); }
  inline bool addInhibit(expr* x) { return addWhere(x, inhibit, inhibits); }

  inline bool hasEnabling() const { return enabling;  }
  inline bool hasFiring() const   { return firing;  }

  inline expr* getEnabling() const  { return enabling;  }
  inline expr* getFiring() const    { return firing;  }

  inline int Compare(const model_var* p) const { 
    return SIGN(SafeID(place) - SafeID(p));
  }
  inline int Compare(const arc_entry* x) const {
    DCASSERT(x);
    return Compare(x->place);
  }

  void Compile(const exprman* em);
  void WriteDotArc(OutputStream &ds, void* tname) const;
protected:
  // true iff there was a duplicate
  bool addWhere(expr* x, expr* &a, List <expr>* & as);
  expr* makeSum(const exprman* em, List <expr>* &x);
};

// **************************************************************************
// *                           arc_entry  methods                           *
// **************************************************************************

arc_entry::arc_entry()
{
  place = 0;
  input = output = inhibit = 0;
  inputs = outputs = inhibits = 0;
  enabling = firing = 0;
  is_compiled = false;
}

arc_entry::~arc_entry()
{
  Delete(input);
  Delete(output);
  Delete(inhibit);
  Delete(place);
}

void arc_entry::Compile(const exprman* em)
{
  if (is_compiled) return;
  is_compiled = true;
  // build expressions as necessary for input, output, inhibit lists.
  if (inputs) {
    DCASSERT(0==input);
    input = makeSum(em, inputs);
  }
  if (outputs) {
    DCASSERT(0==output);
    output = makeSum(em, outputs);
  }
  if (inhibits) {
    DCASSERT(0==inhibit);
    inhibit = makeSum(em, inhibits);
  }

  if (input || inhibit) 
    enabling = MakeBleVltB(em, Share(input), Share(place), Share(inhibit));
  if (input || output)
    firing = MakeVarUpdate(em, Share(place), Share(input), Share(output));
}

void arc_entry::WriteDotArc(OutputStream &ds, void* t) const
{
  if (!ds.IsActive()) return;
  if (input) {
    ds << "\tp";
    ds.PutAddr(place);
    ds << " -> t";
    ds.PutAddr(t);
    ds << " [label=\"";
    input->Print(ds, 0);
    ds << "\"]\n";
  }
  if (output) {
    ds << "\tt";
    ds.PutAddr(t); 
    ds << " -> p";
    ds.PutAddr(place);
    ds << " [label=\"";
    output->Print(ds, 0);
    ds << "\"]\n";
  }
  if (inhibit) {
    ds << "\tp";
    ds.PutAddr(place);
    ds << " -> t";
    ds.PutAddr(t);
    ds << " [label=\"";
    inhibit->Print(ds, 0);
    ds << "\", arrowhead=odot]\n";
  }
  ds.can_flush();
}

bool arc_entry::addWhere(expr* x, expr* &a, List <expr>* & as)
{
  DCASSERT(!is_compiled);
  if (0==x) return false;
  if (0==as && 0==a) {
    a = x;
    return false;
  }
  // definitely duplicate
  if (0==as) as = new List <expr>;
  if (a) {
    as->Append(a);
    a = 0;
  }
  as->Append(x);
  return true;
}

expr* arc_entry::makeSum(const exprman* em, List <expr> * &x)
{
  DCASSERT(x);
  int nargs = x->Length();
  DCASSERT(nargs > 1);
  expr** args = x->CopyAndClear();
  delete x;
  x = 0;
  return em->makeAssocOp(0, -1, exprman::aop_plus, args, 0, nargs);
}

// **************************************************************************
// *                                                                        *
// *                            transition class                            *
// *                                                                        *
// **************************************************************************

/** Used to store information during model construction.
    Once the model has been finalized, most of this info
    goes away.
*/
class transition : public model_event {
  /// Data used during model construction
  struct extra_info {
    /// arcs touching this transition
    SplayOfPointers <arc_entry> *arclist;
    /// guard expressions
    List <expr>* guards;

  public:
    extra_info();
    ~extra_info();
  };
  /// 0 after model is finalized.
  extra_info* build_data;
public:
  transition(const symbol* wrapper, const model_instance* p);

  /// Returns true iff there was a duplicate arc.
  bool addInput(arc_entry* &tmp, expr* card);
  /// Returns true iff there was a duplicate arc.
  bool addOutput(arc_entry* &tmp, expr* card);
  /// Returns true iff there was a duplicate arc.
  bool addInhibit(arc_entry* &tmp, expr* card);
  /// Returns true iff there was another guard expression.
  bool addGuard(expr* guard);

  void Finalize(OutputStream &ds);
protected:
  inline arc_entry* UniqueInsert(arc_entry* &tmp) {
    DCASSERT(tmp);
    DCASSERT(build_data);
    if (0==build_data->arclist)
      build_data->arclist = new SplayOfPointers <arc_entry> (16, 0);
    arc_entry* find = build_data->arclist->Insert(tmp);
    if (find == tmp) tmp = 0;
    DCASSERT(find);
    return find;
  }
};

// **************************************************************************
// *                           transition methods                           *
// **************************************************************************

transition::extra_info::extra_info()
{
  arclist = 0;
  guards = 0;
}

transition::extra_info::~extra_info()
{
  if (arclist) arclist->DeleteAndClear();
  delete arclist;
  delete guards;
}

transition::transition(const symbol* wrapper, const model_instance* p)
 : model_event(wrapper, p)
{
  build_data = new extra_info;
}

bool transition::addInput(arc_entry* &tmp, expr* card)
{
  arc_entry* find = UniqueInsert(tmp);
  DCASSERT(card);
  return find->addInput(card);
}

bool transition::addOutput(arc_entry* &tmp, expr* card)
{
  arc_entry* find = UniqueInsert(tmp);
  DCASSERT(card);
  return find->addOutput(card);
}

bool transition::addInhibit(arc_entry* &tmp, expr* card)
{
  arc_entry* find = UniqueInsert(tmp);
  DCASSERT(card);
  return find->addInhibit(card);
}

bool transition::addGuard(expr* guard)
{
  DCASSERT(guard);
  DCASSERT(build_data);
  bool answer = true;
  if (0==build_data->guards) {
    build_data->guards = new List <expr>;
    answer = false;
  }
  build_data->guards->Append(guard);
  return answer;
}

void transition::Finalize(OutputStream &ds)
{
  ds << "\tt";
  ds.PutAddr(this);
  ds << " [shape=box, label=\"" << Name() << "\"];\n";
  DCASSERT(build_data);
  int num_guards = (build_data->guards) ? build_data->guards->Length() : 0;
  int num_enable = num_guards;
  int num_fire = 0;
  if (build_data->arclist) {
    for (int i=0; i<build_data->arclist->NumElements(); i++) {
      arc_entry* a = build_data->arclist->GetItem(i);
      DCASSERT(a);
      a->Compile(em);
      a->WriteDotArc(ds, this);
      if (a->hasEnabling())  num_enable++;
      if (a->hasFiring())  num_fire++;
    } // for i
    expr** enablist = num_enable ? new expr*[num_enable] : 0;
    expr** firelist = num_fire   ? new expr*[num_fire]   : 0;
    int eptr = 0;
    int fptr = 0;
    for (int i=0; i<build_data->arclist->NumElements(); i++) {
      arc_entry* a = build_data->arclist->GetItem(i);
      DCASSERT(a);
      if (a->getEnabling()) {
        DCASSERT(enablist);
        enablist[eptr] = a->getEnabling();
        eptr++;
      }
      if (a->getFiring()) {
        DCASSERT(firelist);
        firelist[fptr] = a->getFiring();
        fptr++;
      }
    } // for i
    // add guards (if any) to enabling list
    for (int i=0; i<num_guards; i++) {
      expr* tmp = build_data->guards->Item(i);
      DCASSERT(tmp);
      CHECK_RANGE(0, eptr, num_enable);
      enablist[eptr++] = tmp;
    }    
    if (eptr==1)  setEnabling(enablist[0]);
    if (fptr==1)  setNextstate(firelist[0]);
    if (eptr > 1) {
      setEnabling(em->makeAssocOp(0, -1, exprman::aop_and, enablist, 0, eptr));
    } else {
      delete[] enablist;
    }
    if (fptr > 1) {
      setNextstate(em->makeAssocOp(0, -1, exprman::aop_semi, firelist, 0, fptr));
    } else {
      delete[] firelist;
    }
  }
#ifdef DEBUG_PNS
  em->cout() << "Finalized " << Name() << "\n";
  em->cout() << "\tEnabling: ";
  if (isEnabled()) isEnabled()->Print(em->cout(), 0);
  else em->cout() << "null";
  em->cout() << "\n\t  Firing: ";
  if (getNextstate()) getNextstate()->Print(em->cout(), 0);
  else em->cout() << "null";
  em->cout() << "\n";
#endif
  delete build_data;
  build_data = 0;
}

// **************************************************************************
// *                                                                        *
// *                            petri_hlm  class                            *
// *                                                                        *
// **************************************************************************

class petri_hlm : public dsde_hlm {
protected:
  static int MarkingStyle;
  static const int INDEXED = 0;
  static const int SAFE    = 1;
  static const int SPARSE  = 2;
  static const int VECTOR  = 3;

  friend class init_pnform;
public:
  petri_hlm(const model_instance* s, place_sv** P, int np, model_event** T, int nt);
  virtual ~petri_hlm();

  // required for hldsm:
  virtual void showState(OutputStream &s, const shared_state* x) const;
  static inline void showTokens(OutputStream &s, bool un, int tk) {
    if (un) s.Put('?');
    else    s.Put(tk);
  }

  // required for dsde_hlm:
  virtual int NumInitialStates() const;
  virtual double GetInitialState(int n, shared_state* s) const;
};

int petri_hlm::MarkingStyle;

// ******************************************************************
// *                       petri_hlm  methods                       *
// ******************************************************************

petri_hlm::petri_hlm(const model_instance* s, place_sv** P, int np, model_event** T, int nt)
 : dsde_hlm(s, (model_statevar**)P, np, T, nt)
{
}

petri_hlm::~petri_hlm()
{
}

void petri_hlm::showState(OutputStream &s, const shared_state* st) const
{
  DCASSERT(st);

  bool printed = false;
  int i;
  s << '[';
  for (i=0; i<num_vars; i++) {
    bool un = st->unknown(state_data[i]->GetIndex());
    int tk = un ? 1 : st->get(state_data[i]->GetIndex());

    if (0==tk && (SPARSE == MarkingStyle || SAFE == MarkingStyle))
      continue;

    if (printed) s << ", ";
    printed = true;

    switch (MarkingStyle) {
      case VECTOR:
          showTokens(s, un, tk);
          continue;

      case SAFE:
          if (1==tk) {
            s << state_data[i]->Name();
            continue;
          }
          // no continue

      case INDEXED:
      case SPARSE:
          s << state_data[i]->Name() << ":";
          showTokens(s, un, tk);
          continue;

      default:
          if (em->startInternal(__FILE__, __LINE__)) {
            em->noCause();
            em->internal() << "Unknown marking style " << MarkingStyle;
            em->stopIO();
          }
    } // switch
  } // for i

  s << ']';
}

int petri_hlm::NumInitialStates() const
{
  return 1;
}

double petri_hlm::GetInitialState(int n, shared_state* st) const
{
  DCASSERT(0==n);
  DCASSERT(st);
  for (int i=0; i<num_vars; i++) {
    const place_sv* pl = smart_cast <place_sv*> (state_data[i]);
    DCASSERT(pl);
    int ndx = pl->GetIndex();
    CHECK_RANGE(0, ndx, st->getStateSize());
    st->set(ndx, pl->getInit());
  } // for i
  return 1.0;
}

// **************************************************************************
// *                                                                        *
// *                            petri_def  class                            *
// *                                                                        *
// **************************************************************************

/// Smart support for the Petri net formalism.
class petri_def : public dsde_def {
  bool error;

  symbol* places;
  int num_places;
  symbol* transitions;
  int num_trans;

  List <expr> *assertion_list;

  static const type* place_type;
  static const type* trans_type;

  static expr* ONE;
  static arc_entry* tmp_arc;

  static named_msg pn_debug;
  static named_msg dup_init;
  static named_msg dup_bound;
  static named_msg dup_arc;
  static named_msg dup_guard;
  static named_msg dup_fire;
  static named_msg dup_weight;
  static named_msg no_trans;
  static named_msg no_place;
  static named_msg no_init;
  static named_msg no_fire;
  static named_msg no_weight;
  static named_msg zero_init;
  static named_msg zero_bound;

  friend class init_pnform;

  int weight_class;
public:
  petri_def(const char* fn, int line, const type* t, char*n, 
      formal_param **pl, int np);

  virtual ~petri_def();

  // Required for models:
  virtual model_var* MakeModelVar(const symbol* wrap, shared_object* bnds);

  // For model construction:
  void AddInit(const expr* call, model_var* pl, int tokens);
  void AddBound(const expr* call, shared_set* pset, int upper);
  void AddInput(const expr* call, model_var* pl, transition* t, expr* card);
  void AddOutput(const expr* call, transition* t, model_var* pl, expr* card);
  void AddInhibitor(const expr* call, model_var* pl, transition* t, expr* card);
  void AddGuard(const expr* call, transition* t, expr* guard);
  void HideTransition(const expr* call, transition* t);
  void AddFiring(const expr* call, transition* t, expr* dist);
  inline int NewWeightClass() { return ++weight_class; }
  void AddWeight(const expr* call, transition* t, expr* wt, int wc);
  void AddAssertion(expr* a);
  

protected:
  virtual void InitModel();
  virtual void FinalizeModel(OutputStream &ds);
};

const type* petri_def::place_type;
const type* petri_def::trans_type;
expr* petri_def::ONE = 0;
arc_entry* petri_def::tmp_arc = 0;

named_msg petri_def::pn_debug;
named_msg petri_def::dup_init;
named_msg petri_def::dup_bound;
named_msg petri_def::dup_arc;
named_msg petri_def::dup_guard;
named_msg petri_def::dup_fire;
named_msg petri_def::dup_weight;
named_msg petri_def::no_trans;
named_msg petri_def::no_place;
named_msg petri_def::no_init;
named_msg petri_def::no_fire;
named_msg petri_def::no_weight;
named_msg petri_def::zero_init;
named_msg petri_def::zero_bound;

// ******************************************************************
// *                       petri_def  methods                       *
// ******************************************************************

petri_def::petri_def(const char* fn, int line, const type* t, 
   char*n, formal_param **pl, int np) : dsde_def(fn, line, t, n, pl, np)
{
  error = 0;
}

petri_def::~petri_def()
{
}

model_var* petri_def::MakeModelVar(const symbol* wrap, shared_object* bnds)
{
  if (error) return 0;
  DCASSERT(wrap);
  DCASSERT(0==bnds);

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding ";
    pn_debug.report() << wrap->Type()->getName();
    pn_debug.report() << " " << wrap->Name() << "\n";
    pn_debug.stopIO();
  }

  if (wrap->Type() == place_type) {
    model_statevar* v = new place_sv(wrap, current);
    v->SetIndex(num_places);
    num_places++;
    v->LinkTo(places);
    places = v;
    return v;
  }

  if (wrap->Type() == trans_type) {
    DCASSERT(0==bnds);
    model_var* v = new transition(wrap, current);
    num_trans++;
    v->LinkTo(transitions);
    transitions = v;
    return v;
  }

  return 0;
}

void petri_def::AddInit(const expr* call, model_var* v, int tokens)
{
  DCASSERT(v);

  if (!isVariableOurs(v, call, "ignoring initial tokens")) return;

  place_sv* pl = smart_cast <place_sv*> (v);
  DCASSERT(pl);

  if (0==tokens) {
    if (StartWarning(zero_init, call)) {
      em->warn() << "Ignoring initialization: zero tokens for place ";
      em->warn() << pl->Name();
      DoneWarning();
    }
    return;
  }

  if (tokens < 0) {
    StartError(call);
    em->cerr() << "Bad value: " << tokens << " for initialization of place ";
    em->cerr() << pl->Name();
    DoneError();
    return;
  }
  
  if (pn_debug.startReport()) {
    pn_debug.report() << "adding " << tokens << " tokens to place ";
    pn_debug.report() << pl->Name() << " in initial marking\n";
    pn_debug.stopIO();
  }

  if (pl->hasInit()) {
    if (StartWarning(dup_init, call)) {
      em->warn() << "Summing duplicate initialization for place " << pl->Name();
      DoneWarning();
    }
  }
  pl->addInit(tokens);
}

void petri_def::AddBound(const expr* call, shared_set* pset, int upper)
{
  DCASSERT(pset);

  if (upper < 1) {
    if (StartWarning(zero_bound, call)) {
      em->warn() << "Ignoring upper bound of " << upper;
      em->warn() << " tokens for places ";
      pset->Print(em->warn(), 0);
      DoneWarning();
    }
    return;
  }

  result elem;
  for (int z=0; z<pset->Size(); z++) {
    pset->GetElement(z, elem);
    DCASSERT(elem.isNormal());
    place_sv* pl = smart_cast <place_sv*> (elem.getPtr());
    DCASSERT(pl);  
    if (!isVariableOurs(pl, call, "ignoring bound")) continue;

    if (pn_debug.startReport()) {
      pn_debug.report() << "setting " << upper;
      pn_debug.report() << " upper bound on tokens in place ";
      pn_debug.report() << pl->Name() << "\n";
      pn_debug.stopIO();
    }

    if (pl->hasUpper()) {
      if (StartWarning(dup_bound, call)) {
        em->warn() << "Duplicate bound for place " << pl->Name();
        em->warn() << ", taking smaller";
        DoneWarning();
      }
    }
    pl->addUpper(upper);
  } // for z
}

void petri_def
::AddInput(const expr* call, model_var* pl, transition* t, expr* card)
{
  DCASSERT(pl);
  DCASSERT(t);
  if (!isVariableOurs(pl, call, "ignoring arc")) return;
  if (!isVariableOurs(t, call, "ignoring arc")) return;
  if (0==card) card = Share(ONE);

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding   input   arc ";
    pn_debug.report() << pl->Name() << " : " << t->Name() << " : ";
    card->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  if (0==tmp_arc) tmp_arc = new arc_entry;
  tmp_arc->setPlace(pl);
  bool dup = t->addInput(tmp_arc, card);

  if (dup) if (StartWarning(dup_arc, call)) {
    // Duplicate entry, give warning
    em->warn() << "Summing cardinalities on duplicate arc";
    em->newLine();
    em->warn() << "from " << pl->Name() << " to " << t->Name();
    DoneWarning();
  }
}

void petri_def
::AddOutput(const expr* call, transition* t, model_var* pl, expr* card)
{
  DCASSERT(pl);
  DCASSERT(t);
  if (!isVariableOurs(t, call, "ignoring arc")) return;
  if (!isVariableOurs(pl, call, "ignoring arc")) return;
  if (0==card) card = Share(ONE);

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding  output   arc ";
    pn_debug.report() << t->Name() << " : " << pl->Name() << " : ";
    card->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  if (0==tmp_arc) tmp_arc = new arc_entry;
  tmp_arc->setPlace(pl);
  bool dup = t->addOutput(tmp_arc, card);

  if (dup) if (StartWarning(dup_arc, call)) {
    // Duplicate entry, give warning
    em->warn() << "Summing cardinalities on duplicate arc";
    em->newLine();
    em->warn() << "from " << t->Name() << " to " << pl->Name();
    DoneWarning();
  }
}

void petri_def
::AddInhibitor(const expr* call, model_var* pl, transition* t, expr* card)
{
  DCASSERT(pl);
  DCASSERT(t);
  if (!isVariableOurs(pl, call, "ignoring arc")) return;
  if (!isVariableOurs(t, call, "ignoring arc")) return;
  if (0==card) card = Share(ONE);

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding inhibitor arc ";
    pn_debug.report() << pl->Name() << " : " << t->Name() << " : ";
    card->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  if (0==tmp_arc) tmp_arc = new arc_entry;
  tmp_arc->setPlace(pl);
  bool dup = t->addInhibit(tmp_arc, card);

  if (dup) if (StartWarning(dup_arc, call)) {
    // Duplicate entry, give warning
    em->warn() << "Summing cardinalities on duplicate arc";
    em->newLine();
    em->warn() << "from " << pl->Name() << " to " << t->Name();
    DoneWarning();
  }
}

void petri_def::AddGuard(const expr* call, transition* t, expr* guard)
{
  DCASSERT(t);
  DCASSERT(guard);
  if (!isVariableOurs(t, call, "ignoring guard")) return;

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding guard ";
    pn_debug.report() << t->Name() << " : ";
    guard->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  bool dup = t->addGuard(guard);

  if (dup) if (StartWarning(dup_guard, call)) {
    em->warn() << "Merging guards on transition " << t->Name();
    DoneWarning();
  }
}

void petri_def::HideTransition(const expr* call, transition* t)
{
  DCASSERT(t);
  if (!isVariableOurs(t, call, "ignoring hide")) return;

  if (pn_debug.startReport()) {
    pn_debug.report() << "hiding transition " << t->Name() << "\n";
    pn_debug.stopIO();
  }

  if (!t->hasFiringType(model_event::Unknown)) {
    if (StartWarning(dup_fire, call)) {
      em->warn() << "Ignoring duplicate firing/hiding assignment ";
      em->warn() << "for transition " << t->Name();
      DoneWarning();
    }
    return;
  }

  t->setHidden();
}

void petri_def::AddFiring(const expr* call, transition* t, expr* dist)
{
  DCASSERT(t);
  DCASSERT(dist);
  if (!isVariableOurs(t, call, "ignoring firing distribution")) return;

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding firing ";
    pn_debug.report() << t->Name() << " : ";
    dist->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  if (!t->hasFiringType(model_event::Unknown)) {
    if (StartWarning(dup_fire, call)) {
      em->warn() << "Ignoring duplicate firing/hiding assignment ";
      em->warn() << "for transition " << t->Name();
      DoneWarning();
    }
    return;
  }  

  // get range of values for dist
  DCASSERT(dist);
  traverse_data x(traverse_data::FindRange);
  result range;
  x.answer = &range;
  dist->Traverse(x);
  shared_object* so = range.getPtr();
  DCASSERT(so);
  interval_object* io = smart_cast <interval_object*> (so);
  DCASSERT(io);

  // Check for negative support
  if (io->Left().getSign()<0) {
    if (StartError(call)) {
      em->cerr() << "Firing distribution for transition ";
      em->cerr() << t->Name();
      em->cerr() << " has negative support";
      DoneError();
    }
    return;
  }

  // Check for 0 distribution; super easy thanks to range info :^)
  if (0==io->Right().getSign()) {
    Delete(dist);
    t->setImmediate();
    return;
  } 

  // still here?  not immediate.
  t->setTimed(dist);
}

void petri_def::AddWeight(const expr* call, transition* t, expr* wt, int wc)
{
  DCASSERT(t);
  DCASSERT(wt);
  if (!isVariableOurs(t, call, "ignoring weight")) return;

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding weight ";
    pn_debug.report() << "( class " << wc << ") ";
    pn_debug.report() << t->Name() << " : ";
    wt->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  if (t->getWeightClass()) {
    if (StartWarning(dup_weight, call)) {
      em->warn() << "Ignoring duplicate weight assignment for transition ";
      em->warn() << t->Name();
      DoneWarning();
    }
    return;
  }  

  t->setWeight(wc, wt);
}

void petri_def::AddAssertion(expr* a)
{
  DCASSERT(a);
  DCASSERT(assertion_list);
  expr* mya = a->Substitute(0);
  mya->PreCompute();

  if (pn_debug.startReport()) {
    pn_debug.report() << "adding assertion ";
    mya->Print(pn_debug.report(), 0);
    pn_debug.report().Put('\n');
    pn_debug.stopIO();
  }

  assertion_list->Append(mya);
}

void petri_def::InitModel()
{
  dsde_def::InitModel();
  error = false;
  places = 0;
  transitions = 0;
  num_places = num_trans = 0;
  weight_class = 0;
  assertion_list = new List <expr>;
}

void petri_def::FinalizeModel(OutputStream &ds)
{
  // "compile" the places
  if (0==num_places) {
    if (StartWarning(no_place, 0)) {
      em->warn() << "No places defined";
      DoneWarning();
    }
  } 

  // move places from list into array
  place_sv** parray = num_places ? new place_sv*[num_places] : 0;
  bool has_init = false;
  for (int i=num_places-1; i>=0; i--) {
    parray[i] = smart_cast <place_sv*> (places);
    DCASSERT(parray[i]);
    places = places->Next();
    parray[i]->LinkTo(0);
    if (parray[i]->hasInit()) has_init = true;
    parray[i]->Affix();
  }
  DCASSERT(0==places);

  if (!has_init) if (StartWarning(no_init, 0)) {
    em->warn() << "No initial marking given, assuming zero";
    DoneWarning();
  }

  PartitionVars((model_statevar**) parray, num_places);

  ds << "digraph pn {\n";
  for (int i=0; i<num_places; i++) {
    model_statevar* p = smart_cast <model_statevar*> (parray[i]);
    ds << "\tp";
    ds.PutAddr(p);
    ds << " [shape=circle, label=\"" << p->Name() << "\"];\n";
    ds.can_flush();
  }


  // "compile" the transitions
  model_event** elist;
  if (0==num_trans) {
    elist = 0;
    if (StartWarning(no_trans, 0)) {
      em->warn() << "No transitions defined";
      DoneWarning();
    }
  } else {
    elist = new model_event*[num_trans];
    transition* t = smart_cast <transition*> (transitions);
    for (int i=num_trans-1; i>=0; i--) {
      DCASSERT(t);
      t->Finalize(ds);
      elist[i] = t;
      t = smart_cast <transition*> (t->Next());
    } // for i
  }

  // transitions: anything with no "firing" becomes non-deterministic
  bool has_timed = false;
  bool has_immed = false;
  bool without_wt = false;
  bool has_undef = false;
  for (int i=0; i<num_trans; i++) {
    DCASSERT(elist[i]);
    switch (elist[i]->getFiringType()) {
      case model_event::Expo:
      case model_event::Phase_int:
      case model_event::Phase_real:
      case model_event::Timed_general:
          has_timed = true;
          continue;

      case model_event::Immediate:
          has_immed = true;
          if (0==elist[i]->getWeight()) without_wt = true;
          continue;

      case model_event::Nondeterm:
          DCASSERT(0);

      case model_event::Hidden:
          continue;

      default:
          has_undef = true;
          elist[i]->setNondeterministic();
    }
  } // for i
  if (has_undef && (has_timed || has_immed)) if (StartWarning(no_fire, 0)) {
    em->warn() << "No firing distributions given for transitions:";
    em->newLine(1);
    em->warn() << "{";
    bool printed = false;
    for (int i=0; i<num_trans; i++) 
      if (elist[i]->hasFiringType(model_event::Nondeterm)) {
        if (printed) em->warn() << ", ";
        em->warn() << elist[i]->Name();
        printed = true;
    }
    em->warn() << "}";
    em->changeIndent(-1);
    DoneWarning();
  }
  if (without_wt) if (StartWarning(no_weight, 0)) {
    em->warn() << "No weight given for immediate transitions:";
    em->newLine(1);
    em->warn() << "{";
    bool printed = false;
    for (int i=0; i<num_trans; i++) 
      if (elist[i]->hasFiringType(model_event::Immediate))
        if (0==elist[i]->getWeight()) {
          if (printed) em->warn() << ", ";
          em->warn() << elist[i]->Name();
          printed = true;
        }
    em->warn() << "}";
    em->changeIndent(-1);
    DoneWarning();
  }

  ds << "}\n";

  petri_hlm* build = new petri_hlm(current, parray, num_places, elist, num_trans); 

  // add assertions, if any
  long na = assertion_list->Length();
  build->setAssertions(assertion_list->CopyAndClear(), na);

  ConstructionSuccess(build);
}

// **************************************************************************
// *                                                                        *
// *                         petri_formalism  class                         *
// *                                                                        *
// **************************************************************************

class petri_formalism : public formalism {
public:
  petri_formalism(const char* n, const char* sd, const char* ld);

  virtual model_def* makeNewModel(const char* fn, int ln, char* name,
          symbol** formals, int np) const;

  virtual bool canDeclareType(const type* vartype) const;
  virtual bool canAssignType(const type* vartype) const;

  virtual bool includeCTL() const { return true; }
  virtual bool includeStochastic() const { return true; }
};

// ******************************************************************
// *                    petri_formalism  methods                    *
// ******************************************************************


petri_formalism
::petri_formalism(const char* n, const char* sd, const char* ld)
 : formalism(n, sd, ld)
{
}

model_def* petri_formalism::makeNewModel(const char* fn, int ln, char* name, 
          symbol** formals, int np) const
{
  // TBD: check formals?
  return new petri_def(fn, ln, this, name, (formal_param**) formals, np);
}

bool petri_formalism::canDeclareType(const type* vartype) const
{
  if (0==vartype)                 return 0;
  if (vartype->matches("place"))  return 1;
  if (vartype->matches("trans"))  return 1;
  return 0;
}

bool petri_formalism::canAssignType(const type* vartype) const
{
  return 0;
}

// **************************************************************************
// *                                                                        *
// *                          Petri net  Functions                          *
// *                                                                        *
// **************************************************************************

// ********************************************************
// *                    pn_init  class                    *
// ********************************************************

class pn_init : public model_internal {
public:
  pn_init();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_init::pn_init() : model_internal(em->VOID, "init", 2)
{
  typelist* t = new typelist(2);
  t->SetItem(0, em->findType("place"));
  t->SetItem(1, em->INT);
  SetFormal(1, t, "p:n");
  SetRepeat(1);
  SetDocumentation("Sets the number of tokens for place p to n in the initial marking.");
}

void pn_init::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    x.aggregate = 0;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());

    result second;
    x.answer = &second;
    x.aggregate = 1;
    SafeCompute(pass[i], x);
    if (! second.isNormal() || second.getInt() < 0) {
      mdl->StartError(pass[i]);
      em->cerr() << "Bad token value: ";
      em->INT->print(em->cerr(), second, 0);
      em->cerr() << " for token initialization, ignoring";
      mdl->DoneError();
      continue;
    }

    model_statevar* pl = smart_cast <model_statevar*> (first.getPtr());
    DCASSERT(pl);
    mdl->AddInit(pass[i], pl, second.getInt());
  }
  x.answer = answer;
  x.aggregate = 0;
}

// ********************************************************
// *                    pn_bound class                    *
// ********************************************************

class pn_bound : public model_internal {
public:
  pn_bound();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_bound::pn_bound() : model_internal(em->VOID, "bound", 2)
{
  typelist* t = new typelist(2);
  const type* place = em->findType("place");
  DCASSERT(place);
  t->SetItem(0, place->getSetOfThis());
  t->SetItem(1, em->INT);
  SetFormal(1, t, "pset:n");
  SetRepeat(1);
  SetDocumentation("For each place p in set pset, fix the largest number of tokens that can appear in p to n.");
}

void pn_bound::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);

    result second;
    x.answer = &second;
    x.aggregate = 1;
    SafeCompute(pass[i], x);
    if (! second.isNormal() || second.getInt() < 0) {
      mdl->StartError(pass[i]);
      em->cerr() << "Bad token value: ";
      em->INT->print(em->cerr(), second, 0);
      em->cerr() << " for place bound, ignoring";
      mdl->DoneError();
      continue;
    }

    result first;
    x.answer = &first;
    x.aggregate = 0;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    shared_set* ps = smart_cast <shared_set*> (first.getPtr());
    mdl->AddBound(pass[i], ps, second.getInt());
  }
  x.answer = answer;
  x.aggregate = 0;
}

// ********************************************************
// *                    pn_arcs  class                    *
// ********************************************************

class pn_arcs : public custom_internal {
  const type* PLACE;
  const type* TRANS;
public:
  pn_arcs();
  virtual void Compute(traverse_data &x, expr** pass, int np);
  virtual int Traverse(traverse_data &x, expr** pass, int np);
protected:
  int Typecheck(expr** pass, int np) const;
  int Promote(expr** pass, int np) const;
};

pn_arcs::pn_arcs()
 : custom_internal("arcs", "void arcs(..., arc, ...)")
{
  SetDocumentation("Adds arcs to a Petri net.  Input arcs are specified using \"place:trans:card\", where the cardinality <card> has type proc int, or using \"place:trans\" for cardinalty of one.  Output arcs are specified using \"trans:place:card\", or \"trans:place\" for cardinality of one.");
  PLACE = em->findType("place");
  TRANS = em->findType("trans");
  DCASSERT(PLACE);
  DCASSERT(TRANS);
}

void pn_arcs::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    x.aggregate = 0;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());

    result second;
    x.answer = &second;
    x.aggregate = 1;
    SafeCompute(pass[i], x);
    DCASSERT(second.isNormal());

    // TBD: add error checking of first and second here   

    model_var* pl;
    transition* t;
    expr* card = 0;
    if (pass[i]->NumComponents()==3) card = pass[i]->Substitute(2);
    if (pass[i]->Type(0) == PLACE) {
      pl = smart_cast <model_var*> (first.getPtr());
      t = smart_cast <transition*> (second.getPtr());
      mdl->AddInput(pass[i], pl, t, card);
      continue;
    }
    if (pass[i]->Type(0) == TRANS) {
      t = smart_cast <transition*> (first.getPtr());
      pl = smart_cast <model_var*> (second.getPtr());
      mdl->AddOutput(pass[i], t, pl, card);
      continue;
    }
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(pass[i]);
      em->internal() << "Bad parameter for pn function arcs\n";
      em->stopIO();
    }
  }
  x.answer = answer;
  x.aggregate = 0;
}

int pn_arcs::Traverse(traverse_data &x, expr** pass, int np)
{
  switch (x.which) {
    case traverse_data::GetType:  
        x.the_type = em->VOID;
        return 0;

    case traverse_data::Typecheck:
        return Typecheck(pass, np);

    case traverse_data::Promote:
        return Promote(pass, np);

    default:
        return custom_internal::Traverse(x, pass, np);
  }
}

int pn_arcs::Typecheck(expr** pass, int np) const
{
  if (np<2)    return NotEnoughParams(np);

  if ((0==pass[0]) || (pass[0]->NumComponents() > 1) 
       || !em->isPromotable(pass[0]->Type(), em->MODEL)) 
  return BadParam(0, np); 

  for (int i=1; i<np; i++) {
    if (0==pass[i] || pass[i]->NumComponents()<2 || pass[i]->NumComponents()>3)
      return BadParam(i, np);

    bool ok1 = (pass[i]->Type(0)==PLACE) && (pass[i]->Type(1)==TRANS);
    bool ok2 = (pass[i]->Type(0)==TRANS) && (pass[i]->Type(1)==PLACE);
    if (!(ok1 || ok2)) 
      return BadParam(i, np);

    // check cardinality, if it is there
    if (pass[i]->NumComponents()==2) continue;

    if (!em->isPromotable(pass[i]->Type(2), em->INT->addProc()))
      return BadParam(i, np);
  } // for i
  return 0;
}

int pn_arcs::Promote(expr** pass, int np) const
{
  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    // check cardinality, if it is there
    if (pass[i]->NumComponents()==2) continue;
    expr* card = Share(pass[i]->GetComponent(2));
    expr* picard = em->promote(card, em->INT->addProc());
    if (card == picard) {
      Delete(picard);
      continue;
    }
    expr** newagg = new expr*[3];
    newagg[0] = Share(pass[i]->GetComponent(0));
    newagg[1] = Share(pass[i]->GetComponent(1));
    newagg[2] = picard;
    expr* newpass = em->makeAssocOp(pass[i]->Filename(), pass[i]->Linenumber(),
        exprman::aop_colon, newagg, 0, 3);
    Delete(pass[i]);
    pass[i] = newpass;
  } // for i
  return Promote_Success;
}


// ********************************************************
// *                   pn_inhibit class                   *
// ********************************************************

class pn_inhibit : public custom_internal {
  const type* PLACE;
  const type* TRANS;
public:
  pn_inhibit();
  virtual void Compute(traverse_data &x, expr** pass, int np);
  virtual int Traverse(traverse_data &x, expr** pass, int np);
protected:
  int Typecheck(expr** pass, int np) const;
  int Promote(expr** pass, int np) const;
};

pn_inhibit::pn_inhibit()
 : custom_internal("inhibit", "void inhibit(..., arc, ...)")
{
  SetDocumentation("Adds inhibitor arcs to a Petri net.  Arcs are specified using \"place:trans:card\", where the cardinality <card> has type proc int, or using \"place:trans\" for cardinalty of one.");
  PLACE = em->findType("place");
  TRANS = em->findType("trans");
  DCASSERT(PLACE);
  DCASSERT(TRANS);
}

void pn_inhibit::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    x.aggregate = 0;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());

    result second;
    x.answer = &second;
    x.aggregate = 1;
    SafeCompute(pass[i], x);
    DCASSERT(second.isNormal());

    // TBD: add error checking of first and second here   

    model_var* pl;
    transition* t;
    expr* card = 0;
    if (pass[i]->NumComponents()==3) card = pass[i]->Substitute(2);
    pl = smart_cast <model_var*> (first.getPtr());
    t = smart_cast <transition*> (second.getPtr());
    mdl->AddInhibitor(pass[i], pl, t, card);
  }
  x.answer = answer;
  x.aggregate = 0;
}

int pn_inhibit::Traverse(traverse_data &x, expr** pass, int np)
{
  switch (x.which) {
    case traverse_data::GetType:  
        x.the_type = em->VOID;
        return 0;

    case traverse_data::Typecheck:
        return Typecheck(pass, np);

    case traverse_data::Promote:
        return Promote(pass, np);

    default:
        return custom_internal::Traverse(x, pass, np);
  }
}

int pn_inhibit::Typecheck(expr** pass, int np) const
{
  if (np<2)    return NotEnoughParams(np);

  if ((0==pass[0]) || (pass[0]->NumComponents() > 1) 
       || !em->isPromotable(pass[0]->Type(), em->MODEL)) 
          return BadParam(0, np); 

  for (int i=1; i<np; i++) {
    if (0==pass[i] || pass[i]->NumComponents()<2 || pass[i]->NumComponents()>3)
      return BadParam(i, np);

    bool ok1 = (pass[i]->Type(0)==PLACE) && (pass[i]->Type(1)==TRANS);
    if (!ok1) 
      return BadParam(i, np);

    // check cardinality, if it is there
    if (pass[i]->NumComponents()==2) continue;

    if (!em->isPromotable(pass[i]->Type(2), em->INT->addProc()))
      return BadParam(i, np);
  } // for i
  return 0;
}

int pn_inhibit::Promote(expr** pass, int np) const
{
  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    // check cardinality, if it is there
    if (pass[i]->NumComponents()==2) continue;
    expr* card = Share(pass[i]->GetComponent(2));
    expr* picard = em->promote(card, em->INT->addProc());
    if (card == picard) {
      Delete(picard);
      continue;
    }
    expr** newagg = new expr*[3];
    newagg[0] = Share(pass[i]->GetComponent(0));
    newagg[1] = Share(pass[i]->GetComponent(1));
    newagg[2] = picard;
    expr* newpass = em->makeAssocOp(pass[i]->Filename(), pass[i]->Linenumber(),
        exprman::aop_colon, newagg, 0, 3);
    Delete(pass[i]);
    pass[i] = newpass;
  } // for i
  return Promote_Success;
}

// ********************************************************
// *                    pn_guard class                    *
// ********************************************************

class pn_guard : public model_internal {
public:
  pn_guard();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_guard::pn_guard() : model_internal(em->VOID, "guard", 2)
{
  typelist* t = new typelist(2);
  const type* trans = em->findType("trans");
  t->SetItem(0, trans->getSetOfThis());
  t->SetItem(1, em->BOOL->addProc());
  SetFormal(1, t, "tset:b");
  SetRepeat(1);
  SetDocumentation("For each transition t in the set tset, adds guard b on transition t (t cannot fire if b is false).");
}

void pn_guard::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    shared_set* tset = smart_cast <shared_set*> (first.getPtr());
    DCASSERT(tset);
    expr* guard = pass[i]->Substitute(1);

    for (int z=0; z<tset->Size(); z++) {
      result tr;
      tset->GetElement(z, tr);
      transition* t = smart_cast <transition*> (tr.getPtr());
      DCASSERT(t);
      mdl->AddGuard(pass[i], t, guard);
    }
  }
  x.answer = answer;
}

// ********************************************************
// *                   pn_firing  class                   *
// ********************************************************

class pn_firing : public custom_internal {
  const type* TRANS;
public:
  pn_firing();
  virtual void Compute(traverse_data &x, expr** pass, int np);
  virtual int Traverse(traverse_data &x, expr** pass, int np);
protected:
  int Typecheck(expr** pass, int np) const;
};

pn_firing::pn_firing()
 : custom_internal("firing", "void firing(..., trans:dist t:d, ...)")
{
  SetDocumentation("Sets the firing distribution of transition t to d.");
  TRANS = em->findType("trans");
  DCASSERT(TRANS);
}

void pn_firing::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    transition* t = smart_cast <transition*> (first.getPtr());
    expr* firing = pass[i]->Substitute(1);
    mdl->AddFiring(pass[i], t, firing);
  }
  x.answer = answer;
}

int pn_firing::Traverse(traverse_data &x, expr** pass, int np)
{
  switch (x.which) {
    case traverse_data::GetType:  
        x.the_type = em->VOID;
        return 0;

    case traverse_data::Typecheck:
        return Typecheck(pass, np);

    case traverse_data::Promote:
        return Promote_Success;

    default:
        return custom_internal::Traverse(x, pass, np);
  }
}

int pn_firing::Typecheck(expr** pass, int np) const
{
  if (np<2)    return NotEnoughParams(np);

  if ((0==pass[0]) || (pass[0]->NumComponents() > 1) 
       || !em->isPromotable(pass[0]->Type(), em->MODEL)) 
          return BadParam(0, np); 

  for (int i=1; i<np; i++) {
    if (0==pass[i] || pass[i]->NumComponents()!=2)
      return BadParam(i, np);

    if (pass[i]->Type(0) != TRANS)
      return BadParam(i, np);

    // check distribution
    const simple_type* bt = pass[i]->Type(1)->getBaseType();
    DCASSERT(bt);

    if (bt == em->INT)    continue;
    if (bt == em->REAL)   continue;
    if (bt == em->EXPO)   continue;

    return BadParam(i, np);
  } // for i
  return 0;
}

// ********************************************************
// *                   pn_weight  class                   *
// ********************************************************

class pn_weight : public model_internal {
public:
  pn_weight();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_weight::pn_weight() : model_internal(em->VOID, "weight", 2)
{
  typelist* t = new typelist(2);
  t->SetItem(0, em->findType("trans"));
  t->SetItem(1, em->REAL->addProc());
  SetFormal(1, t, "t:w");
  SetRepeat(1);
  SetDocumentation("Sets weight w on transition t, all transitions for a single call to weight will be in the same weight class.  If two or more transitions try to fire at the same time, then their weights are used to probabilistically choose which one fires, but only if the transitions are in the same weight class.  If they are in different weight classes, an error occurs.");
}

void pn_weight::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  int wc = mdl->NewWeightClass();
  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    transition* t = smart_cast <transition*> (first.getPtr());
    expr* wt = pass[i]->Substitute(1);
    mdl->AddWeight(pass[i], t, wt, wc);
  }
  x.answer = answer;
}

// ********************************************************
// *                   pn_weight2 class                   *
// ********************************************************

class pn_weight2 : public model_internal {
public:
  pn_weight2();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_weight2::pn_weight2() : model_internal(em->VOID, "weight", 3)
{
  const type* trans = em->findType("trans");  DCASSERT(trans);
  SetFormal(1, trans, "c");
  typelist* t = new typelist(2);
  t->SetItem(0, trans);
  t->SetItem(1, em->REAL->addProc());
  SetFormal(2, t, "t:w");
  SetRepeat(2);
  SetDocumentation("Sets weight w on transition t, and put transition t in the same weight class as transition c (which has already been assigned a weight).  If two or more transitions try to fire at the same time, then their weights are used to probabilistically choose which one fires, but only if the transitions are in the same weight class.  If they are in different weight classes, an error occurs.");
}

void pn_weight2::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  // Get the weight class
  DCASSERT(pass[1]);
  result tres;
  x.answer = &tres;
  pass[1]->Compute(x);
  DCASSERT(tres.isNormal());
  transition* t = smart_cast <transition*> (tres.getPtr());
  DCASSERT(t);
  int wc = t->getWeightClass();
  if (0==wc) {
    if (mdl->StartError(pass[1])) {
      em->cerr() << "Transition " << t->Name();
      em->cerr() << " has no weight class yet";
      mdl->DoneError();
    }
    wc = mdl->NewWeightClass();
  }

  for (int i=2; i<np; i++) {
    DCASSERT(pass[i]);
    result first;
    x.answer = &first;
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    transition* t = smart_cast <transition*> (first.getPtr());
    expr* wt = pass[i]->Substitute(1);
    mdl->AddWeight(pass[i], t, wt, wc);
  }
  x.answer = answer;
}

// ********************************************************
// *                   pn_assert  class                   *
// ********************************************************

class pn_assert : public model_internal {
public:
  pn_assert();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_assert::pn_assert() : model_internal(em->VOID, "assert", 2)
{
  SetFormal(1, em->BOOL->addProc(), "b");
  SetRepeat(1);
  SetDocumentation("Define a set of assertions that must be true in each marking.  An error message will be displayed if an assertion does not evaluate to true in some marking.");
}

void pn_assert::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  for (int i=1; i<np; i++) {
    if (0==pass[i]) continue;
    mdl->AddAssertion(pass[i]);
  }
  x.answer = answer;
}

// ********************************************************
// *                    pn_hide  class                    *
// ********************************************************

class pn_hide : public model_internal {
public:
  pn_hide();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_hide::pn_hide() : model_internal(em->VOID, "hide", 2)
{
  const type* trans = em->findType("trans");
  DCASSERT(trans);
  SetFormal(1, trans, "t");
  SetRepeat(1);
  SetDocumentation("Hide transition t.  Hidden transitions are similar to immediate ones, but are untimed and non-deterministic.");
}

void pn_hide::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(pass);
  DCASSERT(pass[0]);
  petri_def* mdl = smart_cast<petri_def*>(pass[0]);
  DCASSERT(mdl);
  
  if (x.stopExecution())  return;
  result* answer = x.answer;

  result first;
  x.answer = &first;
  for (int i=1; i<np; i++) {
    DCASSERT(pass[i]);
    SafeCompute(pass[i], x);
    DCASSERT(first.isNormal());
    transition* t = smart_cast <transition*> (first.getPtr());
    DCASSERT(t);
    mdl->HideTransition(pass[i], t);
  }
  x.answer = answer;
}

// ********************************************************
// *                     pn_tk  class                     *
// ********************************************************

class pn_tk : public model_internal {
public:
  pn_tk();
  virtual void Compute(traverse_data &x, expr** pass, int np);
  virtual int Traverse(traverse_data &x, expr** pass, int np);
};

pn_tk::pn_tk() : model_internal(em->INT->addProc(), "tk", 2)
{
  const type* place = em->findType("place");
  SetFormal(1, place, "p");
  SetDocumentation("The number of tokens in place p (in the current state of the Petri net).");
}

void pn_tk::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(x.current_state);
  DCASSERT(np==2);
  // Get place from second parameter
  SafeCompute(pass[1], x);
  if (!x.answer->isNormal())  return;

  DCASSERT(x.answer->isNormal());
  model_var* place = smart_cast <model_var*> (x.answer->getPtr());
  DCASSERT(place);

  place->ComputeInState(x);
}

int pn_tk::Traverse(traverse_data &x, expr** pass, int np)
{
  if (x.which != traverse_data::BuildDD) {
    return model_internal::Traverse(x, pass, np);
  }
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(np==2);
  DCASSERT(x.ddlib);

  // Get place from second parameter
  SafeCompute(pass[1], x);
  if (!x.answer->isNormal())  return 0;

  DCASSERT(x.answer->isNormal());
  symbol* pl = smart_cast <symbol*> (x.answer->getPtr());
  DCASSERT(pl);

  shared_object* pldd = x.ddlib->makeEdge(0);
  DCASSERT(pldd);
  x.ddlib->buildSymbolicSV(pl, false, 0, pldd);
  x.answer->setPtr(pldd);
  return 0;
}

// ********************************************************
// *                    pn_rate  class                    *
// ********************************************************

class pn_rate : public model_internal {
public:
  pn_rate();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_rate::pn_rate() : model_internal(em->REAL->addProc(), "rate", 2)
{
  const type* trans = em->findType("trans");
  SetFormal(1, trans, "t");
  SetDocumentation("In the current marking, if t is disabled, then 0; if t is enabled, the firing rate of t.  This assumes that the firing distribution of t is expo(), but marking-dependent rates are allowed.  Returns infinity for immediate (time 0) transitions.  Otherwise, if the transition does not have an expo() firing distribution, returns null.");
}

void pn_rate::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(x.current_state);
  DCASSERT(np==2);
  // Get trans from second parameter
  SafeCompute(pass[1], x);
  if (!x.answer->isNormal())  return;

  DCASSERT(x.answer->isNormal());
  transition* t = smart_cast <transition*> (x.answer->getPtr());
  DCASSERT(t);

  // check that the models match!
  model_instance* mi = grabModelInstance(x, pass[0]);
  DCASSERT(mi);
  if (t->getParent() != mi) {
    if (mi->StartError(x.parent)) {
      em->cerr() << "transition " << t->Name() << " belongs to another model";
      mi->DoneError();
    }
    x.answer->setNull();
    return;
  } 

  // first: check if the transition is enabled in this state
  SafeCompute(t->getEnabling(), x);
  if (! x.answer->isNormal()) return;
  if (0==x.answer->getInt()) {
    // not enabled: set rate to 0
    x.answer->setReal(0.0);
    return;
  }

  x.which = traverse_data::ComputeExpoRate;
  SafeComputeExpoRate(t->getDistribution(), x);
  x.which = traverse_data::Compute;
}

// ********************************************************
// *                   pn_enabled class                   *
// ********************************************************

class pn_enabled : public model_internal {
public:
  pn_enabled();
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_enabled::pn_enabled() : model_internal(em->BOOL->addProc(), "enabled", 2)
{
  const type* trans = em->findType("trans");
  DCASSERT(trans);
  SetFormal(1, trans->getSetOfThis(), "ts");
  SetDocumentation("Returns true if any transition in the set ts is enabled in the current marking.");
}

void pn_enabled::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(2==np);
  DCASSERT(pass);
  model_instance* mi = grabModelInstance(x, pass[0]);
  petri_hlm* mypn;
  if (mi) {
    mypn = smart_cast <petri_hlm*> (mi->GetCompiledModel());
  } else {
    mypn = 0;
  }
  if (0==mypn) {
    x.answer->setNull();
    return;
  }
  // find out who is enabled
  List <model_event> enablist;
  mypn->makeEnabledList(x, &enablist);
  
  // Get the set of transitions
  SafeCompute(pass[1], x);
  if (!x.answer->isNormal()) return;
  shared_set* ts = smart_cast <shared_set*> (x.answer->getPtr());
  DCASSERT(ts);

  // See if anyone is enabled
  result elem;
  for (int z=0; z<ts->Size(); z++) {
    ts->GetElement(z, elem);
    DCASSERT(elem.isNormal());
    DCASSERT(elem.getPtr());
    model_event* e = smart_cast <model_event*> (elem.getPtr());
    DCASSERT(e);
    if (e->isEnabled()) {
      x.answer->setBool(true);
      return;
    }
  } // for z
  x.answer->setBool(false);
}

// ********************************************************
// *                   pn_places  class                   *
// ********************************************************

class pn_places : public model_internal {
  const type* pset;
public:
  pn_places(const type* pset);
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_places::pn_places(const type* ps) : model_internal(ps, "places", 1)
{
  pset = ps;
  DCASSERT(pset);
  SetDocumentation("Returns the set of places of the Petri net.");
}

void pn_places::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(1==np);
  DCASSERT(pass);
  model_instance* mi = grabModelInstance(x, pass[0]);
  petri_hlm* mypn;
  if (mi) {
    mypn = smart_cast <petri_hlm*> (mi->GetCompiledModel());
  } else {
    mypn = 0;
  }
  if (0==mypn) {
    x.answer->setNull();
    return;
  }
  int npl = mypn->NumStateVars();
  if (0==npl) {
    // empty set
    x.answer->setPtr(MakeSet(pset->getSetElemType(), 0, 0, 0));
    return;
  }
  // build a set of places
  result* plist = new result[npl];
  for (int i=0; i<npl; i++) {
    plist[i].setPtr(Share(mypn->getStateVar(i)));
  }
  x.answer->setPtr(MakeSet(pset->getSetElemType(), npl, plist));
}


// ********************************************************
// *                 pn_transitions class                 *
// ********************************************************

class pn_transitions : public model_internal {
  const type* tset;
public:
  pn_transitions(const type* tset);
  virtual void Compute(traverse_data &x, expr** pass, int np);
};

pn_transitions::pn_transitions(const type* ts) 
 : model_internal(ts, "transitions", 1)
{
  tset = ts;
  DCASSERT(tset);
  SetDocumentation("Returns the set of transitions of the Petri net.");
}

void pn_transitions::Compute(traverse_data &x, expr** pass, int np)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  DCASSERT(1==np);
  DCASSERT(pass);
  model_instance* mi = grabModelInstance(x, pass[0]);
  petri_hlm* mypn;
  if (mi) {
    mypn = smart_cast <petri_hlm*> (mi->GetCompiledModel());
  } else {
    mypn = 0;
  }
  if (0==mypn) {
    x.answer->setNull();
    return;
  }
  int nt = mypn->getNumEvents();
  if (0==nt) {
    // empty set
    x.answer->setPtr(MakeSet(tset->getSetElemType(), 0, 0, 0));
    return;
  }
  // build a set of transitions
  result* tlist = new result[nt];
  for (int i=0; i<nt; i++) {
    tlist[i].setPtr(Share(mypn->getEvent(i)));
  }
  x.answer->setPtr(MakeSet(tset->getSetElemType(), nt, tlist));
}


// ******************************************************************
// *                                                                *
// *                                                                *
// *                         Initialization                         *
// *                                                                *
// *                                                                *
// ******************************************************************

class init_pnform : public initializer {
  public:
    init_pnform();
    virtual bool execute();
};
init_pnform the_pnform_initializer;

init_pnform::init_pnform() : initializer("init_pnform")
{
  usesResource("em");
  usesResource("CML");
  buildsResource("formalisms");
}

bool init_pnform::execute()
{
  if (0==em) return false;

  // misc. static vars
  result one(1L);
  petri_def::ONE = em->makeLiteral(0, -1, em->INT->addProc(), one);

  // Set up options
  option* debug = em->findOption("Debug");
  petri_def::pn_debug.Initialize(debug,
    "pns",
    "When set, diagnostic messages are displayed regarding Petri net model construction.",
    false
  );

  option* warning = em->findOption("Warning");
  group_of_named pnwarnings(13);
  pnwarnings.AddItem(petri_def::zero_init.Initialize(warning,
    "pn_zero_init",
    "For zero tokens specified in an initial marking in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::zero_bound.Initialize(warning,
    "pn_zero_bound",
    "For zero tokens specified as an upper bound in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::no_trans.Initialize(warning,
    "pn_no_trans",
    "For absence of transitions in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::no_place.Initialize(warning,
    "pn_no_place",
    "For absence of places in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::no_init.Initialize(warning,
    "pn_no_init",
    "For no specified initial marking in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::no_fire.Initialize(warning,
    "pn_no_fire",
    "If some, but not all, transitions are given a firing distribution",
    true
  ));
  pnwarnings.AddItem(petri_def::no_weight.Initialize(warning,
    "pn_no_weight",
    "For immediate transitions with no weight given",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_init.Initialize(warning,
    "pn_dup_init",
    "For duplicate place token initialization in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_bound.Initialize(warning,
    "pn_dup_bound",
    "For duplicate place token bounding in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_arc.Initialize(warning,
    "pn_dup_arc",
    "For duplicate arcs in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_guard.Initialize(warning,
    "pn_dup_guard",
    "For multiple guards on the same transition in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_fire.Initialize(warning,
    "pn_dup_fire",
    "For multiple firing assignments on the same transition in Petri net models",
    true
  ));
  pnwarnings.AddItem(petri_def::dup_weight.Initialize(warning,
    "pn_dup_weight",
    "For multiple weight assignments on the same transition in Petri net models",
    true
  ));
  pnwarnings.Finish(warning, "pn_ALL", "Group of all Petri net warnings");

  radio_button** ms_list = new radio_button*[4];
  ms_list[petri_hlm::INDEXED] = new radio_button(
    "INDEXED",
    "Format is [p1:1, p2:0, p3:2, p4:0, p5:0, p6:1]",
    petri_hlm::INDEXED
  );
  ms_list[petri_hlm::SAFE] = new radio_button(
    "SAFE",
    "Format is [p1, p3:2, p6]",
    petri_hlm::SAFE
  );
  ms_list[petri_hlm::SPARSE] = new radio_button(
    "SPARSE",
    "Format is [p1:1, p3:2, p6:1]",
    petri_hlm::SPARSE
  );
  ms_list[petri_hlm::VECTOR] = new radio_button(
    "VECTOR",
    "Format is [1, 0, 2, 0, 0, 1]",
    petri_hlm::VECTOR
  );
  petri_hlm::MarkingStyle = petri_hlm::SPARSE;
  em->addOption(
    MakeRadioOption("PNMarkingStyle",
      "How to display a Petri net marking",
      ms_list, 4, petri_hlm::MarkingStyle
    )
  );

  // Set up and register formalisms
  const char* longdocs = "The Petri net formalism allows high-level description of a model as a Petri net.  The places and transitions are declared, and connections between the two (e.g., input, output, and inhibitor arcs) and other features (e.g., transition guards) are specified via the appropriate function calls.";

  formalism* pn = new petri_formalism("pn", "Petri net", longdocs);
  if (!em->registerType(pn)) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->noCause();
      em->internal() << "Couldn't register pn type";
      em->stopIO();
    }
    return false;
  }

  // set up and register place types
  simple_type* t_place  = new void_type("place", "Petri net place", "Place of a Petri net, can hold a non-negative number of tokens.");
  t_place->setPrintable();
  type* t_set_place = newSetType("{place}", t_place);
  em->registerType(t_place);
  em->registerType(t_set_place);

  // set up and register trans types
  simple_type* t_trans  = new void_type("trans", "Petri net transition", "Transition of a Petri net, can move tokens.");
  t_trans->setPrintable();
  type* t_set_trans = newSetType("{trans}", t_trans);
  em->registerType(t_trans);
  em->registerType(t_set_trans);

  // another formalism may have already registered these types.
  // all we care is that they are registered.
  petri_def::place_type = em->findType("place");
  petri_def::trans_type = em->findType("trans");
  DCASSERT(petri_def::place_type);
  DCASSERT(petri_def::trans_type);
  DCASSERT(em->findType("{place}"));
  DCASSERT(em->findType("{trans}"));

  // fill symbol table
  symbol_table* pnsyms = MakeSymbolTable();
  pnsyms->AddSymbol(  new pn_init     );
  pnsyms->AddSymbol(  new pn_bound    );
  pnsyms->AddSymbol(  new pn_arcs     );
  pnsyms->AddSymbol(  new pn_inhibit  );
  pnsyms->AddSymbol(  new pn_guard    );
  pnsyms->AddSymbol(  new pn_firing   );
  pnsyms->AddSymbol(  new pn_weight   );
  pnsyms->AddSymbol(  new pn_weight2  );
  pnsyms->AddSymbol(  new pn_assert   );
  pnsyms->AddSymbol(  new pn_hide     );
  pnsyms->AddSymbol(  new pn_tk       );
  pnsyms->AddSymbol(  new pn_rate     );
  pnsyms->AddSymbol(  new pn_enabled  );
  pnsyms->AddSymbol(  new pn_places(t_set_place)        );
  pnsyms->AddSymbol(  new pn_transitions(t_set_trans)   );
  Add_DSDE_varfuncs(petri_def::place_type, pnsyms);
  Add_DSDE_eventfuncs(petri_def::trans_type, pnsyms);
  Add_MCC_varfuncs(petri_def::place_type, pnsyms);

  pn->setFunctions(pnsyms); 
  pn->addCommonFuncs(CML);

  return true;
}

