
// $Id$

#ifndef MODELS_H
#define MODELS_H

/** @name models.h
    @type File
    @args \ 

  Language support for models.
  Actual discrete-state models are defined in another module.

 */

#include "functions.h"
#include "stmts.h"

//@{
  

// ******************************************************************
// *                                                                *
// *                        model_var  class                        *
// *                                                                *
// ******************************************************************

/**
     Base class of variables (without "data") defined within a model.
*/
class model_var : public symbol {
public:
  /** Linkage between state and variables.
      If this variable requires state, then this is the index
      of the variable within a state array.
      Otherwise, stateindex should be negative (e.g., for a transition).
  */
  int state_index;

  /// Partition info (for structured approaches).
  int part_index;  
public:
  model_var(const char* fn, int line, type t, char* n);

  // stuff for derived classes ?
};

// ******************************************************************
// *                                                                *
// *                          model  class                          *
// *                                                                *
// ******************************************************************

/**   The base class of high-level formalisms (for language support).

      Specific formalisms should be derived from this class.

      Note that models are derived from functions so that we can
      use the same mechanism for parameters.
*/  

class model : public function {
private:
  result* current_params;
  result* last_params;
  inline void FreeLast() {
    for (int i=0; i<num_params; i++)
      DeleteResult(parameters[i]->Type(0), last_params[i]);
  }
  inline void FreeCurrent() {
    for (int i=0; i<num_params; i++)
      DeleteResult(parameters[i]->Type(0), current_params[i]);
  }
  bool SameParams();
protected:
  statement **stmt_block;
  int num_stmts;
  result last_build;

public:
  model(const char* fn, int line, type t, char* n,
  	formal_param **pl, int np);

  virtual ~model();

  // Required as a function...
  virtual void Compute(expr **, int np, result &x);
  virtual void Sample(Rng &, expr **, int np, result &x);


  /** Fill the body of the function.
      @param	b	Array of statements
      @param	n	Number of statements
  */
  void SetStatementBlock(statement **b, int n);
    
  /** Create a model variable of the specified type.
      Must be provided in derived classes.
  */
  virtual model_var* MakeModelVar(const char *fn, int l, type t, char* n) = 0;

  /** Prepare for instantiation.
      Provided in derived classes.  Called immediately before
      the model is constructed for specific parameters.
      Allows formalisms to initialize any structures needed.
  */
  virtual void InitModel() = 0;

  /** Complete instantiation.
      Provided in derived classes.  Called immediately after
      the model is constructed for specific parameters.
      Allows formalisms to perform any finalization necessary.

      @param	x	Result.  Should have error set if there was an error,
      			otherwise should simply contain a pointer to the model
			(i.e., x.other = this).
  */
  virtual void FinalizeModel(result &x) = 0;
  
};






// ******************************************************************
// *                                                                *
// *                                                                *
// *                          Global stuff                          *
// *                                                                *
// *                                                                *
// ******************************************************************

/** Make an expression to call a model measure.
    Passed parameters must match exactly in type.
    @param	m	The model to instantiate (if necessary)

    @param	p	The parameters to pass to the model.
    @param	np	Number of passed parameters.

    @param	fn	Filename we are declared in.
    @param	line	line number we are declared on.
 */
expr* MakeMeasureCall(model *m, expr **p, int np, const char *fn, int line);

// expr* MakeArrayMeasureCall(model *m, expr **p, int np, expr **i, int ni);

//@}

#endif
