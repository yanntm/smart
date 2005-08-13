
// $Id$

#ifndef TREES_H
#define TREES_H

#include "flatss.h"
#include "../Templates/splay.h"
#include "../Templates/stack.h"
#include "../Templates/bitvector.h"

/** @name trees.h
    @type File
    @args \
 
    Balanced binary trees of states, for dictionary operations.
    (search, add unique)

    Clever trick: we store left and right pointers in arrays,
    so the "node index" is the same as the "state index";
    this saves us a pointer per node
    (but requires us to use state indices, which speed things up anyway).

    IN FUTURE:
	make a similar "binary_forest" for multi-level
	but the above clever trick won't work...
*/

class binary_tree {
protected:
  /// Array of left "pointers"
  int* left;
  /// Array of right "pointers"
  int* right;
  /// Number of tree "nodes" allocated (dimension of left and right)
  int nodes_alloc;
  /// Number of nodes in the tree
  int nodes_used;
  /// Pointer to root node (for single tree version)
  int root;
  /// State data
  state_array *states; 
  /// Stack for searches and rotations
  Stack <int> *path;
public:
  binary_tree(state_array *s);
  ~binary_tree();  

  /// Put states (indices) into the array, in order.
  inline void FillOrderList(int* order) { 
    int i = 0; 
    FillOrderList(root, i, order);
  }

  /// Empty nodes from tree; don't deallocate memory.
  inline void Clear() {
    root = -1;
    nodes_used = 0;
  }
protected:
  inline int Pop() { return (path->Empty()) ? -1 : path->Pop(); }
  /// Traverse subtree, add to order array starting with "index".
  void FillOrderList(int tree, int &index, int* order);

  /** Traverse subtree searching for s.
      The path taken is saved by pushing onto a stack.
      @return	The compare result of the last item seen:
      		0, s was found 
		1, last item was larger than s 
		-1, last item was smaller than s
  */
  int FindPath(const state &s);
};

class splay_state_tree : public binary_tree {
  SplayTree <splay_state_tree> *stree; 
public:
  splay_state_tree(state_array *s);
  /** Insert this state into the tree.
      If a duplicate is already present, do not insert the state.
      @param 	s	The state to insert.

      @return	The handle (index) of the state.
  */
  ~splay_state_tree();
  int AddState(const state &s);

  /** Find this state in the tree.
      @param	s 	The state to find.
      @return	The handle (index) of the state, if found;
		otherwise, -1.
  */
  int FindState(const state &s);
  void Report(OutputStream &r);
  // For splay tree
  inline int Null() const { return -1; }
  inline int getLeft(int h) const { return left[h]; }
  inline void setLeft(int h, int n) { left[h] = n; }
  inline int getRight(int h) const { return right[h]; }
  inline void setRight(int h, int n) { right[h] = n; }
  inline int Compare(int h1, int h2) const { return states->Compare(h1, h2); }
protected:
  void Resize(int newsize); 
  // Copied & adapted from splay template
/*
  inline void SplayRotate(int C, int P, int GP) {
    // swap parent and child
    if (left[P] == C) {
      left[P] = right[C];
      right[C] = P;
    } else {
      DCASSERT(C==right[P]);
      right[P] = left[C];
      left[C] = P;
    } 
    if (GP>=0) {
      if (P==left[GP]) {
	left[GP] = C;
      } else {
	DCASSERT(P==right[GP]);
	right[GP] = C;
      }
    } // if gp
  }
  inline void SplayStep(int c, int p, int gp, int ggp) {
    if (gp<0) {
      SplayRotate(c, p, -1);
    } else {
      bool ParentIsRight = (right[gp] == p);
      bool ChildIsRight = (right[p] == c);
      if (ParentIsRight == ChildIsRight) {
	SplayRotate(p, gp, ggp);
	SplayRotate(c, p, ggp);
      } else {
	SplayRotate(c, p, gp);
	SplayRotate(c, gp, ggp);
      }
    }
  }
*/
  /**
      Perform a splay operation: find the desired node and move it to the top.
      @param	s	The state to find
      @return	The compare result of the root item:
      		0, s was found (and moved to root)
		1, an item larger than s was moved to root
		-1, an item smaller than s was moved to root
  */
  // int Splay(const state &s);
};


class red_black_tree : public binary_tree {
  bitvector* isRed; 
public:
  red_black_tree(state_array *s);
  ~red_black_tree();

  /** Insert this state into the tree.
      If a duplicate is already present, do not insert the state.
      @param 	s	The state to insert.

      @return	The handle (index) of the state.
  */
  int AddState(const state &s);

  /** Find this state in the tree.
      @param	s 	The state to find.
      @return	The handle (index) of the state, if found;
		otherwise, -1.
  */
  int FindState(const state &s);
  void Report(OutputStream &r);
protected:
  inline bool is4node(int p) const {
    if (left[p]<0 || right[p]<0) return false;  // not enough children
    return isRed->IsSet(left[p]) && isRed->IsSet(right[p]);
  }
  void Resize(int newsize); 
};


#endif
