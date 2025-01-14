
/** \file statevects.h

    Module for statevects.
    Defines the statedist, stateprobs, statemsrs types.
*/

#ifndef STATEPROBS_H
#define STATEPROBS_H

#include "../_GraphLib/graphlib.h"

class exprman;
class symbol_table;
class stochastic_lldsm;
class stateset;
struct LS_Vector;
class intset;

// ******************************************************************
// *                                                                *
// *                        statevect  class                        *
// *                                                                *
// ******************************************************************

/**
    A vector of reals, per state.
    For now - explicit storage only.
    Vectors are stored either in full or sparsely.
*/
class statevect : public shared_object {
protected:
  const stochastic_lldsm* parent;
  long* indexes;    // If null, then we're stored as truncated full
  double* vect;     // Entries
  long vectsize;    // Dimension of vect[] and indexes[] arrays.
public:
  /**
    Create a statevect by copying from an ordinary vector.
    Will be stored sparsely or truncated full (whichever is more compact).
      @param  p   Parent, can be 0 if we want to set it later
      @param  d   Truncated full array of doubles, which we copy from
      @param  N   size of d[] array.
  */
  statevect(const stochastic_lldsm* p, const double* d, long N);

  /**
    Create a statevect, assuming the user has built it correctly.
    I.e., either I is null and D is a full vector,
    or I is a list of indexes for elements in D (sparse).
  */
  statevect(const stochastic_lldsm* p, long* I, double* D, long N);

  /**
    Create a statevect from the given LS_Vector,
    but renumber the indexes.
    This may mean we want to change how the vector is stored.
      @param  p     Parent, can be 0 if we want to set it later
      @param  V     Vector; will be "ours" now
      @param  Ren   Renumbering map, or 0 to indicate no renumbering.
  */
  statevect(const stochastic_lldsm* p, LS_Vector &V,
    const GraphLib::node_renumberer* Ren);

  virtual ~statevect();

public:
  inline bool isSparse() const {
    return indexes;
  }
  inline double readFull(long i) const {
    DCASSERT(0==indexes);
    DCASSERT(vect);
    CHECK_RANGE(0, i, vectsize);
    return vect[i];
  }
  inline long readSparseIndex(long z) const {
    DCASSERT(indexes);
    CHECK_RANGE(0, z, vectsize);
    return indexes[z];
  }
  inline double readSparseValue(long z) const {
    DCASSERT(vect);
    CHECK_RANGE(0, z, vectsize);
    return vect[z];
  }
  inline long size() const {
    return vectsize;
  }

  inline const stochastic_lldsm* getParent() const { return parent; }

  inline void setParent(const stochastic_lldsm* p) {
    DCASSERT(0==parent);
    parent = p;
  }

  /**
      Export to a LS_Vector struct, for analysis.
        @param  V   Vector to write into
        @return     True on success, false on error.
  */
  bool ExportTo(LS_Vector &V) const;

  /**
      Export to an explicit, full vector of doubles.
        @param  d   Vector to write into, size must be at least as large
                    as the number of states in the parent model.
  */
  void ExportTo(double* d) const;

  /**
      Export to a sparse vector of doubles.
        @param  index   Vector of indexes, must be large enough.
                        Use countNNs() to determine this size.
        @param  value   Vector of values, same dimension as index[].
  */
  void ExportTo(long* index, double* value) const;

  /**
      Count the number of nonzero entries.
      Useful with ExportTo() above.
  */
  long countNNZs() const;

  // required for shared_object
  virtual bool Print(OutputStream &s, int width) const;
  virtual bool Equals(const shared_object *o) const;

  // for conversions to explicit statesets

  void greater_than(double v, intset* I) const;
  void less_than(double v, intset* I) const;
  void equals(double v, intset* I) const;

  // handy operation
  double dot_product(const statevect* x) const;

public:
  /// this[i] = (i in e) ? (sv[i]) : 0, for all i.
  void copyRestricted(const statevect* sv, const intset* e);
private:
  inline static bool storeAsTruncated(long tsize, long nnz) {
    return (tsize * sizeof(double) <= nnz * (sizeof(double)+sizeof(long)));
  }
private:
  static unsigned display_style;
  static const unsigned FULL = 0;
  static const unsigned SINDEX = 1;
  static const unsigned SSTATE = 2;
  friend class init_statevects;
  friend class statevect_printer;
};


// ******************************************************************
// *                                                                *
// *                        statedist  class                        *
// *                                                                *
// ******************************************************************

class statedist : public statevect {
  public:
    statedist(const stochastic_lldsm *p, const double *d, long N);
    statedist(const stochastic_lldsm* p, long* I, double* D, long N);
    statedist(const stochastic_lldsm* p, LS_Vector &V,
      const GraphLib::node_renumberer* Ren);

    /// Normalize vector if possible; returns original sum of elements.
    double normalize();
};


// ******************************************************************
// *                                                                *
// *                        stateprobs class                        *
// *                                                                *
// ******************************************************************

class stateprobs : public statevect {
  public:
    stateprobs(const stochastic_lldsm *p, const double *d, long N);
    stateprobs(const stochastic_lldsm* p, long* I, double* D, long N);
    stateprobs(const stochastic_lldsm* p, LS_Vector &V,
      const GraphLib::node_renumberer* Ren);

  // ANY difference?
};


// ******************************************************************
// *                                                                *
// *                        statemsrs  class                        *
// *                                                                *
// ******************************************************************

class statemsrs : public statevect {
  public:
    statemsrs(const stochastic_lldsm *p, const double *d, long N);
    statemsrs(const stochastic_lldsm* p, long* I, double* D, long N);
    statemsrs(const stochastic_lldsm* p, LS_Vector &V,
      const GraphLib::node_renumberer* Ren);

  // ANY difference?
};

#endif
