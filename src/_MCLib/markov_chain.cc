
/**
  Implementation of Markov_chain class.
*/

#include "mclib.h"

#include <math.h>
#include <string.h>

// #define DEBUG_CONSTRUCTOR
// #define DEBUG_PERIOD
// #define DEBUG_UNIFORMIZATION
// #define DEBUG_ACCUMULATE
// #define DEBUG_CTMCDIST
// #define DEBUG_REACHES

//------------------------------------------------------------

#ifdef DEBUG_PERIOD
  #define USES_IOSTREAM
#endif

#ifdef DEBUG_CONSTRUCTOR
  #define USES_IOSTREAM
#endif

#ifdef DEBUG_UNIFORMIZATION
  #define USES_IOSTREAM
#endif

#ifdef DEBUG_ACCUMULATE
  #define USES_IOSTREAM
#endif

#ifdef DEBUG_CTMCDIST
  #define USES_IOSTREAM
#endif

#ifdef DEBUG_REACHES
  #define USES_IOSTREAM
#endif

#ifdef USES_IOSTREAM
#include <iostream>

using namespace std;

template <class TYPE>
void showArray(const TYPE* A, long size)
{
  if (0==A) {
    cout << "null\n";
    return;
  }
  cout << "[";
  cout << A[0];
  for (long i=1; i<size; i++) cout << ", " << A[i];
  cout << "]\n";
}

void showGraph(GraphLib::static_graph &G)
{
  cout << "    #nodes: " << G.getNumNodes() << "\n";
  cout << "    #edges: " << G.getNumEdges() << "\n";
  cout << "    row pointer: ";
  showArray(G.RowPointer(), G.getNumNodes()+1);
  cout << "    column index: ";
  showArray(G.ColumnIndex(), G.getNumEdges());
  if (G.EdgeBytes() == sizeof(double)) {
    cout << "    value (doubles): ";
    showArray( (const double*) G.Labels(), G.getNumEdges());
  }
  if (G.EdgeBytes() == sizeof(float)) {
    cout << "    value (floats): ";
    showArray( (const float*) G.Labels(), G.getNumEdges());
  }
  if (G.EdgeBytes() == 0) {
    cout << "    no values\n";
  }
}

void showUnifStep(int steps, double poiss, double* p, double* a, long size)
{
  cout << "After " << steps << " steps, dtmc distribution is:\n\t";
  showArray(p, size);
  cout << "\tPoisson: " << poiss << "\n";
  cout << "accumulator so far:\n\t";
  showArray(a, size);
}

void showAccStep(int steps, double* p, double* a, long size)
{
  cout << "After " << steps << " steps, dtmc distribution is:\n\t";
  showArray(p, size);
  cout << "accumulator so far:\n\t";
  showArray(a, size);
}

void showDist(const char* name, const discrete_pdf &p)
{
  cout << name << ":\n"; 
  cout << "\tLeft " << p.left_trunc() << "\n";
  cout << "\tRight " << p.right_trunc() << "\n";
  cout << "\tInfinity " << p.f_infinity() << "\n";
  cout << "\tpdf ";
  if (p.left_trunc()) cout << "..., ";
  bool comma = false;
  for (long i=p.left_trunc(); i<=p.right_trunc(); i++) {
    if (comma) cout << ", ";
    cout << p.f(i);
    comma = true;
  }
  cout << "\n";
}

#endif  // #ifdef USES_IOSTREAM

template <class TYPE>
inline void zeroArray(TYPE* A, long size)
{
  for (long i=0; i<size; i++) A[i] = 0;
}


template <class REAL>
inline void fillVector(REAL* x, long size, const LS_Vector &y)
{
  zeroArray(x, size);
  if (y.index) {
    //
    // y is sparse
    //
    if (y.d_value) {
      for (long z=0; z<y.size; z++) {
        x[y.index[z]] = y.d_value[z];
      }
    } else {
      DCASSERT(y.f_value);
      for (long z=0; z<y.size; z++) {
        x[y.index[z]] = y.f_value[z];
      }
    }
  } else {
    //
    // y is (truncated) full
    //
    if (y.d_value) {
      for (long i=0; i<y.size; i++) {
        x[i] = y.d_value[i];
      }
    } else {
      DCASSERT(y.f_value);
      for (long i=0; i<y.size; i++) {
        x[i] = y.f_value[i];
      }
    }
  }
}


//
// Compute rowsums on a float graph
//
void float_graph_rowsums(const GraphLib::static_graph &G, double* rowsums)
{
  for (long s=0; s<G.getNumNodes(); s++) {
    double sum = 0;
    for (long e=G.RowPointer(s); e<G.RowPointer(s+1); e++) {
      sum += *((float*) G.Label(e));
    } // for e
    rowsums[s] += sum;
  } // for s
}
  
//
// Compute rowsums on a double graph
//
void double_graph_rowsums(const GraphLib::static_graph &G, double* rowsums)
{
  for (long s=0; s<G.getNumNodes(); s++) {
    double sum = 0;
    for (long e=G.RowPointer(s); e<G.RowPointer(s+1); e++) {
      sum += *((double*) G.Label(e));
    } // for e
    rowsums[s] += sum;
  } // for s
}

//
// In a double graph, find the first edge from state s
// such that (sum of edge probabilities/rates) > u.
//    @param  G       The graph, must have double edge weights.
//    @param  s       State.
//    @param  u       Value.
//    @param  total   Total so far, caller needs this if we fail
//    @return         Destination state of that edge, or -1 if none
long double_graph_edge_idf(const GraphLib::static_graph &G, long s, 
        double u, double &total)
{
    DCASSERT(G.EdgeBytes() == sizeof(double));
    for (long e=G.RowPointer(s); e<G.RowPointer(s+1); e++) {
      total += *((double*) G.Label(e));
      if (total > u) {
        return G.ColumnIndex(e);
      }
    }
    return -1;
}

//
// In a float  graph, find the first edge from state s
// such that (sum of edge probabilities/rates) > u.
//    @param  G       The graph, must have double edge weights.
//    @param  s       State.
//    @param  u       Value.
//    @param  total   Total so far, caller needs this if we fail
//    @return         Destination state of that edge, or -1 if none
long float_graph_edge_idf(const GraphLib::static_graph &G, long s, 
        double u, double &total)
{
    DCASSERT(G.EdgeBytes() == sizeof(float));
    for (long e=G.RowPointer(s); e<G.RowPointer(s+1); e++) {
      total += *((float*) G.Label(e));
      if (total > u) {
        return G.ColumnIndex(e);
      }
    }
    return -1;
}

template <class REAL>
inline void graphToMatrix(const GraphLib::static_graph &G, LS_CCS_Matrix<REAL> &M)
{
  DCASSERT(G.EdgeBytes() == sizeof(REAL));

  M.start = 0;
  M.stop = G.getNumNodes();
  M.size = G.getNumNodes();

  M.val = (const REAL*) G.Labels();
  M.row_ind = G.ColumnIndex();
  M.col_ptr = G.RowPointer();

  M.one_over_diag = 0;  // We'll do this by hand later
}
  
template <class REAL>
inline void graphToMatrix(const GraphLib::static_graph &G, LS_CRS_Matrix<REAL> &M)
{
  DCASSERT(G.EdgeBytes() == sizeof(REAL));

  M.start = 0;
  M.stop = G.getNumNodes();
  M.size = G.getNumNodes();

  M.val = (const REAL*) G.Labels();
  M.col_ind = G.ColumnIndex();
  M.row_ptr = G.RowPointer();

  M.one_over_diag = 0;  // We'll do this by hand later
}

/**
    Multiply by diagonals, where diagonals are stored explicitly.
    Essentially, the same as element-wise product.
    
      @param  x     Solution vector
      @param  in    Input vector
      @param  d     Diagonal vector
      @param  size  Vector size
*/
template <class REAL>
inline void adjustDiagonals(double* x, const double* in, const REAL* d,
  const long size)
{
  for (long i=0; i<size; i++) {
    x[i] += in[i] * d[i];
  }
}

/**
    Multiply by diagonals, where diagonals are determined implicitly
    based on rowsums.
      
      @param  x     Solution vector
      @param  in    Input vector
      @param  q     Uniformization constant or 1
      @param  rs    Rowsums
      @param  size  Vector size
*/
inline void adjustDiagonals(double* x, const double* in, double q, 
  const double* rs, const long size)
{
  for (long i=0; i<size; i++) {
    x[i] += (q - rs[i]) * in[i];
  }
}

/**
    Return true iff the given vectors are within a relative precision
    of epsilon of each other.
*/
bool vectorsWithinEpsilon(const double* A, const double* B, const long size,
  const double epsilon)
{
  for (long i=0; i<size; i++) {
    double d = A[i] - B[i];
    if (d < 0) d = -d;
    if (A[i]) d /= A[i];
    if (d > epsilon) return false;
  }
  return true;
}

/**
    Divide a vector by a scalar.
*/
inline void divideVector(double* A, double s, const long size)
{
  DCASSERT(s!=0);
  if (1.0 == s) return;
  for (long i=0; i<size; i++) {
    A[i] /= s;
  }
}

/**
    Normalize a vector so that its elements sum to one.
    (Unless the vector contains all zeroes.)
*/
inline void normalizeVector(double* A, const long size)
{
  double total = 0;
  for (long i=0; i<size; i++) {
    total += A[i];
  }
  if (0==total) return;
  divideVector(A, total, size);
}

/**
    Add to vector.
    Specifically, does A[i] += b*x[i] for all i.
*/
inline void addToVector(double* A, const double b, const double* x, 
  const long size)
{
  for (long i=0; i<size; i++) {
    A[i] += b*x[i];
  }
}
  
/**
    Add to vector.
    Specifically, does A[i] += x[i] for all i.
*/
inline void addToVector(double* A, const double* x, const long size)
{
  for (long i=0; i<size; i++) {
    A[i] += x[i];
  }
}


// ======================================================================
// |                                                                    |
// |                        grab_diagonals class                        |
// |                                                                    |
// ======================================================================

namespace MCLib {

  template <class REAL>
  class grab_diagonals : public GraphLib::BF_graph_traversal {
    public:
      grab_diagonals(long numst) {
        diagonals = new REAL[numst];
        zeroArray(diagonals, numst);
        current = 0;
        num_states = numst;
      }
      virtual ~grab_diagonals() {
        delete[] diagonals;
      }

      virtual bool hasNodesToExplore() {
        return current < num_states;
      }
      virtual long getNextToExplore() {
        return current++;
      }
      virtual bool visit(long src, long dest, const void* wt) {
        if (src == dest) {
          const REAL* label = (const REAL*) wt;
          diagonals[src] = *label;
        }
        return false;
      }

      REAL* takeDiags() {
        REAL* foo = diagonals;
        diagonals = 0;
        return foo;
      }
    public:
      long current;
      long num_states;
      REAL* diagonals; 
  };

};
  
// ======================================================================
// |                                                                    |
// |          Markov_chain::DTMC_distribution_options  methods          |
// |                                                                    |
// ======================================================================

void MCLib::Markov_chain::DTMC_distribution_options
::setMaxSize(long ms)
{
  if (ms<0) {
    ms = 0;
    // TBD - or should we throw a fit?
  }
  double* newdistro = (double*) realloc(distro, ms * sizeof(double));
  if (ms>0 && 0==newdistro) {
    throw MCLib::error(error::Out_Of_Memory);
  }
  distro = newdistro;
  if (needs_error) {
    double* newerror = (double*) realloc(error_distro, ms * sizeof(double));
    if (ms>0 && 0==newerror) {
      throw MCLib::error(error::Out_Of_Memory);
    }
    error_distro = newerror;
  }
  if (needs_distprod) {
    double* newdp = (double*) realloc(distprod, ms * sizeof(double));
    if (ms>0 && 0==newdp) {
      throw MCLib::error(error::Out_Of_Memory);
    }
    distprod = newdp;
  }
  max_size = ms;
}

// ======================================================================
// |                                                                    |
// |                        Markov_chain methods                        |
// |                                                                    |
// ======================================================================

MCLib::Markov_chain::Markov_chain(bool discrete, 
  GraphLib::dynamic_summable<double> &G, 
  const GraphLib::static_classifier &TSCCinfo,
  GraphLib::timer_hook *sw) : stateClass(TSCCinfo)
{
#ifdef DEBUG_CONSTRUCTOR
  cout << "Inside Markov_chain (double) constructor.\n";
#endif

  is_discrete = discrete;
  double_graphs = true;

  //
  // Defaults for selfloops arrays
  // 
  selfloops_d = 0;
  selfloops_f = 0;

  //
  // Allocate row sum array
  //
  rowsums = new double[G.getNumNodes()];

  //
  // If we're a DTMC, normalize rows and grab diagonals
  //
  if (discrete) {
    zeroArray(rowsums, G.getNumNodes());
    G.addRowSums(rowsums);
    G.divideRows(rowsums);

    grab_diagonals <double> foo(G.getNumNodes()); 
    G.traverse(foo);
    selfloops_d = foo.takeDiags();
    // Any rowsums equal to zero?  Add a self loop.
    for (long i=0; i<G.getNumNodes(); i++) {
      if (0==rowsums[i]) {
        selfloops_d[i] = 1;
      }
    }
  } 

  //
  // Common stuff (to double/float graphs) here
  //
  finish_construction(G, sw);

#ifdef DEBUG_CONSTRUCTOR
  cout << "Exiting Markov_chain (double) constructor.\n";
#endif
}

// ******************************************************************

MCLib::Markov_chain::Markov_chain(bool discrete, 
  GraphLib::dynamic_summable<float> &G, 
  const GraphLib::static_classifier &TSCCinfo,
  GraphLib::timer_hook *sw) : stateClass(TSCCinfo)
{
#ifdef DEBUG_CONSTRUCTOR
  cout << "Inside Markov_chain (float) constructor.\n";
#endif

  is_discrete = discrete;
  double_graphs = false;

  //
  // Defaults for selfloops arrays
  // 
  selfloops_d = 0;
  selfloops_f = 0;

  //
  // Allocate row sum array
  //
  rowsums = new double[G.getNumNodes()];

  //
  // If we're a DTMC, normalize rows and grab diagonals
  //
  if (discrete) {
    zeroArray(rowsums, G.getNumNodes());
    G.addRowSums(rowsums);
    G.divideRows(rowsums);

    grab_diagonals <float> foo(G.getNumNodes()); 
    G.traverse(foo);
    selfloops_f = foo.takeDiags();
    for (long i=0; i<G.getNumNodes(); i++) {
      if (0==rowsums[i]) {
        selfloops_f[i] = 1;
      }
    }
  } 

  //
  // Common stuff (to double/float graphs) here
  //
  finish_construction(G, sw);

#ifdef DEBUG_CONSTRUCTOR
  cout << "Exiting Markov_chain (float) constructor.\n";
#endif
}

// ******************************************************************

MCLib::Markov_chain::~Markov_chain()
{
  delete[] selfloops_d;
  delete[] selfloops_f;
  delete[] rowsums;
  delete[] one_over_rowsums_d;
  delete[] one_over_rowsums_f;
}

// ******************************************************************

void MCLib::Markov_chain::finish_construction(GraphLib::dynamic_graph &G,
    GraphLib::timer_hook *sw)
{
  //
  // Build subgraphs and remove any self loops
  //

  if (G.isByRows()) {
    G.splitAndExport(stateClass, false, G_byrows_diag, G_byrows_off, sw);
    G_bycols_diag.transposeFrom(G_byrows_diag);
    G_bycols_off.transposeFrom(G_byrows_off);
  } else {
    G.splitAndExport(stateClass, false, G_bycols_diag, G_bycols_off, sw);
    G_byrows_diag.transposeFrom(G_bycols_diag);
    G_byrows_off.transposeFrom(G_bycols_off);
  }

  //
  // Determine rowsums from split graphs
  //
  zeroArray(rowsums, G.getNumNodes());
  if (double_graphs) {
    double_graph_rowsums(G_byrows_diag, rowsums);
    double_graph_rowsums(G_byrows_off, rowsums);
  } else {
    float_graph_rowsums(G_byrows_diag, rowsums);
    float_graph_rowsums(G_byrows_off, rowsums);
  }


  //
  // Uniformization constant
  //
  if (isDiscrete()) {
    // 
    // DTMCs: set uniformization constant to 1
    // (TBD - should we do the same as CTMCs?)
    //
    uniformization_const = 1;
  } else {
    //
    // CTMCs: determine uniformization constant from rowsums
    //
    uniformization_const = rowsums[0];
    for (long i=1; i<G.getNumNodes(); i++) {
      if (rowsums[i] > uniformization_const) {
        uniformization_const = rowsums[i];
      }
    }
  }

  //
  // Sanity checks
  //
  DCASSERT(G_byrows_diag.getNumNodes() == G.getNumNodes());
  DCASSERT(G_byrows_off.getNumNodes() == G.getNumNodes());
  DCASSERT(G_bycols_diag.getNumNodes() == G.getNumNodes());
  DCASSERT(G_bycols_off.getNumNodes() == G.getNumNodes());

#ifdef DEBUG_CONSTRUCTOR
  cout << "  G_byrows_diag:\n";
  showGraph(G_byrows_diag);
  cout << "  G_byrows_off:\n";
  showGraph(G_byrows_off);
  cout << "  G_bycols_diag:\n";
  showGraph(G_bycols_diag);
  cout << "  G_bycols_off:\n";
  showGraph(G_bycols_off);
  cout << "  rowsums:\n";
  cout << "    ";
  showArray(rowsums, getNumStates());
#endif

  //
  // Build one_over_rowsums_d/f array, used for linear solvers
  //
  if (double_graphs) {
    one_over_rowsums_f = 0;
    one_over_rowsums_d = new double[getNumStates()];
    for (long i=0; i<G_byrows_diag.getNumNodes(); i++) {
      one_over_rowsums_d[i] = rowsums[i] ? (1.0/rowsums[i]) : 0.0;
    }
  } else {
    one_over_rowsums_d = 0;
    one_over_rowsums_f = new float[getNumStates()];
    for (long i=0; i<G_byrows_diag.getNumNodes(); i++) {
      one_over_rowsums_f[i] = rowsums[i] ? (1.0/rowsums[i]) : 0.0;
    }
  }

#ifdef DEBUG_CONSTRUCTOR
  if (selfloops_f) {
    cout << "  selfloops_f:\n";
    cout << "    ";
    showArray(selfloops_f, getNumStates());
  }
  if (selfloops_d) {
    cout << "  selfloops_d:\n";
    cout << "    ";
    showArray(selfloops_d, getNumStates());
  }
  if (one_over_rowsums_f) {
    cout << "  one_over_rowsums_f:\n";
    cout << "    ";
    showArray(one_over_rowsums_f, getNumStates());
  }
  if (one_over_rowsums_d) {
    cout << "  one_over_rowsums_d:\n";
    cout << "    ";
    showArray(one_over_rowsums_d, getNumStates());
  }
#endif
}

// ******************************************************************

long MCLib::Markov_chain::getNumEdges() const
{
  long ne = G_byrows_diag.getNumEdges() + G_byrows_off.getNumEdges();
  if (selfloops_d) {
    for (long i=0; i<getNumStates(); i++) {
      if (selfloops_d[i]) ne++;
    }
  } else if (selfloops_f) {
    for (long i=0; i<getNumStates(); i++) {
      if (selfloops_f[i]) ne++;
    }
  }
  return ne;
}

// ******************************************************************

long MCLib::Markov_chain::getNumEdgesFor(bool incoming, long i) const
{
  long edges = 0;
  if (incoming) {
    edges += G_bycols_diag.getNumEdgesFor(i);
    edges += G_bycols_off.getNumEdgesFor(i);
  } else {
    edges += G_byrows_diag.getNumEdgesFor(i);
    edges += G_byrows_off.getNumEdgesFor(i);
  }
  // 
  // Add diagonals, if we have them
  //
  if (selfloops_d) {
    if (selfloops_d[i]) edges++;
  } else if (selfloops_f) {
    if (selfloops_f[i]) edges++;
  }
  return edges;
}

// ******************************************************************

size_t MCLib::Markov_chain::getMemTotal() const
{
  size_t mem = 
    stateClass.getMemTotal() +
    G_byrows_diag.getMemTotal() +
    G_byrows_off.getMemTotal() +
    G_bycols_diag.getMemTotal() +
    G_bycols_off.getMemTotal(); 

  if (rowsums)            mem += getNumStates() * sizeof(double);
  if (one_over_rowsums_d) mem += getNumStates() * sizeof(double);
  if (one_over_rowsums_f) mem += getNumStates() * sizeof(float);
  if (selfloops_d)        mem += getNumStates() * sizeof(double);
  if (selfloops_f)        mem += getNumStates() * sizeof(float);

  return mem;
}

// ******************************************************************

bool MCLib::Markov_chain::traverseOutgoing(GraphLib::BF_graph_traversal &t)
const
{
  while (t.hasNodesToExplore()) {
      long s = t.getNextToExplore();

      if (s<0 || s>=G_byrows_diag.getNumNodes()) {
        throw GraphLib::error(GraphLib::error::Bad_Index);
      }

      // Explore diagonal edges from s
      for (long z=G_byrows_diag.RowPointer(s); 
            z<G_byrows_diag.RowPointer(s+1); z++) 
      {
        if (t.visit(s, G_byrows_diag.ColumnIndex(z), G_byrows_diag.Label(z))) {
          return true;
        }
      }

      // Explore self loop edges from s
      if (selfloops_d && selfloops_d[s]) {
        if (t.visit(s, s, selfloops_d+s)) {
          return true;
        }
      }
      if (selfloops_f && selfloops_f[s]) {
        if (t.visit(s, s, selfloops_f+s)) {
          return true;
        }
      }

      // Explore off-diagonal edges from s
      for (long z=G_byrows_off.RowPointer(s); 
            z<G_byrows_off.RowPointer(s+1); z++) 
      {
        if (t.visit(s, G_byrows_off.ColumnIndex(z), G_byrows_off.Label(z))) {
          return true;
        }
      }

  } // while
  return false;
}

// ******************************************************************

bool MCLib::Markov_chain::traverseIncoming(GraphLib::BF_graph_traversal &t)
const
{
  while (t.hasNodesToExplore()) {
      long s = t.getNextToExplore();

      if (s<0 || s>=G_bycols_diag.getNumNodes()) {
        throw GraphLib::error(GraphLib::error::Bad_Index);
      }

      // Explore diagonal edges to s
      for (long z=G_bycols_diag.RowPointer(s); 
            z<G_bycols_diag.RowPointer(s+1); z++) 
      {
        if (t.visit(s, G_bycols_diag.ColumnIndex(z), G_bycols_diag.Label(z))) {
          return true;
        }
      }

      // Explore self loop edges to s
      if (selfloops_d && selfloops_d[s]) {
        if (t.visit(s, s, selfloops_d+s)) {
          return true;
        }
      }
      if (selfloops_f && selfloops_f[s]) {
        if (t.visit(s, s, selfloops_f+s)) {
          return true;
        }
      }

      // Explore off-diagonal edges to s
      for (long z=G_bycols_off.RowPointer(s); 
            z<G_bycols_off.RowPointer(s+1); z++) 
      {
        if (t.visit(s, G_bycols_off.ColumnIndex(z), G_bycols_off.Label(z))) {
          return true;
        }
      }

  } // while
  return false;
}

// ******************************************************************

long MCLib::Markov_chain::computePeriodOfClass(long c) const
{
  /*
    Algorithm adapted from discussions in chapter 7 of Stewart.  Idea:
      (1) Select a starting state (for us, the first one)
      (2) For each state, determine its distance from the start.
      (3) If there is an edge from i to j, with
              distance(i) >= distance(j),
          then add  
              distance(i) - distance(j) + 1
          to a set of integers.
      (4) The GCD of the resulting set gives the period.  
          Since GCD is associative, we don't need to construct
          the set of integers, just remember the GCD "so far".

    The following helper class magically does most of this.
  */

  // ======================================================================
  class BF_period : public GraphLib::BF_with_queue {
      public:
          // Constructor.
          //    first: index of first state in the recurrent class
          //    size:  total number of states in the recurrent class
          // Dists: initialize to -1 everywhere
          BF_period(long first, long size) 
          : BF_with_queue(size)
          {
              raw_dist = new long[size];
              raw_dist[0] = 0;
              for (long i=1; i<size; i++) {
                raw_dist[i] = -1;
              }
              distance = raw_dist-first;
              // note that distance[first] is the same as raw_dist[0]
              queuePush(first);
              first_pass = true;
#ifdef DEBUG_PERIOD
              cout << "Computing period, first pass.\n";
#endif
          }
          virtual ~BF_period() {
              delete[] raw_dist;
          }
          virtual bool visit(long src, long dest, const void* wt) {
              if (first_pass) {
                  if (distance[dest]>=0) return false;
                  distance[dest] = distance[src]+1;
                  queuePush(dest);
#ifdef DEBUG_PERIOD
                  cout << "  edge " << src << " -> " << dest << " updates distance\n";
                  cout << "      of " << dest << " to " << distance[dest] << "\n";
#endif
              } else {
                  long d = distance[src] - distance[dest] + 1;
                  if (d) {
                      if (period) period = GCD(period, d);
                      else        period = d;
#ifdef DEBUG_PERIOD
                  cout << "  edge " << src << " -> " << dest << " has d=" ;
                  cout << distance[src] << "-" << distance[dest] << "+1=" << d << "\n";
                  cout << "      updating period to " << period << "\n";
#endif
                  }
              }
              return false;
          }
          inline void secondPass(long first, long size) {
#ifdef DEBUG_PERIOD
              cout << "First pass completed, distances:\n  [" << distance[first];
              for (long i=1; i<size; i++) {
                cout << ", " << distance[first+i];
              }
              cout << "]\n";
#endif
              queueReset();
              first_pass = false;
              for (long i=0; i<size; i++) {
                queuePush(first+i);
              }
              period = 0;
          }
          inline long getPeriod() const { 
              return period;
          }
      private:
        long* raw_dist;
        long* distance;
        long period;
        bool first_pass;
  };  // class BF_distances
  // ======================================================================

  //
  // Special case: absorbing states
  //
  if (1==c) return 1;

  //
  // Check for invalid c's
  //
  if (c<1)  return 0;
  if (c>= stateClass.getNumClasses()) return 0;

  //
  // Easy case - if we have a self loop, then the period must be 1
  //
  DCASSERT(rowsums);
  if (isDiscrete()) {
    if (double_graphs) {
      DCASSERT(selfloops_d);
      for (long i=stateClass.firstNodeOfClass(c); i<=stateClass.lastNodeOfClass(c); i++) {
        if (selfloops_d[i]) return 1;
      }
    } else {
      DCASSERT(selfloops_f);
      for (long i=stateClass.firstNodeOfClass(c); i<=stateClass.lastNodeOfClass(c); i++) {
        if (selfloops_f[i]) return 1;
      }
    }
  }

  // First pass: forward reachability search from
  // start state, track state distances.
  BF_period T(stateClass.firstNodeOfClass(c), stateClass.sizeOfClass(c));
  G_byrows_diag.traverse(T); 

  // Second pass: check all edges, and update period
  // based on (3) above.
  T.secondPass(stateClass.firstNodeOfClass(c), stateClass.sizeOfClass(c));
  G_byrows_diag.traverse(T); 

  return T.getPeriod();
}

// ******************************************************************

namespace MCLib {
  template <class MATRIX, class REAL>
  void templ_dtmc_transient(MATRIX &Qdiag, MATRIX &Qoff, const REAL* diags,
    int t, double* p, bool normalize, Markov_chain::DTMC_transient_options &opts)
  {
      DCASSERT(diags);

      const long size = Qdiag.Size();
      //
      // Set up auxiliary vectors if necessary
      //
      if (0== opts.vm_result) {
        opts.vm_result = new double[size];
      }

      double* aux = opts.vm_result;

      for (opts.multiplications=0; opts.multiplications<t; opts.multiplications++) {
        // VM multiply
        zeroArray(aux, size);
        Qdiag.VectorMatrixMultiply(aux, p);
        Qoff.VectorMatrixMultiply(aux, p);
        adjustDiagonals(aux, p, diags, size);

        if (normalize) normalizeVector(aux, size);

        // aux is now the distribution after one step.

        // Check if we've hit steady state
        if (vectorsWithinEpsilon(p, aux, size, opts.ssprec)) {
          opts.multiplications++;
          break;
        }
    
        SWAP(aux, p);
      } // for opts.multiplications

      // we're done.  p is our answer.
      // Since we swapped pointers around, p might be different
      // from the original p.  Check for that:
      if (aux != opts.vm_result) {
        // aux is the original p.  Copy into p.
        memcpy(aux, p, size * sizeof(double));
      }
  }
}

// ******************************************************************

void MCLib::Markov_chain::computeTransient(int t, double* p, 
  DTMC_transient_options &opts) const
{
  if (0==p) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (!isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_transient(Qdiag, Qoff, selfloops_d, t, p, true, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_transient(Qdiag, Qoff, selfloops_f, t, p, true, opts);
  }
}


// ******************************************************************

void MCLib::Markov_chain::reverseTransient(int t, double* p, 
  DTMC_transient_options &opts) const
{
  if (0==p) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (!isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_bycols_diag, Qdiag);
    graphToMatrix(G_bycols_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_transient(Qdiag, Qoff, selfloops_d, t, p, false, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_bycols_diag, Qdiag);
    graphToMatrix(G_bycols_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_transient(Qdiag, Qoff, selfloops_f, t, p, false, opts);
  }
}


// ******************************************************************

namespace MCLib {
  template <class MATRIX, class DISTRO>
  void templ_ctmc_transient(MATRIX &Qdiag, MATRIX &Qoff, const double* rowsums,
    double t, double* p, bool normalize, const DISTRO &dist, double dadj,
    Markov_chain::CTMC_transient_options &opts)
  {
      const long size = Qdiag.Size();
      //
      // Set up auxiliary vectors if necessary
      //
      if (0== opts.vm_result) {
        opts.vm_result = new double[size];
      }
      if (0== opts.accumulator) {
        opts.accumulator = new double[size];
      }

      opts.poisson_right = dist.right_trunc();

      //
      // Initialize vectors
      //
      zeroArray(opts.accumulator, size);
      double* aux = opts.vm_result;
      double* myp = p;

      // 
      // Add initial distribution
      //
      addToVector(opts.accumulator, dist.f(0)/dadj, p, size);
#ifdef DEBUG_UNIFORMIZATION
      showUnifStep(0, dist.f(0)/dadj, p, opts.accumulator, size);
#endif

      //
      // Loop and add distribution at time n 
      //
      opts.multiplications = 0;
      long i;
      for (i=0; i<dist.right_trunc(); i++) {
        // VM multiply
        zeroArray(aux, size);
        Qdiag.VectorMatrixMultiply(aux, myp);
        Qoff.VectorMatrixMultiply(aux, myp);
        adjustDiagonals(aux, myp, opts.q, rowsums, size);

        if (normalize)  normalizeVector(aux, size);
        else            divideVector(aux, opts.q, size);

        // aux is now the distribution after one step.

        // Check if we've hit steady state
        opts.multiplications++;
        if (vectorsWithinEpsilon(myp, aux, size, opts.ssprec)) break;

        // Add to accumulator
        addToVector(opts.accumulator, dist.f(i+1)/dadj, aux, size);

#ifdef DEBUG_UNIFORMIZATION
        showUnifStep(i+1, dist.f(i+1)/dadj, aux, opts.accumulator, size);
#endif
    
        SWAP(aux, myp);
      } // for i

      // If we detected steady state, then finish the computation
      // assuming aux vector does not change.
      double remaining_probs = 0;
      for (; i<dist.right_trunc(); i++) {
        remaining_probs += dist.f(i+1);
      }
      if (remaining_probs) {
        addToVector(opts.accumulator, remaining_probs/dadj, aux, size);
#ifdef DEBUG_UNIFORMIZATION
        showUnifStep(dist.right_trunc(), remaining_probs/dadj, aux, opts.accumulator, size);
#endif
      }

      // we're done.  accumulator is our answer.
      // Copy it into p.
      memcpy(p, opts.accumulator, size * sizeof(double));
  }
}

// ******************************************************************

void MCLib::Markov_chain::computeTransient(double t, double* p, 
  CTMC_transient_options &opts) const
{
  if (0==p) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  opts.q = MAX(opts.q, getUniformizationConst());

  //
  // Set up poisson distribution
  //
  discrete_pdf poisson_pdf;
  computePoissonPDF(opts.q * t, opts.epsilon, poisson_pdf);

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, p, true, poisson_pdf, 1, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, p, true, poisson_pdf, 1, opts);
  }
}


// ******************************************************************

void MCLib::Markov_chain::reverseTransient(double t, double* p, 
  CTMC_transient_options &opts) const
{
  if (0==p) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  opts.q = MAX(opts.q, getUniformizationConst());

  //
  // Set up poisson distribution
  //
  discrete_pdf poisson_pdf;
  computePoissonPDF(opts.q * t, opts.epsilon, poisson_pdf);

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_bycols_diag, Qdiag);
    graphToMatrix(G_bycols_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, p, false, poisson_pdf, 1, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_bycols_diag, Qdiag);
    graphToMatrix(G_bycols_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, p, false, poisson_pdf, 1, opts);
  }
}


// ******************************************************************

namespace MCLib {
  template <class MATRIX, class REAL>
  void templ_dtmc_accumulate(MATRIX &Qdiag, MATRIX &Qoff, const REAL* diags,
    int t, double* n, Markov_chain::DTMC_transient_options &opts)
  {
      DCASSERT(diags);
      const long size = Qdiag.Size();
      //
      // Set up auxiliary vectors if necessary
      //
      if (0== opts.vm_result) {
        opts.vm_result = new double[size];
      }
      if (0== opts.accumulator) {
        opts.accumulator = new double[size];
      }
      zeroArray(opts.accumulator, size);

      double* aux = opts.vm_result;
      double* myp = n;

      addToVector(opts.accumulator, n, size);
#ifdef DEBUG_ACCUMULATE
      showAccStep(0, n, opts.accumulator, size);
#endif

      for (opts.multiplications=0; opts.multiplications<t; opts.multiplications++) {
        // VM multiply
        zeroArray(aux, size);
        Qdiag.VectorMatrixMultiply(aux, myp);
        Qoff.VectorMatrixMultiply(aux, myp);
        adjustDiagonals(aux, myp, diags, size);

        normalizeVector(aux, size);

        // aux is now the distribution after one step.
        // Add it to the accumulator.
        addToVector(opts.accumulator, aux, size);

#ifdef DEBUG_ACCUMULATE
        showAccStep(1+opts.multiplications, aux, opts.accumulator, size);
#endif

        // Check if we've hit steady state
        if (vectorsWithinEpsilon(myp, aux, size, opts.ssprec)) {
          opts.multiplications++;
          break;
        }
    
        SWAP(aux, myp);
      } // for opts.multiplications

      //
      // If we bailed out early because aux has converged,
      // add aux the appropriate number of times to the accumulator
      //
      if (opts.multiplications<t) {
        addToVector(opts.accumulator, t-opts.multiplications, aux, size);
#ifdef DEBUG_ACCUMULATE
        showAccStep(t, aux, opts.accumulator, size);
#endif
      }

      // we're done.  accumulator is our answer.
      // Copy it into n.
      memcpy(n, opts.accumulator, size * sizeof(double));
  }
}

// ******************************************************************

void MCLib::Markov_chain::accumulate(int t, const double* p0, double* n0t,
    DTMC_transient_options &opts) const
{
  if (0==n0t) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (!isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  if (p0) {
    memcpy(n0t, p0, getNumStates() * sizeof(double));
  }

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_accumulate(Qdiag, Qoff, selfloops_d, t, n0t, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_accumulate(Qdiag, Qoff, selfloops_f, t, n0t, opts);
  }

}

// ******************************************************************

void MCLib::Markov_chain::accumulate(double t, const double* p0, double* n0t,
    CTMC_transient_options &opts) const
{
  if (0==n0t) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  opts.q = MAX(opts.q, getUniformizationConst());

  //
  // Set up poisson distribution
  //
  discrete_pdf poisson_pdf;
  computePoissonPDF(opts.q * t, opts.epsilon, poisson_pdf);
  discrete_1mcdf poisson_1mcdf;
  poisson_1mcdf.setFromPDF(poisson_pdf);

#ifdef DEBUG_UNIFORMIZATION
  cout << "Poisson " << opts.q*t << " distribution (Prob x = i):\n\t" << poisson_pdf.f(0);
  for (int i=1; i<=poisson_pdf.right_trunc(); i++) {
    cout << ", " << poisson_pdf.f(i);
  }
  cout << "\n";
  cout << "Poisson " << opts.q*t << " distribution (Prob x > i):\n\t" << poisson_1mcdf.f(0);
  for (int i=1; i<=poisson_1mcdf.right_trunc(); i++) {
    cout << ", " << poisson_1mcdf.f(i);
  }
  cout << "\n";
#endif

  //
  // Set up n0t
  //
  if (p0) {
    memcpy(n0t, p0, getNumStates() * sizeof(double));
  }

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, n0t, true, poisson_1mcdf, opts.q, opts);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_ctmc_transient(Qdiag, Qoff, rowsums, t, n0t, true, poisson_1mcdf, opts.q, opts);
  }
}




// ******************************************************************

void MCLib::Markov_chain::computeTTA(const LS_Vector &p0, double* p, 
    const LS_Options &opt, LS_Output &out) const
{
  //
  // Trivial case: no transient states
  //
  if (0==stateClass.sizeOfClass(0)) {
    out.status = LS_Success;
    out.num_iters = 0;
    out.relaxation = 0;
    out.precision = 0;
    return;
  }

  //
  // At least one transient state.  Have to do real work.
  // Check vectors.
  //
  if (0==p) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }
  if (0==p0.size) {
    throw MCLib::error(MCLib::error::Null_Vector);
  }

  //
  // Clear out p
  //
  for (long i=stateClass.firstNodeOfClass(0); i<=stateClass.lastNodeOfClass(0); i++) {
    p[i] = 0;
  } // for i

  //
  // Solve linear system of equations:
  //    n * Qtt = -p0
  // where n gives the expected time spent in each state.
  // Note: since we actually use p0, we need to negate n when we're done.

  if (double_graphs) {
    //
    // Set up matrix (shallow copies here)
    //
    LS_CRS_Matrix_double Qtt;
    graphToMatrix(G_bycols_diag, Qtt);
    Qtt.start = stateClass.firstNodeOfClass(0);
    Qtt.stop  = 1+stateClass.lastNodeOfClass(0);
    Qtt.one_over_diag = one_over_rowsums_d;

    //
    // Call the linear solver
    //
    Solve_Axb(Qtt, p, p0, opt, out);
  } else {
    //
    // Set up matrix (shallow copies here)
    //
    LS_CRS_Matrix_float Qtt;
    graphToMatrix(G_bycols_diag, Qtt);
    Qtt.start = stateClass.firstNodeOfClass(0);
    Qtt.stop  = 1+stateClass.lastNodeOfClass(0);
    Qtt.one_over_diag = one_over_rowsums_f;

    //
    // Call the linear solver
    //
    Solve_Axb(Qtt, p, p0, opt, out);
  }

  //
  // Negate our solution
  //
  for (long i=stateClass.firstNodeOfClass(0); i<=stateClass.lastNodeOfClass(0); i++) {
    p[i] = -p[i];
  } // for i
}

// ******************************************************************

void MCLib::Markov_chain::computeFirstRecurrentProbs(const LS_Vector &p0, 
    double* np, const LS_Options &opt, LS_Output &out) const
{
  out.status = LS_Success;
  out.num_iters = 0;
  out.relaxation = 0;
  out.precision = 0;

  for (long i=0; i<getNumStates(); i++) {
    np[i] = 0;
  }
  if (stateClass.sizeOfClass(0)) {
    // 
    // Determine time spent in each transient state
    //
    computeTTA(p0, np, opt, out);

    //
    // Multiply np by  Pta
    //

    if (double_graphs) {
      //
      // Set up matrix (shallow copies here)
      //
      LS_CRS_Matrix_double Qta;
      graphToMatrix(G_byrows_off, Qta);
      
      //
      // Multiply np += np * Qta 
      //
      Qta.VectorMatrixMultiply(np, np);
    } else {
      //
      // Set up matrix (shallow copies here)
      //
      LS_CRS_Matrix_float Qta;
      graphToMatrix(G_byrows_off, Qta);

      //
      // Multiply np += np * Qta 
      //
      Qta.VectorMatrixMultiply(np, np);
    }

  } // if there are transient states

  //
  // Add initial probabilities
  //
  if (p0.index) {
    //
    // p0 is sparse
    //
    if (p0.d_value) {
      for (long z=0; z<p0.size; z++) {
        if (p0.index[z] > stateClass.lastNodeOfClass(0)) {
          np[p0.index[z]] += p0.d_value[z];
        }
      }
    } else {
      DCASSERT(p0.f_value);
      for (long z=0; z<p0.size; z++) {
        if (p0.index[z] > stateClass.lastNodeOfClass(0)) {
          np[p0.index[z]] += p0.f_value[z];
        }
      }
    }
  } else {
    //
    // p0 is (truncated) full
    //
    if (p0.d_value) {
      for (long i=stateClass.firstNodeOfClass(1); i<p0.size; i++) {
        np[i] += p0.d_value[i];
      }
    } else {
      DCASSERT(p0.f_value);
      for (long i=stateClass.firstNodeOfClass(1); i<p0.size; i++) {
        np[i] += p0.f_value[i];
      }
    }
  }

  //
  // Normalize probs for recurrent states 
  //
  normalizeVector(
    np+stateClass.firstNodeOfClass(1), 
    getNumStates()-stateClass.firstNodeOfClass(1)
  );
}

// ******************************************************************

void MCLib::Markov_chain::computeInfinityDistribution(const LS_Vector &p0, 
        double* p, const LS_Options &opt, LS_Output &out) const
{
  //
  // Start with first hitting probabilities,
  // we will use it to determine the probability of reaching
  // each of the recurrent classes.
  //
  computeFirstRecurrentProbs(p0, p, opt, out);
  
  //
  // Bail out if that went badly.
  //
  if ((LS_Success != out.status) && (LS_No_Convergence != out.status))  return;

  //
  //  For each recurrent class (except absorbing states, those are done :^)
  //    (1) determine total probability of hitting the class by summing
  //    (2) if this is non-zero, determine stationary distro for the class
  //    (3) scale this by the hitting probability
  //
  for (long c=2; c<stateClass.getNumClasses(); c++) {

    //
    // Determine probability of reaching class c
    //
    double reachprob = 0.0;
    for (long i=stateClass.firstNodeOfClass(c); 
      i<=stateClass.lastNodeOfClass(c); i++) 
    {
      reachprob += p[i];
    } // for i

    if (0==reachprob) continue; // nothing to do

    //
    // Solve linear system p*Q_cc = 0, for block of Q restricted to class c.
    //

    //
    // Set p to uniform distribution; required for linear solver
    //
    for (long i=stateClass.firstNodeOfClass(c); 
      i<=stateClass.lastNodeOfClass(c); i++) 
    {
      p[i] = 1;
    } // for i

    if (double_graphs) {
      //
      // Set up matrix (shallow copies here)
      //
      LS_CRS_Matrix_double Qcc;
      graphToMatrix(G_bycols_diag, Qcc);
      Qcc.start = stateClass.firstNodeOfClass(c);
      Qcc.stop  = 1+stateClass.lastNodeOfClass(c);
      Qcc.one_over_diag = one_over_rowsums_d;

      //
      // Call the linear solver
      //
      Solve_AxZero(Qcc, p, opt, out);
    } else {
      //
      // Set up matrix (shallow copies here)
      //
      LS_CRS_Matrix_float Qcc;
      graphToMatrix(G_bycols_diag, Qcc);
      Qcc.start = stateClass.firstNodeOfClass(c);
      Qcc.stop  = 1+stateClass.lastNodeOfClass(c);
      Qcc.one_over_diag = one_over_rowsums_f;

      //
      // Call the linear solver
      //
      Solve_AxZero(Qcc, p, opt, out);
    }

    //
    // We have conditional probabilities:
    //   given we start in class c, what is the steady-state
    //   probability for state i.
    // Fix that by multiplying by the probability to reach class c.
    //
    for (long i=stateClass.firstNodeOfClass(c); 
      i<=stateClass.lastNodeOfClass(c); i++) 
    {
      p[i] *= reachprob;
    } // for i

  } // for c

  //
  // Done, just need to zero out the transient part
  //
  for (long i=stateClass.firstNodeOfClass(0); 
      i<=stateClass.lastNodeOfClass(0); i++) 
  {
      p[i] = 0;
  } // for i
}

// ******************************************************************

void MCLib::Markov_chain::computeProbsToReach(const intset &target, double* p,
      double* aux, const LS_Options &opt, LS_Output &out) const
{
  //
  // allocate auxiliary vector, if needed
  //
  double* myaux;
  if (aux) {
    myaux = aux;
  } else {
    myaux = (double*) malloc(getNumStates() * sizeof(double));
    if (0==myaux) {
      throw error(error::Out_Of_Memory);
    }
  }

  //
  // set up rhs for our linear solver
  //
  LS_Vector b;
  b.size = getNumStates();
  b.index = 0;
  b.d_value = myaux;
  b.f_value = 0;

  //
  // Set p[i] = 1, if i is recurrent and belongs to target set; 0 otherwise
  //
  zeroArray(p, getNumStates());
  long i = stateClass.lastNodeOfClass(0);
  for (i = target.getSmallestAfter(i); i>=0 && i<=getNumStates();
       i = target.getSmallestAfter(i))
  {
    p[i] = 1;
  }  // for i

#ifdef DEBUG_REACHES
  cout << "Target set: ";
  showArray(p, getNumStates());
#endif

  //
  // Build b = -Qta * target, and then solve Qtt * p = b
  //
  zeroArray(myaux, getNumStates());
  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qta;
    LS_CRS_Matrix_double Qtt;
    graphToMatrix(G_byrows_diag, Qtt);
    Qtt.start = stateClass.firstNodeOfClass(0);
    Qtt.stop  = 1+stateClass.lastNodeOfClass(0);
    Qtt.one_over_diag = one_over_rowsums_d;
    graphToMatrix(G_byrows_off, Qta);

    //
    // Multiply b += Qta * p, then negate it
    //
    Qta.MatrixVectorMultiply(myaux, p);
    for (long i=stateClass.firstNodeOfClass(0); i<=stateClass.lastNodeOfClass(0); i++) {
      myaux[i] = -myaux[i];
    } // for i

    //
    // Call the linear solver
    //
    Solve_Axb(Qtt, p, b, opt, out);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qta;
    LS_CRS_Matrix_float Qtt;
    graphToMatrix(G_byrows_diag, Qtt);
    Qtt.start = stateClass.firstNodeOfClass(0);
    Qtt.stop  = 1+stateClass.lastNodeOfClass(0);
    Qtt.one_over_diag = one_over_rowsums_f;
    graphToMatrix(G_byrows_off, Qta);

    //
    // Multiply b += Qta * p, then negate it
    //
    Qta.MatrixVectorMultiply(myaux, p);
    for (long i=stateClass.firstNodeOfClass(0); i<=stateClass.lastNodeOfClass(0); i++) {
      myaux[i] = -myaux[i];
    } // for i

    //
    // Call the linear solver
    //
    Solve_Axb(Qtt, p, b, opt, out);
  }


  //
  // Cleanup, if needed
  //
  if (0==aux) {
    free(myaux);
  }
}

// ******************************************************************

namespace MCLib {
  template <class MATRIX>
  void templ_dtmc_distro(MATRIX &Qdiag, MATRIX &Qoff, const double* rowsums,
    Markov_chain::DTMC_distribution_options &opts, double q,
    long cstart, long cstop, long num_transient,
    discrete_pdf &dist)
  {
      const long size = Qdiag.Size();

      double prob_infinity = 0;

      long time;
      for (time=0; ; time++) {

        // 
        // Get the probability that we just entered class c,
        // by summing the probabilities for class c.
        //
        double just_entered = 0;
        for (long s=cstart; s<cstop; s++) {
          just_entered += opts.prob_vect[s];
        }

        // 
        // Add to distribution
        //
        CHECK_RANGE(0, time, opts.max_size);
        opts.distro[time] = just_entered;

        //
        // Get the probability that we just entered all other classes.
        //
        just_entered = 0;
        for (long s=num_transient; s<cstart; s++) {
          just_entered += opts.prob_vect[s];
        }
        for (long s=cstop; s<size; s++) {
          just_entered += opts.prob_vect[s];
        }
        //
        // Once we get to another class, we cannot possibly reach c.
        // Call that the probability of infinity.
        //
        prob_infinity += just_entered;

        if (opts.needs_error) {
          //
          // Keep track of the amount of probability mass 
          // still in transient states.  Used by CTMCs
          // to compute precision.
          //
          opts.error_distro[time] = 0;
          for (long s=0; s<num_transient; s++) {
            opts.error_distro[time] += opts.prob_vect[s];
          }
        }

        //
        // Did we timeout?  If so, determine error and break.
        //
        if (time >= opts.max_size) {
          if (opts.needs_error) {
            // we already computed this!
            opts.precision = opts.error_distro[time];
          } else {
            opts.precision = 0;
            for (long s=0; s<num_transient; s++) {
              opts.precision += opts.prob_vect[s];
            }
          }
          break;
        }

        //
        // Check if error is below epsilon;
        // if so, we can stop.
        //
        double error = 0;
        if (opts.needs_error) {
          // we already computed this!
          error = opts.error_distro[time];
        } else {
          for (long s=0; s<num_transient; s++) {
            error += opts.prob_vect[s];
            if (error > opts.epsilon) break;
          }
        }
        if (error <= opts.epsilon) {
          opts.precision = error;
          break;
        }

        //
        // Determine probability distribution at next time.
        // Except, and this is the clever bit, we'll
        // first set the probabilities to zero for non-transient states,
        // that way we capture the probability of entering
        // at exactly this time.
        //
        for (long s=num_transient; s<size; s++) {
          opts.prob_vect[s] = 0;
        }
        zeroArray(opts.vm_result, size);
        Qdiag.VectorMatrixMultiply(opts.vm_result, opts.prob_vect);
        Qoff.VectorMatrixMultiply(opts.vm_result, opts.prob_vect);
        adjustDiagonals(opts.vm_result, opts.prob_vect, q, rowsums, size);

        //
        // Do NOT normalize.  Elements will not necessarily sum to one,
        // because we have zeroed out the non-transient probabilities.
        //
        // However, we do need to divide by the uniformization constant q.
        //
        divideVector(opts.vm_result, q, size);

        SWAP(opts.prob_vect, opts.vm_result);

      } // for time

      //
      // Done.
      // Answer is in opts.distro,
      // convert it to a proper distribution.
      //
      dist.copyFromAndTruncate(opts.distro, time+1, prob_infinity);
  }
}
// ******************************************************************

void MCLib::Markov_chain::computeDiscreteDistTTA(
  DTMC_distribution_options &opts,
  const LS_Vector &p0,
  long c,
  discrete_pdf &dist) const
{
  if (!isDiscrete()) {
    throw MCLib::error(error::Wrong_Type);
  }
  if (0==p0.size) {
    throw MCLib::error(error::Null_Vector);
  }
  if (0==opts.max_size) {
    throw MCLib::error(error::Null_Vector);
  }
  long cstart, cstop;
  if (c<=0) {
    // want a single state -c
    cstart = -c;
    cstop = cstart+1;
    // Make sure it is an absorbing state
    if (!stateClass.isNodeInClass(cstart, 1)) {
      throw MCLib::error(error::Bad_Class);
    }
  } else {
    // want an entire class c
    if (c >= stateClass.getNumClasses()) {
      throw MCLib::error(error::Bad_Class);
    }
    cstart = stateClass.firstNodeOfClass(c);
    cstop = 1+stateClass.lastNodeOfClass(c);
  }

  //
  // Set up auxiliary vectors if necessary
  //
  if (0== opts.vm_result) {
    opts.vm_result = new double[getNumStates()];
  }
  if (0== opts.prob_vect) {
    opts.prob_vect = new double[getNumStates()];
  }

  //
  // Fill opts.prob_vect with initial distribution
  //
  fillVector(opts.prob_vect, getNumStates(), p0);


  //
  // Call template function to do the actual
  // vector matrix multiplications and real work.
  //

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_distro(Qdiag, Qoff, rowsums, opts, 1, cstart, cstop, 
      1+stateClass.lastNodeOfClass(0), dist);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_distro(Qdiag, Qoff, rowsums, opts, 1, cstart, cstop, 
      1+stateClass.lastNodeOfClass(0), dist);
  }
}

// ******************************************************************

void MCLib::Markov_chain::computeContinuousDistTTA(
  CTMC_distribution_options &opts,
  const LS_Vector &p0,
  long c, double dt,
  discrete_pdf &dist) const
{
  if (isDiscrete()) {
    throw MCLib::error(error::Wrong_Type);
  }
  if (0==p0.size) {
    throw MCLib::error(error::Null_Vector);
  }
  if (0==opts.max_size) {
    throw MCLib::error(error::Null_Vector);
  }
  opts.q = MAX(opts.q, getUniformizationConst());

  long cstart, cstop;
  if (c<=0) {
    // want a single state -c
    cstart = -c;
    cstop = cstart+1;
    // Make sure it is an absorbing state
    if (!stateClass.isNodeInClass(cstart, 1)) {
      throw MCLib::error(error::Bad_Class);
    }
  } else {
    // want an entire class c
    if (c >= stateClass.getNumClasses()) {
      throw MCLib::error(error::Bad_Class);
    }
    cstart = stateClass.firstNodeOfClass(c);
    cstop = 1+stateClass.lastNodeOfClass(c);
  }

  //
  // Set up auxiliary vectors if necessary
  //
  if (0== opts.vm_result) {
    opts.vm_result = new double[getNumStates()];
  }
  if (0== opts.prob_vect) {
    opts.prob_vect = new double[getNumStates()];
  }

  //
  // Fill opts.prob_vect with initial distribution
  //
  fillVector(opts.prob_vect, getNumStates(), p0);

  //
  // Call template function to build distribution
  // for the uniformized CTMC.
  //
  discrete_pdf dtmc_tta;

  if (double_graphs) {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_double Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_distro(Qdiag, Qoff, rowsums, opts, opts.q, cstart, cstop, 
      1+stateClass.lastNodeOfClass(0), dtmc_tta);
  } else {
    //
    // Set up matrices (shallow copies here)
    //
    LS_CRS_Matrix_float Qdiag, Qoff;
    graphToMatrix(G_byrows_diag, Qdiag);
    graphToMatrix(G_byrows_off, Qoff);

    //
    // And pass everything to our nice template function :^)
    //
    templ_dtmc_distro(Qdiag, Qoff, rowsums, opts, opts.q, cstart, cstop, 
      1+stateClass.lastNodeOfClass(0), dtmc_tta);
  }

#ifdef DEBUG_CTMCDIST
  showDist("Got DTMC distributon", dtmc_tta);
#endif

  //
  // Now, build the continuous distribution for each time point,
  // by multiplying the discrete distribution (shifted by 1)
  // by the appropriate poisson distribution.
  //
  opts.distro[0] = dtmc_tta.f(0);

  long i;
  for (i=1; ; i++) {
    const double t = i * dt;
    const double qt = t * opts.q;

    //
    // Compute poisson distribution
    //

    discrete_pdf poisson;
    computePoissonPDF(qt, opts.poisson_epsilon, poisson);

    //
    // Did we timeout?  If so, determine error and break.
    //
    if (i >= opts.max_size) {
      opts.precision = 0;
      for (long s=poisson.left_trunc(); s<=poisson.right_trunc(); s++) {
        if (s>=opts.max_size) break;
        opts.precision += opts.error_distro[s] * poisson.f(s);
      }
      break;
    }

    //
    // Compute and store product of discrete distribution and poisson
    //
    long dp_start = poisson.left_trunc();
    long dp_stop = MIN(1+poisson.right_trunc(), dtmc_tta.right_trunc());
    for (long s=dp_start; s<dp_stop; s++) {
      CHECK_RANGE(0, s, opts.max_size);
      opts.distprod[s] = poisson.f(s) * dtmc_tta.f(s+1);
    }

    //
    // Now, sum those products.  Instead of simply looping in order,
    // we add the smallest elements first, frim the left and right ends.
    //
    CHECK_RANGE(0, i, opts.max_size);
    opts.distro[i] = 0;
    long left=dp_start;
    long right=dp_stop-1;
    while (left<right) {
      if (opts.distprod[left] < opts.distprod[right]) {
        opts.distro[i] += opts.distprod[left];
        left++;
      } else {
        opts.distro[i] += opts.distprod[right];
        right--;
      }
    }
    if (left == right) {
      opts.distro[i] += opts.distprod[left];
    }

    opts.distro[i] *= opts.q;

    //
    // Compute current error value,
    // which is the error distribution scaled by the poisson distribution
    //
    double error = 0;
    for (long s=poisson.left_trunc(); s<=poisson.right_trunc(); s++) {
      if (s>=opts.max_size) break;
      error += opts.error_distro[s] * poisson.f(s);
      if (error > opts.epsilon) break;
    }

    //
    // See if we can stop
    //
    if (error <= opts.epsilon) {
      opts.precision = error;
      break;
    }

  } // for i

  //
  // Done.
  // Answer is in opts.distro,
  // convert it to a proper distribution.
  //
  dist.copyFromAndTruncate(opts.distro, i, dtmc_tta.f_infinity());
}

// ******************************************************************

long MCLib::Markov_chain::randomWalk(rng_stream &rng, long &state, 
        const intset* F, long maxt, double q) const
{
  long elapsed;
  for (elapsed = 0; elapsed < maxt; elapsed++) {

    //
    // Check stopping conditions
    //
    if (F && F->contains(state)) {
      return elapsed;
    }
    if (stateClass.isNodeInClass(state, 1)) {
      // 1 is the class of absorbing states
      return elapsed;
    }

    //
    // Take an edge in the Markov chain,
    // with the same probability as the Markov chain.
    //
    // Sample a value...
    //
    double u;
    if (isDiscrete())   u = rng.Uniform32();
    else                u = q * rng.Uniform32();
    //
    // ...and select the "first" edge where
    // u < sum of edges so far
    //

    //
    // First, check diagonal block
    //
    long next;
    double total = 0;
    if (double_graphs) {
      next = double_graph_edge_idf(G_byrows_diag, state, u, total);
    } else {
      next = float_graph_edge_idf(G_byrows_diag, state, u, total);
    }
    if (next >= 0) {
      state = next;
      continue;
    }

    //
    // Not found, try off-diagonal block
    //
    if (double_graphs) {
      next = double_graph_edge_idf(G_byrows_off, state, u, total);
    } else {
      next = float_graph_edge_idf(G_byrows_off, state, u, total);
    }
    if (next >= 0) {
      state = next;
    }
    //
    // If still not found, we must have selected the self loop,
    // and the state remains the same
    //
  }
  return elapsed;
}

// ******************************************************************

double MCLib::Markov_chain::randomWalk(rng_stream &rng, long &state, 
        const intset* F, double maxt) const
{
  if (isDiscrete()) {
    throw MCLib::error(MCLib::error::Wrong_Type);
  }

  double elapsed = 0;
  for (;;) {

    //
    // Check stopping conditions
    //
    if (F && F->contains(state)) {
      return elapsed;
    }
    if (stateClass.isNodeInClass(state, 1)) {
      // 1 is the class of absorbing states
      return elapsed;
    }

    //
    // Sample time of next state change
    //
    if (double_graphs) {
      DCASSERT(one_over_rowsums_d);
      elapsed += -log(rng.Uniform32()) * one_over_rowsums_d[state];
    } else {
      DCASSERT(one_over_rowsums_f);
      elapsed += -log(rng.Uniform32()) * one_over_rowsums_f[state];
    }
    if (elapsed >= maxt) {
      return maxt;
    }

    //
    // Take an edge in the Markov chain,
    // with the same probability as the Markov chain.
    //
    // Sample a Uniform(0, sum of outgoing rates)...
    //
    DCASSERT(rowsums);
    double u = rng.Uniform32() * rowsums[state];

    //
    // ...and select the "first" edge where
    // u < sum of edges so far
    //

    //
    // First, check diagonal block
    //
    long next;
    double total = 0;
    if (double_graphs) {
      next = double_graph_edge_idf(G_byrows_diag, state, u, total);
    } else {
      next = float_graph_edge_idf(G_byrows_diag, state, u, total);
    }
    if (next >= 0) {
      state = next;
      continue;
    }

    //
    // Not found, try off-diagonal block
    //
    if (double_graphs) {
      next = double_graph_edge_idf(G_byrows_off, state, u, total);
    } else {
      next = float_graph_edge_idf(G_byrows_off, state, u, total);
    }
    if (next >= 0) {
      state = next;
      continue;
    }

    // Impossible to be not found.  
    // But in practice maybe floating point roundoff error?
    // Anyway not sure what to do yet, if we get here
    DCASSERT(0);
  }
  return elapsed;
}


