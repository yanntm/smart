
#include <string.h>

#undef CHECK_RANGE

#include "gen_meddly.h"
#include "expl_meddly.h"

#include "../Options/options.h"
#include "../Options/optman.h"
#include "../ExprLib/startup.h"

// Modules
#include "../Modules/glue_meddly.h"

// Formalisms
#include "../Formlsms/dsde_hlm.h"
#include "../Formlsms/graph_llm.h"
#include "../Formlsms/stoch_llm.h"
#include "../Formlsms/rss_meddly.h"
#include "../Formlsms/rgr_meddly.h"
#include "../Formlsms/proc_meddly.h"

// Generation templates
#include "gen_templ.h"

#include <limits.h>

// external libs

#include "../_Timer/timerlib.h"
#include "../_LSLib/lslib.h"

// For matrix diagrams

#include "../include/hash.h"

#define MEASURE_STATS

// #define MEASURE_TIMES

// #define DEBUG_UNREDUCED

// #define FORCE_FULL
// #define FORCE_SPARSE

// #define DEBUG_ADD_EDGES

// #define DEBUG_FREQ
#define ENABLE_CMDS

// Handy stuff

inline void convert(MEDDLY::error ce)
{
  switch (ce.getCode()) {
    case MEDDLY::error::INSUFFICIENT_MEMORY:  throw  subengine::Out_Of_Memory;
    default:                                  throw  subengine::Engine_Failed;
  }
}

inline void convert(sv_encoder::error sve)
{
  switch (sve) {
    case sv_encoder::Out_Of_Memory:     throw  subengine::Out_Of_Memory;
    default:                            throw  subengine::Engine_Failed;
  }
}

// **************************************************************************
// *                                                                        *
// *                                                                        *
// * Helper classes.  These are the data structures used, conceptually, for *
// *   sets of states, reachability graph edges, and Markov chain rates.    *
// *      Below, these are used in other "generator" wrapper classes.       *
// *                                                                        *
// *                                                                        *
// **************************************************************************

// **************************************************************************
// *                                                                        *
// *                          edge_minterms  class                          *
// *                                                                        *
// **************************************************************************

class edge_minterms {
  meddly_encoder &wrap;
  minterm_pool &mp;
  shared_ddedge* Edges;
  int** from_batch;
  int** to_batch;
  float* rate_batch;
  int alloc;
  int used;
  shared_ddedge* tempedge;
#ifdef MEASURE_STATS
  int min_batch;
  int max_batch;
  long num_batches;
  long num_edges;
#endif
#ifdef MEASURE_TIMES
  timer* clock;
  double time_mtadd;
  double time_create;
  double time_union;
  double time_misc;
#endif
public:
  edge_minterms(meddly_encoder &w, minterm_pool &m, int alloc_size, bool needs_rates);
  // edge_minterms(meddly_monolithic_rg &r, minterm_pool &m, int alloc_size, bool needs_rates);
  ~edge_minterms();

  void reportStats(DisplayStream &out, const char* name="reachability graph") const;

  inline shared_ddedge* shareProc() { return Share(Edges); }

  void addBatch();

  inline void addEdge(int* from, int* to) {
    DCASSERT(0==rate_batch);
#ifdef MEASURE_TIMES
    clock->reset();
#endif
    from_batch[used] = mp.shareMinterm(from);
    to_batch[used] = mp.shareMinterm(to);
    used++;
#ifdef MEASURE_TIMES
    time_mtadd += clock->elapsed();
#endif
    if (used < alloc) return;
    addBatch();
  }

  // required for interface, but should never be called

  inline void eliminateVanishing(debugging_msg &) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }

  inline void addTTEdge(int* from, int* to, double wt) {
    // Ignore self-loops:
    if (mp.equalMinterms(from, to)) return;

    DCASSERT(rate_batch);
#ifdef MEASURE_TIMES
    clock->reset();
#endif
    from_batch[used] = mp.shareMinterm(from);
    to_batch[used] = mp.shareMinterm(to);
    rate_batch[used] = wt;
    used++;
#ifdef MEASURE_TIMES
    time_mtadd += clock->elapsed();
#endif
    if (used < alloc) return;
    addBatch();
  }
  inline void addTVEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
  inline void addVTEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
  inline void addVVEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
};

// **************************************************************************
// *                         edge_minterms  methods                         *
// **************************************************************************

edge_minterms::edge_minterms(meddly_encoder &w, minterm_pool &m, int as, bool nr) : wrap(w), mp(m)
// edge_minterms::edge_minterms(meddly_monolithic_rg &r, minterm_pool &m, int as, bool nr) : rgr(r), mp(m)
{
  Edges = new shared_ddedge(wrap.getForest());
  // Edges = rgr.newMxdEdge();
  alloc = as;
  used = 0;
  from_batch = new int*[alloc];
  to_batch = new int*[alloc];
  if (nr) rate_batch = new float[alloc];
  else    rate_batch = 0;
  for (int i=0; i<alloc; i++) {
    from_batch[i] = 0;
    to_batch[i] = 0;
  }
  tempedge = new shared_ddedge(wrap.getForest());
  // tempedge = rgr.newMxdEdge();

#ifdef MEASURE_STATS
  min_batch = INT_MAX;
  max_batch = 0;
  num_batches = 0;
  num_edges = 0;
#endif
#ifdef MEASURE_TIMES
  clock = makeTimer();
  time_mtadd = 0.0;
  time_create = 0.0;
  time_union = 0.0;
  time_misc = 0.0;
#endif
}

edge_minterms::~edge_minterms()
{
  // DON'T delete minterms
  delete[] from_batch;
  delete[] to_batch;
  delete[] rate_batch;
  Delete(Edges);
  Delete(tempedge);
#ifdef MEASURE_TIMES
  doneTimer(clock);
#endif
}

void edge_minterms::reportStats(DisplayStream &out, const char* who) const
{
  out << "\tBatch for " << who << " required ";
  size_t batchmem = 2*alloc*sizeof(int);
  out.PutMemoryCount(batchmem, 3);
  out << "\n";
#ifdef MEASURE_STATS
  if (num_batches) {
    out << "\t\t# batches: " << num_batches << "\n";
    out << "\t\tmin batch: " << min_batch << "\n";
    out << "\t\tmax batch: " << max_batch << "\n";
    double avg = num_edges;
    avg /= num_batches;
    out << "\t\tavg batch: " << avg << "\n";
  }
#endif
#ifdef MEASURE_TIMES
  out << "\tCritical times for " << who << ":\n";
  out << "\t\tAdd to batch (total): " << time_mtadd << " s\n";
  out << "\t\tCreate edge (total): " << time_create << " s\n";
  out << "\t\tUnion (total): " << time_union << " s\n";
  out << "\t\tMisc. (total): " << time_misc << " s\n";
#endif
}

void edge_minterms::addBatch()
{
  if (0==used) return;

#ifdef DEBUG_ADD_EDGES
  fprintf(stderr, "Adding batch of edges of size %d\n", used);
  for (int i=0; i<used; i++) {
    fprintf(stderr, "  ");
    mp.showMinterm(stderr, from_batch[i]);
    if (rate_batch) {
      fprintf(stderr, " --(%9f)--> ", rate_batch[i]);
    } else {
      fprintf(stderr, " ---------> ");
    }
    mp.showMinterm(stderr, to_batch[i]);
    fprintf(stderr, "\n");
  }
#endif

  try {
#ifdef MEASURE_TIMES
    clock->reset();
#endif
    if (rate_batch) {
      wrap.createMinterms(from_batch, to_batch, rate_batch, used, tempedge);
    } else {
      wrap.createMinterms(from_batch, to_batch, used, tempedge);
    }
#ifdef MEASURE_TIMES
    time_create += clock->elapsed();
#endif
  }
  catch (sv_encoder::error e) {
    convert(e);
  }
#ifdef MEASURE_TIMES
  clock->reset();
#endif
  Edges->E += tempedge->E;
#ifdef MEASURE_TIMES
  time_union += clock->elapsed();
  clock->reset();
#endif
  for (int i=0; i<used; i++) {
    mp.doneMinterm(from_batch[i]);
    mp.doneMinterm(to_batch[i]);
    from_batch[i] = 0;
    to_batch[i] = 0;
  }
#ifdef MEASURE_TIMES
  time_misc += clock->elapsed();
#endif
#ifdef MEASURE_STATS
  min_batch = MIN(min_batch, used);
  max_batch = MAX(max_batch, used);
  num_edges += used;
  num_batches++;
#endif
  used = 0;
}


#ifdef ENABLE_CMDS
// **************************************************************************
// *                                                                        *
// *                          edge_2001_cmds class                          *
// *                                                                        *
// **************************************************************************

/** Canonical matrix diagram class.
    Yanked from the implementation for the 2001 paper,
    except the real values have been removed.

    Conventions:

    Levels are numbered in reverse.  The level 0 matrices are at
    the bottom, with no downward pointers.  (We actually do have
    downward pointers but we ignore them.)

    Limits:
    \begin{tabular}{lll}
       {\bf Spec} & {\bf Limit} & {\bf Reason} \\ \hline
       Number of variables/levels & 65,535 & stored as unsigned short\\
       Submatrix size & 65,535 x 65,535 & stored as unsigned short\\
       Number of submatrices & 1,048,576 & Initialized in constructor\\
    \end{tabular}
 */
class edge_2001_cmds {

  /// The nodes of the md.
  struct submatrix {
    // required for hash table
    submatrix* next;

    /// submatrix elements.
    struct element {
      /// The column (if we're stored by rows).
      unsigned short index;
      /** Downward pointer.
          This will be zero if we're at the bottom level.
       */
      submatrix* down;
      element *next;
      //
      void Set(int i, submatrix* d, element *n) {
        index = i; down = d; next = n;
      }
      inline bool Equals(element *e) const {
        DCASSERT(e);
        if (e->index != index) return false;
        return e->down == down;
      }
      void Show(OutputStream &s) const {
        s << index << ":" << down << " ";
        if (next) next->Show(s);
        else s << "\n";
      }
      int Nonzeroes() const {
        if (next) return 1+next->Nonzeroes();
        return 1;
      }
    }; // end of struct element

    /** Only one of these makes sense at a time.
        The value that is used is based on {\em status}.
     */
    union {
      /// If we're stored by rows, the linked-list for each row.
      element **lists;
      /// Pointer to the matrix we've been merged with.
      submatrix* mergedptr;
      /// Pointer to the next matrix in recycle list.
      submatrix* nextrecycled;
    }; // union

    /// The number of this submatrix.
    int number;

    /** The number of incoming pointers.
        Only maintained if the state is CANONICAL.
     */
    int incoming;

    /// The level of this submatrix.
    unsigned short level;

    /// If we're stored by rows, the number of rows.
    unsigned short size;

    enum status_type {
      BUILDING = 0,
      CANONICAL = 1,
      MERGED = 2,
      RECYCLED = 3
    };

    /** Status of this submatrix.

        Possible values:
        \begin{description}

        \item[BUILDING]
        The submatrix is still under construction.  {\em The submatrix must
        have exactly one incoming pointer!}   Otherwise we might change
        something we're not supposed to!  It hasn't been inserted into the
        uniqueness table yet.  {\em lists} is active.

        \item[CANONICAL]
        The submatrix is unique, and is present in the uniqueness table.  {\em
        lists} is active.

        \item[MERGED]
        The submatrix {\em lists} have been deleted, because we have been
        merged with a canonical submatrix.  {\em mergedptr} points to the
        canonical submatrix.  Used to update pointers.

        \item[RECYCLED]
        The submatrix {\em lists} have been deleted.  The structure is on the
        recycled list.  {\em nextrecycled} is a pointer to the next recycled
        submatrix.

        \end{description}
    */
    status_type state;

    /** Constructor.
        Sets state to BUILDING, and sizes to zero.
     */
    submatrix();
    /// Destructor.
    ~submatrix();
    // Handy methods
    void Alloc(unsigned short sz, long &memused);
    void Free(element* &freelist, long &memused);
    void FindInList(int l, int ind, element* &prev, element* &curr);
    void Show(OutputStream &s, bool summarize) const;
    int Nonzeroes();
    // required by hash table
    long Signature(long M) const;
    bool Equals(const submatrix *t) const;

    inline submatrix* GetNext() const { return next; }
    inline void SetNext(submatrix* n) { next = n;    }
  }; // end of struct submatrix
  /// Reduced, root MxD
  submatrix* root;
  /// Unreduced, batch MxD
  submatrix* batch;
  /// List of merged submatrices
  submatrix* merged_list;
  /// List of recycled submatrices
  submatrix* freed;
  /// List of recycled elements;
  submatrix::element *oldelements;
  /// Allocation of submatrices
  // heaparray <submatrix> *Nodes;
  /// Uniqueness table
  HashTable <submatrix> *Table;
  /// Forest for final MxD
  MEDDLY::expert_forest* mxdfor;
  /// How often to accumulate
  int batch_size;
  /// number of edges so far
  long curr_batch;
#ifdef MEASURE_STATS
  long min_batch;
  long max_batch;
  long num_batches;
  long total_edges;
  long memused;
  long maxused;
  long currnodes;
  long peaknodes;
  long numnonzeroes;
#endif
#ifdef MEASURE_TIMES
  timer* clock;
  double time_mtadd;
  double time_accumulate;
  double time_reduce;
  double time_misc;
#endif
public:
  edge_2001_cmds(meddly_encoder &w, int bs);
  ~edge_2001_cmds();

  void reportStats(DisplayStream &out, const char* name) const;

  // copy into meddly forest
  shared_ddedge* shareProc();

  void addBatch();

  void addEdge(int* from, int* to);

  inline void addTTEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
  inline void addTVEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
  inline void addVTEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
  inline void addVVEdge(int* from, int* to, double wt) {
    DCASSERT(0);
    throw subengine::Engine_Failed;
  }
protected:
  void Accumulate(submatrix* root, const submatrix* canonical);
  submatrix* NewTempMatrix(int k);
  void Recycle(submatrix* n);
  void UnlinkDown(submatrix* n);
  inline submatrix* ShallowCopy(submatrix* n) {
    if (0==n) return 0;
    if (submatrix::MERGED==n->state) {
      n->mergedptr = ShallowCopy(n->mergedptr);
      return n->mergedptr;
    }
    DCASSERT(submatrix::CANONICAL==n->state);
    n->incoming++;
    return n;
  }
  inline submatrix::element* NewElement() {
    submatrix::element* answer;
    if (oldelements) {
      answer = oldelements;
      oldelements = oldelements->next;
    } else {
      answer = new submatrix::element;
    }
#ifdef MEASURE_STATS
    memused += sizeof(submatrix::element);
    maxused = MAX(memused, maxused);
#endif
    return answer;
  };
  submatrix* Reduce(submatrix* n);
  inline void RecycleMergedList() {
    while (merged_list) {
      submatrix* next = merged_list->GetNext();
      memused -= sizeof(submatrix);
      merged_list->state = submatrix::RECYCLED;
      merged_list->nextrecycled = freed;
      freed = merged_list;
      currnodes--;
      merged_list = next;
    }
  }
};


// **************************************************************************
// *                   edge_2001_cmds::submatrix  methods                   *
// **************************************************************************

edge_2001_cmds::submatrix::submatrix()
{
  state = BUILDING;
  lists = NULL;
  incoming = 0;
}

edge_2001_cmds::submatrix::~submatrix()
{
  if (state==BUILDING || state==CANONICAL) if (lists) delete[] lists;
}

void edge_2001_cmds::submatrix::Alloc(unsigned short sz, long &memused)
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  size = sz;
  lists = new element*[size];
  int i;
  for (i=0; i<size; i++) lists[i] = 0;
  memused += size * sizeof(element*);
}

void edge_2001_cmds::submatrix::Free(element* &freelist, long &memused)
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int i;
  for (i=0; i<size; i++) {
    element *ptr = lists[i];
    while (ptr) {
      element *temp = ptr->next;
      ptr->next = freelist;
      freelist = ptr;
      ptr = temp;
      memused -= sizeof(element);
    }
  }
  memused -= size * sizeof(element*);
  delete[] lists;
  lists = 0;
}

void edge_2001_cmds::submatrix
  ::FindInList(int l, int ind, element* &prev, element* &curr)
  // Check list l for ind.
  // If it exists, it will be returned in "curr".  Otherwise,
  // "curr" will be NULL.
  // "prev" will be set to the element immediately before "curr",
  // regardless of whether "curr" exists.
  // If "prev" is NULL, then there is no element in the list before "curr".
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  prev = 0;
  curr = lists[l];
  while ((curr)&&(curr->index < ind)) {
    prev = curr;
    curr = curr->next;
  }
  if (curr) if (curr->index!=ind) curr = 0;
}

void edge_2001_cmds::submatrix::Show(OutputStream &s, bool summarize) const
{
  if (summarize) if (state==MERGED || state==RECYCLED) return;
  s << "Submatrix #" << number << " : ";
  switch (state) {
    case BUILDING: s << "BUILDING\n"; break;
    case CANONICAL: s << "CANONICAL\n"; break;
    case MERGED: s << "MERGED"; break;
    case RECYCLED: s << "RECYCLED"; break;
  }
  int i;
  switch (state) {
    case RECYCLED:
      s << "\t Next : " << nextrecycled << "\n";
      break;
    case MERGED:
      s << "\t with : " << mergedptr << "\n";
      break;
    case BUILDING:
    case CANONICAL:
      s << "\tLevel " << level << " \t#incoming: " << incoming << "\n";
      s << "\tRows: " << size << "\n";
      if (lists) {
        for (i=0; i<size; i++) {
          s << "\tRow " << i << " : ";
          if (lists[i]) lists[i]->Show(s);
          else s << "null\n";
        }
      } else s << "lists is null????\n";
  }
}

int edge_2001_cmds::submatrix::Nonzeroes()
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int nnz = 0;
  int i;
  for (i=0; i<size; i++)
    if (lists[i]) nnz += lists[i]->Nonzeroes();
  return nnz;
}

long edge_2001_cmds::submatrix::Signature(long M) const
  // Hashing function: position of first nonzero element in last nonzero row
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int i = size-1;
  while (0==lists[i]) {
    i--;
    DCASSERT(i>=0);
  }
  long sign = i & M;
  sign = (sign * 256 + lists[i]->index) % M;
  sign *= 256;
  if (lists[i]->down) sign += lists[i]->down->number;
  sign %= M;
  return sign;
}

bool edge_2001_cmds::submatrix::Equals(const submatrix *t) const
  // assumes matrix elements are sorted
{
  if (level!=t->level) return false;
  if (size!=t->size) return false;
  int i;
  for (i=0; i<size; i++) {
    element *ptr = lists[i];
    element *tptr = t->lists[i];
    while (ptr) {
      if (0==tptr) return false;
      if (!ptr->Equals(tptr)) return false;
      ptr = ptr->next;
      tptr = tptr->next;
    }
    if (tptr) return false;
  }
  return true;
}

// **************************************************************************
// *                         edge_2001_cmds methods                         *
// **************************************************************************

edge_2001_cmds::edge_2001_cmds(meddly_encoder &w, int bs)
{
  mxdfor = smart_cast <MEDDLY::expert_forest*>(w.getForest());
  DCASSERT(mxdfor);
  root = 0;
  batch = 0;
  batch_size = bs;
  curr_batch = 0;
  merged_list = 0;
  freed = 0;
  oldelements = 0;
  Table = new HashTable <submatrix>;
#ifdef MEASURE_STATS
  min_batch = LONG_MAX;
  max_batch = 0;
  num_batches = 0;
  total_edges = 0;
  memused = 0;
  maxused = 0;
  currnodes = 0;
  peaknodes = 0;
  numnonzeroes = 0;
#endif
#ifdef MEASURE_TIMES
  clock = makeTimer();
  time_mtadd = 0;
  time_accumulate = 0;
  time_reduce = 0;
  time_misc = 0;
#endif
}

edge_2001_cmds::~edge_2001_cmds()
{
  delete Table;
#ifdef MEASURE_TIMES
  doneTimer(clock);
#endif
}

void edge_2001_cmds::reportStats(DisplayStream &out, const char* name) const
{
#ifdef MEASURE_STATS
  out << "\tMatrix diagram report for " << name << ":\n";
  out << "\t\t" << numnonzeroes << " elements added\n";
  out << "\t\tCurrent memory: ";
  out.PutMemoryCount(memused, 3);
  out << "\n\t\tMaximum memory: ";
  out.PutMemoryCount(maxused, 3);
  out << "\n\t\tCurrent nodes: " << currnodes << "\n";
  out << "\t\tMaximum nodes: " << peaknodes << "\n";
  if (num_batches) {
    out << "\t\t# batches: " << num_batches << "\n";
    out << "\t\tmin batch: " << min_batch << "\n";
    out << "\t\tmax batch: " << max_batch << "\n";
    double avg = numnonzeroes;
    avg /= num_batches;
    out << "\t\tavg batch: " << avg << "\n";
  }
  out << "\t\tUnique table stats:\n";
  out << "\t\t\tCurrent Size: " << Table->getSize() << "\n";
  out << "\t\t\tMaximum Size: " << Table->getMaxSize() << "\n";
  out << "\t\t\t# Resizes: " << Table->getNumResizes() << "\n";
  out << "\t\t\tCurrent Elements: " << Table->getNumElements() << "\n";
  out << "\t\t\tMaximum Elements: " << Table->getMaxElements() << "\n";
  out << "\t\t\tMax chain: " << Table->getMaxChain() << "\n";
#endif
#ifdef MEASURE_TIMES
  out << "\tCritical times for " << name << ":\n";
  out << "\t\tAdd minterm (total): " << time_mtadd << " s\n";
  out << "\t\tAccumulate (total): " << time_accumulate << " s\n";
  out << "\t\tReduce (total): " << time_reduce << " s\n";
  out << "\t\tMisc. (total): " << time_misc << " s\n";
#endif
}


shared_ddedge* edge_2001_cmds::shareProc()
{
  return 0;
}

void edge_2001_cmds::addBatch()
{
  if (0==batch) return;
#ifdef MEASURE_TIMES
  clock->reset();
#endif
  Accumulate(batch, root);
#ifdef MEASURE_TIMES
  time_accumulate += clock->elapsed();
  clock->reset();
#endif
  batch = Reduce(batch);
#ifdef MEASURE_TIMES
  time_reduce += clock->elapsed();
  clock->reset();
#endif
  RecycleMergedList();
  Recycle(root);
  root = batch;
  batch = 0;
  num_batches++;
  min_batch = MIN(min_batch, curr_batch);
  max_batch = MAX(max_batch, curr_batch);
  curr_batch = 0;
#ifdef MEASURE_TIMES
  time_misc += clock->elapsed();
#endif
}

void edge_2001_cmds::addEdge(int* from, int* to)
{
#ifdef MEASURE_TIMES
  clock->reset();
#endif
  int k = mxdfor->getDomain()->getNumVariables();
  if (0==batch) batch = NewTempMatrix(k);
  submatrix* node = batch;
  for ( ; k; k--) {
    submatrix::element* prev, *curr;
    node->FindInList(from[k], to[k], prev, curr);
    if (0==curr) { // element doesn't exist, we need to add it
      curr = NewElement();
      submatrix *down;
      submatrix::element* next;
      if (k-1==0) {
        down = 0;
        numnonzeroes++;
      } else {
        down = NewTempMatrix(k-1);
      }
      if (prev) next = prev->next; else next = node->lists[from[k]];
      curr->Set(to[k], down, next);
      if (prev) prev->next = curr; else node->lists[from[k]] = curr;
    } // if 0==curr
    node = curr->down;
  } // for k
  curr_batch++;
#ifdef MEASURE_TIMES
  time_mtadd += clock->elapsed();
#endif
  if (curr_batch >= batch_size) addBatch();
}

// protected

void edge_2001_cmds::Accumulate(submatrix* build, const submatrix* canon)
{
  if (0==canon) return;
  DCASSERT(build);
  DCASSERT(submatrix::BUILDING == build->state);
  DCASSERT(build->lists);
  DCASSERT(canon->lists);
  DCASSERT(build->size == canon->size);
  DCASSERT(build->level == canon->level);

  for (int i=0; i<build->size; i++) {
    // merge the two lists, *copying* the ptrs in the canonical md.
    submatrix::element *newptr = 0;
    submatrix::element *bptr = build->lists[i];
    const submatrix::element *cptr = canon->lists[i];
    int bindex;
    if (bptr) bindex = bptr->index;
    else bindex = INT_MAX;
    while (cptr) {
      if (bindex < cptr->index) {
        // next node is from b list
        if (newptr) newptr->next = bptr;
        else build->lists[i] = bptr;
        newptr = bptr;
        // advance b
        bptr = bptr->next;
        if (bptr) bindex = bptr->index;
        else bindex = INT_MAX;
      } else if (bindex == cptr->index) {
        // equal index
        if (build->level>1) {
          // Not bottom level, add recursively
          Accumulate(bptr->down, cptr->down);
        } // if k
        // next node is modified b node
        if (newptr) newptr->next = bptr;
        else build->lists[i] = bptr;
        newptr = bptr;
        // advance b
        bptr = bptr->next;
        if (bptr) bindex = bptr->index;
        else bindex = INT_MAX;
        // advance c
        cptr = cptr->next;
      } else {
        // bindex > cptr->index
        // copy current element of c list
        submatrix::element *copy = NewElement();
        copy->Set(cptr->index, ShallowCopy(cptr->down), 0);
        if (newptr) newptr->next = copy;
        else build->lists[i] = copy;
        newptr = copy;
        // advance c
        cptr = cptr->next;
      }// if bindex
      // we've exhausted c-list, add remainder of b-list (if any)
      if (newptr) newptr->next = bptr;
      else build->lists[i] = bptr;
    } // while cptr
  } // for i
}

edge_2001_cmds::submatrix* edge_2001_cmds::NewTempMatrix(int k)
{
  if (k<1) return 0;
  currnodes++;
  submatrix* next = freed;
  if (next) {
    freed = next->nextrecycled;
  } else {
    next = new submatrix;
    peaknodes++;
    next->number = peaknodes;
  }
  memused += sizeof(submatrix);
  // set up submatrix
  next->state = submatrix::BUILDING;
  next->Alloc(mxdfor->getDomain()->getVariableBound(k, false), memused);
  maxused = MAX(memused, maxused);
  next->level = k;
  return next;
}

void edge_2001_cmds::Recycle(submatrix* n)
{
  if (0==n) return;
  DCASSERT(n->state == submatrix::CANONICAL);
  DCASSERT(n->incoming > 0);
  n->incoming--;
  if (n->incoming) return;

  Table->Remove(n);
  UnlinkDown(n);
  memused -= sizeof(submatrix);
  n->Free(oldelements, memused);
  n->state = submatrix::RECYCLED;
  n->nextrecycled = freed;
  freed = n;
  currnodes--;
}

void edge_2001_cmds::UnlinkDown(submatrix* n)
{
  if (0==n) return;
  DCASSERT(n->state == submatrix::BUILDING || n->state == submatrix::CANONICAL);
  if (n->lists) {
    for (int i=0; i<n->size; i++) {
      for (submatrix::element* ptr=n->lists[i]; ptr; ptr = ptr->next)
        Recycle(ptr->down);
    }
  }
}

edge_2001_cmds::submatrix* edge_2001_cmds::Reduce(submatrix* n)
{
  if (0==n) return 0;
  if (submatrix::CANONICAL == n->state) {
    // already reduced
    return n;
  }
  if (submatrix::MERGED == n->state) {
    // Essentially, this is a cache hit
    return ShallowCopy(n->mergedptr);
  }

  // Reduce below
  for (int i=0; i<n->size; i++) {
    for (submatrix::element* ptr = n->lists[i]; ptr; ptr=ptr->next)
      if (ptr->down) ptr->down = Reduce(ptr->down);
  } // for i

  submatrix* find = Table->UniqueInsert(n);
  if (n != find) {
    // We're a duplicate
    UnlinkDown(n);
    n->Free(oldelements, memused);
    n->state = submatrix::MERGED;
    n->mergedptr = find;
    n->SetNext(merged_list);
    merged_list = n;
    DCASSERT(submatrix::CANONICAL == find->state);
    find->incoming++;
  } else {
    // We're canonical
    n->state = submatrix::CANONICAL;
    n->incoming = 1;
  }

  return find;
}

// **************************************************************************
// *                                                                        *
// *                          real_2001_cmds class                          *
// *                                                                        *
// **************************************************************************

/** New canonical matrix diagram class.
    Yanked from the implementation for the 2001 paper,
    complete with real values.

    Conventions:

    Levels are numbered in reverse.  The level 0 matrices are at
    the bottom, with no downward pointers.  (We actually do have
    downward pointers but we ignore them.)

    Limits:
    \begin{tabular}{lll}
       {\bf Spec} & {\bf Limit} & {\bf Reason} \\ \hline
       Number of variables/levels & 65,535 & stored as unsigned short\\
       Submatrix size & 65,535 x 65,535 & stored as unsigned short\\
       Number of submatrices & 1,048,576 & Initialized in constructor\\
    \end{tabular}
 */
class real_2001_cmds {

  /// The nodes of the md.
  struct submatrix {
    // required for hash table
    submatrix* next;

    /// submatrix elements.
    struct element {
      /// The column (if we're stored by rows).
      unsigned short index;
      /// Value.  Should be positive.
      double value;
      /** Downward pointer (submatrix number).
          This will be zero if we're at the bottom level.
       */
      submatrix* down;
      element *next;
      //
      void Set(int i, double v, submatrix* d, element *n) {
        index = i; value = v; down = d; next = n;
      }
      bool Equals(element *e) const;
      void Show(OutputStream &s) {
        s << index << ":(" << value << ", " << down << ") ";
        if (next) next->Show(s);
        else s << "\n";
      }
      int Nonzeroes() {
        if (next) return 1+next->Nonzeroes();
        return 1;
      }
    }; // end of struct element

    /** Only one of these makes sense at a time.
        The value that is used is based on {\em status}.
     */
    union {
      /// If we're stored by rows, the linked-list for each row.
      element **lists;
      /// Pointer to the matrix we've been merged with.
      submatrix* mergedptr;
      /// Pointer to the next matrix in recycle list.
      submatrix* nextrecycled;
    }; // union

    /// The number of this submatrix.
    int number;

    /** The number of incoming pointers.
        Only maintained if the state is CANONICAL.
     */
    int incoming;

    /// The level of this submatrix.
    unsigned short level;

    /// If we're stored by rows, the number of rows.
    unsigned short size;

    /// If we're stored by rows, the number of columns.
    // unsigned short length;

    /// Are we stored by rows?
    // bool is_by_rows;

    enum status_type {
      BUILDING = 0,
      CANONICAL = 1,
      MERGED = 2,
      RECYCLED = 3
    };
    /** Status of this submatrix.

        Possible values:
        \begin{description}

        \item[BUILDING]                The submatrix is still under construction.
                                {\em The submatrix must have exactly
                                one incoming pointer!}   Otherwise we might
                                change something we're not supposed to!
                                It hasn't been inserted into the uniqueness
                                table yet.  {\em lists} is active.

        \item[CANONICAL]        The submatrix is unique, and is present in
                                the uniqueness table.  {\em lists} is active.

        \item[MERGED]                The submatrix {\em lists} have been deleted,
                                because we have been merged with a canonical
                                submatrix.  {\em mergedptr} points to the
                                canonical submatrix.  Used to update pointers.

        \item[RECYCLED]                The submatrix {\em lists} have been deleted.
                                The structure is on the recycled list.
                                {\em nextrecycled} is a pointer to the next
                                recycled submatrix.

        \end{description}
    */
    status_type state;

    /** Constructor.
        Sets state to BUILDING, and sizes to zero.
     */
    submatrix();
    /// Destructor.
    ~submatrix();
    // Handy methods
    void Alloc(unsigned short sz, long &memused);
    void Free(element* &freelist, long &memused);
    void FindInList(int l, int ind, element* &prev, element* &curr);
    void MultiplyBy(double a);
    void Show(OutputStream &s, bool summarize);
    int Nonzeroes();
    // convert to storage by columns
    // void Transpose();
    // required by hash table
    int Signature(int M);
    bool Equals(submatrix *t) const;

    inline submatrix* GetNext() const { return next; }
    inline void SetNext(submatrix* n) { next = n;    }
  }; // end of struct submatrix

  /// Precision for real comparison
  static const double mergeprecision;
  //= 1e-7;

  /// Reduced, root MxD
  submatrix* root;
  /// Unreduced, batch MxD
  submatrix* batch;
  /// List of merged submatrices
  submatrix* merged_list;
  /// List of recycled submatrices
  submatrix* freed;
  /// List of recycled elements
  submatrix::element *oldelements;

  /// Allocation of submatrices
  // heaparray <submatrix> *Nodes;

  /// Uniqueness table
  HashTable <submatrix> *Table;

  /// Forest for MxD
  MEDDLY::expert_forest* mxdfor;

  /// How often to accumulate
  int batch_size;
  /// Number of edges so far
  long curr_batch;
#ifdef MEASURE_STATS
  long min_batch;
  long max_batch;
  long num_batches;
  long total_edges;
  long memused;
  long maxused;
  long currnodes;
  long peaknodes;
  long numnonzeroes;
#endif
#ifdef MEASURE_TIMES
  timer* clock;
  double time_mtadd;
  double time_accumulate;
  double time_reduce;
  double time_misc;
#endif
  public:
    /// constructor
    real_2001_cmds(meddly_encoder &w, int bs);
    /// destructor
    ~real_2001_cmds();

    void reportStats(DisplayStream &out, const char* name) const;

    shared_ddedge* shareProc();

    void addBatch();

    inline void addEdge(int* from, int* to) {
      DCASSERT(0);
      throw subengine::Engine_Failed;
    }
    void addTTEdge(int* from, int* to, double wt);
    inline void addTVEdge(int* from, int* to, double wt) {
      DCASSERT(0);
      throw subengine::Engine_Failed;
    }
    inline void addVTEdge(int* from, int* to, double wt) {
      DCASSERT(0);
      throw subengine::Engine_Failed;
    }
    inline void addVVEdge(int* from, int* to, double wt) {
      DCASSERT(0);
      throw subengine::Engine_Failed;
    }

  protected:
    /** Adds two MDs.
        The MDs must be at the same level and of the same sizes.
        One MD is allowed to be canonical.  It is left untouched.
        The other is considered an accumulator, and will be modified.
        It must have state "BUILDING".
        Naturally, this may generate recursive calls.
    */
    void Accumulate(submatrix* root, const submatrix* canonical);

    submatrix* NewTempMatrix(int k);

    /** Delete the specified node.
        Of course, if we're not the last pointer to the object, we
        won't delete it.
    */
    void Recycle(submatrix *n);

    /** Called before we delete a node.
        The downward pointers of this node are counted in the nodes pointed at.
        Before destroying the downward pointers of this node, we must
        follow each one and subtract one from the incoming count.
        Otherwise the incoming counts will be wrong.
    */
    void UnlinkDown(submatrix *n);

    inline submatrix* ShallowCopy(submatrix* n) {
      if (0==n) return 0;
      if (submatrix::MERGED==n->state) {
        n->mergedptr = ShallowCopy(n->mergedptr);
        return n->mergedptr;
      }
      DCASSERT(submatrix::CANONICAL==n->state);
      n->incoming++;
      return n;
    }

    inline submatrix::element* NewElement() {
      submatrix::element *answer;
      if (oldelements) {
        answer = oldelements;
        oldelements = oldelements->next;
      } else {
        answer = new submatrix::element;
      }
#ifdef MEASURE_STATS
      memused += sizeof(submatrix::element);
      maxused = MAX(memused, maxused);
#endif
      return answer;
    }

    /** Recursively reduce the given MD.
        If we're a top-level node, then we'll also normalize the
        nodes below us.
    */
    submatrix* Canonicalize(submatrix* root);

    /// recursively scale matrices (in place)
    double Normalize(submatrix* node);

    inline void RecycleMergedList() {
      while (merged_list) {
        submatrix* next = merged_list->GetNext();
        memused -= sizeof(submatrix);
        merged_list->state = submatrix::RECYCLED;
        merged_list->nextrecycled = freed;
        freed = merged_list;
        currnodes--;
        merged_list = next;
      }
    }

}; // end of real_2001_cmds

const double real_2001_cmds::mergeprecision = 1e-7;

// ******************************************************************
// *              real_2001_cmds::submatrix Methods                 *
// ******************************************************************

bool real_2001_cmds::submatrix::element::Equals(element *e) const
{
  if (e->index != index) return false;
  if (e->down != down) return false;
  double den = MAX(e->value, value);
  if (0.0==den) return true;
  double reldiff = e->value - value;
  if (reldiff<0) reldiff /= -den;
  else reldiff /= den;
  return reldiff < mergeprecision;
}

real_2001_cmds::submatrix::submatrix()
{
  state = BUILDING;
  lists = NULL;
  incoming = 0;
}

real_2001_cmds::submatrix::~submatrix()
{
  if (state==BUILDING || state==CANONICAL) if (lists) delete[] lists;
}

void real_2001_cmds::submatrix::Alloc(unsigned short sz, long &memused)
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  size = sz;
  lists = new element*[size];
  int i;
  for (i=0; i<size; i++) lists[i] = NULL;
  memused += size * sizeof(element*);
}

void real_2001_cmds::submatrix::Free(element* &freelist, long &memused)
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int i;
  for (i=0; i<size; i++) {
    element *ptr = lists[i];
    while (ptr) {
      element *temp = ptr->next;
      ptr->next = freelist;
      freelist = ptr;
      ptr = temp;
      memused -= sizeof(element);
    }
  }
  memused -= size * sizeof(element*);
  delete[] lists;
  lists = NULL;
}

void real_2001_cmds::submatrix
  ::FindInList(int l, int ind, element* &prev, element* &curr)
  // Check list l for ind.
  // If it exists, it will be returned in "curr".  Otherwise,
  // "curr" will be NULL.
  // "prev" will be set to the element immediately before "curr",
  // regardless of whether "curr" exists.
  // If "prev" is NULL, then there is no element in the list before "curr".
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  prev = NULL;
  curr = lists[l];
  while ((curr)&&(curr->index < ind)) {
    prev = curr;
    curr = curr->next;
  }
  if (curr) if (curr->index!=ind) curr = NULL;
}

void real_2001_cmds::submatrix::MultiplyBy(double a)
{
  int i;
  element *ptr;
  for (i=0; i<size; i++)
    for (ptr = lists[i]; ptr; ptr = ptr->next)
      ptr->value *= a;
}

void real_2001_cmds::submatrix::Show(OutputStream &s, bool summarize)
{
  if (summarize) if (state==MERGED || state==RECYCLED) return;
  s << "Submatrix #" << number << " : ";
  switch (state) {
    case BUILDING: s << "BUILDING\n"; break;
    case CANONICAL: s << "CANONICAL\n"; break;
    case MERGED: s << "MERGED"; break;
    case RECYCLED: s << "RECYCLED"; break;
  }
  int i;
  switch (state) {
    case RECYCLED:
      s << "\t Next : " << nextrecycled << "\n";
      break;
    case MERGED:
      s << "\t with : " << mergedptr << "\n";
      break;
    case BUILDING:
    case CANONICAL:
      s << "\tLevel " << level << " \t#incoming: " << incoming << "\n";
      // if (is_by_rows)
        s << "\tRows: " << size << "\n";  // "\tColumns: " << length << "\n";
      /*
      else
        s << "\tRows: " << length << "\tColumns: " << size << "\n";
      */
      if (lists) {
        for (i=0; i<size; i++) {
          // if (is_by_rows)
            s << "\tRow";
          // else s << "\tColumn";
          s << " " << i << " : ";
          if (lists[i]) lists[i]->Show(s);
          else s << "null\n";
        }
      } else s << "lists is null????\n";
  }
}

int real_2001_cmds::submatrix::Nonzeroes()
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int nnz = 0;
  int i;
  for (i=0; i<size; i++)
    if (lists[i]) nnz += lists[i]->Nonzeroes();
  return nnz;
}

int real_2001_cmds::submatrix::Signature(int M)
  // Hashing function: position of first nonzero element in last nonzero row
{
  DCASSERT(state==BUILDING || state==CANONICAL);
  DCASSERT(lists);
  int i = size-1;
  while (NULL==lists[i]) {
    i--;
    DCASSERT(i>=0);
  }
  int sign = i & M;
  sign = (sign * 256 + lists[i]->index) % M;
  sign *= 256;
  if (lists[i]->down) sign += lists[i]->down->number;
  sign %= M;
  return sign;
}

bool real_2001_cmds::submatrix::Equals(submatrix *t) const
  // assumes matrix elements are sorted
{
  if (level!=t->level) return false;
  if (size!=t->size) return false;
  int i;
  for (i=0; i<size; i++) {
    element *ptr = lists[i];
    element *tptr = t->lists[i];
    while (ptr) {
      if (NULL==tptr) return false;
      if (!ptr->Equals(tptr)) return false;
      ptr = ptr->next;
      tptr = tptr->next;
    }
    if (tptr) return false;
  }
  return true;
}

// ******************************************************************
// *                   real_2001_cmds  Methods                      *
// ******************************************************************

real_2001_cmds::real_2001_cmds(meddly_encoder &w, int bs)
{
  mxdfor = smart_cast <MEDDLY::expert_forest*>(w.getForest());
  DCASSERT(mxdfor);
  root = 0;
  batch = 0;
  batch_size = bs;
  curr_batch = 0;
  merged_list = 0;
  freed = 0;
  oldelements = 0;
  Table = new HashTable <submatrix>;
#ifdef MEASURE_STATS
  min_batch = LONG_MAX;
  max_batch = 0;
  num_batches = 0;
  total_edges = 0;
  memused = 0;
  maxused = 0;
  currnodes = 0;
  peaknodes = 0;
  numnonzeroes = 0;
#endif
#ifdef MEASURE_TIMES
  clock = makeTimer();
  time_mtadd = 0;
  time_accumulate = 0;
  time_reduce = 0;
  time_misc = 0;
#endif
}

real_2001_cmds::~real_2001_cmds()
{
  delete Table;
#ifdef MEASURE_TIMES
  doneTimer(clock);
#endif
}

void real_2001_cmds::reportStats(DisplayStream &out, const char* name) const
{
#ifdef MEASURE_STATS
  out << "Matrix diagram report for " << name << ":\n";
  out << "\t\t" << numnonzeroes << " elements added\n";
  out << "\t\tCurrent memory: ";
  out.PutMemoryCount(memused, 3);
  out << "\n\t\tMaximum memory: ";
  out.PutMemoryCount(maxused, 3);
  out << "\n\t\tCurrent nodes: " << currnodes << "\n";
  out << "\t\tMaximum nodes: " << peaknodes << "\n";
  if (num_batches) {
    out << "\t\t# batches: " << num_batches << "\n";
    out << "\t\tmin batch: " << min_batch << "\n";
    out << "\t\tmax batch: " << max_batch << "\n";
    double avg = numnonzeroes;
    avg /= num_batches;
    out << "\t\tavg batch: " << avg << "\n";
  }
  out << "\t\tUnique table stats:\n";
  out << "\t\t\tCurrent Size: " << Table->getSize() << "\n";
  out << "\t\t\tMaximum Size: " << Table->getMaxSize() << "\n";
  out << "\t\t\t# Resizes: " << Table->getNumResizes() << "\n";
  out << "\t\t\tCurrent Elements: " << Table->getNumElements() << "\n";
  out << "\t\t\tMaximum Elements: " << Table->getMaxElements() << "\n";
  out << "\t\t\tMax chain: " << Table->getMaxChain() << "\n";
#endif
#ifdef MEASURE_TIMES
  out << "\tCritical times for " << name << ":\n";
  out << "\t\tAdd minterm (total): " << time_mtadd << " s\n";
  out << "\t\tAccumulate (total): " << time_accumulate << " s\n";
  out << "\t\tReduce (total): " << time_reduce << " s\n";
  out << "\t\tMisc. (total): " << time_misc << " s\n";
#endif
}

shared_ddedge* real_2001_cmds::shareProc()
{
  return 0;
}

void real_2001_cmds::addBatch()
{
  if (0==batch) return;
#ifdef MEASURE_TIMES
  clock->reset();
#endif
  Accumulate(batch, root);
#ifdef MEASURE_TIMES
  time_accumulate += clock->elapsed();
  clock->reset();
#endif
  batch = Canonicalize(batch);
#ifdef MEASURE_TIMES
  time_reduce += clock->elapsed();
  clock->reset();
#endif
  RecycleMergedList();
  Recycle(root);
  root = batch;
  batch = 0;
  num_batches++;
  min_batch = MIN(min_batch, curr_batch);
  max_batch = MAX(max_batch, curr_batch);
  curr_batch = 0;
#ifdef MEASURE_TIMES
  time_misc += clock->elapsed();
#endif
}

void real_2001_cmds::addTTEdge(int* from, int* to, double rate)
{
#ifdef MEASURE_TIMES
  clock->reset();
#endif
  int k = mxdfor->getDomain()->getNumVariables();

  // Ignore self-loops:
  if (0==memcmp(from+1, to+1, k*sizeof(int))) return;

  if (0==batch) batch = NewTempMatrix(k);
  submatrix* node = batch;
  for ( ; k; k--) {
    submatrix::element* prev, *curr;
    node->FindInList(from[k], to[k], prev, curr);
    if (0==curr) { // element doesn't exist, we need to add it
      curr = NewElement();
      double val;
      submatrix *down;
      submatrix::element* next;
      if (k-1==0) {
        down = 0;
        val = rate;
        numnonzeroes++;
      } else {
        down = NewTempMatrix(k-1);
        val = 1.0;
      }
      if (prev) next = prev->next; else next = node->lists[from[k]];
      curr->Set(to[k], val, down, next);
      if (prev) prev->next = curr; else node->lists[from[k]] = curr;
    } // if 0==curr
    node = curr->down;
  } // for k
  curr_batch++;
#ifdef MEASURE_TIMES
  time_mtadd += clock->elapsed();
#endif
  if (curr_batch >= batch_size) addBatch();
}


real_2001_cmds::submatrix* real_2001_cmds::Canonicalize(submatrix* sm)
{
  if (sm->state == submatrix::CANONICAL) {
    // We're already canonicalized
    return sm;
  }
  if (sm->state == submatrix::MERGED) {
    // Essentially, this is a cache hit
    return ShallowCopy(sm->mergedptr);
  }
  // If we're at the top, normalize below
  if (sm->level == mxdfor->getDomain()->getNumVariables()) {
    Normalize(sm);
    // OutLog->getStream() << "after normalizing:\n";
    // Dump(OutLog->getStream(), true);
  }

  // Reduce below...
  int i;
  for (i=0; i<sm->size; i++) {
    submatrix::element *ptr;
    for (ptr = sm->lists[i]; ptr; ptr = ptr->next) if (ptr->down)
      ptr->down = Canonicalize(ptr->down);
  }

  submatrix *find = Table->UniqueInsert(sm);
  if (sm==find) {
    // We're the canonical
    sm->state = submatrix::CANONICAL;
    sm->incoming = 1;
  } else {
    // We've been merged
    // OutLog->getStream() << "Merging " << sm << " with " << find->number << "\n";
    // sm->Show(OutLog->getStream(), true);

    UnlinkDown(sm);
    sm->Free(oldelements, memused);
    sm->state = submatrix::MERGED;
    sm->mergedptr = find;
    sm->SetNext(merged_list);
    merged_list = sm;
    DCASSERT(find->state == submatrix::CANONICAL);
    find->incoming++;
    // OutLog->getStream() << "#incoming of " << find->number << " : " << find->incoming << "\n";
  }

  return find;
}

double real_2001_cmds::Normalize(submatrix* sm)
{
  double scale = 0.0;
  DCASSERT(sm);
  // If this node is canonical, then we're already normalized
  if (sm->state == submatrix::CANONICAL) return 1.0;
  DCASSERT(sm->state == submatrix::BUILDING);
  if (NULL==sm->lists) return 0.0;   // this shouldn't happen, right?
  int i;
  // First, normalize below us and figure out the max element
  for (i=0; i<sm->size; i++) {
    submatrix::element *ptr;
    for (ptr = sm->lists[i]; ptr; ptr = ptr->next) {
      if (ptr->down) ptr->value *= Normalize(ptr->down);
      scale = MAX(scale, ptr->value);
    }
  }
  // Don't scale ourselves at top level...
  if (sm->level == mxdfor->getDomain()->getNumVariables()) return 1.0;
  // Don't bother scaling if we're already...
  if (scale==1.0) return scale;
  // this shouldn't happen either...
  if (scale==0.0) return scale;
  // Now, scale the elements so that max=1.0
  sm->MultiplyBy(1/scale);
  return scale;
}


void real_2001_cmds::Accumulate(submatrix* build, const submatrix* canon)
 // canonical and all its children must not be changed.
 // root and its children may be changed.
{
  if (0==canon) return;

  // Serious probelms if any of these fail
  DCASSERT(build);
  DCASSERT(build->state == submatrix::BUILDING);
  DCASSERT(build->lists);
  DCASSERT(canon->lists);
  DCASSERT(build->size == canon->size);
  DCASSERT(build->level == canon->level);

  int k = build->level;

  int i;
  for (i=0; i<build->size; i++) {
    // merge the two lists, *copying* the ptrs in the canonical md.
    submatrix::element *newptr = NULL;
    submatrix::element *bptr = build->lists[i];
    const submatrix::element *cptr = canon->lists[i];
    int bindex;
    if (bptr) bindex = bptr->index;
    else bindex = INT_MAX;
    while (cptr) {
      if (bindex < cptr->index) {
        // next node is from b list
        if (newptr) newptr->next = bptr;
        else build->lists[i] = bptr;
        newptr = bptr;
        // advance b
        bptr = bptr->next;
        if (bptr) bindex = bptr->index;
        else bindex = INT_MAX;
      } else if (bindex == cptr->index) {
        // equal index
         if (k==1) {
          // bottom level, just add the values!
          bptr->value += cptr->value;
        } else {
          // skip scaling if values are equal
          if (bptr->value != cptr->value) {
            double downscale = bptr->value / cptr->value;
            bptr->down->MultiplyBy(downscale);
            bptr->value = cptr->value;
          }
          // Not bottom level, add recursively
          Accumulate(bptr->down, cptr->down);
        }
        // next node is modified b node
        if (newptr) newptr->next = bptr;
        else build->lists[i] = bptr;
        newptr = bptr;
        // advance b
        bptr = bptr->next;
        if (bptr) bindex = bptr->index;
        else bindex = INT_MAX;
        // advance c
        cptr = cptr->next;
      } else { // bindex > cptr->index
        // copy current element of c list
        submatrix* dncpy = ShallowCopy(cptr->down);
        submatrix::element *copy = NewElement();
        copy->Set(cptr->index, cptr->value, dncpy, NULL);
        if (newptr) newptr->next = copy;
        else build->lists[i] = copy;
        newptr = copy;
        // advance c
        cptr = cptr->next;
      }
    }
    // we've exhausted c-list, add remainder of b-list (if any)
    if (newptr) newptr->next = bptr;
    else build->lists[i] = bptr;
  }
}

void real_2001_cmds::Recycle(submatrix* sm)
{
  if (0==sm) return;
  DCASSERT(sm->state == submatrix::CANONICAL);
  DCASSERT(sm->incoming>0);
  sm->incoming--;
  if (sm->incoming) return;
  // we're the last pointer, recycle the node
  // Remove us from the hash table!
  Table->Remove(sm);

  UnlinkDown(sm);
  memused -= sizeof(submatrix);
  sm->Free(oldelements, memused);
  sm->state = submatrix::RECYCLED;
  sm->nextrecycled = freed;
  freed = sm;
  currnodes--;
}

void real_2001_cmds::UnlinkDown(submatrix* sm)
{
  if (0==sm) return;
  DCASSERT(sm->state == submatrix::BUILDING || sm->state == submatrix::CANONICAL);
  if (sm->level) if (sm->lists) {
    // OutLog->getStream() << "Unlinking " << node << ": ";
    int i;
    submatrix::element *ptr;
    for (i=0; i<sm->size; i++)
      for (ptr = sm->lists[i]; ptr; ptr = ptr->next) if (ptr->down) {
        // OutLog->getStream() << ptr->down << " ";
        Recycle(ptr->down);
      }
    // OutLog->getStream() << endl;
  }
}



real_2001_cmds::submatrix* real_2001_cmds::NewTempMatrix(int k)
{
  if (k<1) return 0;
  currnodes++;
  submatrix* next = freed;
  if (next) {
    freed = next->nextrecycled;
  } else {
    next = new submatrix;
    peaknodes++;
    next->number = peaknodes;
  }
  memused += sizeof(submatrix);
  // set up by rows
  next->state = submatrix::BUILDING;
  next->Alloc(mxdfor->getDomain()->getVariableBound(k, false), memused);
  next->level = k;
  maxused = MAX(memused, maxused);
  return next;
}



#endif  // ifdef ENABLE_CMDS

// **************************************************************************
// *                                                                        *
// *                       mt_known_stategroup  class                       *
// *                                                                        *
// **************************************************************************

// Stategroups for known sets, using minterm batches
class mt_known_stategroup {
  meddly_reachset &rss;
  minterm_pool &mp;
  MEDDLY::enumerator s_iter;
  int* exploring;
  int* discovering;
public:
  mt_known_stategroup(meddly_reachset &r, minterm_pool &m);
  ~mt_known_stategroup();

  inline bool hasUnexplored() const {
    return s_iter;
  }
  int* getUnexplored(shared_state *);
  bool addState(const shared_state *, int* &id);

  inline void reportStats(DisplayStream &out, const char* name) const { }
  inline shared_ddedge* shareS() { DCASSERT(0); return 0; }

  // hook for cool stuff
  inline int getLevelChange() const { return s_iter.levelChanged(); }
};

// **************************************************************************

mt_known_stategroup::
mt_known_stategroup(meddly_reachset &r, minterm_pool &m)
 : rss(r), mp(m)
{
  s_iter.init(MEDDLY::enumerator::FULL, rss.getMddForest());
  s_iter.start(rss.getStates());
  exploring = mp.getMinterm();
  discovering = mp.getMinterm();
}

mt_known_stategroup::~mt_known_stategroup()
{
  mp.doneMinterm(exploring);
  mp.doneMinterm(discovering);
}

int*
mt_known_stategroup
::getUnexplored(shared_state *s)
{
  DCASSERT(s_iter);
  DCASSERT(exploring);
  mp.doneMinterm(exploring);
  exploring = mp.getMinterm();
  mp.fillMinterm(exploring, s_iter.getAssignments());
  ++s_iter;
  try {
    rss.MddMinterm2State(exploring, s);
    return exploring;
  }
  catch (sv_encoder::error e) {
    convert(e);
    return 0;
  }
}

bool
mt_known_stategroup
::addState(const shared_state *s, int* &id)
{
  DCASSERT(discovering);
  mp.doneMinterm(discovering);
  discovering = mp.getMinterm();
  try {
    rss.MddState2Minterm(s, discovering);
    id = discovering;
    return false;
  }
  catch (sv_encoder::error e) {
    convert(e);
    return false;
  }
}

// **************************************************************************
// *                                                                        *
// *                         mt_sr_stategroup class                         *
// *                                                                        *
// **************************************************************************

// Stategroups for minterm batches, one state removed at a time.
class mt_sr_stategroup {
protected:
  meddly_reachset &rss;
  minterm_pool &mp;
  shared_ddedge* S;
  shared_ddedge* U;
  int** batch;
  int alloc;
  int used;
  int* exploring;
  int* discovering;
  shared_ddedge* tempedge;
#ifdef MEASURE_STATS
  int min_batch;
  int max_batch;
  long num_batches;
  long num_additions;
#endif
public:
  mt_sr_stategroup(meddly_reachset &r, minterm_pool &m, int b);
  ~mt_sr_stategroup();

  inline bool hasUnexplored() const {
    DCASSERT(U);
    return (used>0) || U->E.getNode();
  }
  int* getUnexplored(shared_state *);
  bool addState(const shared_state *, int* &id);

  inline void reportStats(DisplayStream &out, const char* name) const {
    out << "\tBatch for " << name << " required ";
    size_t batchmem = alloc*sizeof(int);
    out.PutMemoryCount(batchmem, 3);
    out << "\n";
#ifdef MEASURE_STATS
    if (num_batches) {
      out << "\t\t# batches: " << num_batches << "\n";
      out << "\t\tmin batch: " << min_batch << "\n";
      out << "\t\tmax batch: " << max_batch << "\n";
      double avg = num_additions;
      avg /= num_batches;
      out << "\t\tavg batch: " << avg << "\n";
    }
#endif
  }
  inline shared_ddedge* shareS() { return Share(S); }

  inline int getLevelChange() const { return 0; }

  inline void clear() {
    DCASSERT(!hasUnexplored());
    S->E = U->E;
  }

protected:
  inline void addBatch() {
    if (0==used) return;
    try {
      rss.createMinterms(batch, used, tempedge);
      S->E += tempedge->E;
      U->E += tempedge->E;
      for (int i=0; i<used; i++) {
        mp.doneMinterm(batch[i]);
        batch[i] = 0;
      }
#ifdef MEASURE_STATS
      min_batch = MIN(min_batch, used);
      max_batch = MAX(max_batch, used);
      num_batches++;
      num_additions += used;
#endif
      used = 0;
    } // try
    catch (sv_encoder::error e) {
      convert(e);
    }
  }
};

// **************************************************************************
// *                        mt_sr_stategroup methods                        *
// **************************************************************************

mt_sr_stategroup::
mt_sr_stategroup(meddly_reachset &r, minterm_pool &m, int bs)
 : rss(r), mp(m)
{
  S = rss.newMddEdge();
  U = rss.newMddEdge();
  alloc = bs;
  used = 0;
  batch = new int*[alloc];
  for (int i=0; i<alloc; i++) batch[i] = 0;
  exploring = mp.getMinterm();
  discovering = mp.getMinterm();
  tempedge = rss.newMddEdge();
#ifdef MEASURE_STATS
  min_batch = INT_MAX;
  max_batch = 0;
  num_batches = 0;
  num_additions = 0;
#endif
}

mt_sr_stategroup::~mt_sr_stategroup()
{
  // DON'T delete minterms
  delete[] batch;
  Delete(S);
  Delete(U);
  Delete(tempedge);
  mp.doneMinterm(exploring);
  mp.doneMinterm(discovering);
}

int*
mt_sr_stategroup
::getUnexplored(shared_state *s)
{
  DCASSERT(exploring);
  mp.doneMinterm(exploring);
  exploring = mp.getMinterm();
  if (0==U->E.getNode()) {
    addBatch();
  }
  DCASSERT(U->E.getNode());
  try {
    MEDDLY::enumerator first(U->E);
    mp.fillMinterm(exploring, first.getAssignments());
    rss.MddMinterm2State(exploring, s);
    MEDDLY::dd_edge temp(rss.getMddForest());
    rss.getMddForest()->createEdge(&exploring, 1, temp);
    U->E -= temp;
    return exploring;
  }
  catch (MEDDLY::error ce) {
    convert(ce);
    return 0;
  }
  catch (sv_encoder::error e) {
    convert(e);
    return 0;
  }
}

bool
mt_sr_stategroup
::addState(const shared_state *s, int* &id)
{
  DCASSERT(discovering);
  mp.doneMinterm(discovering);
  discovering = mp.getMinterm();
  id = discovering;
  bool seen;
  try {
    rss.MddState2Minterm(s, discovering);
    rss.getMddForest()->evaluate(S->E, discovering, seen);
  }
  catch (MEDDLY::error e) {
    convert(e);
  }
  catch (sv_encoder::error e) {
    convert(e);
  }
  if (seen) return false;
  batch[used] = mp.shareMinterm(discovering);
  used++;
  if (used >= alloc) {
    addBatch();
  }
  return true;
}


// **************************************************************************
// *                                                                        *
// *                         mt_br_stategroup class                         *
// *                                                                        *
// **************************************************************************

// Stategroups for minterm batches, several states removed at a time.
class mt_br_stategroup : public mt_sr_stategroup {
  shared_ddedge* B; // batch for enumerating unexplored
  MEDDLY::enumerator b_iter;
  bool maximize_refills;
#ifdef MEASURE_STATS
  long iterator_resets;
#endif
public:
  mt_br_stategroup(meddly_reachset &r, minterm_pool &m, int b, bool maxref);
  ~mt_br_stategroup();

  inline bool hasUnexplored() const {
    if (b_iter) return true;  // still traversing B
    DCASSERT(U);
    return (used>0) || U->E.getNode();
  }
  int* getUnexplored(shared_state *);

#ifdef MEASURE_STATS
  inline void reportStats(DisplayStream &out, const char* name) const {
    mt_sr_stategroup::reportStats(out, name);
    if (iterator_resets) {
      out << "\t\t#iterator resets: " << iterator_resets << "\n";
    }
  }
#endif

  inline int getLevelChange() const { return b_iter.levelChanged(); }
};

// **************************************************************************
// *                        mt_br_stategroup methods                        *
// **************************************************************************

mt_br_stategroup::
mt_br_stategroup(meddly_reachset &w, minterm_pool &m, int bs, bool maxref)
 : mt_sr_stategroup(w, m, bs)
{
  b_iter.init(MEDDLY::enumerator::FULL, w.getMddForest());
  B = w.newMddEdge();
  maximize_refills = maxref;
#ifdef MEASURE_STATS
  iterator_resets = 0;
#endif
}

mt_br_stategroup::~mt_br_stategroup()
{
  Delete(B);
}

int*
mt_br_stategroup
::getUnexplored(shared_state *s)
{
  if (!b_iter) {
    // need to refill B
    if (0==U->E.getNode() || maximize_refills) {
      addBatch();
    }
    DCASSERT(U->E.getNode());
    // set B=U, U=0
    SWAP(B, U);
    rss.getMddForest()->createEdge(false, U->E);
    // restart iterator
    b_iter.start(B->E);
#ifdef MEASURE_STATS
    iterator_resets++;
#endif
  }
  DCASSERT(b_iter);
  DCASSERT(exploring);
  mp.doneMinterm(exploring);
  exploring = mp.getMinterm();
  mp.fillMinterm(exploring, b_iter.getAssignments());
  ++b_iter;
  try {
    rss.MddMinterm2State(exploring, s);
    return exploring;
  }
  catch (sv_encoder::error e) {
    convert(e);
    return 0;
  }
}

// **************************************************************************
// *                                                                        *
// *                                                                        *
// * Generator classes.  These are thin wrappers around the above classes,  *
// * and are used when calling the reachability set / reachability graph /  *
// * Markov chain generation engine template class.  The class interfaces   *
// * are all similar, and are imposed by the generation template functions. *
// *                                                                        *
// *                                                                        *
// **************************************************************************


/**
  Generic wrapper around data structures, for generation
*/
template <class TANGR, class VANGR, class EDGEGR>
class gen_wrapper_templ {
    bool states_only;
    long& level_change;

    minterm_pool* minterms;
    TANGR* tangible;
    VANGR* vanishing;
    EDGEGR* edges;

    meddly_reachset* RSS;
    meddly_monolithic_rg* RGR;
    meddly_process* PROC;

  public:
    gen_wrapper_templ(long &lc, bool so, minterm_pool* mp, TANGR *t, VANGR *v, EDGEGR *e,
      meddly_reachset* rss, meddly_monolithic_rg* rgr, meddly_process* proc)
      : level_change(lc)
    {
      states_only = so;
      minterms = mp;
      tangible = t;
      vanishing = v;
      edges = e;
      RSS = rss;
      RGR = rgr;
      PROC = proc;
    }

    ~gen_wrapper_templ() {
      delete tangible;
      delete vanishing;
      delete edges;
      delete minterms;
      Delete(RSS);
      Delete(RGR);
      Delete(PROC);
    }

    //
    // Really convenient methods
    //

    inline void generateRG(debugging_msg &debug, dsde_hlm &hm) {
      generateRGt<gen_wrapper_templ <TANGR, VANGR, EDGEGR>, int*>
        (debug, hm, *this);

      DCASSERT(RSS);
      if (!RSS->hasStates()) {
        RSS->setStates(tangible->shareS());
      }

      if (edges) {
        edges->addBatch(); // add the final batch of edges
      }
    }

    inline void generateMC(debugging_msg &debug, dsde_hlm &hm) {
      generateMCt<gen_wrapper_templ <TANGR, VANGR, EDGEGR>, int*>
        (debug, hm, *this);

      DCASSERT(RSS);
      if (!RSS->hasStates()) {
        RSS->setStates(tangible->shareS());
      }

      if (edges) {
        edges->addBatch(); // add the final batch of edges
      }
    }

    inline void generateSMP(debugging_msg &debug, dsde_hlm &hm) {
      generateSMPt<gen_wrapper_templ <TANGR, VANGR, EDGEGR>, int*>
        (debug, hm, *this);

      DCASSERT(RSS);
      if (!RSS->hasStates()) {
        RSS->setStates(tangible->shareS());
      }

      if (edges) {
        edges->addBatch(); // add the final batch of edges
      }
    }

    void reportStats(DisplayStream &out, const char* proc) {
      DCASSERT(RSS);
      RSS->reportStats(out);
      if (minterms)   minterms->reportStats(out);
      if (tangible)   tangible->reportStats(out, "tangible");
      if (vanishing)  vanishing->reportStats(out, "vanishing");
      if (edges)      edges->reportStats(out, proc);
    }

    void startProcess(hldsm &hm, subengine* completion) {
      DCASSERT(RSS);
      DCASSERT(0==RGR);
      DCASSERT(0==PROC);
      state_lldsm* lm;
      if (hm.GetProcessType() == lldsm::FSM) {
        lm = new graph_lldsm(lldsm::FSM);
      } else {
        lm = new stochastic_lldsm(hm.GetProcessType());
      }
      hm.SetProcess(lm);
      lm->setCompletionEngine(completion);
      lm->setRSS(RSS);
      RSS = 0;
    }

    void finishProcess(hldsm &hm) {
      DCASSERT(RSS);
      DCASSERT(RGR);

      // Old code
      // for RGRs:
      lldsm* lm = hm.GetProcess();
      graph_lldsm* glm = 0;
      stochastic_lldsm* slm = 0;
      if (0==lm) {
        if (PROC) {
          slm = new stochastic_lldsm(lldsm::CTMC);
          glm = slm;
        } else {
          glm = new graph_lldsm(lldsm::FSM);
        }
        glm->setRSS(RSS);
        RSS = 0;
        lm = glm;
        hm.SetProcess(lm);
      } else {
        glm = smart_cast <graph_lldsm*> (lm);
        DCASSERT(glm);
        slm = dynamic_cast <stochastic_lldsm*> (lm);
      }

      shared_ddedge* rg = edges->shareProc();
      if (0==rg) {
        if (hm.StartError(0)) {
          if (PROC) {
            hm.SendError("Could not obtain final MC");
          } else {
            hm.SendError("Could not obtain final RG");
          }
          hm.DoneError();
        }
        // So we don't try to rebuild later
        hm.SetProcess(MakeErrorModel());
      } else {
        if (slm) {
          DCASSERT(PROC);
          PROC->setActual( Share(rg) );
          // TBD!  NEED an actual initial vector!
          LS_Vector initial;
          slm->setPROC(initial, PROC);
          PROC = 0;
        }
        RGR->setActual(rg);
        glm->setRGR(RGR);
        RGR = 0;
        lm->setCompletionEngine(0);
      }
    }

  public:
    //
    // required methods for RS generation
    //

    inline bool add(bool isVan, const shared_state* s, int* &id) {
      if (isVan) {
        DCASSERT(vanishing);
        return vanishing->addState(s, id);
      } else {
        DCASSERT(tangible);
        return tangible->addState(s, id);
      }
    }

    inline bool hasUnexploredVanishing() const {
      DCASSERT(vanishing);
      return vanishing->hasUnexplored();
    }

    inline bool hasUnexploredTangible() const {
      DCASSERT(tangible);
      return tangible->hasUnexplored();
    }

    inline int* getUnexploredVanishing(shared_state *s) {
      DCASSERT(vanishing);
      return vanishing->getUnexplored(s);
    }

    inline int* getUnexploredTangible(shared_state *s) {
      DCASSERT(tangible);
      if (edges) {
        if (tangible->getLevelChange() > level_change) {
#ifdef DEBUG_FREQ
          fprintf(stderr, "Level change %d is above threshold %ld\n",
            tangible->getLevelChange(), level_change
          );
#endif
          edges->addBatch();
        }
      }
      return tangible->getUnexplored(s);
    }

    inline void clearVanishing(debugging_msg &debug) {
      if (debug.startReport()) {
        debug.report() << "Eliminating vanishing states\n";
        debug.stopIO();
      }
      DCASSERT(vanishing);
      vanishing->clear();
    }

    inline bool statesOnly() const {
      return states_only;
    }

    inline void show(OutputStream &s, bool isVan, const int* id, const shared_state* curr) const
    {
      if (isVan) s << "vanishing state: ";
      else       s << "tangible  state: ";
      curr->Print(s, 0);
    }
    inline void show(OutputStream &s, const int* id) const
    {
      s << " minterm ";
      minterms->showMinterm(s, id);
    }
    inline void show(OutputStream &s, const shared_state* curr) const
    {
      s << " state ";
      curr->Print(s, 0);
    }

    static inline void makeIllegalID(int* &id) {
      id = 0;
    }

    //
    // required methods for RG generation
    //

    inline void addInitial(const int*) {
      // wasn't this built already?
    }

    inline void addEdge(int* from, int* to) {
      DCASSERT(edges);
      return edges->addEdge(from, to);
    }

    //
    // required methods for MC generation
    //

    inline void eliminateVanishing(debugging_msg &debug) {
      if (debug.startReport()) {
        debug.report() << "Eliminating vanishing states\n";
        debug.stopIO();
      }
      DCASSERT(vanishing);
      // edges->eliminateVanishing();
      vanishing->clear();
    }

    inline void addInitial(bool isVan, const int* id, double wt) {
      // TBD - not sure about this one
    }

    inline void addTTEdge(int* from, int* to, double wt) {
      DCASSERT(edges);
      edges->addTTEdge(from, to, wt);
    }

    inline void addTVEdge(int* from, int* to, double wt) {
      DCASSERT(edges);
      edges->addTVEdge(from, to, wt);
    }

    inline void addVTEdge(int* from, int* to, double wt) {
      DCASSERT(edges);
      edges->addVTEdge(from, to, wt);
    }

    inline void addVVEdge(int* from, int* to, double wt) {
      DCASSERT(edges);
      edges->addVVEdge(from, to, wt);
    }

};

// **************************************************************************
// *                                                                        *
// *                                                                        *
// *  The actual explicit Meddly generation engine.  Implementation simply  *
// *  builds an appropriate instance of one of the generator classes, and   *
// *                  sends it to the generation function.                  *
// *                                                                        *
// *                                                                        *
// **************************************************************************

// **************************************************************************
// *                                                                        *
// *                                                                        *
// *                          meddly_explgen class                          *
// *                                                                        *
// *                                                                        *
// **************************************************************************

/** Gigantic class for all explicit process generation with meddly.
    States may be added in batch, where the batch size is taken
    from an option.
    This is the "main" engine, for when reachable states are not yet known.
*/
class meddly_explgen : public meddly_procgen {
  friend class init_explmeddly;
  static long batch_size;
  static unsigned matrix_style;
  static bool batch_removal;
  static bool maximize_batch_refills;
  static long level_change;
  bool states_only_this_time;

protected:
#ifdef ENABLE_CMDS
  static const unsigned CMD = 0;
  static const unsigned IRMXD = 1;
  static const unsigned QRMXD = 2;
  static const unsigned NUM_MX_STYLES = 3;
#else
  static const unsigned IRMXD = 0;
  static const unsigned QRMXD = 1;
  static const unsigned NUM_MX_STYLES = 2;
#endif

public:
  meddly_explgen();

  virtual MEDDLY::forest::policies buildRSSPolicies() const;

protected:
  static void preprocess(dsde_hlm &m);

public:
  virtual bool AppliesToModelType(hldsm::model_type mt) const;
  virtual void RunEngine(hldsm* m, result &states_only);

  inline static int getBatchSize() { return batch_size; }
  inline static bool getMBR() { return maximize_batch_refills; }
#ifdef ENABLE_CMDS
  inline static bool isCMD() { return CMD == matrix_style; }
#endif

protected:
  inline bool startGen(const hldsm &hm, const char* proc) const {
    if (!meddly_procgen::startGen(hm, proc)) return false;
    em->report() << "\n";
    em->newLine();
    em->report() << "Using Meddly: ";
    showAlgorithm(em->report());
    em->report() << getStyleName() << " vars.";
    showMatrix(em->report());
    em->report() << "\n";
    em->report() << "\tMax batch: " << batch_size << "\n";
    return true;
  }

  static inline void showAlgorithm(OutputStream &s) {
    s << "Explicit, ";
    if (batch_removal)  s << "batch";
    else                s << "single";
    s << " removal, ";
  }

  inline void showMatrix(OutputStream &s) const {
    if (states_only_this_time) return;
    switch (matrix_style) {
      case CMD:
        s << ", CMDs";
        return;
      case IRMXD:
        s << ", IRMxDs";
        return;
      case QRMXD:
        s << ", QRMxDs";
        return;
      default:
        s << ", unknown DDs";
    }
  }

  void generateRS(dsde_hlm &hm, meddly_reachset* rss);
  void generateRG(dsde_hlm &hm, meddly_reachset* rss);
  void generateMC(dsde_hlm &hm, meddly_reachset* rss);

private:
  template <class GWRAP>
  inline void DoRS(dsde_hlm &hm, GWRAP &G) {
    //
    // Start reporting on generation
    //
    timer watch;
    if (startGen(hm, "reachability set")) {
      em->stopIO();
    }

    //
    // Generate, in a try block
    //
    try {
      em->waitTerm();
      G.generateRG(Debug(), hm);

      // Reporting
      if (meddly_procgen::stopGen(false, hm, "reachability set", watch)) {
        G.reportStats(em->report(), 0);
        em->stopIO();
      }

      // Set process
      G.startProcess(hm, this);

      // Cleanup
      em->resumeTerm();
      return;
    }
    catch (subengine::error e) {
      // Reporting
      if (meddly_procgen::stopGen(true, hm, "reachability set", watch)) {
        G.reportStats(em->report(), 0);
        em->stopIO();
      }

      // Set process
      hm.SetProcess(MakeErrorModel());

      // Cleanup
      em->resumeTerm();
      throw;
    }
  }


  template <class GWRAP>
  inline void DoRG(dsde_hlm &hm, GWRAP &G) {
    //
    // Start reporting on generation
    //
    timer watch;
    if (startGen(hm, "reachability graph")) {
      em->stopIO();
    }

    //
    // Generate, in a try block
    //
    try {
      em->waitTerm();
      G.generateRG(Debug(), hm);

      // Reporting
      if (meddly_procgen::stopGen(false, hm, "reachability graph", watch)) {
        G.reportStats(em->report(), "reachability graph");
        em->stopIO();
      }

      // Set process
      G.finishProcess(hm);

      // Cleanup
      em->resumeTerm();
      return;
    }
    catch (subengine::error e) {
      // Reporting
      if (meddly_procgen::stopGen(true, hm, "reachability graph", watch)) {
        G.reportStats(em->report(), "reachability graph");
        em->stopIO();
      }

      // Set process
      hm.SetProcess(MakeErrorModel());

      // Cleanup
      em->resumeTerm();
      throw;
    }
  }

  template <class GWRAP>
  inline void DoMC(dsde_hlm &hm, GWRAP &G) {
    //
    // Start reporting on generation
    //
    timer watch;
    if (startGen(hm, "Markov chain")) {
      em->stopIO();
    }

    //
    // Generate, in a try block
    //
    try {

      em->waitTerm();

      switch (remove_vanishing) {
        case BY_PATH:
          G.generateMC(Debug(), hm);
          break;

        case BY_SUBGRAPH:
          G.generateSMP(Debug(), hm);
          break;

        default:
          DCASSERT(0);
      }

      // Reporting
      if (meddly_procgen::stopGen(false, hm, "Markov chain", watch)) {
        G.reportStats(em->report(), "Markov chain");
        em->stopIO();
      }

      // Set process
      G.finishProcess(hm);

      // Cleanup
      em->resumeTerm();
      return;
    }
    catch (subengine::error e) {
      // Reporting
      if (meddly_procgen::stopGen(true, hm, "Markov chain", watch)) {
        G.reportStats(em->report(), "Markov chain");
        em->stopIO();
      }

      // Set process
      hm.SetProcess(MakeErrorModel());

      // Cleanup
      em->resumeTerm();
      throw;
    }
  }


};

long meddly_explgen::batch_size;
unsigned meddly_explgen::matrix_style;
bool meddly_explgen::batch_removal;
bool meddly_explgen::maximize_batch_refills;
long meddly_explgen::level_change;

meddly_explgen the_meddly_explgen;

// **************************************************************************
// *                                                                        *
// *                         meddly_explgen methods                         *
// *                                                                        *
// **************************************************************************

meddly_explgen::meddly_explgen() : meddly_procgen()
{
}

MEDDLY::forest::policies
meddly_explgen::buildRSSPolicies() const
{
  MEDDLY::forest::policies p = meddly_procgen::buildRSSPolicies();
  if (QRMXD == matrix_style) {
    p.setQuasiReduced();
  }
  return p;
}

void meddly_explgen::preprocess(dsde_hlm &m)
{
  if (m.buildPartInfo()) return;
  if (m.StartError(0)) {
    em->cerr() << "Meddly requires a structured model (try partitioning)";
    m.DoneError();
  }
  throw Engine_Failed;
}

bool meddly_explgen::AppliesToModelType(hldsm::model_type mt) const
{
  return (hldsm::Asynch_Events == mt);
}

void meddly_explgen::RunEngine(hldsm* hm, result &states_only)
{
  DCASSERT(hm);
  DCASSERT(AppliesToModelType(hm->Type()));
  DCASSERT(states_only.isNormal());
  states_only_this_time = states_only.getBool();
  lldsm* lm = hm->GetProcess();
  if (lm) {
    // we already have something, deal with it
    subengine* e = lm->getCompletionEngine();
    if (0==e)                   return;
    if (states_only.getBool())  return;
    if (e!=this)                return e->RunEngine(hm, states_only);
  }

  dsde_hlm* dhm = smart_cast <dsde_hlm*> (hm);
  DCASSERT(dhm);
  if (0==lm) {
    // Preprocess
    try {
      preprocess(*dhm);
    }
    catch (error status) {
      hm->SetProcess(MakeErrorModel());
      throw status;
    }
  }

  //
  // Set up everything
  //
  meddly_reachset* rss = 0;
  if (0==lm) {
    rss = new meddly_reachset;
    meddly_varoption* mvo = makeVariableOption(*dhm, *rss);
    DCASSERT(mvo);
    rss->saveMxdWrapper(mvo->shareMxdWrap());
    try {
      mvo->initializeVars();
      delete mvo;
    }
    catch (error e) {
      delete mvo;
      Delete(rss);
      throw e;
    }
  } else {
    state_lldsm* slm = smart_cast <state_lldsm*> (lm);
    DCASSERT(slm);
    rss = smart_cast <meddly_reachset*> (Share(slm->useRSS()));
    DCASSERT(rss);
  }

  //
  // Generate process
  //
  if (states_only_this_time) {
    return generateRS(*dhm, rss);
  }
  if (hm->GetProcessType() == lldsm::FSM) {
    return generateRG(*dhm, rss);
  } else {
    return generateMC(*dhm, rss);
  }
}

void meddly_explgen::generateRS(dsde_hlm &hm, meddly_reachset* rss)
{
  DCASSERT(rss);
  DCASSERT(0==hm.GetProcess());

  // Everybody uses this
  minterm_pool* mp = new minterm_pool(6+6*getBatchSize(), rss->getNumLevels());

  //
  // Only 2 cases to consider :^)
  //

  if (batch_removal) {
    //
    // Batch removal
    //
    gen_wrapper_templ <mt_br_stategroup, mt_br_stategroup, edge_minterms>
      G(
        level_change, true, mp,
        new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
        new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
        (edge_minterms*) 0,
        rss, 0, 0
      );

    DoRS(hm, G);
    return;
  } else {
    //
    // Single removal
    //
    gen_wrapper_templ <mt_sr_stategroup, mt_sr_stategroup, edge_minterms>
      G(
        level_change, true, mp,
        new mt_sr_stategroup(*rss, *mp, getBatchSize()),
        new mt_sr_stategroup(*rss, *mp, getBatchSize()),
        (edge_minterms*) 0,
        rss, 0, 0
      );

    DoRS(hm, G);
    return;
  }

}

void meddly_explgen::generateRG(dsde_hlm &hm, meddly_reachset* rss)
{
  DCASSERT(rss);
  // Everybody uses this
  minterm_pool* mp = new minterm_pool(6+6*getBatchSize(), rss->getNumLevels() );

  //
  // Storage for the reachgraph
  //
  meddly_monolithic_rg* rgr = new meddly_monolithic_rg(rss->shareVars(), rss->grabMxdWrapper());

  //
  // A little ugly - consider all possible cases "by hand"
  //

  //
  // CMDs
  //
#ifdef ENABLE_CMDS
  if (isCMD()) {
    if (hm.GetProcess()) {
      //
      // Second pass
      //
      if (batch_removal) {
          //
          // Batch removal
          //
          gen_wrapper_templ <mt_known_stategroup, mt_br_stategroup, edge_2001_cmds>
          G(
            level_change, false, mp,
            new mt_known_stategroup(*rss, *mp),
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new edge_2001_cmds(rgr->useMxdWrapper(), getBatchSize()),
            rss, rgr, 0
          );

          DoRG(hm, G);
          return;
      } else {
          //
          // Single removal
          //
          gen_wrapper_templ <mt_known_stategroup, mt_sr_stategroup, edge_2001_cmds>
          G(
            level_change, false, mp,
            new mt_known_stategroup(*rss, *mp),
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new edge_2001_cmds(rgr->useMxdWrapper(), getBatchSize()),
            rss, rgr, 0
          );

          DoRG(hm, G);
          return;
      }
    } else {
      //
      // First pass
      //
      if (batch_removal) {
          //
          // Batch removal
          //
          gen_wrapper_templ <mt_br_stategroup, mt_br_stategroup, edge_2001_cmds>
          G(
            level_change, false, mp,
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new edge_2001_cmds(rgr->useMxdWrapper(), getBatchSize()),
            rss, rgr, 0
          );

          DoRG(hm, G);
          return;
      } else {
          //
          // Single removal
          //
          gen_wrapper_templ <mt_sr_stategroup, mt_sr_stategroup, edge_2001_cmds>
          G(
            level_change, false, mp,
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new edge_2001_cmds(rgr->useMxdWrapper(), getBatchSize()),
            rss, rgr, 0
          );

          DoRG(hm, G);
          return;
      }
    }
  }
#endif

  //
  // "normal" matrix
  //
  if (hm.GetProcess()) {
    //
    // Second pass
    //
    if (batch_removal) {
        //
        // Batch removal
        //
        gen_wrapper_templ <mt_known_stategroup, mt_br_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_known_stategroup(*rss, *mp),
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new edge_minterms(rgr->useMxdWrapper(), *mp, getBatchSize(), false),
          rss, rgr, 0
        );

        DoRG(hm, G);
        return;
    } else {
        //
        // Single removal
        //
        gen_wrapper_templ <mt_known_stategroup, mt_sr_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_known_stategroup(*rss, *mp),
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new edge_minterms(rgr->useMxdWrapper(), *mp, getBatchSize(), false),
          rss, rgr, 0
        );

        DoRG(hm, G);
        return;
    }
  } else {
    //
    // First pass
    //
    if (batch_removal) {
        //
        // Batch removal
        //
        gen_wrapper_templ <mt_br_stategroup, mt_br_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new edge_minterms(rgr->useMxdWrapper(), *mp, getBatchSize(), false),
          rss, rgr, 0
        );

        DoRG(hm, G);
        return;
    } else {
        //
        // Single removal
        //
        gen_wrapper_templ <mt_sr_stategroup, mt_sr_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new edge_minterms(rgr->useMxdWrapper(), *mp, getBatchSize(), false),
          rss, rgr, 0
        );

        DoRG(hm, G);
        return;
    }
  }
}

void meddly_explgen::generateMC(dsde_hlm &hm, meddly_reachset* rss)
{
  // Everybody uses this
  DCASSERT(rss);
  minterm_pool* mp = new minterm_pool(6+6*getBatchSize(), rss->getNumLevels());

  //
  // Storage for the reachgraph and process
  //
  meddly_monolithic_rg* rgr = new meddly_monolithic_rg(rss->shareVars(), rss->grabMxdWrapper());
  meddly_encoder* procmxd = rgr->newMxdWrapper("proc", MEDDLY::forest::REAL,
    useEVMXD() ? MEDDLY::forest::EVTIMES : MEDDLY::forest::MULTI_TERMINAL);
  meddly_process* proc = new meddly_process( procmxd );

  //
  // A little ugly - consider all possible cases "by hand"
  //

  //
  // CMDs
  //
#ifdef ENABLE_CMDS
  if (isCMD()) {
    if (hm.GetProcess()) {
      //
      // Second pass
      //
      if (batch_removal) {
          //
          // Batch removal
          //
          gen_wrapper_templ <mt_known_stategroup, mt_br_stategroup, real_2001_cmds>
          G(
            level_change, false, mp,
            new mt_known_stategroup(*rss, *mp),
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new real_2001_cmds(proc->useMxdWrapper(), getBatchSize()),
            rss, rgr, proc
          );

          DoMC(hm, G);
          return;
      } else {
          //
          // Single removal
          //
          gen_wrapper_templ <mt_known_stategroup, mt_sr_stategroup, real_2001_cmds>
          G(
            level_change, false, mp,
            new mt_known_stategroup(*rss, *mp),
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new real_2001_cmds(proc->useMxdWrapper(), getBatchSize()),
            rss, rgr, proc
          );

          DoMC(hm, G);
          return;
      }
    } else {
      //
      // First pass
      //
      if (batch_removal) {
          //
          // Batch removal
          //
          gen_wrapper_templ <mt_br_stategroup, mt_br_stategroup, real_2001_cmds>
          G(
            level_change, false, mp,
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
            new real_2001_cmds(proc->useMxdWrapper(), getBatchSize()),
            rss, rgr, proc
          );

          DoMC(hm, G);
          return;
      } else {
          //
          // Single removal
          //
          gen_wrapper_templ <mt_sr_stategroup, mt_sr_stategroup, real_2001_cmds>
          G(
            level_change, false, mp,
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new mt_sr_stategroup(*rss, *mp, getBatchSize()),
            new real_2001_cmds(proc->useMxdWrapper(), getBatchSize()),
            rss, rgr, proc
          );

          DoMC(hm, G);
          return;
      }
    }
  }
#endif

  //
  // "normal" matrix
  //
  if (hm.GetProcess()) {
    //
    // Second pass
    //
    if (batch_removal) {
        //
        // Batch removal
        //
        gen_wrapper_templ <mt_known_stategroup, mt_br_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_known_stategroup(*rss, *mp),
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new edge_minterms(proc->useMxdWrapper(), *mp, getBatchSize(), true),
          rss, rgr, proc
        );

        DoMC(hm, G);
        return;
    } else {
        //
        // Single removal
        //
        gen_wrapper_templ <mt_known_stategroup, mt_sr_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_known_stategroup(*rss, *mp),
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new edge_minterms(proc->useMxdWrapper(), *mp, getBatchSize(), true),
          rss, rgr, proc
        );

        DoMC(hm, G);
        return;
    }
  } else {
    //
    // First pass
    //
    if (batch_removal) {
        //
        // Batch removal
        //
        gen_wrapper_templ <mt_br_stategroup, mt_br_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new mt_br_stategroup(*rss, *mp, getBatchSize(), getMBR()),
          new edge_minterms(proc->useMxdWrapper(), *mp, getBatchSize(), true),
          rss, rgr, proc
        );

        DoMC(hm, G);
        return;
    } else {
        //
        // Single removal
        //
        gen_wrapper_templ <mt_sr_stategroup, mt_sr_stategroup, edge_minterms>
        G(
          level_change, false, mp,
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new mt_sr_stategroup(*rss, *mp, getBatchSize()),
          new edge_minterms(proc->useMxdWrapper(), *mp, getBatchSize(), true),
          rss, rgr, proc
        );

        DoMC(hm, G);
        return;
    }
  }

  throw Engine_Failed;
}



// ******************************************************************
// *                                                                *
// *                                                                *
// *                         Initialization                         *
// *                                                                *
// *                                                                *
// ******************************************************************

class init_explmeddly : public initializer {
  public:
    init_explmeddly();
    virtual bool execute();
};
init_explmeddly the_explmeddly_initializer;

init_explmeddly::init_explmeddly() : initializer("init_explmeddly")
{
  usesResource("em");
  usesResource("meddlyprocgen");
}

bool init_explmeddly::execute()
{
  if (0==em) return false;

  engine* expl_eng = RegisterEngine(em,
    "MeddlyProcessGeneration",
    "EXPLICIT",
    "Explicit generation using MDDs.  States are added to the MDD in batches to improve efficiency.",
    &the_meddly_explgen
  );
  option_manager* subopts = expl_eng->internalOpts();
  DCASSERT(subopts);


  /* Initialize batch size option */
  meddly_explgen::batch_size = 1024;
  subopts->addIntOption(
      "BatchAddSize",
      "Maximum batch size for adding states or edges",
      meddly_explgen::batch_size,
      1, 1000000
  );

  /* Initialize batch removal option */
  meddly_explgen::batch_removal = true;
  subopts->addBoolOption(
      "UseBatchRemoval",
      "Should unexplored states be processed in batch?  If false, then they are processed one at a time.",
      meddly_explgen::batch_removal
  );

  /* Initialize MaximizeBatchRefills option */
  meddly_explgen::maximize_batch_refills = false;
  subopts->addBoolOption(
      "MaximizeBatchRefills",
      "For batch removal of unexplored states, should we try to refill the batches as much as possible; otherwise, we take a more relaxed approach.",
      meddly_explgen::maximize_batch_refills
  );

  /* Initialize LevelChange option */
  meddly_explgen::level_change = 1000000;
  subopts->addIntOption(
      "LevelChange",
      "Force process edges to be accumulated whenever the source state changes at this level or above.  Use 0 for constant accumulations, #levels for no accumulations except at the end.  Notes: (1) we are still limited by the batch size; see option BatchAddSize. (2) this may not be supported for all variations of explicit Meddly generation.",
      meddly_explgen::level_change,
      0, 1000000
  );

  /* Initialize matrix style option */
  meddly_explgen::matrix_style = meddly_explgen::IRMXD;
  option* sty = subopts->addRadioOption(
      "MatrixStyle",
      "Data structure to use for matrix for underlying process",
      meddly_explgen::NUM_MX_STYLES, meddly_explgen::matrix_style
  );


#ifdef ENABLE_CMDS
  sty->addRadioButton(
    "CMD", "Canonical Matrix Diagram (from 2001 paper)", meddly_explgen::CMD
  );
#endif
  sty->addRadioButton(
    "IRMXD", "Identity-reduced matrix diagram", meddly_explgen::IRMXD
  );
  sty->addRadioButton(
    "QRMXD", "Quasi-reduced matrix diagram", meddly_explgen::QRMXD
  );

  return true;
}

