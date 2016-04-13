
// $Id$

#include "order_base.h"

#include "../ExprLib/startup.h"
#include "../ExprLib/engine.h"
#include "../Formlsms/dsde_hlm.h"

#include "../Options/options.h"

#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <queue>
#include <iostream>
#include <limits>
#include <stdint.h>

// **************************************************************************
// *                                                                        *
// *                        static_varorder  methods                        *
// *                                                                        *
// **************************************************************************

named_msg static_varorder::report;
named_msg static_varorder::debug;

static_varorder::static_varorder()
 : subengine()
{
}

static_varorder::~static_varorder()
{
}

// **************************************************************************
// *                                                                        *
// *                          user_varorder  class                          *
// *                                                                        *
// **************************************************************************

class user_varorder : public static_varorder {
  public:
    user_varorder();
    virtual ~user_varorder();

    virtual bool AppliesToModelType(hldsm::model_type mt) const;
    virtual void RunEngine(hldsm* m, result &);
};
user_varorder the_user_varorder;

// **************************************************************************
// *                         user_varorder  methods                         *
// **************************************************************************

user_varorder::user_varorder()
{
}

user_varorder::~user_varorder()
{
}

bool user_varorder::AppliesToModelType(hldsm::model_type mt) const
{
  return (hldsm::Asynch_Events == mt);
}

void user_varorder::RunEngine(hldsm* hm, result &)
{
  DCASSERT(hm);
  DCASSERT(AppliesToModelType(hm->Type()));

  if (hm->hasPartInfo()) return;  // already done!

  dsde_hlm* dm = dynamic_cast <dsde_hlm*> (hm);
  DCASSERT(dm);

  if (debug.startReport()) {
    debug.report() << "using user-defined variable order\n";
    debug.stopIO();
  }
  dm->useDefaultVarOrder();
}

// **************************************************************************
// *                                                                        *
// *                      heuristic_varorder class                          *
// *                                                                        *
// **************************************************************************

class heuristic_varorder : public user_varorder {
  public:
    heuristic_varorder(int heuristic, double factor);
    virtual ~heuristic_varorder();
    virtual void RunEngine(hldsm* m, result &);

  private:
    int heuristic;
    double factor;
};
heuristic_varorder the_force_000_varorder(0, 0.0);
heuristic_varorder the_force_025_varorder(0, 0.25);
heuristic_varorder the_force_050_varorder(0, 0.5);
heuristic_varorder the_force_075_varorder(0, 0.75);
heuristic_varorder the_force_100_varorder(0, 1.0);
heuristic_varorder the_noack_varorder(1, 0);

// ******************************************************************
// *                                                                *
// *                                                                *
// *                         Initialization                         *
// *                                                                *
// *                                                                *
// ******************************************************************

class init_static_varorder : public initializer {
  public:
    init_static_varorder();
    virtual bool execute();
};
init_static_varorder the_static_varorder_initializer;

init_static_varorder::init_static_varorder() : initializer("init_static_varorder")
{
  usesResource("em");
  buildsResource("varorders");
  buildsResource("engtypes");
}

bool init_static_varorder::execute()
{
  if (0==em)  return false;

  // Initialize options
  option* report = em->findOption("Report");
  option* debug = em->findOption("Debug");

  static_varorder::report.Initialize(report,
    "varorder",
    "When set, static variable ordering heuristic performance is reported.",
    false
  );

  static_varorder::debug.Initialize(debug,
    "varorder",
    "When set, static variable ordering heuristic details are displayed.",
    false
  );

  MakeEngineType(em,
      "VariableOrdering",
      "Algorithm to use to determine the (static) variable order for a high-level model",
      engtype::Model
  );
  
  RegisterEngine(em,
    "VariableOrdering",
    "USER_DEFINED",
    "Variable order is determined by calls to partition() in the model",
    &the_user_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "FORCE000",
    "Variable order is determined by calls to partition() in the model",
    &the_force_000_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "FORCE025",
    "Variable order is determined by calls to partition() in the model",
    &the_force_025_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "FORCE050",
    "Variable order is determined by calls to partition() in the model",
    &the_force_050_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "FORCE075",
    "Variable order is determined by calls to partition() in the model",
    &the_force_075_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "FORCE100",
    "Variable order is determined by calls to partition() in the model",
    &the_force_100_varorder
  );

  RegisterEngine(em,
    "VariableOrdering",
    "NOACK",
    "Variable order is determined by calls to partition() in the model",
    &the_noack_varorder
  );

  return true;
}



// ******************************************************************
// *                                                                *
// *                                                                *
// *                  Static Variable Ordering                      *
// *                                                                *
// *                                                                *
// ******************************************************************


typedef uint_fast64_t u64;

struct DoubleInt {
	double theDouble;
	int theInt;
};

struct OrderPair {
	int name;
	int item;
};

struct ARC {
	int source;
	int target;
	int cardinality;
};

struct MODEL {
	int numPlaces;
	int numTrans;
	int numArcs;
	int * placeInits;
	ARC * theArcs;
};

std::vector<OrderPair> getBFSx(MODEL theModel, int start);
double getSpanTopParam(MODEL theModel, std::vector<OrderPair> theOrder,
    double param);
std::vector<int> generateForceOrder(MODEL theModel, int numIter,
    std::vector<OrderPair>& startOrder, double param);

// sort by
bool comparePair(DoubleInt a, DoubleInt b) {
	return (a.theDouble < b.theDouble);
}

// sort by
bool comparePairName(OrderPair a, OrderPair b) {
	return (a.name < b.name);
}

// sort by
bool comparePairItem(OrderPair a, OrderPair b) {
	return (a.item < b.item);
}


// check if a set contains an item
// not thread safe
bool setContains(std::set<int> theSet, int element) {
	std::set<int>::iterator isUsed = theSet.find(element);
	return (isUsed != theSet.end());
} 


// check if a map contains a given key
// not thread safe
bool mapContainsKey(std::map<int, double> theMap, int key) {
	std::map<int, double>::iterator isUsed = theMap.find(key);
	return (isUsed != theMap.end());
} 


// used by Noack
std::map<int, std::set<int> > getDotP(MODEL theModel) {
	std::map<int, std::set<int> > result;
	
	for (int p = 0; p < theModel.numPlaces; p++) {
		std::set<int> temp;
		result[p] = temp;
	}
	
	for (int a = 0; a < theModel.numArcs; a++) {
		if (theModel.theArcs[a].source < theModel.numPlaces) {
			// source is a place
		} else {
			// target is the place
			result[theModel.theArcs[a].target].insert(theModel.theArcs[a].source);
		}
	}
	
	return result;
}


// used by Noack
std::map<int, std::set<int> > getPDot(MODEL theModel) {
	std::map<int, std::set<int> > result;
	
	for (int p = 0; p < theModel.numPlaces; p++) {
		std::set<int> temp;
		result[p] = temp;
	}
	
	for (int a = 0; a < theModel.numArcs; a++) {
		if (theModel.theArcs[a].source < theModel.numPlaces) {
			// source is a place
			result[theModel.theArcs[a].source].insert(theModel.theArcs[a].target);
		} else {
			// target is the place
		}
	}
	
	return result;
}


// used by Noack
std::vector<int> getpUp(std::map<int, std::set<int> > dotp,
    std::map<int, std::set<int> > pdot, MODEL theModel) {
	std::vector<int> result;
	
	for (int p = 0; p < theModel.numPlaces; p++) {
		std::set<int> temp (dotp[p]);
    std::set<int>& pp = pdot[p];
    temp.insert(pp.begin(), pp.end());
		//for (auto pp : pdot[p]) { temp.insert(pp); }
		result.push_back(temp.size());
	}
	
	return result;
}


// used by Noack
std::map<int, std::set<int> > getDotT(MODEL theModel) {
	std::map<int, std::set<int> > result;
	
	for (int p = 0; p < theModel.numTrans; p++) {
		std::set<int> temp;
		result[p + theModel.numPlaces] = temp;
	}
	
	for (int a = 0; a < theModel.numArcs; a++) {
		if (theModel.theArcs[a].source < theModel.numPlaces) {
			// source is a place
			result[theModel.theArcs[a].target].insert(theModel.theArcs[a].source);
		} else {
			// target is the place
		}
	}
	
	return result;
}


// used by Noack
std::map<int, std::set<int> > getTDot(MODEL theModel) {
	std::map<int, std::set<int> > result;
	
	for (int p = 0; p < theModel.numTrans; p++) {
		std::set<int> temp;
		result[p + theModel.numPlaces] = temp;
	}
	
	for (int a = 0; a < theModel.numArcs; a++) {
		if (theModel.theArcs[a].source < theModel.numPlaces) {
			// source is a place
		} else {
			// target is the place
			result[theModel.theArcs[a].source].insert(theModel.theArcs[a].target);
		}
	}
	
	return result;
}

// generate an order using the Noack method with given parameters
// OrderPair format in case this ordering is used as input to force|other algorithms
std::vector<OrderPair> noackParam(MODEL theModel, double param) {
	std::vector<OrderPair> resultOrder;
	const double PARAM_W = param;
	// get the model information organized
	std::map<int, std::set<int> > dotPs = getDotP(theModel);
	std::map<int, std::set<int> > pDots = getPDot(theModel);
	std::map<int, std::set<int> > dotTs = getDotT(theModel);
	std::map<int, std::set<int> > tDots = getTDot(theModel);
	std::vector<int> pUp = getpUp(dotPs, pDots, theModel);
	
	double bestStart = 0.0;
	int start = 0;
	std::set<int> unused;
	for (int p = 0; p < theModel.numPlaces; p++) {
    double current = 0.0;
    unused.insert(p);
    //for (auto t : dotPs[p]) current += dotTs[t].size();
    for (unsigned t = dotPs[p].size()-1; t >= 0; t--)
      current += dotTs[t].size();

    //for (auto t : pDots[p]) current += PARAM_W * (tDots[t].size());
    for (unsigned t = pDots[p].size()-1; t >= 0; t--)
      current += PARAM_W * (tDots[t].size());

    if (pUp[p] > 0) {
      current /= (double)pUp[p];
      if (current > bestStart) {
        start = p;
        bestStart = current;
      }
    }
	}
	
	std::map<int, std::map<int, double> > adders;
	//for (auto p : unused) {
  for (std::set<int>::iterator p = unused.begin(); p != unused.end(); p++) {
		std::map<int, double> totals;
		//for (auto t : dotPs[p]) {
    for (std::set<int>::iterator t = dotPs[*p].begin(); t != dotPs[*p].end(); t++) {
			double fraction = 1.0 / dotTs[*t].size();
			// for (auto pp : dotTs[t]) {
      for (std::set<int>::iterator pp = dotTs[*t].begin(); pp != dotTs[*t].end(); pp++) {
				if (!mapContainsKey(totals, *pp)) {
					totals[*pp] = fraction;
				} else {
					totals[*pp] += fraction;
				}
			}
		}
		// for (auto t : pDots[p]) {
    for (std::set<int>::iterator t = pDots[*p].begin(); t != pDots[*p].end(); t++) {
			double fraction = PARAM_W / dotTs[*t].size();
			// for (auto pp : tDots[t]) {
      for (std::set<int>::iterator pp = tDots[*t].begin(); pp != tDots[*t].end(); pp++) {
				if (!mapContainsKey(totals, *pp)) {
					totals[*pp] = fraction;
				} else {
					totals[*pp] += fraction;
				}
			}
		}
		adders[*p] = totals;
	}
	
	double * currentWeights = new double[theModel.numPlaces];
	int current = start;
	int count = 0;
	OrderPair op;
	op.name = count;
	op.item = start;
	resultOrder.push_back(op);
	count++;
	
	do {
		// keep track of best weight so far
		double maxWeight = -1.0;
		int best = -1;
		unused.erase(current);
		//for (auto p : unused) {
    for (std::set<int>::iterator p = unused.begin(); p!= unused.end(); p++) {
			double toAdd = 0.0;
			// auto check = adders.find(p);
      std::map<int, std::map<int, double> >::iterator check = adders.find(*p);
			if (check != adders.end()) {
				// auto check2 = adders[p].find(current);
        std::map<int, double>::iterator check2 = adders[*p].find(current);
				if (check2 != adders[*p].end()) toAdd = adders[*p][current] / pUp[*p];
			}
			currentWeights[*p] += toAdd;
			if (currentWeights[*p] > maxWeight) {
				best = *p;
				maxWeight = currentWeights[*p];
			}
		}
		current = best;
		OrderPair theNext;
		theNext.name = count;
		theNext.item = best;
		count++;
		resultOrder.push_back(theNext);
	} while (unused.size() > 1);
	
	delete [] currentWeights;
	return resultOrder;
}




// generate an order using the Noack method with default parameters
// OrderPair format in case this ordering is used as input to force|other algorithms
std::vector<OrderPair> noack(MODEL theModel) {
  printf("In %s\n", __func__);
	return noackParam(theModel, 2.0);
}



// generate an int std::vector order from a pair-style
std::vector<int> pairToInt(std::vector<OrderPair> paired) {
	std::vector<int> result (paired.size(), 0);
	//for (auto a : paired) {
  for (std::vector<OrderPair>::iterator a = paired.begin();
      a != paired.end(); a++) {
		result[a->name] = a->item;
	}
	return result;
}


// generate an int std::vector order from a pair-style
std::vector<OrderPair> intToPair(std::vector<int> asInt) {
	OrderPair fill;
	std::vector<OrderPair> result (asInt.size(), fill);
	for (int index = 0; index < int(asInt.size()); index++) {
		OrderPair temp;
		temp.name = index;
		temp.item = asInt[index];
		result[index] = temp;
	}
	return result;
}

std::vector<int> defaultOrder(MODEL theModel, double paramAlpha) {
	// paramAlpha, default 0.5, 1.0 is all spans, 0.0 is all tops
	
	OrderPair fill;
	std::vector<OrderPair> best (theModel.numPlaces, fill);
	// start with best as the given order until found otherwise
	for (int index = 0; index < theModel.numPlaces; index++) {
		OrderPair temp;
		temp.name = index;
		temp.item = index;
		best[index] = temp;	
	}
	// the score to beat
	double bestScore = getSpanTopParam(theModel, best, paramAlpha);
  double newScore  = 0.0;
	
	// reverse of the previous order
#if 0
	std::vector<OrderPair> reverse (theModel.numPlaces, fill);
	for (int index = 0; index < theModel.numPlaces; index++) {
		OrderPair temp;
		temp.name = theModel.numPlaces - index - 1;
		temp.item = index;
		reverse[index] = temp;	
	}
	newScore = getSpanTopParam(theModel, reverse, paramAlpha);
	if (newScore < bestScore) {
		bestScore = newScore;
		for (int index = 0; index < theModel.numPlaces; index++) {
			best[index] = reverse[index];
		}
	}
#endif
	
	int maxIter = 100;
	std::vector<OrderPair> startOrder (best);	// try force on the given order
	std::vector<OrderPair> forceGiven = intToPair(generateForceOrder(theModel,
        maxIter, startOrder, paramAlpha));
	
	newScore = getSpanTopParam(theModel, forceGiven, paramAlpha);
	if (newScore < bestScore) {
		bestScore = newScore;
		for (int index = 0; index < theModel.numPlaces; index++) {
			best[index] = forceGiven[index];
		}
	}
	
	// try force on a number of different BFS orders
	int bfsIters = 10;
	for (int iter = 0; iter < bfsIters; iter++) {
		std::vector<OrderPair> startOrderBFS = getBFSx(theModel, iter);
		std::vector<OrderPair> forceBFS = intToPair(generateForceOrder(theModel,
          maxIter, startOrderBFS, paramAlpha));
		
		newScore = getSpanTopParam(theModel, forceBFS, paramAlpha);
		if (newScore < bestScore) {
			bestScore = newScore;
			for (int index = 0; index < theModel.numPlaces; index++) {
				best[index] = forceBFS[index];
			}
		}
	}
	// try a number of random orders for FORCE here:

#if 0
  // try noack
  std::vector<OrderPair> noackGiven = noack(theModel); // noack with default params
  newScore = getSpanTopParam(theModel, noackGiven, paramAlpha);
  if (newScore < bestScore) {
    bestScore = newScore;
    for (int index = 0; index < theModel.numPlaces; index++) {
      best[index] = noackGiven[index];
    }
  }
#endif
  

	return pairToInt(best);
}


// generate a breadth first order from the given model,
// starting with variable "start"
std::vector<OrderPair> getBFSx(MODEL theModel, int start) {

  std::vector<OrderPair> result;

  std::set<int> used;
  std::set<int>::iterator isUsed;

  std::map<int, std::set<int> > connections;
  std::map<int, std::set<int> >::iterator found;

  for (int a = 0; a < theModel.numArcs; a++) {
    int source = theModel.theArcs[a].source;
    int target = theModel.theArcs[a].target;
    found = connections.find(source);
    if (found == connections.end()) {
      // not there yet
      std::set<int> temp;
      temp.insert(target);
      connections[source] = temp;
    } else {
      // set already present
      connections[source].insert(target);
    }
  }

  for (int p = 0; p < theModel.numPlaces; p++) {
    int s = ((p + start) % theModel.numPlaces);
    std::queue<int> theQueue;
    isUsed = used.find(s);
    if (isUsed == used.end()) {
      OrderPair opTemp;
      opTemp.name = result.size();
      opTemp.item = s;
      result.push_back(opTemp);
      used.insert(s);
      theQueue.push(s);
      while (!theQueue.empty()) {
        int c = theQueue.front();
        theQueue.pop();
        found = connections.find(c);
        if (found != connections.end()) {
          // for (int t : connections[c]) {
          for (std::set<int>::iterator t = connections[c].begin(); t != connections[c].end(); t++) {
            std::map<int, std::set<int> >::iterator foundI = connections.find(*t);
            if (foundI != connections.end()) {
              // for (int n : connections[*t]) {
              for (std::set<int>::iterator n = connections[*t].begin(); n != connections[*t].end(); n++) {
                isUsed = used.find(*n);
                if (isUsed == used.end()) {
                  OrderPair opTemp2;
                  opTemp2.name = result.size();
                  opTemp2.item = *n;
                  result.push_back(opTemp2);
                  theQueue.push(*n);
                  used.insert(*n);
                }
              }
            }
          }
        }
      }
    }
  }

  return result;
}



// generate a breadth-first order from the given model
std::vector<OrderPair> getBFS(MODEL theModel) {
	return getBFSx(theModel, 0);
}




// output an orderpair as an EDN formatted map
void outputEDN_orderMap(std::vector<OrderPair> theOrder) {
	std::cout << "{";
	// for (OrderPair op : theOrder) {
  for (std::vector<OrderPair>::iterator op = theOrder.begin(); op != theOrder.end(); op++) {
		std::cout << op->name << " " << op->item << ", ";
	}
	std::cout << "}" << std::endl;
}

// output an order std::vector as an EDN formatted std::vector
void outputEDN_orderVec(std::vector<int> theOrder) {
	std::cout << "[";
	//for (auto a : theOrder) {
  for (std::vector<int>::iterator a = theOrder.begin(); a != theOrder.end(); a++) {
		std::cout << *a << " ";
	}
	std::cout << "]" << std::endl;
}


// calculate the center of gravity and apply FORCE
// return a hash value for the new order
u64 cogDiff(MODEL theModel, std::vector<OrderPair>& theOrder) {
	// find the center of gravity for every variable in the model
	int * counts = new int[theModel.numTrans + theModel.numPlaces];
	DoubleInt * totals = new DoubleInt[theModel.numTrans + theModel.numPlaces];
	for (int index = 0; index < (theModel.numTrans + theModel.numPlaces); index++) {
		if (index < theModel.numPlaces) {
			counts[index] = 1;//0;
			totals[index].theDouble = 0.0;//(double)theModel.numPlaces;//0.0;
		} else {
			counts[index] = 0;
			totals[index].theDouble = 0.0;
		}
		totals[index].theInt = index;	// "names" 0..numPlaces - 1 
	}	
	
	// for every arc, update the cog value
	for (int index = 0; index < theModel.numArcs; index++) {
		int source = theModel.theArcs[index].source;
		int target = theModel.theArcs[index].target;
		if (source < target) {
			// source is a place
			int currentPlace = theOrder[source].item;
			counts[target]++;
			totals[target].theDouble += (double)currentPlace;
		} else {
			// source is the transition
			int currentPlace = theOrder[target].item;
			counts[source]++;
			totals[source].theDouble += (double)currentPlace;
		}
	}
	// the cog value is the average (mean)
	for (int index = 0; index < theModel.numArcs; index++) {
		int source = theModel.theArcs[index].source;
		int target = theModel.theArcs[index].target;
		if (source < target) {
			counts[source] += counts[target];
			totals[source].theDouble += totals[target].theDouble;
		} else {
			counts[target] += counts[source];
			totals[target].theDouble += totals[source].theDouble;
		}
	}
	for (int index = 0; index < theModel.numPlaces; index++) {
		if (counts[index] != 0) {
			totals[index].theDouble /= (double)counts[index];
		} else {
			totals[index].theDouble = (double)totals[index].theInt;
		}
		totals[index].theInt = index;
	}	
	
	// sort by the cog values
	std::vector<DoubleInt> toBeSorted;
	toBeSorted.assign(totals, totals + theModel.numPlaces);
	stable_sort(toBeSorted.begin(), toBeSorted.end(), comparePair);
	
	// update the order, and calc a quick hash (FNV1a - modified)
	u64 base = 109951168211;//theOrder.size();
	u64 result = 14695981039346656037ULL;
	for (int index = 0; index < int(theOrder.size()); index++) {
		theOrder[index].name = index;
		theOrder[index].item = toBeSorted[index].theInt;
		result ^= theOrder[index].item;
		result *= base;
	}
	
	delete [] totals;
	delete [] counts;
	
	return result;
}


// calculate the combined span|top heuristic for a given ordering
double getSpanTopParam(MODEL theModel, std::vector<OrderPair> theOrder,
    double param) {
	int * eventMax = new int[theModel.numTrans + theModel.numPlaces];
	int * eventMin = new int[theModel.numTrans + theModel.numPlaces];
	for (int index = 0; index < theModel.numTrans + theModel.numPlaces; index++) {
		eventMax[index] = 0;
		eventMin[index] = theModel.numPlaces;
	}	
	u64 tops = 0LL;
	u64 spans = 0LL;
	for (int index = 0; index < theModel.numArcs; index++) {
		int source = theModel.theArcs[index].source;
		int target = theModel.theArcs[index].target;
		if (source < target) {
			int currentPlace = theOrder[source].item;
			if (eventMax[target] < currentPlace) {
				tops += currentPlace - eventMax[target];
				spans += currentPlace - eventMax[target];
				eventMax[target] = currentPlace;
			}
			if (eventMin[target] > currentPlace) {
				spans += eventMin[target] - currentPlace;
				eventMin[target] = currentPlace;
			}
		} else {
			int currentPlace = theOrder[target].item;
			if (eventMax[source] < currentPlace) {
				tops += currentPlace - eventMax[source];
				spans += currentPlace - eventMax[source];
				eventMax[source] = currentPlace;
			}
			if (eventMin[source] > currentPlace) {
				spans += eventMin[source] - currentPlace;
				eventMin[source] = currentPlace;
			}
		}
	}
	delete [] eventMax;
	delete [] eventMin;
	double result = param * (double)spans + (1.0 - param) * (double)tops;
	return result;
}



// generate an order using the parameterized FORCE method
// param should be between 0.0 and 1.0 inclusive
std::vector<int> generateForceOrder(MODEL theModel, int numIter,
    std::vector<OrderPair>& startOrder, double param) {
  printf("In %s\n", __func__);
	std::vector<OrderPair> current (startOrder);
	std::vector<int> resultOrder (theModel.numPlaces, 0);
	const int CYCLE = 5;
	double bestScore = std::numeric_limits<double>::max();
	
	u64 * cycleCheck = new u64[CYCLE];
	for (int index = 0; index < CYCLE; index++) {
		cycleCheck[index] = 0;
	}
	
	int iter = 0;	// in case # iterations wanted after a cycle breaks
	for (iter = 0; iter < numIter; iter++) {
		u64 theNew = cogDiff(theModel, current);
		int hash = theNew % CYCLE;
		if (theNew == cycleCheck[hash]) break;
		cycleCheck[hash] = theNew;
		
		double score = getSpanTopParam(theModel, current, param);
		if (score < bestScore) {
			bestScore = score;
			for (int index = 0; index < theModel.numPlaces; index++) {
				resultOrder[index] = current[index].item;
			}
		}
	}
	
	delete [] cycleCheck;
	
	return resultOrder;
}


// given a model, return an order using the given parameters
// heuristics: {0: FORCE, 1: NOACK}
std::vector<int> primaryOrder(MODEL theModel, int heuristic, double paramAlpha) {
	int maxIter = 10000;

  if (heuristic == 0) {
    std::vector<OrderPair> startOrder = getBFS(theModel);
    return generateForceOrder(theModel, maxIter, startOrder, paramAlpha);
  }
  return pairToInt(noack(theModel));	// noack with default params
}

MODEL translateModel(dsde_hlm& smartModel) {
	MODEL result;
	result.numPlaces = smartModel.getNumStateVars();
	result.numTrans = smartModel.getNumEvents();
	
	std::vector<ARC> vecArcs;
	
	for (int index = 0; index < result.numTrans; index++) {
		model_event * theEvent = smartModel.getEvent(index);
		int arcTarget = index + result.numPlaces;	// using target as the transition
		
		expr* enabling = theEvent->getEnabling();
		
    // Using the magic "List" from SMART with O(1) random access?
		List <symbol> L;
		enabling->BuildSymbolList(traverse_data::GetSymbols, 0, &L);
		for (int i = 0; i < L.Length(); i++) {
			symbol* s = L.Item(i);
			model_statevar* mv = dynamic_cast <model_statevar*> (s);
			if (0 != mv) {
				ARC tempArc;
				tempArc.target = arcTarget;
				tempArc.source = mv->GetIndex(); 
				vecArcs.push_back(tempArc);
			} 
		}
	}

	result.numArcs = vecArcs.size();
	result.theArcs = new ARC[vecArcs.size()];
	for (unsigned index = 0; index < vecArcs.size(); index++) {
		result.theArcs[index] = vecArcs[index];

    // printf("p: %d, t: %d\n", vecArcs[index].source, vecArcs[index].target);
	}

  // printf("In translateModel()\n");
	
	return result;
}


// **************************************************************************
// *                    heuristic_varorder  methods                         *
// **************************************************************************

heuristic_varorder::heuristic_varorder(int heuristic, double factor)
: heuristic(heuristic), factor(factor)
{
}

heuristic_varorder::~heuristic_varorder()
{
}

void heuristic_varorder::RunEngine(hldsm* hm, result &)
{
  DCASSERT(hm);
  DCASSERT(AppliesToModelType(hm->Type()));

  if (hm->hasPartInfo()) return;  // already done!

  dsde_hlm* dm = dynamic_cast <dsde_hlm*> (hm);
  DCASSERT(dm);

  if (debug.startReport()) {
    debug.report() << "using "
      << (heuristic == 0? "force": "noack")
      << "-defined variable order\n";

    debug.report() << "indexes: ";
    for (int i = 0; i < dm->getNumStateVars(); i++) {
      model_statevar* var = dm->getStateVar(i);
      debug.report() << "var: " << i << " index: " << var->GetIndex() << "\n";
    }

    debug.report() << "parts: ";
    for (int i = 0; i < dm->getNumStateVars(); i++) {
      model_statevar* var = dm->getStateVar(i);
      debug.report() << "var: " << i << " part: " << var->GetPart() << "\n";
    }
  }

  MODEL model = translateModel(*dm);
#if 0
  std::vector<int> order = primaryOrder(model, heuristic, factor);
#else
  std::vector<int> order = defaultOrder(model, factor);
#endif

  for (int j = 0; j < int(order.size()); j++) {
    dm->getStateVar(j)->SetPart(order[j]+1);
  }

  debug.report() << "Order: \n";
  for (int j = 0; j < int(order.size()); j++) {
    dm->getStateVar(j)->SetPart(order[j]+1);
    debug.report() << j << " " << order[j]+1 << " " << dm->getStateVar(j)->Name() << "\n";
  }
  debug.report() << "]\n";
  debug.stopIO();

  dm->useHeuristicVarOrder();
  // dm->reorderPartInfo();
}

