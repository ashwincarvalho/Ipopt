/*
  Author: Ashwin Carvalho (modified code by Carl Laird, Andreas Waechter)
  Date: 2015-11-10
  Description: Solve statistics for Ipopt C interface

  gcc -c -Wall -Werror -fpic IpStdCInterfaceStats.cpp -I. -I/Users/Ty/CoinIpopt/build/include/coin
  gcc -shared -o libipoptstats.so IpStdCInterfaceStats.o -lstdc++
*/

#include "IpStdCInterface.h"
#include "IpStdInterfaceTNLP.hpp"
#include "IpOptionsList.hpp"
#include "IpIpoptApplication.hpp"
#include "IpSolveStatistics.hpp"
#include "IpStdCInterfaceStats.h"

/* Define IpoptProblemInfo struct to avoid forward declaration errors */
struct IpoptProblemInfo
{
  Index n;
  Number* x_L;
  Number* x_U;
  Index m;
  Number* g_L;
  Number* g_U;
  Index nele_jac;
  Index nele_hess;
  Index index_style;
  Eval_F_CB eval_f;
  Eval_G_CB eval_g;
  Eval_Grad_F_CB eval_grad_f;
  Eval_Jac_G_CB eval_jac_g;
  Eval_H_CB eval_h;
  Intermediate_CB intermediate_cb;
  Ipopt::SmartPtr<Ipopt::IpoptApplication> app;
  Number obj_scaling;
  Number* x_scaling;
  Number* g_scaling;
};

IpoptProblemStats ReturnIpoptProblemStats(IpoptProblem ipopt_problem)
{
  if (ipopt_problem == NULL) {
    return NULL;
  }
  IpoptProblemStats retval = new IpoptProblemInfoStats;

  retval->num_iters = ipopt_problem->app->Statistics()->IterationCount();
  retval->total_cpu_time = ipopt_problem->app->Statistics()->TotalCpuTime();
  retval->total_sys_time = ipopt_problem->app->Statistics()->TotalSysTime();
  retval->obj_val = ipopt_problem->app->Statistics()->FinalObjective();

  return retval;
}

void GetIpoptProblemStats(IpoptProblem ipopt_problem,
                          Index *num_iters,
                          Number *total_cpu_time,
                          Number *total_sys_time,
                          Number *total_wallclock_time,
                          Number *obj_val)
{
  if (ipopt_problem == NULL) {
    return;
  }
  *num_iters = ipopt_problem->app->Statistics()->IterationCount();
  *total_cpu_time = ipopt_problem->app->Statistics()->TotalCpuTime();
  *total_sys_time = ipopt_problem->app->Statistics()->TotalSysTime();
  *total_wallclock_time = ipopt_problem->app->Statistics()->TotalWallclockTime();
  *obj_val = ipopt_problem->app->Statistics()->FinalObjective();
}

void FreeIpoptProblemStats(IpoptProblemStats ipopt_problem_stats)
{
  delete ipopt_problem_stats;
}
