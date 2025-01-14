
#ifndef PROC_MARKOV_H
#define PROC_MARKOV_H

#include "stoch_llm.h"
#include "../ExprLib/exprman.h"

#include "../_GraphLib/graphlib.h"
#include "../_Timer/timerlib.h"

namespace Old_MCLib {
  class Markov_chain;
};

struct LS_Vector;
struct LS_Options;
class timer;

class markov_process : public stochastic_lldsm::process {
  protected:
    class reporter : public GraphLib::timer_hook {
      const exprman* em;
      reporting_msg report;
      timer watch;
    public:
      reporter(const exprman* The_em);
      virtual ~reporter();
      virtual void start(const char* w);
      virtual void stop();
      inline reporter* switchMe() { return report.isActive() ? this : 0; }
    };


  public:
    markov_process();
  protected:
    virtual ~markov_process();

  // Options.  The whole point of this class.

  private:
    static LS_Options* lsopts;
    // options, shared by all Markov-chain low-level models.
    static unsigned solver;
    static const unsigned GAUSS_SEIDEL = 0;
    static const unsigned JACOBI       = 1;
    static const unsigned ROW_JACOBI   = 2;
    static const unsigned NUM_SOLVERS  = 3;
    static reporting_msg report;
    static unsigned access;
    static const unsigned BY_COLUMNS = 0;
    static const unsigned BY_ROWS    = 1;
  protected:
    static reporter* my_timer;

  public:
    inline static bool storeByRows() { return BY_ROWS == access; }
    static const LS_Options& getSolverOptions();
    static const char* getSolver();

  // reporting.  The other point of this class.

  protected:
    void startTransientReport(timer& w, double t) const;
    void stopTransientReport(timer& w, long iters) const;
    void startSteadyReport(timer& w) const;
    void stopSteadyReport(timer& w, long iters) const;
    void startTTAReport(timer& w) const;
    void stopTTAReport(timer& w, long iters) const;
    void startAccumulatedReport(timer& w, double t) const;
    void stopAccumulatedReport(timer& w, long iters) const;

    void startRevTransReport(timer& w, double t) const;
    void stopRevTransReport(timer& w, long iters) const;

    /*
      CSL "reverse" probabilities of reaching an accepting state
    */
    void startReachAcceptReport(timer&w) const;
    void stopReachAcceptReport(timer&w, long iters) const;

    friend class init_markovproc;
};

#endif
