
#include "sccs.h"
#include "graphlib.h"

#include <stdlib.h>

// #define DEBUG_SCCS

#ifdef DEBUG_SCCS
#include <stdio.h>
#endif

// ==================================================================
// |                                                                |
// |                           Front  end                           |
// |                                                                |
// ==================================================================

GraphLib::abstract_classifier*
GraphLib::dynamic_graph::determineSCCs(long nonterminal, long sinks, 
  bool cons, timer_hook* sw) const
{
  //
  // Sanity checks
  //

  if (getNumNodes() < 1) return 0;

  //
  // Build scc map and auxiliary memory
  //
  long* sccmap;
  long* aux;

  sccmap = new long[getNumNodes()];
  aux = new long[getNumNodes()];

  for (long i=0; i<getNumNodes(); i++) {
    sccmap[i] = 0;
  }

  //
  // First and hardest step - determine "raw" SCCs
  //

  if (sw) sw->start("Finding SCCs");

  scc_traversal foo(*this, sccmap, aux);

  if (cons) {
    for (long i=0; i<getNumNodes(); i++) 
      if (0==sccmap[i]) foo.Visit1(i);
  } else {
    for (long i=0; i<getNumNodes(); i++) 
      if (0==sccmap[i]) foo.Visit2(i);
  }
  const long scc_count = foo.NumSCCs();

  if (sw) sw->stop();


  //
  // Handy constants to make code readable :^)
  //

  const long TSCC = -1;

  //
  // Now, use aux array to renumber and to
  // further classify the SCCs.
  // Initially we'll set everything to TSCC
  // until we learn otherwise.

  for (long i=0; i<scc_count; i++) aux[i] = TSCC;


  //
  // If we're merging nonterminals, then we need to determine
  // which SCCs are terminal and which are not.
  //

  if (nonterminal>=0 && scc_count>1) {
    if (sw) sw->start("Finding terminal SCCs");

    //
    // Figure out which SCCs can reach other SCCs.
    // Mark those as "nonterminal" in array aux.
    //
    for (long i=0; i<getNumNodes(); i++) {
      //
      // Get the class for state i
      //
      const long c = sccmap[i] - getNumNodes();
      DCASSERT(c>=0);
      DCASSERT(c<getNumNodes());

      // If we already know this class is non-terminal,
      // then we can skip this state
      if (nonterminal == aux[c]) continue;

      //
      // Loop over all outgoing edges from this state,
      // and check if we reach a state in a different SCC.
      // If so, mark this SCC as non-terminal.
      //
      long edge = row_pointer[i];
      if (edge<0) continue;   // no outgoing edges
      do {
        const long j = column_index[edge];
        if (sccmap[j] != sccmap[i]) {
          aux[c] = nonterminal;
          break;  // no point continuing in the loop
        }
        edge = next[edge];
      } while (edge != row_pointer[i]);
    } // for i

    if (sw) sw->stop();
  }

  //
  // If we're merging sink/absorbing states, then we need to
  // determine which states cannot reach other states.
  //

  if (sinks>=0) {
    if (sw) sw->start("Finding sink nodes");

    for (long i=0; i<getNumNodes(); i++) {
      //
      // Check outgoing edges for state i,
      // if we have an edge not to stae i
      // then this is not an absorbing state.
      //
      bool absorbing = true;

      long edge = row_pointer[i];
      if (edge>=0) do {
        if (column_index[edge] != i) {
          absorbing = false;
          break;
        }
        edge = next[edge];
      } while (edge != row_pointer[i]);

      //
      // If we're absorbing, mark the SCC as such
      //
      if (absorbing) {
        const long c = sccmap[i] - getNumNodes();
        DCASSERT(c>=0);
        DCASSERT(c<getNumNodes());
        aux[c] = sinks;
      }
    } // for i

    if (sw) sw->stop();
  }


  //
  // Compact the numbering of the SCCs.
  // We'll use array aux to remember
  // the new number for each class.
  //

  if (sw) sw->start("Renumbering SCCs");

  //
  // Initialize SCC number.
  // The three lines ensure that termcount skips
  // over the special values "nonterminal" and "sink".
  // It takes three lines because we don't know if
  // sinks < nonterminal or nonterminal < sinks.
  //
  long termcount = 0;
  if (nonterminal == termcount) termcount++;
  if (sinks == termcount) termcount++;
  if (nonterminal == termcount) termcount++;
  
  for (long i=0; i<getNumNodes(); i++) {
    const long c = sccmap[i] - getNumNodes();
    
    if (aux[c] != TSCC) {
      // i's class is already renumbered
      sccmap[i] = aux[c];
      continue;
    }

    //
    // Still here?  Need to renumber this SCC.
    //

    aux[c] = termcount;
    sccmap[i] = aux[c];

    //
    // Increment SCC number, skipping over special values
    //
    termcount++;
    if (nonterminal == termcount) termcount++;
    if (sinks == termcount) termcount++;
    if (nonterminal == termcount) termcount++;
  } // for state i

  //
  // Determine number of classes, including special ones
  // so that all classes are within the range 
  //      0, 1, 2, ..., numclasses-1
  //
  long num_classes = MAX(termcount, nonterminal+1);
  num_classes = MAX(num_classes, sinks+1);

  if (sw) sw->stop();

  //
  // Clean up and package up results
  //
  delete[] aux;
  return new general_classifier(sccmap, getNumNodes(), num_classes);
}


// ======================================================================
// |                                                                    |
// |                dynamic_graph::scc_traversal methods                |
// |                                                                    |
// ======================================================================

GraphLib::dynamic_graph::scc_traversal
  ::scc_traversal(const dynamic_graph &graph, long* sccs, long* vstack)
  : G(graph)
{
  if (!G.isByRows()) throw error(error::Format_Mismatch);

  scc_val = sccs;
  scc_count = G.getNumNodes();

  visit_stack = vstack;
  visit_stack_top = 0;
  visit_id = 0;

  call_stack = 0;
  call_stack_top = 0;
  call_stack_size = 0;
}

// ******************************************************************

GraphLib::dynamic_graph::scc_traversal::~scc_traversal()
{
  free(call_stack);

  // DON'T delete scc_val or visit_stack
}

// ******************************************************************

void GraphLib::dynamic_graph::scc_traversal::Visit3(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif

  long edge = G.row_pointer[i];
  VisitPush(i);
  long min = scc_val[i];
  while (1) {
    if (edge>=0) {
      // arc i-->j
      long j = G.column_index[edge];
      edge = G.next[edge];
      if (edge==G.row_pointer[i]) // we've cycled around
        edge = -1;  
      if (0==scc_val[j]) {
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        // "by hand" recursion
        CallPush(i, edge, min);
        VisitPush(j);
        i = j;
        edge = G.row_pointer[j];
        min = scc_val[j];
      } else {
        min = MIN(min, scc_val[j]);
      }
      continue;
    } // if edge>=0
    DCASSERT(edge<0);
    // All states reachable from i have been visited

    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } // if min

    long m;
    if (!CallPop(i, edge, m)) return;  // empty stack = done!
    min = MIN(min, m);
  } // while 1
}


// ******************************************************************

void GraphLib::dynamic_graph::scc_traversal::Visit2(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif

  long edge = G.row_pointer[i];
  VisitPush(i);
  while (1) {
    if (edge>=0) {
      // arc i-->j
      long j = G.column_index[edge];
      edge = G.next[edge];
      if (edge==G.row_pointer[i]) // we've cycled around
        edge = -1;  
      if (0==scc_val[j]) {
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        // "by hand" recursion
        CallPush(i, edge);
        VisitPush(j);
        i = j;
        edge = G.row_pointer[j];
      } 
      continue;
    } // if edge>=0
    DCASSERT(edge<0);
    // All states reachable from i have been visited
    // Now, compute the minimum value.
    long min = scc_val[i];
    edge = G.row_pointer[i];
    while (edge >= 0) {
      long j = G.column_index[edge];
      min = MIN(min, scc_val[j]);
      edge = G.next[edge];
      if (edge == G.row_pointer[i]) break;
    } 
    
    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } else {
      // not minimum, save it for later
      scc_val[i] = min;
    }

    if (!CallPop(i, edge)) return;  // empty stack = done!
  } // while 1
}


// ******************************************************************

void GraphLib::dynamic_graph::scc_traversal::Visit1(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif

  VisitPush(i);
  while (1) {
    // Try to get through all our children
    // optimistically compute minimum as we go
    long min = scc_val[i];
    long edge = G.row_pointer[i];
    while (edge >= 0) {
      long j = G.column_index[edge];
      min = MIN(min, scc_val[j]);
      if (0==min) {
        // we've never visited j, "by hand" recursion
        CallPush(i);  // out of memory
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        VisitPush(j);
        i = j;
        break;
      }
      edge = G.next[edge];
      if (edge == G.row_pointer[i]) break;
    } // while edge >= 0
    if (0==min) continue;  // start on one of our children
  
    // All states reachable from i have been visited
    // and the minimum value we've computed is valid.
    
    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } else {
      // not minimum, save it for later
      scc_val[i] = min;
    }

    if (!CallPop(i)) return;  // empty stack = done!
  } // while 1
}

// ******************************************************************

void GraphLib::dynamic_graph::scc_traversal::EnlargeCallStack(long newsize)
{
  DCASSERT(newsize > call_stack_size);
  long* foo = (long*) realloc(call_stack, newsize*sizeof(long));
  if (0==foo) throw error(error::Out_Of_Memory);
  call_stack = foo;
  call_stack_size = newsize;
}




// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// OLD INTERFACE BELOW, will eventually be discarded!
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================
// ==========================================================================================================================================================================


#ifdef ALLOW_OLD_GRAPH_INTERFACE

// ==================================================================
// |                         scc_data class                         |
// ==================================================================

class scc_data {
protected:
  const GraphLib::generic_graph* g;
  long scc_count;
  long* scc_val;
  long* visit_stack;
  long visit_stack_top;
  long visit_id;
  long* call_stack;
  long call_stack_top;
  long call_stack_size;
public:
  scc_data(const GraphLib::generic_graph* graph, long* sccs, long* vstack);
  ~scc_data();
   
  /* Stack-based (not recursive) SCC algorithm.
     Yanked from Sedgewick's Algorithms book (from undergrad)
     and skillfully rewritten to eliminate recursion.

     This version pushes state, edge, and minimum to the call stack.
  */
  void Visit3(long k);

  /* Stack-based (not recursive) SCC algorithm.
     Yanked from Sedgewick's Algorithms book (from undergrad)
     and skillfully rewritten to eliminate recursion.

     Modified slightly: we can compute the minimum after visiting
     all of our "children".  As such, this version pushes state
     and edge (only) to the call stack.
  */
  void Visit2(long k);

  /* Stack-based (not recursive) SCC algorithm.
     Yanked from Sedgewick's Algorithms book (from undergrad)
     and skillfully rewritten to eliminate recursion.

     Modified significantly, added time in favor of memory reduction: 
     We don't remember which children we've visited.   On "return" we have 
     to re-visit all children, but this is fast for children who are already 
     visited.  This version pushes only the state to the call stack.
  */
  void Visit1(long k);

  inline long NumSCCs() const { return scc_count - g->getNumNodes(); }

  inline long CallAlloc() const { return call_stack_size; }

protected:
  void EnlargeCallStack(long newsize);
  inline void VisitPush(long k) {
    DCASSERT(visit_stack_top < g->getNumNodes());
    visit_stack[visit_stack_top++] = k;
    visit_id++;
    scc_val[k] = visit_id;
  }
  inline long VisitPop() {
    DCASSERT(visit_stack_top>0);
    return visit_stack[--visit_stack_top];
  }

  // For 3 vars on call stack

  inline void CallPush(long state, long edge, long min) {
    if (call_stack_top+3>call_stack_size) {
      EnlargeCallStack(call_stack_size+1024);
    }
    call_stack[call_stack_top++] = state;
    call_stack[call_stack_top++] = edge;
    call_stack[call_stack_top++] = min;
  }
  inline bool CallPop(long& state, long& edge, long& min) {
    if (0==call_stack_top) return false;
    DCASSERT(call_stack_top>2);
    min = call_stack[--call_stack_top];
    edge = call_stack[--call_stack_top];
    state = call_stack[--call_stack_top];
    return true;
  }

  // For 2 vars on call stack

  inline void CallPush(long state, long edge) {
    if (call_stack_top+2>call_stack_size) {
      EnlargeCallStack(call_stack_size+1024);
    }
    call_stack[call_stack_top++] = state;
    call_stack[call_stack_top++] = edge;
  }
  inline bool CallPop(long& state, long& edge) {
    if (0==call_stack_top) return false;
    DCASSERT(call_stack_top>1);
    edge = call_stack[--call_stack_top];
    state = call_stack[--call_stack_top];
    return true;
  }

  // For 1 var on call stack!

  inline void CallPush(long state) {
    if (call_stack_top+1>call_stack_size) {
      EnlargeCallStack(call_stack_size+1024);
    }
    call_stack[call_stack_top++] = state;
  }
  inline bool CallPop(long& state) {
    if (0==call_stack_top) return false;
    state = call_stack[--call_stack_top];
    return true;
  }
};

// ==================================================================
// |                        scc_data methods                        |
// ==================================================================

scc_data::scc_data(const GraphLib::generic_graph* graph, long* sccs, long* vstack)
{
  DCASSERT(graph);
  g = graph;
  scc_val = sccs;
  scc_count = g->getNumNodes();

  visit_stack = vstack;
  visit_stack_top = 0;
  visit_id = 0;

  call_stack = 0;
  call_stack_top = 0;
  call_stack_size = 0;
}

scc_data::~scc_data()
{
  free(call_stack);
}

void scc_data::Visit3(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif
  DCASSERT(g);
  DCASSERT(!g->isFinished());
  DCASSERT(g->isByRows());

  long edge = g->RowPtr(i);
  VisitPush(i);
  long min = scc_val[i];
  while (1) {
    if (edge>=0) {
      // arc i-->j
      long j = g->ColIndx(edge);
      edge = g->Next(edge);
      if (edge==g->RowPtr(i)) // we've cycled around
        edge = -1;  
      if (0==scc_val[j]) {
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        // "by hand" recursion
        CallPush(i, edge, min);
        VisitPush(j);
        i = j;
        edge = g->RowPtr(j);
        min = scc_val[j];
      } else {
        min = MIN(min, scc_val[j]);
      }
      continue;
    } // if edge>=0
    DCASSERT(edge<0);
    // All states reachable from i have been visited

    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } // if min

    long m;
    if (!CallPop(i, edge, m)) return;  // empty stack = done!
    min = MIN(min, m);
  } // while 1
}




void scc_data::Visit2(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif
  DCASSERT(g);
  DCASSERT(!g->isFinished());
  DCASSERT(g->isByRows());

  long edge = g->RowPtr(i);
  VisitPush(i);
  while (1) {
    if (edge>=0) {
      // arc i-->j
      long j = g->ColIndx(edge);
      edge = g->Next(edge);
      if (edge==g->RowPtr(i)) // we've cycled around
        edge = -1;  
      if (0==scc_val[j]) {
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        // "by hand" recursion
        CallPush(i, edge);
        VisitPush(j);
        i = j;
        edge = g->RowPtr(j);
      } 
      continue;
    } // if edge>=0
    DCASSERT(edge<0);
    // All states reachable from i have been visited
    // Now, compute the minimum value.
    long min = scc_val[i];
    edge = g->RowPtr(i);
    while (edge >= 0) {
      long j = g->ColIndx(edge);
      min = MIN(min, scc_val[j]);
      edge = g->Next(edge);
      if (edge == g->RowPtr(i)) break;
    } 
    
    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } else {
      // not minimum, save it for later
      scc_val[i] = min;
    }

    if (!CallPop(i, edge)) return;  // empty stack = done!
  } // while 1
}




void scc_data::Visit1(long i)
{
#ifdef DEBUG_SCCS
  fprintf(stderr, "Visiting node %d\n", i);
#endif
  DCASSERT(g);
  DCASSERT(!g->isFinished());
  DCASSERT(g->isByRows());

  VisitPush(i);
  while (1) {
    // Try to get through all our children
    // optimistically compute minimum as we go
    long min = scc_val[i];
    long edge = g->RowPtr(i);
    while (edge >= 0) {
      long j = g->ColIndx(edge);
      min = MIN(min, scc_val[j]);
      if (0==min) {
        // we've never visited j, "by hand" recursion
        CallPush(i);  // out of memory
#ifdef DEBUG_SCCS
        fprintf(stderr, "Visiting node %d\n", j);
#endif
        VisitPush(j);
        i = j;
        break;
      }
      edge = g->Next(edge);
      if (edge == g->RowPtr(i)) break;
    } // while edge >= 0
    if (0==min) continue;  // start on one of our children
  
    // All states reachable from i have been visited
    // and the minimum value we've computed is valid.
    
    // Determine if i was first state entered in its scc.
    // This is true iff its value is less than all neighbors.

#ifdef DEBUG_SCCS
    fprintf(stderr, "Done visiting node %d; min=%d\n", i, min);
#endif

    // Is this node the minimum? 
    if (min == scc_val[i]) {
      // yes, pop until we get i, those states are a SCC
#ifdef DEBUG_SCCS
      fprintf(stderr, "SCC #%d contains:\t", scc_count-g->getNumNodes());
#endif
      long mbr;
      do {
        mbr = VisitPop();
        scc_val[mbr] = scc_count;
#ifdef DEBUG_SCCS
        fprintf(stderr, " %d", mbr);
#endif
      } while (mbr!=i);
      scc_count++;
#ifdef DEBUG_SCCS
      fprintf(stderr, "\n");
#endif
    } else {
      // not minimum, save it for later
      scc_val[i] = min;
    }

    if (!CallPop(i)) return;  // empty stack = done!
  } // while 1
}

void scc_data::EnlargeCallStack(long newsize)
{
  DCASSERT(newsize > call_stack_size);
  long* foo = (long*) realloc(call_stack, newsize*sizeof(long));
  if (0==foo) throw GraphLib::error(GraphLib::error::Out_Of_Memory);
  call_stack = foo;
  call_stack_size = newsize;
}

// ==================================================================
// |                        Helper functions                        |
// ==================================================================

void FwdArcsFindTerminal(const GraphLib::generic_graph *g, const long* sccmap, long* isterm)
{
  DCASSERT(g);
  DCASSERT(!g->isFinished());
  DCASSERT(g->isByRows());

  // For each state i
  for (long i=0; i<g->getNumNodes(); i++) {

    // Get the class for this state
    long c = sccmap[i] - g->getNumNodes();
    DCASSERT(c>=0);
    DCASSERT(c<g->getNumNodes());

    // If the class is not terminal, skip this state.
    if (0==isterm[c]) continue;

    // check all outgoing arcs from this state
    long edge = g->RowPtr(i);
    if (edge<0) continue;  // no outgoing arcs

    do {
      long j = g->ColIndx(edge);
      // there is an arc from i to j
      if (sccmap[j] != sccmap[i]) {
        // arc to another class, we cannot be a terminal scc
        isterm[c] = 0;
#ifdef DEBUG_SCCS
        fprintf(stderr, "SCC %d is not terminal due to arc from %d to %d\n", c, i, j);
#endif
        // no sense checking remaining arcs
        break;
      } // if
      edge = g->Next(edge);
    } while (edge!=g->RowPtr(i));
  } // for i
}

void BackArcsFindTerminal(const GraphLib::generic_graph *g, const long* sccmap, long* isterm)
{
  DCASSERT(g);
  DCASSERT(!g->isFinished());
  DCASSERT(!g->isByRows());

  // For each state j
  for (int j=0; j<g->getNumNodes(); j++) {

    // check all incoming arcs to this state
    long edge = g->RowPtr(j);
    if (edge<0) continue;  // no incoming arcs

    do {
      long i = g->ColIndx(edge);
      // there is an arc from i to j
      if (sccmap[j] != sccmap[i]) {
        // arc to another class, node i cannot be in a terminal scc
        long c = sccmap[i] - g->getNumNodes();
        DCASSERT(c>=0);
        DCASSERT(c<g->getNumNodes());
        isterm[c] = 0;
#ifdef DEBUG_SCCS
        fprintf(stderr, "SCC %d is not terminal due to arc from %d to $d\n", c, i, j);
#endif
      } // if
      edge = g->Next(edge);
    } while (edge!=g->RowPtr(j));
  } // for j
}

// ==================================================================
// |                           Front ends                           |
// ==================================================================

long  find_sccs(const GraphLib::generic_graph* g, bool cons, long* sccmap, long* aux)
{
  if (0==g) return 0;
  if (g->getNumNodes() < 1) return 0;
  if (g->isFinished()) 
    throw GraphLib::error(GraphLib::error::Finished_Mismatch);

  scc_data foo(g, sccmap, aux);
  for (long i=g->getNumNodes()-1; i>=0; i--)
    sccmap[i] = 0;

  if (cons) {
    for (long i=0; i<g->getNumNodes(); i++) 
      if (0==sccmap[i]) foo.Visit1(i);
  } else {
    for (long i=0; i<g->getNumNodes(); i++) 
      if (0==sccmap[i]) foo.Visit2(i);
  }

  return foo.NumSCCs();
}

void find_tsccs(const GraphLib::generic_graph* g, const long* sccmap, long* aux, long scc_count)
{
  if (scc_count < 0) return;

  DCASSERT(scc_count > 0);
  DCASSERT(scc_count <= g->getNumNodes());

  // figure out which sccs are terminal.  If there is an arc out of
  // one, then it is not terminal.
  // We'll store the results in the aux vector.
  // -1 means "terminal", 0 means the scc can reach another one.
  for (long i=0; i<scc_count; i++) aux[i] = -1;
  
  if (scc_count > 1) {
    if (g->isByRows()) {
      FwdArcsFindTerminal(g, sccmap, aux);
    } else {
      BackArcsFindTerminal(g, sccmap, aux);
    }
  }
}

long compact(const GraphLib::generic_graph* g, long* sccmap, long* aux, long scc_count)
{
  // sccs are numbered #states ... 2*#states-1 (at most)
  // renumber classes.  
  // first, compact the terminal sccs to have numbers 1,2,3,...
  // AND to be ordered by lowest numbered state in the SCC,
  // AND so that absorbing states are last.
  long termcount = 0;
  // First pass: renumber non-absorbing SCCs
  for (long i=0; i<g->getNumNodes(); i++) {
    long c = sccmap[i] - g->getNumNodes();
    if (aux[c]>=0) continue; // i's class already renumbered.
    if (g->RowPtr(i)<0) continue; // i is absorbing, renumber later
    termcount++;
    aux[c] = termcount;
  }
  // second pass: renumber absorbing SCCs
  for (long i=0; i<g->getNumNodes(); i++) if (g->RowPtr(i)<0) {
    long c = sccmap[i] - g->getNumNodes();
    termcount++;
    aux[c] = termcount;
  }
  // finally, renumber the scc mapping array.
  for (long i=0; i<g->getNumNodes(); i++) {
    long c = sccmap[i] - g->getNumNodes();
    DCASSERT(c>=0);
    DCASSERT(c < scc_count);
    sccmap[i] = aux[c];
  }
  return termcount;
}

#endif // ALLOW_OLD_GRAPH_INTERFACE

