
// $Id$

#include "operators.h"
//@Include: operators.h

/** @name operators.cc
    @type File
    @args \ 

   Implementation of operator classes.

 */

//@{


// ******************************************************************
// *                                                                *
// *                         bool_not class                         *
// *                                                                *
// ******************************************************************

/** Negation of a boolean expression.
 */
class bool_not : public expr {
  expr *opnd;
  public:
  bool_not(const char* fn, int line, expr *x) : expr (fn, line) {
    opnd = x;
  }
  virtual ~bool_not() {
    delete opnd;
  }
  virtual expr* Copy() const { 
    return new bool_not(Filename(), Linenumber(), CopyExpr(opnd));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "!(" << opnd << ")";
  }
};

void bool_not::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  if (opnd) opnd->Compute(0, x); else x.null = true;

  // Trace errors?
  if (x.error) return;
  if (x.null) return;

  x.bvalue = !x.bvalue;
}

// ******************************************************************
// *                                                                *
// *                         bool_or  class                         *
// *                                                                *
// ******************************************************************

/** Or of two boolean expressions.
 */
class bool_or : public expr {
  expr *left;
  expr *right;
  public:
  bool_or(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~bool_or() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new bool_or(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " | " << right << ")";
  }
};

void bool_or::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.bvalue = l.bvalue || r.bvalue;
}

// ******************************************************************
// *                                                                *
// *                         bool_and class                         *
// *                                                                *
// ******************************************************************

/** And of two boolean expressions.
 */
class bool_and : public expr {
  expr *left;
  expr *right;
  public:
  bool_and(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~bool_and() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new bool_and(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " & " << right << ")";
  }
};

void bool_and::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.bvalue = l.bvalue && r.bvalue;
}

// ******************************************************************
// *                                                                *
// *                         int_neg  class                         *
// *                                                                *
// ******************************************************************

/** Negation of an integer expression.
 */
class int_neg : public expr {
  expr *opnd;
  public:
  int_neg(const char* fn, int line, expr *x) : expr (fn, line) {
    opnd = x;
  }
  virtual ~int_neg() {
    delete opnd;
  }
  virtual expr* Copy() const { 
    return new int_neg(Filename(), Linenumber(), CopyExpr(opnd));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return INT;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "-(" << opnd << ")";
  }
};

void int_neg::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  if (opnd) opnd->Compute(0, x); else x.null = true;

  // Trace errors?
  if (x.error) return;
  if (x.null) return;

  x.ivalue = -x.ivalue;
}

// ******************************************************************
// *                                                                *
// *                         int_add  class                         *
// *                                                                *
// ******************************************************************

/** Addition of two integer expressions.
 */
class int_add : public expr {
  expr *left;
  expr *right;
  public:
  int_add(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_add() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_add(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return INT;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " + " << right << ")";
  }
};

void int_add::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.ivalue = l.ivalue + r.ivalue;
}

// ******************************************************************
// *                                                                *
// *                         int_sub  class                         *
// *                                                                *
// ******************************************************************

/** Subtraction of two integer expressions.
 */
class int_sub : public expr {
  expr *left;
  expr *right;
  public:
  int_sub(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_sub() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_sub(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return INT;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " - " << right << ")";
  }
};

void int_sub::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.ivalue = l.ivalue - r.ivalue;
}

// ******************************************************************
// *                                                                *
// *                         int_mult class                         *
// *                                                                *
// ******************************************************************

/** Multiplication of two integer expressions.
 */
class int_mult : public expr {
  expr *left;
  expr *right;
  public:
  int_mult(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_mult() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_mult(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return INT;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " * " << right << ")";
  }
};

void int_mult::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.ivalue = l.ivalue * r.ivalue;
}

// ******************************************************************
// *                                                                *
// *                         int_div  class                         *
// *                                                                *
// ******************************************************************

/** Division of two integer expressions.
 */
class int_div : public expr {
  expr *left;
  expr *right;
  public:
  int_div(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_div() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_div(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " / " << right << ")";
  }
};

void int_div::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.rvalue = l.ivalue;
  if (0==r.ivalue) {
    x.error = CE_ZeroDivide;
    // spew out some error message here
  } else {
    x.rvalue /= r.ivalue;
  }
}

// ******************************************************************
// *                                                                *
// *                         real_neg class                         *
// *                                                                *
// ******************************************************************

/** Negation of a real expression.
 */
class real_neg : public expr {
  expr *opnd;
  public:
  real_neg(const char* fn, int line, expr *x) : expr (fn, line) {
    opnd = x;
  }
  virtual ~real_neg() {
    delete opnd;
  }
  virtual expr* Copy() const { 
    return new real_neg(Filename(), Linenumber(), CopyExpr(opnd));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "-(" << opnd << ")";
  }
};

void real_neg::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  if (opnd) opnd->Compute(0, x); else x.null = true;

  // Trace errors?
  if (x.error) return;
  if (x.null) return;

  x.rvalue = -x.rvalue;
}

// ******************************************************************
// *                                                                *
// *                         real_add class                         *
// *                                                                *
// ******************************************************************

/** Addition of two real expressions.
 */
class real_add : public expr {
  expr *left;
  expr *right;
  public:
  real_add(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~real_add() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new real_add(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " + " << right << ")";
  }
};

void real_add::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.rvalue = l.rvalue + r.rvalue;
}

// ******************************************************************
// *                                                                *
// *                         real_sub class                         *
// *                                                                *
// ******************************************************************

/** Subtraction of two real expressions.
 */
class real_sub : public expr {
  expr *left;
  expr *right;
  public:
  real_sub(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~real_sub() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new real_sub(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " - " << right << ")";
  }
};

void real_sub::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.rvalue = l.rvalue - r.rvalue;
}

// ******************************************************************
// *                                                                *
// *                        real_mult  class                        *
// *                                                                *
// ******************************************************************

/** Multiplication of two real expressions.
 */
class real_mult : public expr {
  expr *left;
  expr *right;
  public:
  real_mult(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~real_mult() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new real_mult(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " * " << right << ")";
  }
};

void real_mult::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.rvalue = l.rvalue * r.rvalue;
}

// ******************************************************************
// *                                                                *
// *                         real_div class                         *
// *                                                                *
// ******************************************************************

/** Division of two real expressions.
 */
class real_div : public expr {
  expr *left;
  expr *right;
  public:
  real_div(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~real_div() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new real_div(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return REAL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " / " << right << ")";
  }
};

void real_div::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.rvalue = l.rvalue / r.rvalue;
}

// ******************************************************************
// *                                                                *
// *                        bool_equal class                        *
// *                                                                *
// ******************************************************************

/** Check equality of two boolean expressions.
 */
class bool_equal : public expr {
  expr *left;
  expr *right;
  public:
  bool_equal(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~bool_equal() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new bool_equal(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " == " << right << ")";
  }
};

void bool_equal::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.bvalue = (l.bvalue == r.bvalue);
}

// ******************************************************************
// *                                                                *
// *                         bool_neq class                         *
// *                                                                *
// ******************************************************************

/** Check inequality of two boolean expressions.
 */
class bool_neq : public expr {
  expr *left;
  expr *right;
  public:
  bool_neq(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~bool_neq() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new bool_neq(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " != " << right << ")";
  }
};

void bool_neq::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  x.bvalue = (l.bvalue != r.bvalue);
}

// ******************************************************************
// *                                                                *
// *                        int_equal  class                        *
// *                                                                *
// ******************************************************************

/** Check equality of two boolean expressions.
 */
class int_equal : public expr {
  expr *left;
  expr *right;
  public:
  int_equal(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_equal() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_equal(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " == " << right << ")";
  }
};

void int_equal::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  // check infinity
  x.bvalue = (l.ivalue == r.ivalue);
}

// ******************************************************************
// *                                                                *
// *                         int_neq class                         *
// *                                                                *
// ******************************************************************

/** Check inequality of two boolean expressions.
 */
class int_neq : public expr {
  expr *left;
  expr *right;
  public:
  int_neq(const char* fn, int line, expr *l, expr *r) : expr (fn, line) {
    left = l;
    right = r;
  }
  virtual ~int_neq() {
    delete left;
    delete right;
  }
  virtual expr* Copy() const { 
    return new int_neq(Filename(), Linenumber(), 
	               CopyExpr(left), CopyExpr(right));
  }
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x) const;
  virtual void show(ostream &s) const {
    s << "(" << left << " != " << right << ")";
  }
};

void int_neq::Compute(int i, result &x) const
{
  DCASSERT(0==i);
  result l;
  result r;
  if (left) left->Compute(0, l); else l.null = true;
  if (right) right->Compute(0, r); else r.null = true;

  if (l.error) {
    // some option about error tracing here, I guess...
    x.error = l.error;
    return;
  }
  if (r.error) {
    x.error = r.error;
    return;
  }
  if (l.null || r.null) {
    x.null = true;
    return;
  }
  // check infinity
  x.bvalue = (l.ivalue != r.ivalue);
}

// ******************************************************************
// *                                                                *
// *             Global functions  to build expressions             *
// *                                                                *
// ******************************************************************

expr* SimpleUnaryOp(int op, expr *opnd);
expr* SimpleBinaryOr(expr *left, int op, expr *right);

//@}

