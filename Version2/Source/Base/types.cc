
// $Id$

#include "types.h"  
//@Include: types.h

/** @name types.cc
    @type File
    @args \ 

  Implementation of type stuff.

 */

//@{

// ********************************************************
// *                    Global  stuff                     *
// ********************************************************

const char* OneWordSimple[] = {
  "void",
  "bool",
  "int",
  "real",
  "string",
  "bigint",
  "stateset",
  "expo"
};

const char* Modifs[] = {
  "phase",
  "rand"
};

const char* NonProcNames[] = {
  "void",
  "bool",
  "int",
  "real",
  "string",
  "bigint",
  "stateset",
  "expo",
  "ph int",
  "ph real",
  "rand bool",
  "rand int",
  "rand real"
};

const char* ProcNames[] = {
  "proc bool",
  "proc int",
  "proc real",
  "proc expo",
  "proc ph int",
  "proc ph real",
  "proc rand bool",
  "proc rand int",
  "proc rand real"
};

const char* ModelNames[] = {
  "any model",
  "dtmc",
  "ctmc",
  "spn"
};

const char* VoidNames[] = {
  "state",
  "place",
  "trans"
};

const char* SetNames[] = {
  "{int}",
  "{real}"
};

const char* InternalNames[] = {
  "(internal) engine info",
  "(internal) expr*"
};

//
//  Code --> Type name conversion
//

const char* GetType(type t)
{
  if (t<=LAST_SIMPLE)
    return NonProcNames[t];

  if (t<=LAST_PROC)
    return ProcNames[t-FIRST_PROC];

  if (t<=LAST_MODEL)
    return ModelNames[t-FIRST_MODEL];

  if (t<=LAST_VOID)
    return VoidNames[t-FIRST_VOID];

  if (t<=LAST_SET)
    return SetNames[t-FIRST_SET];

  if (t<=LAST_INTERNAL)
    return InternalNames[t-FIRST_INTERNAL];

  // unknown type?
  
  return "Unknown type";
}

//
//  Type name --> integer code conversion
//

int FindSimpleType(const char *t)
{
  int i;
  for (i=0; i<=LAST_ONE_WORD; i++)
    if (0==strcmp(t,OneWordSimple[i])) return i;
  return -1;
} 

int FindModel(const char *t)
{
  int i;
  for (i=0; i<=LAST_MODEL-FIRST_MODEL; i++)
    if (0==strcmp(t,ModelNames[i])) return FIRST_MODEL + i;
  return -1;
} 

int FindVoidType(const char *t)
{
  int i;
  for (i=0; i<=LAST_VOID-FIRST_VOID; i++)
    if (0==strcmp(t,VoidNames[i])) return FIRST_VOID + i;
  return -1;
  
}

int FindModif(const char *m)
{
  int i;
  for (i=0; i<=LAST_MODIF; i++)
    if (0==strcmp(m,Modifs[i])) return i;
  return -1;
}


// *******************************************************************
// *                                                                 *
// *                  Compile-time  type promotions                  *
// *                                                                 *
// *******************************************************************

bool Promotable(unsigned char t1, unsigned char t2)
// Can type t1 be promoted to type t2?
{
  if (t1==t2) return true;
  if (t1==VOID) return false;
  if (t2==VOID) return false; // I think...

  // Handle "promotion" of specific models to generic models.
  if (IsModelType(t1) && (ANYMODEL == t2)) return true;

  if (t1>=FIRST_MODEL) return false;
  if (t2>=FIRST_MODEL) return false;

  switch (t1) {
    case BOOL		: return (RAND_BOOL==t2) 	|| 
			         (PROC_BOOL==t2) 	|| 
				 (PROC_RAND_BOOL==t2);

    case INT		: return (BIGINT==t2) 		||
			  	 (REAL==t2)		||
				 (PH_INT==t2)		||
				 (PH_REAL==t2)		|| // needed for 0
				 (RAND_REAL==t2)	||
				 (RAND_INT==t2)		||
				 (PROC_INT==t2)		||
				 (PROC_REAL==t2)	||
				 (PROC_PH_INT==t2)	||
				 (PROC_PH_REAL==t2)	||
				 (PROC_RAND_INT==t2)	||
				 (PROC_RAND_REAL==t2);
			  	 // whew!

    case REAL 		: return (RAND_REAL==t2)	||
			  	 (PROC_REAL==t2)	||
				 (PROC_RAND_REAL==t2);

    case EXPO		: return (PH_REAL==t2)		||
			  	 (RAND_REAL==t2)	||
				 (PROC_EXPO==t2)	||
				 (PROC_PH_REAL==t2)	||
				 (PROC_RAND_REAL==t2);

    case PH_INT		: return (RAND_INT==t2)		||
                             	 (RAND_REAL==t2)	||
			  	 (PROC_PH_INT==t2)	||
				 (PROC_RAND_INT==t2) 	||
				 (PROC_RAND_REAL==t2);

    case PH_REAL	: return (RAND_REAL==t2)	||
			  	 (PROC_PH_REAL==t2)	||
				 (PROC_RAND_REAL==t2);

    case RAND_BOOL	: return (PROC_RAND_BOOL==t2);

    case RAND_INT	: return (RAND_REAL==t2)	||
			  	 (PROC_RAND_INT==t2)    ||
				 (PROC_RAND_REAL==t2);

    case RAND_REAL	: return (PROC_RAND_REAL==t2);

    case PROC_BOOL	: return (PROC_RAND_BOOL==t2);

    case PROC_INT	: return (PROC_REAL==t2)	||
				 (PROC_PH_INT==t2)	||
				 (PROC_PH_REAL==t2)	||
				 (PROC_RAND_INT==t2)	||
				 (PROC_RAND_REAL==t2);

    case PROC_REAL	: return (PROC_RAND_REAL==t2);

    case PROC_EXPO	: return (PROC_PH_REAL==t2)	||
			  	 (PROC_RAND_REAL==t2);

    case PROC_PH_INT	: return (PROC_RAND_INT==t2) 	||
			  	 (PROC_RAND_REAL==t2);

    case PROC_PH_REAL	: return (PROC_RAND_REAL==t2);

    case PROC_RAND_INT	: return (PROC_RAND_REAL==t2);

    // internal stuff

    case SET_INT	: return (SET_REAL==t2);

    default: return false;  // no promotions
  };

  // shouldn't get here
  return false;
}


// *******************************************************************
// *                                                                 *
// *                    Compile-time type casting                    *
// *                                                                 *
// *******************************************************************

bool Castable(int t1, int t2)
// Can type t1 be cast to type t2?
{
  if (Promotable(t1,t2)) return true;
  if ((t1==REAL)&&(t2==INT)) return true;
  if ((t1==REAL)&&(t2==BIGINT)) return true;
  if ((t1==INT)&&(t2==BIGINT)) return true;
  if ((t1==BIGINT)&&(t2==INT)) return true;
  if (t2==EXPO)
    return (t1==BIGINT) || (t1==INT)||(t1==REAL);
  return false;
}


//@}
