
// $Id$

/*  
    Replacement for linked lists of pointers: use arrays!
*/

#ifndef LIST_H
#define LIST_H

#include <string.h>

class PtrList {
  void** data;
  int size;
  int last;
protected:
  void Enlarge(int newsize) {
    void ** foo = (void**) realloc(data, newsize*sizeof(void*));
    if (NULL==foo) {
      cerr << "Memory overflow on PtrList resize\n";
      exit(0);
    }
    data = foo;
    size = newsize;
  }

public:
  PtrList(int inits) {
    data = (void**) malloc(inits * sizeof(void*));
    size = inits;
    last = 0;
  }
  ~PtrList() {
    free(data);
  }
  inline int Length() const { return last; }
  void** Copy() const {
    if (0==last) return NULL;
    void** thing = new void* [Length()];
    memcpy(thing, data, last * sizeof(void*));
    return thing;
  };

  inline void* Item(int n) const { return data[n]; }

  void Append(void* x) {
    if (last>=size) Enlarge(2*size);
    data[last] = x;
    last++;
  }

  void Append(PtrList *x) {
    if (x) {
      while (last + x->last >= size) size*=2;
      Enlarge(size);
      // risky....
      memcpy(data+last, x->data, x->last * sizeof(void*));
      last += x->last;
      delete x;
    }
  }

  void Clear() {
    last = 0;
  }
};

template <class DATA> 
class List {
  PtrList *p;
public:
  List(int size) { p = new PtrList(size); }  
  ~List() { delete p; }
  inline int Length() const { return p->Length(); }
  inline DATA** Copy() const { return static_cast<DATA**>(p->Copy()); }
  inline DATA* Item(int n) const { return static_cast<DATA*>(p->Item(n)); }
  inline void Append(DATA *x) { p->Append(x); }
  inline void Append(List <DATA> *x) { if (x) { p->Append(x->p); delete x; } }
  inline void Clear() { p->Clear(); }
};

template <class DATA>
inline List <DATA> *Append(List<DATA> *a, List<DATA> *b)
{
  if (NULL==a) return b;
  a->Append(b);
  return a;
}

#endif
