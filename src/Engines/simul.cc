
#include "simul.h"

// External libs
#include "../_SimLib/sim.h"
#include "../_RngLib/rng.h"
#include "../_Timer/timerlib.h"

#include "../Streams/streams.h"

#include "../Options/options.h"
#include "../Options/optman.h"

#include "../ExprLib/startup.h"
#include "../ExprLib/exprman.h"
#include "../ExprLib/engine.h"

#include <climits>

// #define DEBUG

// **************************************************************************
// *                                                                        *
// *                             rng_lib  class                             *
// *                                                                        *
// **************************************************************************

class rng_lib : public library {
  const rng_manager* rngm;
public:
  rng_lib(const rng_manager* rm);
  virtual const char* getVersionString() const;
  virtual bool hasFixedPointer() const { return false; }
};

rng_lib::rng_lib(const rng_manager* rm) : library(false, false)
{
  rngm = rm;
}

const char* rng_lib::getVersionString() const
{
  if (rngm)  return rngm->GetVersion();
  return 0;
}

// **************************************************************************
// *                                                                        *
// *                            conf_intl  class                            *
// *                                                                        *
// **************************************************************************

class conf_intl : public shared_object {
  double half_width;
  float conf_level;
public:
  conf_intl(double hw, float cl);

  virtual bool Print(OutputStream &s, int prec) const;
  virtual bool Equals(const shared_object *o) const;
};

conf_intl::conf_intl(double hw, float cl) : shared_object()
{
  half_width = hw;
  conf_level = cl;
}

bool conf_intl::Print(OutputStream &s, int prec) const
{
  s << " +- ";
  s.Put(half_width, 0, prec);
  s << " (" << conf_level*100 << "%)";
  return true;
}

bool conf_intl::Equals(const shared_object* o) const
{
  return (o == this);
}

// **************************************************************************
// *                                                                        *
// *                            sim_engine class                            *
// *                                                                        *
// **************************************************************************

/** Abstract base class for all simulation engines.
    This allows us to define a single, static, random number
    generator stream manager.
    And, it provides encapsulation for the simulation and RNG options.
*/
class sim_engine : public subengine {
  static rng_manager* rngm;
  static long Samples;
  static double Confidence;
  static double Precision;
  static unsigned Type;
protected:
  static rng_stream* rng_main;
  static const unsigned V_CONFIDENCE  = 0;
  static const unsigned V_PRECISION   = 1;
  static const unsigned V_SAMPLES   = 2;
public:
  sim_engine();
  virtual ~sim_engine();

  inline long GetSamples() const { return Samples; }
  inline double GetConfidence() const { return Confidence; }
  inline double GetPrecision() const { return Precision; }
  inline int GetType() const { return Type; }

  friend void PrintSimLibraryVersions(OutputStream &s);
  friend class jump_distance_option;
  friend class init_simul;

  friend class jump_watcher;
  friend class seed_watcher;
};

rng_manager* sim_engine::rngm = 0;
rng_stream* sim_engine::rng_main = 0;
long sim_engine::Samples;
double sim_engine::Confidence;
double sim_engine::Precision;
unsigned sim_engine::Type;

// **************************************************************************
// *                           sim_engine methods                           *
// **************************************************************************

sim_engine::sim_engine() : subengine()
{
}

sim_engine::~sim_engine()
{
}

// **************************************************************************
// *                                                                        *
// *                        monte_carlo_engine class                        *
// *                                                                        *
// **************************************************************************

/** Monte carlo simulation engine base class.
    Through the magic of virtual functions, specific engines
    can be easily derived from this class.
*/
class monte_carlo_engine : public sim_engine {
  static reporting_msg report;
public:
  monte_carlo_engine();
  virtual ~monte_carlo_engine();

  virtual bool AppliesToModelType(hldsm::model_type mt) const;
  virtual void RunEngine(result* pass, int np, traverse_data &x);

  // Provide in derived classes:
  virtual sim_experiment* MakeExperiment(expr* e, traverse_data &x) const = 0;

  friend class init_simul;
};

reporting_msg monte_carlo_engine::report;

// **************************************************************************
// *                       monte_carlo_engine methods                       *
// **************************************************************************

monte_carlo_engine::monte_carlo_engine() : sim_engine()
{
}

monte_carlo_engine::~monte_carlo_engine()
{
}

bool monte_carlo_engine::AppliesToModelType(hldsm::model_type mt) const
{
  return (0==mt);
}

void monte_carlo_engine
::RunEngine(result* pass, int np, traverse_data &x)
{
  DCASSERT(x.answer);
  DCASSERT(1==np);
  expr* e = pass ? smart_cast <expr*> (pass[0].getPtr()) : 0;
  if (0==e) {
    x.answer->setNull();
    return;
  }
  timer watch;
  e->PreCompute();
  sim_experiment* se = MakeExperiment(e, x);
  if (0==se)  throw Engine_Failed;

  rng_stream* old = x.stream;
  x.stream = rng_main;

  sim_confintl avg;
  switch (GetType()) {
    case V_SAMPLES:
        SIM_MonteCarlo_S(se, &avg, 1, GetConfidence(), GetPrecision());
        break;
    case V_PRECISION:
        SIM_MonteCarlo_W(se, &avg, 1, GetSamples(), GetConfidence());
        break;
    case V_CONFIDENCE:
        SIM_MonteCarlo_C(se, &avg, 1, GetSamples(), GetPrecision());
        break;
    default:
        if (em->startInternal(__FILE__, __LINE__)) {
          em->causedBy(e);
          em->internal() << "Bad simulation type";
          em->stopIO();
        }
        avg.is_valid = false;
  } // switch

  if (avg.is_valid) {
    x.answer->setConfidence( avg.average,
      new conf_intl(avg.half_width, avg.confidence) );
  } else {
    x.answer->setNull();
  }

  // Info dump here
  if (report.startReport()) {
      report.report() << "Finished estimation of E[";
      e->Print(report.report(), 0);
      report.report() << "]\n";
      report.report() << "\t" << avg.samples << " iterations\n";
      report.report() << "\t" << avg.confidence << " confidence\n";
      double prec = 0.0;
      if (avg.half_width) prec = avg.half_width / avg.average;
      report.report() << "\t" << prec << " precision\n";
      report.report() << "Monte_Carlo: Simulation took ";
      report.report() << watch.elapsed_seconds() << " seconds\n";
      report.stopIO();
  }

  x.stream = old;
  delete se;
}

// **************************************************************************
// *                                                                        *
// *                      Simulation of avg(rand real)                      *
// *                                                                        *
// **************************************************************************

class real_expr_experiment : public sim_experiment {
  expr* e;
  traverse_data &x;
public:
  real_expr_experiment(expr* what, traverse_data &where)
   : sim_experiment(), x(where) { e = what; }

  virtual ~real_expr_experiment() { };

  virtual void PerformExperiment(sim_outcome* r, int n);
};

void real_expr_experiment::PerformExperiment(sim_outcome* r, int n)
{
  DCASSERT(1==n);
  if (e)  e->ClearCache();
  SafeCompute(e, x);
  if (x.answer->isNormal()) {
    DCASSERT(e);
    r[0].is_valid = true;
    r[0].value = x.answer->getReal();
#ifdef DEBUG
    fprintf(stderr, "sampled %f\n", r[0].value);
#endif
  } else {
#ifdef DEBUG
    fprintf(stderr, "sampled abnormal value\n");
#endif
    r[0].is_valid = false;
  }
}

class sim_rr_avg : public monte_carlo_engine {
public:
  sim_rr_avg();
  virtual sim_experiment* MakeExperiment(expr* e, traverse_data &x) const;
};

sim_rr_avg::sim_rr_avg() : monte_carlo_engine()
{
}

sim_experiment* sim_rr_avg::MakeExperiment(expr* e, traverse_data &x) const
{
  return new real_expr_experiment(e, x);
}

sim_rr_avg the_sim_rr_avg;


// **************************************************************************
// *                                                                        *
// *                           jump_watcher class                           *
// *                                                                        *
// **************************************************************************

class jump_watcher : public option::watcher {
        long jump;
    public:
        jump_watcher();
        virtual ~jump_watcher();
        virtual void notify(const option* opt);

        inline long& Link() { return jump; }
};

jump_watcher::jump_watcher()
{
    jump = sim_engine::rngm->GetJumpValue();
}

jump_watcher::~jump_watcher()
{
}

void jump_watcher::notify(const option* opt)
{
    sim_engine::rngm->SetJumpValue(jump);
}

// **************************************************************************
// *                                                                        *
// *                           seed_watcher class                           *
// *                                                                        *
// **************************************************************************

class seed_watcher : public option::watcher {
        long seed;
    public:
        seed_watcher();
        virtual ~seed_watcher();
        virtual void notify(const option* opt);

        inline long& Link() { return seed; }
};

seed_watcher::seed_watcher()
{
    seed = 12345;
    sim_engine::rngm->InitStreamFromSeed(sim_engine::rng_main, seed);
}

seed_watcher::~seed_watcher()
{
}

void seed_watcher::notify(const option* opt)
{
    sim_engine::rngm->InitStreamFromSeed(sim_engine::rng_main, seed);
}

// ******************************************************************
// *                                                                *
// *                                                                *
// *                         Initialization                         *
// *                                                                *
// *                                                                *
// ******************************************************************

class init_simul : public initializer {
  public:
    init_simul();
    virtual bool execute();
};
init_simul the_simul_initializer;

init_simul::init_simul() : initializer("init_simul")
{
  usesResource("em");
  usesResource("engtypes");
}

bool init_simul::execute()
{
  //
  // Simulation options
  //
  if (0==em) return false;

  //
  // Option defaults
  //
  sim_engine::Samples = 100000;
  sim_engine::Confidence = 0.95;
  sim_engine::Precision = 0.001;
  sim_engine::Type = sim_engine::V_SAMPLES;

  //
  // Add options
  //
  if (em->OptMan()) {

    em->OptMan()->addIntOption(
      "SimSamples",
      "Number of samples to collect during simulations, if fixed (see option SimType).",
      sim_engine::Samples, 50, 2000000000
    );

    em->OptMan()->addRealOption(
      "SimConfidence",
      "Desired level of confidence for simulations, if fixed (see option SimType).",
      sim_engine::Confidence, true, false, 0.0, true, false, 1.0
    );

    em->OptMan()->addRealOption(
      "SimPrecision",
      "Desired level of (relative) precision for simulations, i.e., the desired half-width size as a fraction of the interval midpoint, if fixed (see option SimType).  Note: for a fixed confidence, one more digit of precision requires 100 times more samples.",
      sim_engine::Precision, true, false, 0.0, true, false, 1.0
    );

    option* stype = em->OptMan()->addRadioOption(
      "SimType",
      "Simulation parameters can be tuned with options SimSamples, SimConfidence, and SimPrecision.  However, only two of the three values can be fixed, as the third is a function of the other two.  This option essentially determines which of the three is allowed to vary in the simulation.",
      3, sim_engine::Type
    );
    stype->addRadioButton(
      "CONFIDENCE", "Level of confidence varies", sim_engine::V_CONFIDENCE
    );
    stype->addRadioButton(
      "PRECISION", "Half-width precision varies", sim_engine::V_PRECISION
    );
    stype->addRadioButton(
      "SAMPLES",
      "Number of samples (i.e., iterations) varies", sim_engine::V_SAMPLES
    );
  }

  monte_carlo_engine::report.initialize(em->OptMan(), "Monte_Carlo",
    "When set, Monte Carlo Simulation performance data is displayed."
  );

  //
  // RNG options and such.
  //
  DCASSERT(0== sim_engine::rngm);
  sim_engine::rngm = RNG_MakeStreamManager();
  if (sim_engine::rngm) {
    sim_engine::rng_main = sim_engine::rngm->NewBlankStream();
  }

  if (em->OptMan()) {
    //
    // stream separation as an observed option
    //
    jump_watcher* jw = new jump_watcher();
    em->OptMan()->addIntOption("RngStreamSeparation",
      "Stream separation distance for creating multiple, independent RNG streams.  The exponent d is specified, and streams will be separated by a distance of at least 2^d.",
      jw->Link(),
      sim_engine::rngm->MinimumJumpValue(),
      sim_engine::rngm->MaximumJumpValue()
    )->registerWatcher(jw);

    //
    // RNG seed as an observed option
    //
    seed_watcher* sw = new seed_watcher();
    em->OptMan()->addIntOption("SeedRng",
      "Re-set the random number generator state based on the given seed value.",
      sw->Link(), 0, LONG_MAX
    )->registerWatcher(sw);

  }


  //
  // Add engines
  //
  RegisterEngine(em,
    "AvgRandReal",
    "MONTE_CARLO",
    "Average determined using Monte-Carlo simulation.",
    &the_sim_rr_avg
  );

  //
  // Register libraries
  //
  em->registerLibrary(  new rng_lib(sim_engine::rngm)  );

  return true;
}

