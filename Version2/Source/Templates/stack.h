
// $Id$

/*
    New array-based stack templates.
*/

#ifndef STACK_H
#define STACK_H

#include <stdlib.h>
#include "../Base/errors.h"

template <class DATA>
class Stack {
  DATA *data;
  int size;
  int top;
  int maxsize;  
protected:
  void Enlarge(int newsize) {
    if (size == maxsize) {
      Internal.Start(__FILE__, __LINE__);
      Internal << "Stack overflow";
      Internal.Stop();
    }
    DATA *foo = (DATA *) realloc(data, newsize*sizeof(DATA));
    if (NULL==foo) OutOfMemoryError("stack resize");
    data = foo;
    size = newsize;
  }
public:
  Stack(int currentsize, int msize=1000000000) {
    // 1 billion max by default is effectively infinite
    size = 0;
    data = NULL;
    top = 0;
    maxsize = msize;
    Enlarge(currentsize);
  }
  ~Stack() { free(data); }
  inline bool Empty() const { return 0==top; }
  inline int NumEntries() const { return top; }
  inline void Clear() { top = 0; }
  inline void Push(const DATA &x) {
    if (top>=size) Enlarge(MIN(2*size, maxsize));
    data[top] = x;
    top++;
  }
  inline DATA Pop() {
    DCASSERT(top);
    top--;
    return data[top];
  }
  inline DATA Top() const {
    DCASSERT(top);
    return data[top-1];
  }
  // Not so efficient, but sometimes necessary...
  bool Contains(const DATA &key) const {
    for (int i=0; i<top; i++)
      if (data[i] == key) return true;
    return false;
  }
};

typedef Stack<void*> PtrStack;

#endif

