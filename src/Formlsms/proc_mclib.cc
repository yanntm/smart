
#include "proc_mclib.h"

#include "../Streams/streams.h"
#include "../include/heap.h"
#include "../Modules/expl_ssets.h"
#include "../Modules/statevects.h"
#include "../_IntSets/intset.h"

#include "show_graph.h"

bool statusOK(exprman* em, const LS_Output &o, const char* who)
{
    switch (o.status) {
      case LS_Success:
          return true;

      case LS_No_Convergence:
          if (em->startWarning()) {
            em->causedBy(0);
            em->warn() << "Markov chain linear solver (for ";
            em->warn() << who << ") did not converge";
            em->stopIO();
          }
          return true;

      case LS_Out_Of_Memory:
          if (em->startError()) {
            em->causedBy(0);
            em->cerr() << "Insufficient memory for Markov chain ";
            em->cerr() << who << " solver";
            em->stopIO();
          }
          return false;

      case LS_Wrong_Format:
          if (em->startError()) {
            em->causedBy(0);
            em->cerr() << "Wrong matrix format for Markov chain linear solver";
            em->stopIO();
          }
          return false;

      default:
          if (em->startInternal(__FILE__, __LINE__)) {
            em->causedBy(0);
            em->internal() << "Unexpected error";
            em->stopIO();
          }
    } // switch
    return false;
}

bool status(exprman* em, MCLib::error e, const char* who)
{
    switch (e.getCode()) {
      case MCLib::error::Out_Of_Memory:
          if (em->startError()) {
            em->causedBy(0);
            em->cerr() << "Insufficient memory for Markov chain ";
            em->cerr() << who << " solver";
            em->stopIO();
          }
          break;

      case MCLib::error::Null_Vector:
          if (em->startError()) {
            em->causedBy(0);
            em->cerr() << "Initial probability vector required for Markov chain solver";
            em->stopIO();
          }
          break;

      default:
        if (em->startInternal(__FILE__, __LINE__)) {
            em->causedBy(0);
            em->internal() << "Unexpected error: " << e.getString();
            em->stopIO();
        }
    } // switch
    return false;
}

// ******************************************************************
// *                                                                *
// *                     mclib_process  methods                     *
// *                                                                *
// ******************************************************************

mclib_process::mclib_process(bool discrete, GraphLib::dynamic_graph* _G)
{
  is_discrete = discrete;
  G = _G;
  VC = 0;

  chain = 0;
  initial = 0;

  trap = -1;
  accept = -1;
}

// ******************************************************************

mclib_process::mclib_process(MCLib::vanishing_chain* vc)
{
  is_discrete = vc->isDiscrete();
  G = 0;
  VC = vc;

  chain = 0;
  initial = 0;

  trap = -1;
  accept = -1;
}

// ******************************************************************

mclib_process::~mclib_process()
{
  delete G;
  delete VC;
  delete chain;
  Delete(initial);
}

// ******************************************************************

GraphLib::node_renumberer* mclib_process::initChain(GraphLib::dynamic_graph *g)
{
  // Classify states
  GraphLib::timer_hook *sw = my_timer ? my_timer->switchMe() : 0;
  GraphLib::abstract_classifier* ac = g->determineSCCs(0, 1, true, sw);
  GraphLib::static_classifier C;
  GraphLib::node_renumberer *Ren = ac->buildRenumbererAndStatic(C);

  // Renumber states
  DCASSERT(Ren);
  g->renumberNodes(*Ren);

  // Build chain; try using doubles
  GraphLib::dynamic_summable <double> *Gd =
    dynamic_cast < GraphLib::dynamic_summable <double> *> (g);

  if (Gd) {
    chain = new MCLib::Markov_chain(is_discrete, *Gd, C, sw);
  }

  if (0==chain) {
    // try using floats
    GraphLib::dynamic_summable <float> *Gf =
      dynamic_cast < GraphLib::dynamic_summable <float> *> (g);

    if (Gf) {
      chain = new MCLib::Markov_chain(is_discrete, *Gf, C, sw);
    }
  }

  if (0==chain) {
    if (em->startInternal(__FILE__, __LINE__)) {
        em->causedBy(0);
        em->internal() << "Couldn't convert graph to Markov chain: bad graph type?\n";
        em->stopIO();
    }
  }

  //
  // Cleanup
  //
  delete ac;

  return Ren;
}

// ******************************************************************

void mclib_process::attachToParent(stochastic_lldsm* p, LS_Vector &init, state_lldsm::reachset* rss)
{
  process::attachToParent(p, init, rss);

  // Finish rss
  indexed_reachset* irs = smart_cast <indexed_reachset*> (rss);
  DCASSERT(irs);
  irs->Finish();

  //
  // Finish MC
  //
  GraphLib::node_renumberer *Ren = 0;
  if (G) {
    Ren = initChain(G);
  } else if (VC) {
    Ren = initChain(& VC->TT() );
  }
  DCASSERT(Ren);

  //
  // Done with G, VC
  //
  delete G;
  G = 0;
  delete VC;
  VC = 0;

  //
  // Renumber states
  //
  irs->Renumber(Ren);
  if (trap >= 0) {
    trap = Ren->new_number(trap);
  }
  if (accept >= 0) {
    accept = Ren->new_number(accept);
  }
  initial = new statedist(p, init, Ren);

  // Copy initial states to RSS
  intset initial_set( chain->getNumStates() );
  initial->greater_than(0, &initial_set);
  irs->setInitial(initial_set);

  // NEAT TRICK!!!
  // Set the reachability graph, using
  // a thin wrapper around the Markov chain.
  p->setRGR( new mclib_reachgraph(this) );

  //
  // Done with Ren
  //
  delete Ren;
}

// ******************************************************************

long mclib_process::getNumStates() const
{
  DCASSERT(chain);
  return chain->getNumStates();
}

// ******************************************************************

void mclib_process::getNumClasses(long &count) const
{
  DCASSERT(chain);
  const GraphLib::static_classifier& C = chain->getStateClassification();
  count = C.getNumClasses() - 2 + C.sizeOfClass(1);
  // There's always one class for all transient states (#0)
  // and one class for all absorbing states (#1).  Subtract those,
  // and add back the number of absorbing states.
}

// ******************************************************************

void mclib_process::showClasses(OutputStream &os, state_lldsm::reachset* RSS,
  shared_state* st) const
{
  DCASSERT(chain);
  const GraphLib::static_classifier& C = chain->getStateClassification();
  // TBD : try/catch around this
  indexed_reachset::indexed_iterator &I =
    dynamic_cast <indexed_reachset::indexed_iterator &> (
      RSS->iteratorForOrder(state_lldsm::NATURAL)
    );

  for (long c=0; c<C.getNumClasses(); c++) {
    if (0==C.sizeOfClass(c)) continue;

    if (0==c) {
      os << "Transient states";
    } else if (1==c) {
      os << "Absorbing states";
    } else {
      os << "Recurrent class " << c-1;
    }

    os << ":\n\t{";
    bool comma = false;
    for (long i=C.firstNodeOfClass(c); i<=C.lastNodeOfClass(c); i++) {
      if (comma) os << ", ";
      comma = true;
      I.copyState(st, i);
      RSS->showState(os, st);
      os.flush();
    }
    os << "}\n";
  }

}

// ******************************************************************

bool mclib_process::isTransient(long st) const
{
  DCASSERT(chain);
  const GraphLib::static_classifier& C = chain->getStateClassification();
  return C.isNodeInClass(st, 0);
}

// ******************************************************************

statedist* mclib_process::getInitialDistribution() const
{
  return Share(initial);
}

// ******************************************************************

// Helper class or getOutgoingWeights
// ======================================================================
template <class REAL>
class copy_edges : public GraphLib::BF_graph_traversal {
    public:
        copy_edges(long from, long* to, double* w, long n) {
            dests = to;
            weights = w;
            array_size = n;

            source = from;
            array_needed = 0;
        }
        virtual ~copy_edges() {
            // Don't delete anything!
        }

        virtual bool hasNodesToExplore() {
            return source>=0;
        }

        virtual long getNextToExplore() {
            long ret = source;
            source = -1;
            return ret;
        }

        virtual bool visit(long src, long dest, const void* wt) {
            if (array_needed < array_size) {
              dests[array_needed] = dest;
              const REAL* weight = (const REAL*) wt;
              weights[array_needed] = *weight;
            }
            array_needed++;
            return false;
        }

        inline long getNeededSize() const {
            return array_needed;
        }

    private:
        long* dests;
        double* weights;
        long array_size;
        long array_needed;

        long source;

}; // class copy_edges
// ======================================================================

long mclib_process::getOutgoingWeights(long from, long* to, double* w, long n) const
{


  if (chain->edgesStoredAsDoubles()) {
    copy_edges <double> T(from, to, w, n);
    chain->traverseOutgoing(T);
    return T.getNeededSize();
  } else {
    copy_edges <float> T(from, to, w, n);
    chain->traverseOutgoing(T);
    return T.getNeededSize();
  }
}

// ******************************************************************

bool mclib_process
::computeTransient(double t, double* probs, double* aux1, double* aux2) const
{
  if (0==chain || 0==probs || 0==aux1)  return false;


  try {
    timer w;
    if (is_discrete) {
      MCLib::Markov_chain::DTMC_transient_options opts;
      opts.vm_result = aux1;
      opts.accumulator = aux2;

      int it = int(t);
      startTransientReport(w, it);
      chain->computeTransient(it, probs, opts);
      stopTransientReport(w, opts.multiplications);

      opts.vm_result = 0;
      opts.accumulator = 0;
    } else {
      MCLib::Markov_chain::CTMC_transient_options opts;
      opts.vm_result = aux1;
      opts.accumulator = aux2;

      startTransientReport(w, t);
      chain->computeTransient(t, probs, opts);
      stopTransientReport(w, opts.multiplications);

      opts.vm_result = 0;
      opts.accumulator = 0;
    }
    return true;
  }
  catch (MCLib::error e) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Unexpected error: ";
      em->internal() << e.getString();
      em->stopIO();
    }
    return false;
  }
}

// ******************************************************************

bool mclib_process::computeAccumulated(double t, const double* p0, double* n,
                                  double* aux, double* aux2) const
{
  if (0==chain || 0==p0 || 0==n || 0==aux || 0==aux2) return false;

  try {
    timer w;
    if (is_discrete) {
      MCLib::Markov_chain::DTMC_transient_options opts;
      opts.vm_result = aux;
      opts.accumulator = aux2;

      int it = int(t);
      startAccumulatedReport(w, it);
      chain->accumulate(it, p0, n, opts);
      stopAccumulatedReport(w, opts.multiplications);

      opts.vm_result = 0;
      opts.accumulator = 0;
    } else {
      MCLib::Markov_chain::CTMC_transient_options opts;
      opts.vm_result = aux;
      opts.accumulator = aux2;

      startAccumulatedReport(w, t);
      chain->accumulate(t, p0, n, opts);
      stopAccumulatedReport(w, opts.multiplications);

      opts.vm_result = 0;
      opts.accumulator = 0;
    }
    return true;
  }
  catch (MCLib::error e) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Unexpected error: ";
      em->internal() << e.getString();
      em->stopIO();
    }
    return false;
  }
}

// ******************************************************************

bool mclib_process::computeSteadyState(double* probs) const
{
  if (0==chain) return false;
  if (0==probs) return false;

  try {
    LS_Output outdata;
    LS_Vector ls_init;
    timer w;
    startSteadyReport(w);
    DCASSERT(initial);
    initial->ExportTo(ls_init);
    chain->computeInfinityDistribution(ls_init, probs, getSolverOptions(), outdata);
    stopSteadyReport(w, outdata.num_iters);
    return statusOK(em, outdata, "steady-state");
  }
  catch (MCLib::error e) {
    return status(em, e, "steady-state");
  }
}

// ******************************************************************

bool mclib_process::computeTimeInStates(const double* p0, double* x) const
{
  if (0==chain || 0==p0 || 0==x) return false;

  LS_Output outdata;
  LS_Vector p0vect;
  p0vect.size = chain->getNumStates();
  p0vect.index = 0;
  p0vect.d_value = p0;
  p0vect.f_value = 0;
  try {
    timer w;
    startTTAReport(w);
    chain->computeTTA(p0vect, x, getSolverOptions(), outdata);
    stopTTAReport(w, outdata.num_iters);
    return statusOK(em, outdata, "time in states");
  }
  catch (MCLib::error e) {
    return status(em, e, "time in states");
  }
}

// ******************************************************************

bool mclib_process::computeClassProbs(const double* p0, double* x) const
{
  if (0==chain || 0==p0 || 0==x) return false;

  LS_Output outdata;
  LS_Vector p0vect;
  p0vect.size = chain->getNumStates();
  p0vect.index = 0;
  p0vect.d_value = p0;
  p0vect.f_value = 0;
  try {
    timer w;
    startTTAReport(w);
    chain->computeFirstRecurrentProbs(p0vect, x, getSolverOptions(), outdata);
    stopTTAReport(w, outdata.num_iters);
    return statusOK(em, outdata, "class probabilities");
  }
  catch (MCLib::error e) {
    return status(em, e, "class probabilities");
  }
}

// ******************************************************************

bool mclib_process::randomTTA(rng_stream &st, long &state, const stateset* F,
                          long maxt, long &elapsed)
{
  const expl_stateset* final = dynamic_cast <const expl_stateset*> (F);
  DCASSERT(final);
  DCASSERT(chain);
  if (chain->isContinuous()) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Can't simulate discrete-time random walk on a CTMC.";
      em->stopIO();
    }
    return false;
  }

  try {
    elapsed = chain->randomWalk(st, state, &final->getExplicit(), maxt, 1.0);
    return true;
  }
  catch (MCLib::error e) {
    if (em->startError()) {
      em->causedBy(0);
      em->cerr() << "Couldn't simulate DTMC random walk: ";
      em->cerr() << e.getString();
      em->stopIO();
    }
    return false;
  }
}

// ******************************************************************

bool mclib_process::randomTTA(rng_stream &st, long &state, const stateset* F,
                          double maxt, double &elapsed)
{
  const expl_stateset* final = dynamic_cast <const expl_stateset*> (F);
  DCASSERT(chain);
  if (chain->isDiscrete()) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Can't simulate continuous-time random walk on a DTMC.";
      em->stopIO();
    }
    return false;
  }

  try {
    elapsed = chain->randomWalk(st, state, &final->getExplicit(), maxt);
    return true;
  }
  catch (MCLib::error e) {
    if (em->startError()) {
      em->causedBy(0);
      em->cerr() << "Couldn't simulate CTMC random walk: ";
      em->cerr() << e.getString();
      em->stopIO();
    }
    return false;
  }
}

//
// For phase?
//

// ******************************************************************

bool mclib_process::computeDiscreteTTA(double epsilon, long maxsize,
  discrete_pdf &dist) const
{
  DCASSERT(chain);
  if (chain->isContinuous()) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Can't compute discrete TTA on a CTMC.";
      em->stopIO();
    }
    return false;
  }

  long acc_state = getAcceptingState();
  if (acc_state < 0) {
    // Degenerate case - no accepting state
    // This should be a distribution of "infinity"
    dist.reset(0, -1, 0, 1.0);
    return true;
  }

#ifdef DEVELOPMENT_CODE
  const GraphLib::static_classifier& C = chain->getStateClassification();
  DCASSERT(C.isNodeInClass(acc_state, 1));
#endif

  try {
    // int goal = chain->getClassOfState(acc_state);
    long goal = -acc_state;
    MCLib::Markov_chain::DTMC_distribution_options opts;
    opts.epsilon = epsilon;
    opts.setMaxSize(maxsize);
    LS_Vector ls_init;
    DCASSERT(initial);
    initial->ExportTo(ls_init);
    chain->computeDiscreteDistTTA(opts, ls_init, goal, dist);
    return true;
  }
  catch (MCLib::error e) {
    if (em->startError()) {
      em->causedBy(0);
      em->cerr() << "Couldn't compute discrete TTA: ";
      em->cerr() << e.getString();
      em->stopIO();
    }
    return false;
  }
}


// ******************************************************************

bool mclib_process::computeContinuousTTA(double dt, double epsilon,
  long maxsize, discrete_pdf &dist) const
{
  DCASSERT(chain);
  if (chain->isDiscrete()) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Can't compute continuous TTA on a DTMC.";
      em->stopIO();
    }
    return false;
  }

  long acc_state = getAcceptingState();
  if (acc_state < 0) {
    // Degenerate case - no accepting state
    // This should be a distribution of "infinity"
    dist.reset(0, -1, 0, 1.0);
    return true;
  }

  try {
    // int goal = chain->getClassOfState(acc_state);
    long goal = -acc_state;
    MCLib::Markov_chain::CTMC_distribution_options opts;
    opts.epsilon = epsilon;
    opts.setMaxSize(maxsize);
    LS_Vector ls_init;
    DCASSERT(initial);
    initial->ExportTo(ls_init);
    chain->computeContinuousDistTTA(opts, ls_init, goal, dt, dist);
    return true;
  }
  catch (MCLib::error e) {
    if (em->startError()) {
      em->causedBy(0);
      em->cerr() << "Couldn't compute continuous TTA: ";
      em->cerr() << e.getString();
      em->stopIO();
    }
    return false;
  }
}


// ******************************************************************
bool mclib_process::reachesAccept(double* x) const
{
  DCASSERT(chain);

  //
  // Check for degenerate case: no accepting state
  //
  long acc_state = getAcceptingState();
  if (acc_state < 0) {
    for (long i=chain->getNumStates()-1; i>=0; i--) x[i] = 0;
    return true;
  }

  //
  // Build set of target states = { accept }
  //
  intset target(chain->getNumStates());
  target.removeAll();
  target.addElement(acc_state);

  try {
    LS_Output outdata;
    timer w;
    startReachAcceptReport(w);
    chain->computeProbsToReach(target, x, 0, getSolverOptions(), outdata);
    stopReachAcceptReport(w, outdata.num_iters);
    return statusOK(em, outdata, "reaches accept");
  }
  catch (MCLib::error e) {
    return status(em, e, "reaches accept");
  }
}

// ******************************************************************

bool mclib_process::reachesAcceptBy(double t, double* x) const
{
  DCASSERT(chain);

  //
  // Set x to be all zeroes, except for the accepting state
  //
  for (long i=chain->getNumStates()-1; i>=0; i--) x[i] = 0;

  long acc_state = getAcceptingState();
  if (acc_state < 0) {
    // Degenerate case - no accepting state,
    // so nothing will reach it
    return true;
  }
  x[acc_state] = 1;

  if (t<=0) return true;

  try {
    timer w;
    if (is_discrete) {
      MCLib::Markov_chain::DTMC_transient_options opts;
      int it = int(t);
      startRevTransReport(w, it);
      chain->reverseTransient(it, x, opts);
      stopRevTransReport(w, opts.multiplications);
    } else {
      MCLib::Markov_chain::CTMC_transient_options opts;
      startRevTransReport(w, t);
      chain->reverseTransient(t, x, opts);
      stopRevTransReport(w, opts.multiplications);
    }
    return true;
  }
  catch (MCLib::error e) {
    if (em->startInternal(__FILE__, __LINE__)) {
      em->causedBy(0);
      em->internal() << "Unexpected error: ";
      em->internal() << e.getString();
      em->stopIO();
    }
    return false;
  }
}



// ******************************************************************

void mclib_process::showInternal(OutputStream &os) const
{
  // TBD

  os << "Internal representation for Markov chain:\n";
  os << "  Explicit representation using MCLib.  Cannot display yet, sorry\n";
  return;
}

// ******************************************************************

void mclib_process::showProc(OutputStream &os,
  const graph_lldsm::reachgraph::show_options &opt,
  state_lldsm::reachset* RSS, shared_state* st) const
{
  DCASSERT(chain);

  long na = chain->getNumEdges();
  long num_states = chain->getNumStates();

  if (state_lldsm::tooManyStates(num_states, &os))  return;
  if (graph_lldsm::tooManyArcs(na, &os))            return;

  if (graph_lldsm::TRIPLES == opt.STYLE) {
    os << "#states " << num_states << "\n";
    os << "#edges " << na << "\n";
  }

  const graphlib_displayer::edge_type reals =
    chain->edgesStoredAsDoubles()
    ? graphlib_displayer::DOUBLE
    : graphlib_displayer::FLOAT;

  graphlib_displayer foo(os, reals, opt, RSS, st);

  foo.pre_traversal();

  const bool by_rows = (graph_lldsm::INCOMING != opt.STYLE);
  if (by_rows)  chain->traverseOutgoing(foo);
  else          chain->traverseIncoming(foo);

  foo.post_traversal();
}




//
// For reachgraphs
//

// ******************************************************************

void mclib_process::getDeadlocked(intset &r) const
{
  //
  // Remove from r any state with some outgoing edge.
  // For a DTMC, that's everything.
  // For a CTMC, that's any state except absorbing ones.
  //
  DCASSERT(chain);
  if (chain->isDiscrete()) {
    r.removeAll();
    return;
  }
  //
  // CTMC case
  //
  const GraphLib::static_classifier& C = chain->getStateClassification();

  // Remove transients
  r.removeRange(C.firstNodeOfClass(0), C.lastNodeOfClass(0));

  // Remove all others
  r.removeRange(C.firstNodeOfClass(2), chain->getNumStates()-1);
}

// ******************************************************************

void mclib_process::getTSCCsSatisfying(intset &p) const
{
  DCASSERT(chain);
  const GraphLib::static_classifier& C = chain->getStateClassification();

  //
  // Remove transient states from p
  //
  p.removeRange(C.firstNodeOfClass(0), C.lastNodeOfClass(0));

  //
  // For each recurrent class, check if ALL states in the class satisfy p.
  // If not, remove all states in the class from p.
  //

  //
  // We do this by complementing the set, and searching for the smallest
  // element in !p to see if it is in the class or not.  If not,
  // then all states in the class satisfy p.
  //
  p.complement();
  for (long c=2; c<C.getNumClasses(); c++) {
    long nextnotp = p.getSmallestAfter( C.firstNodeOfClass(c)-1 );
    if (nextnotp < 0) continue; // not found
    if (nextnotp > C.lastNodeOfClass(c)) continue;
    //
    // Remove all states in this class from p,
    // which means *adding* them to !p.
    //
    p.addRange(C.firstNodeOfClass(c), C.lastNodeOfClass(c));
  } // for c
  p.complement();
}

// ******************************************************************

void mclib_process::count_edges(bool rt, ectl_reachgraph::CTL_traversal &TH) const
{
  DCASSERT(chain);
  for (long i=0; i<getNumStates(); i++) {
    TH.set_obligations(i, chain->getNumEdgesFor(rt, i) );
  }
}

// ******************************************************************

void mclib_process::traverse(bool rt, GraphLib::BF_graph_traversal &T) const
{
  DCASSERT(chain);
  if (rt) chain->traverseOutgoing(T);
  else    chain->traverseIncoming(T);
}



// ******************************************************************
// *                                                                *
// *                    mclib_reachgraph methods                    *
// *                                                                *
// ******************************************************************

mclib_reachgraph::mclib_reachgraph(mclib_process* MC)
{
  chain = Share(MC);
  DCASSERT(chain);
}

mclib_reachgraph::~mclib_reachgraph()
{
  Delete(chain);
}

void mclib_reachgraph::getNumArcs(long &na) const
{
  DCASSERT(chain);
  chain->getNumArcs(na);
}

void mclib_reachgraph::showInternal(OutputStream &os) const
{
  DCASSERT(chain);
  chain->showInternal(os);
}

void mclib_reachgraph::showArcs(OutputStream &os, const show_options &opt,
  state_lldsm::reachset* RSS, shared_state* st) const
{
  DCASSERT(chain);
  chain->showProc(os, opt, RSS, st);
}

void mclib_reachgraph::count_edges(bool rt, CTL_traversal &T) const
{
  DCASSERT(chain);
  chain->count_edges(rt, T);
}

void mclib_reachgraph::traverse(bool rt, GraphLib::BF_graph_traversal &T) const
{
  DCASSERT(chain);
  chain->traverse(rt, T);
}

void mclib_reachgraph::getDeadlocked(intset &r) const
{
  DCASSERT(chain);
  chain->getDeadlocked(r);
}

void mclib_reachgraph::getTSCCsSatisfying(intset &p) const
{
  DCASSERT(chain);
  chain->getTSCCsSatisfying(p);
}

