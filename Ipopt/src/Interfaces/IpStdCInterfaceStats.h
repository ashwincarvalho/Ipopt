/*
  Author: Ashwin Carvalho (modified code by Carl Laird, Andreas Waechter)
  Date: 2015-11-10
  Description: Solve statistics for Ipopt C interface
*/

#ifndef __IPSTDCINTERFACESTATS_H__
#define __IPSTDCINTERFACESTATS_H__

#ifndef IPOPT_EXPORT
#ifdef _MSC_VER
#ifdef IPOPT_DLL
#define IPOPT_EXPORT(type) __declspec(dllexport) type __cdecl
#else
#define IPOPT_EXPORT(type) type __cdecl
#endif
#else
#define IPOPT_EXPORT(type) type
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  /** Example solver statistics type (mirrored in Ipopt.jl) **/
  struct IpoptProblemInfoStats
  {
    Index num_iters;
    Number total_cpu_time;
    Number total_sys_time;
    Number total_wallclock_time;
    Number obj_val;
  };

  /** Pointer to a Ipopt Problem statistics */
  typedef struct IpoptProblemInfoStats* IpoptProblemStats;

  IPOPT_EXPORT(IpoptProblemStats) ReturnIpoptProblemStats(IpoptProblem ipopt_problem);

  IPOPT_EXPORT(struct IpoptProblemInfoStats)
      ReturnIpoptProblemInfoStats(IpoptProblem ipopt_problem);

  IPOPT_EXPORT(void) GetIpoptProblemStatsAll(IpoptProblem ipopt_problem,
                                             Index *num_iters,
                                             Number *total_cpu_time,
                                             Number *total_sys_time,
                                             Number *total_wallclock_time,
                                             Index *num_obj_evals,
                                             Index *num_constr_evals,
                                             Index *num_obj_grad_evals,
                                             Index *num_constr_jac_evals,
                                             Index *num_hess_evals,
                                             Number *scaled_obj_val,
                                             Number *obj_val,
                                             Number *scaled_dual_inf,
                                             Number *dual_inf,
                                             Number *scaled_constr_viol,
                                             Number *scaled_compl,
                                             Number *compl_,  /* leading underscore
                                                                 due to name conflict */
                                             Number *scaled_kkt_error,
                                             Number *kkt_error);

  IPOPT_EXPORT(void) GetIpoptProblemStats(IpoptProblem ipopt_problem,
                                          Index *num_iters,
                                          Number *total_cpu_time,
                                          Number *total_sys_time,
                                          Number *total_wallclock_time,
                                          Number *obj_val);

  IPOPT_EXPORT(void) FreeIpoptProblemStats(IpoptProblemStats ipopt_problem_stats);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif
