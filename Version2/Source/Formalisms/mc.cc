
// $Id$

#include "mc.h"

#include "../Base/api.h"
#include "../Language/api.h"
#include "../Main/tables.h"
#include "../States/reachset.h"
#include "../Templates/sparsevect.h"

#include "../Chains/mc_expl.h"

#include "dsm.h"

#define DEBUG_MC

// ******************************************************************
// *                                                                *
// *                        markov_dsm  class                       *
// *                                                                *
// ******************************************************************

/** A discrete-state model (internel representation) for Markov chains.
*/
class markov_dsm : public state_model {
public:
  model_var** statenames;
  int numstates;
  classified_chain <float> *arcs;
  sparse_vector <float> *initial;
public:
  /** Constructor.
	@param	name	Model name
	@param	sn	Array of states
	@param	ns	Number of states
	@param	a	P or R matrix.
	@param  p0	Initial distribution
  */
  markov_dsm(const char* name, model_var** sn, int ns, 
	     classified_chain <float> *a,
             sparse_vector <float> *p0);

  virtual ~markov_dsm();

  // required stuff:

  virtual void ShowState(OutputStream &s, const state &x);
  virtual void ShowEventName(OutputStream &s, int e);

  virtual int NumInitialStates() const;
  virtual void GetInitialState(int n, state &s) const;

  virtual expr* EnabledExpr(int e) { return NULL; } // fix later
  virtual expr* NextStateExpr(int e) { return NULL; } // fix later
  virtual expr* EventDistribution(int e) { return NULL; } // fix later

};

// ******************************************************************
// *                       markov_dsm methods                       *
// ******************************************************************

markov_dsm::markov_dsm(const char *name, model_var **sn, int ns, 
		       classified_chain <float> *a, sparse_vector <float> *p0) 
: state_model(name, 1)
{
  statenames = sn;
  numstates = ns;
  arcs = a;
  initial = p0;
  statespace = new reachset;
  statespace->CreateEnumerated(numstates);
#ifdef DEBUG_MC
  Output << "Built Markov chain state model " << name << "\n";
  int i;
  Output << "States:\n";
  for (i=0; i<ns; i++) {
    Output << "\t" << statenames[i]->Name();
    Output << " index " << statenames[i]->state_index << "\n";
  }
  Output.flush();
  Output << "Initial distribution:\n";
  Output << "\tindex: [";
  Output.PutArray(initial->index, initial->nonzeroes);
  Output << "]\n\tvalues:[";
  Output.PutArray(initial->value, initial->nonzeroes);
  Output << "]\nArcs:\n";
  arcs->Show(Output);
#endif
}

markov_dsm::~markov_dsm()
{
  delete arcs;
  delete initial;
}

void markov_dsm::ShowState(OutputStream &s, const state &x)
{
  // check state legality and range here...
  s << statenames[x.Read(0).ivalue];
}

void markov_dsm::ShowEventName(OutputStream &s, int e)
{
  CHECK_RANGE(0, e, NumEvents());

  // Is there something better to do here?
  s << "Markov chain";
}

int markov_dsm::NumInitialStates() const
{
  // fill this in later
  return 0;
}

void markov_dsm::GetInitialState(int n, state &s) const
{
  // fill this in later
  s[0].ivalue = 0;
}

// ******************************************************************
// *                                                                *
// *                       markov_model class                       *
// *                                                                *
// ******************************************************************

/** Smart support for the Markov chain "formalism".
    I.e., front-end stuff for Markov chain formalism.
*/
class markov_model : public model {
  List <model_var> *statelist;
  model_var** states;
  int numstates;
  sparse_vector <float> *initial;
  labeled_digraph <float> *wdgraph;
public:
  markov_model(const char* fn, int line, type t, char*n, 
  		formal_param **pl, int np);

  virtual ~markov_model();

  void AddInitial(int state, double weight, const char *fn, int line);
  void AddArc(int fromstate, int tostate, double weight, const char *fn, int line);

  // Required for models:

  virtual model_var* MakeModelVar(const char *fn, int l, type t, char* n);

  virtual void InitModel();
  virtual void FinalizeModel(result &);
  virtual state_model* BuildStateModel();
};

// ******************************************************************
// *                      markov_model methods                      *
// ******************************************************************

markov_model::markov_model(const char* fn, int line, type t, char*n, 
  formal_param **pl, int np) : model(fn, line, t, n, pl, np)
{
  statelist = NULL; 
  states = NULL;
  numstates = 0;
  initial = NULL;
  wdgraph = NULL;
}

markov_model::~markov_model()
{
  delete initial;
  delete wdgraph;
}

void markov_model::AddInitial(int state, double weight, const char* fn, int line)
{
  int e = initial->BinarySearchIndex(state);
  if (e<0) {
    initial->SortedAppend(state, weight);
    return;
  }
  // Duplicate entry, give a warning
  Warning.Start(fn, line);
  Warning << "Ignoring duplicate initial probability for \n\tstate ";
  Warning << statelist->Item(state) << " in Markov chain " << Name();
  Warning.Stop();
}

void markov_model::AddArc(int fromstate, int tostate, double weight, const char *fn, int line)
{
  DCASSERT(wdgraph);
  float f = weight;
  if (wdgraph->AddEdgeInOrder(fromstate, tostate, f) == wdgraph->NumEdges()-1) 
    return;

  // Duplicate entry, give a warning
  Warning.Start(fn, line);
  Warning << "Summing duplicate arc from \n\tstate ";
  Warning << statelist->Item(fromstate) << " to " << statelist->Item(tostate);
  Warning << " in Markov chain " << Name();
  Warning.Stop();
}

model_var* markov_model::MakeModelVar(const char *fn, int l, type t, char* n)
{
  DCASSERT(wdgraph);
  int ndx = statelist->Length();
  wdgraph->AddNode();
  model_var* s = new model_var(fn, l, t, n);
  s->SetIndex(ndx);
  Copy(s);  // hack
  statelist->Append(s);
#ifdef DEBUG_MC
  Output << "\tModel " << Name() << " created state " << s << " index " << ndx << "\n"; 
  Output.flush();
#endif
  return s;
}

void markov_model::InitModel()
{
  statelist = new List <model_var> (16);
  states = NULL;
  numstates = 0;
  initial = new sparse_vector <float>(2);
  wdgraph = new labeled_digraph <float>;
  wdgraph->ResizeNodes(4);
  wdgraph->ResizeEdges(4);
}

void markov_model::FinalizeModel(result &x)
{
  numstates = statelist->Length();
  states = statelist->MakeArray();
  delete statelist;
  statelist = NULL;
#ifdef DEBUG_MC
  Output << "\tMC " << Name() << " has " << numstates << " states\n";
  int i;
  for (i=0; i<numstates; i++) {
    Output << "\t" << states[i] << "\n";
  }
  Output << "\tInitial weights:\n";
  for (i=0; i<initial->NumNonzeroes(); i++) {
    Output << "\t" << states[initial->index[i]];
    Output << " : " << initial->value[i] << "\n"; 
  }
  Output.flush();
  Output << "Markov chain itself:\n";
  for (i=0; i<numstates; i++) {
    wdgraph->ShowNodeList(Output, i);
    Output.flush();
  }
#endif

  x.Clear();
  x.notFreeable();
  x.other = this;
}

state_model* markov_model::BuildStateModel()
{
  classified_chain <float> *foo = new classified_chain <float>(wdgraph);
  wdgraph = NULL;
  if (!foo->isIrreducible()) {
    int i;
    // renumber states
    for (i=0; i<numstates; i++)
      states[i]->state_index = foo->Renumber(states[i]->state_index);
    // fix initial distribution
    for (i=0; i<initial->nonzeroes; i++)
      initial->index[i] = foo->Renumber(initial->index[i]);
    initial->isSorted = false;

    foo->DoneRenumbering();
  }
  return new markov_dsm(Name(), states, numstates, foo, initial);
}

// ******************************************************************
// *                                                                *
// *                      MC-specific functions                     *
// *                                                                *
// ******************************************************************


// ********************************************************
// *                         init                         *
// ********************************************************

void compute_mc_init(expr **pp, int np, result &x)
{
  DCASSERT(np>1);
  DCASSERT(pp);

  markov_model *mc = dynamic_cast<markov_model*>(pp[0]);
  DCASSERT(mc);

#ifdef DEBUG_MC
  Output << "\tInside init for model " << mc << "\n";
#endif

  x.Clear();
  int i;
  for (i=1; i<np; i++) {
#ifdef DEBUG_MC
    Output << "\tparameter " << i << " is " << pp[i] << "\n";
    Output << "\t state ";
#endif
    SafeCompute(pp[i], 0, x);
#ifdef DEBUG_MC
    PrintResult(Output, INT, x);
    Output << "\n\t value ";
#endif
    int index = x.ivalue;
    // check for errors here...

    SafeCompute(pp[i], 1, x);
#ifdef DEBUG_MC
    PrintResult(Output, REAL, x);
    Output << "\n";
#endif
    double weight = x.rvalue;
    // again with the errors

    mc->AddInitial(index, weight, pp[i]->Filename(), pp[i]->Linenumber());
  }

#ifdef DEBUG_MC
  Output << "\tExiting init for model " << mc << "\n";
  Output.flush();
#endif

  x.setNull();
}

void Add_init(PtrTable *fns)
{
  const char* helpdoc = "Sets the initial state(s) with probabilities for a Markov Chain model.";

  formal_param **pl = new formal_param*[2];
  pl[0] = new formal_param(MARKOV, "m");
  type *tl = new type[2];
  tl[0] = STATE;
  tl[1] = REAL;
  pl[1] = new formal_param(tl, 2, "pair");
  internal_func *p = new internal_func(VOID, "init", 
	compute_mc_init, NULL,
	pl, 2, 1, helpdoc);  // parameter 1 repeats...
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ********************************************************
// *                         arcs                         *
// ********************************************************

void compute_mc_arcs(expr **pp, int np, result &x)
{
  DCASSERT(np>1);
  DCASSERT(pp);
  markov_model *mc = dynamic_cast<markov_model*>(pp[0]);
  DCASSERT(mc);

#ifdef DEBUG_MC
  Output << "\tInside arcs for model " << mc << "\n";
#endif


  x.Clear();
  int i;
  for (i=1; i<np; i++) {
#ifdef DEBUG_MC
    Output << "\tparameter " << i << " is " << pp[i] << "\n";
    Output << "\t from state ";
#endif
    SafeCompute(pp[i], 0, x);
#ifdef DEBUG_MC
    PrintResult(Output, INT, x);
    Output << "\n\t to state ";
#endif
    int fromstate = x.ivalue;
    // check for errors here...

    SafeCompute(pp[i], 1, x);
#ifdef DEBUG_MC
    PrintResult(Output, INT, x);
    Output << "\n\t weight ";
#endif
    int tostate = x.ivalue;

    SafeCompute(pp[i], 2, x);
#ifdef DEBUG_MC
    PrintResult(Output, REAL, x);
    Output << "\n";
#endif
    double weight = x.rvalue;
    // again with the errors

    mc->AddArc(fromstate, tostate, weight, pp[i]->Filename(), pp[i]->Linenumber());
  }

#ifdef DEBUG_MC
  Output << "\tExiting arcs for model " << mc << "\n";
  Output.flush();
#endif

  x.setNull();
}

void Add_arcs(PtrTable *fns)
{
  const char* helpdoc = "Adds a set of arcs to the Markov chain";

  formal_param **pl = new formal_param*[2];
  pl[0] = new formal_param(MARKOV, "m");
  type *tl = new type[3];
  tl[0] = STATE;
  tl[1] = STATE;
  tl[2] = REAL;
  pl[1] = new formal_param(tl, 3, "triple");
  internal_func *p = new internal_func(VOID, "arcs", 
	compute_mc_arcs, NULL,
	pl, 2, 1, helpdoc);  // parameter 1 repeats...
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ********************************************************
// *                        instate                       *
// ********************************************************

// A Proc function!
void compute_mc_instate(const state &m, expr **pp, int np, result &x)
{
  DCASSERT(np==2);
  DCASSERT(pp);
#ifdef DEBUG
  Output << "Checking instate\n";
  Output.flush();
#endif
  x.Clear();
  SafeCompute(pp[1], 0, x);

  // error checking here...
#ifdef DEBUG
  Output << "\tgot param: ";
  PrintResult(Output, INT, x);
  Output << "\n";
  Output.flush();

  Output << "\tcurrent state: ";
  PrintResult(Output, INT, m.Read(0));
  Output << "\n";
  Output.flush();
#endif
  // error checking here for m

  if (x.ivalue == m.Read(0).ivalue) {
    x.bvalue = true;
  } else {
    x.bvalue = false;
  }
}

void Add_instate(PtrTable *fns)
{
  const char* helpdoc = "Returns true if the Markov chain is in the specified state";

  formal_param **pl = new formal_param*[2];
  pl[0] = new formal_param(MARKOV, "m");
  pl[1] = new formal_param(STATE, "s");
  internal_func *p = new internal_func(PROC_BOOL, "instate", 
	compute_mc_instate, NULL,
	pl, 2, helpdoc);  
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ********************************************************
// *                       transient                      *
// ********************************************************

void compute_mc_transient(const state &m, expr **pp, int np, result &x)
{
  DCASSERT(np==1);
  DCASSERT(pp);
  markov_model *mm = dynamic_cast<markov_model*>(pp[0]);
  DCASSERT(mm);
  markov_dsm *md = dynamic_cast<markov_dsm*>(mm->GetModel());
  DCASSERT(md);
  DCASSERT(md->arcs);
#ifdef DEBUG
  Output << "Checking transient\n";
  Output << "\tcurrent state: ";
  PrintResult(Output, INT, m.Read(0));
  Output << "\n";
  Output.flush();
#endif
  // error checking here for m

  x.Clear();
  x.bvalue = md->arcs->isTransient(m.Read(0).ivalue);
}

void Add_transient(PtrTable *fns)
{
  const char* helpdoc = "Returns true if the Markov chain is in a transient state";

  formal_param **pl = new formal_param*[1];
  pl[0] = new formal_param(MARKOV, "m");
  internal_func *p = new internal_func(PROC_BOOL, "transient", 
	compute_mc_transient, NULL,
	pl, 1, helpdoc);  
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ********************************************************
// *                       absorbing                      *
// ********************************************************

void compute_mc_absorbing(const state &m, expr **pp, int np, result &x)
{
  DCASSERT(np==1);
  DCASSERT(pp);
  markov_model *mm = dynamic_cast<markov_model*>(pp[0]);
  DCASSERT(mm);
  markov_dsm *md = dynamic_cast<markov_dsm*>(mm->GetModel());
  DCASSERT(md);
  DCASSERT(md->arcs);
#ifdef DEBUG
  Output << "Checking absorbing\n";
  Output << "\tcurrent state: ";
  PrintResult(Output, INT, m.Read(0));
  Output << "\n";
  Output.flush();
#endif
  // error checking here for m

  x.Clear();
  x.bvalue = md->arcs->isAbsorbing(m.Read(0).ivalue);
}

void Add_absorbing(PtrTable *fns)
{
  const char* helpdoc = "Returns true if the Markov chain is in an absorbing state";

  formal_param **pl = new formal_param*[1];
  pl[0] = new formal_param(MARKOV, "m");
  internal_func *p = new internal_func(PROC_BOOL, "absorbing", 
	compute_mc_absorbing, NULL,
	pl, 1, helpdoc);  
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ********************************************************
// *                          test                        *
// ********************************************************


// A hook for testing things
void compute_mc_test(expr **pp, int np, result &x)
{
  DCASSERT(np==2);
  DCASSERT(pp);
  DCASSERT(pp[0]);
  model *mcmod = dynamic_cast<model*> (pp[0]);
  DCASSERT(mcmod);
  markov_dsm *mc = dynamic_cast <markov_dsm*> (mcmod->GetModel());
  DCASSERT(mc);

  Output << "Got markov_dsm " << mc << "\n";
  Output.flush();
  x.Clear();
  x.ivalue = 0;
}

void Add_test(PtrTable *fns)
{
  formal_param **pl = new formal_param*[2];
  pl[0] = new formal_param(MARKOV, "m");
  pl[1] = new formal_param(BOOL, "dummy");
  internal_func *p = new internal_func(INT, "test",
	compute_mc_test, NULL, pl, 2, NULL);
  p->setWithinModel();
  InsertFunction(fns, p);
}

// ******************************************************************
// *                                                                *
// *                        Global front-ends                       *
// *                                                                *
// ******************************************************************

model* MakeMarkovChain(type t, char* id, formal_param **pl, int np,
			const char* fn, int line)
{
  return new markov_model(fn, line, t, id, pl, np);
}

void InitMCModelFuncs(PtrTable *t)
{
  Add_init(t);
  Add_arcs(t);

  Add_instate(t);
  Add_transient(t);
  Add_absorbing(t);

  Add_test(t);
}

