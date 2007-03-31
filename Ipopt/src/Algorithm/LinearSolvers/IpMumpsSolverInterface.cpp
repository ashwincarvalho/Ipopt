// Copyright (C) 2006, 2007 Damien Hocking, KBC Advanced Technologies
// All Rights Reserved.
// This code is published under the Common Public License.
//
// Authors: Damien Hocking                 KBC    2006-03-20
//        (included his original contribution into Ipopt package on 2006-03-25)
//          Andreas Waechter               IBM    2006-03-25
//           (minor changes and corrections)
//          Scott Turnberg                 CMU    2006-05-12
//           (major revision)
//           (incorporated by AW on 2006-11-11 into Ipopt package)

#include "IpMumpsSolverInterface.hpp"

extern "C"
{
#include "dmumps_c.h"
#include "mpi.h"
}

#ifdef HAVE_CMATH
# include <cmath>
#else
# ifdef HAVE_MATH_H
#  include <math.h>
# else
#  error "don't have header file for math"
# endif
#endif

#ifdef HAVE_CSTDLIB
# include <cstdlib>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# else
#  error "don't have header file for stdlib"
# endif
#endif

namespace Ipopt
{
#if COIN_IPOPT_VERBOSITY > 0
  static const Index dbg_verbosity = 0;
#endif

#define USE_COMM_WORLD -987654

  MumpsSolverInterface::MumpsSolverInterface()
  {
    DBG_START_METH("MumpsSolverInterface::MumpsSolverInterface()",
                   dbg_verbosity);
    //initialize mumps
    DMUMPS_STRUC_C* mumps_ = new DMUMPS_STRUC_C;
    int argc=1;
    char ** argv = 0;
    int myid, ierr;
    ierr = MPI_Init(&argc, &argv);
    ierr = MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    mumps_->n = 0;
    mumps_->nz = 0;
    mumps_->a = NULL;
    mumps_->jcn = NULL;
    mumps_->irn = NULL;
    mumps_->job = -1;//initialize mumps
    mumps_->par = 1;//working host for sequential version
    mumps_->sym = 2;//general symetric matrix
    mumps_->comm_fortran = USE_COMM_WORLD;
    dmumps_c(mumps_);
    mumps_ptr_ = (void*)mumps_;
  }


  MumpsSolverInterface::~MumpsSolverInterface()
  {
    DBG_START_METH("MumpsSolverInterface::~MumpsSolverInterface()",
                   dbg_verbosity);

    DMUMPS_STRUC_C* mumps_ = (DMUMPS_STRUC_C*)mumps_ptr_;
    mumps_->job = -2; //terminate mumps
    dmumps_c(mumps_);
    MPI_Finalize();
    delete [] mumps_->a;
    delete mumps_;
  }

  void MumpsSolverInterface::RegisterOptions(SmartPtr<RegisteredOptions> roptions)
  {
    roptions->AddBoundedNumberOption(
      "mumps_pivtol",
      "Pivot tolerance for the linear solver MUMPS.",
      0, false, 1, false, 1e-6,
      "A smaller number pivots for sparsity, a larger number pivots for "
      "stability.  This option is only available if Ipopt has been compiled "
      "with MUMPS.");
    roptions->AddBoundedNumberOption(
      "mumps_pivtolmax",
      "Maximum pivot tolerance for the linear solver MUMPS.",
      0, false, 1, false, 0.1,
      "Ipopt may increase pivtol as high as pivtolmax to get a more accurate "
      "solution to the linear system.  This option is only available if "
      "Ipopt has been compiled with MUMPS.");
    roptions->AddLowerBoundedIntegerOption(
      "mumps_mem_percent",
      "Percentage increase in the estimated working space for MUMPS.",
      0, 1000,
      "In MUMPS when significant extra fill-in is caused by numerical "
      "pivoting, larger values of mumps_mem_percent may help use the "
      "workspace more efficiently.");
  }

  bool MumpsSolverInterface::InitializeImpl(const OptionsList& options,
      const std::string& prefix)
  {
    options.GetNumericValue("mumps_pivtol", pivtol_, prefix);
    if(options.GetNumericValue("mumps_pivtolmax", pivtolmax_, prefix)) {
      ASSERT_EXCEPTION(pivtolmax_>=pivtol_, OPTION_INVALID,
                       "Option \"mumps_pivtolmax\": This value must be between "
                       "mumps_pivtol and 1.");
    }
    else {
      pivtolmax_ = Max(pivtolmax_, pivtol_);
    }

    options.GetIntegerValue("mumps_mem_percent",
                            mem_percent_, prefix);

    // The following option is registered by OrigIpoptNLP
    options.GetBoolValue("warm_start_same_structure",
                         warm_start_same_structure_, prefix);

    // Reset all private data
    initialized_ = false;
    pivtol_changed_ = false;
    refactorize_ = false;

    DMUMPS_STRUC_C* mumps_ = (DMUMPS_STRUC_C*)mumps_ptr_;
    if (!warm_start_same_structure_) {
      mumps_->n = 0;
      mumps_->nz = 0;
    }
    else {
      ASSERT_EXCEPTION(mumps_->n>0 && mumps_->nz>0, INVALID_WARMSTART,
                       "MumpsSolverInterface called with warm_start_same_structure, but the problem is solved for the first time.");
    }

    return true;
  }

  ESymSolverStatus MumpsSolverInterface::MultiSolve(bool new_matrix,
      const Index* ia,
      const Index* ja,
      Index nrhs,
      double* rhs_vals,
      bool check_NegEVals,
      Index numberOfNegEVals)
  {
    DBG_START_METH("MumpsSolverInterface::MultiSolve", dbg_verbosity);
    DBG_ASSERT(!check_NegEVals || ProvidesInertia());
    DBG_ASSERT(initialized_);
    DBG_ASSERT(((DMUMPS_STRUC_C*)mumps_ptr_)->irn == ia);
    DBG_ASSERT(((DMUMPS_STRUC_C*)mumps_ptr_)->jcn == ja);

    if (pivtol_changed_) {
      DBG_PRINT((1,"Pivot tolerance has changed.\n"));
      pivtol_changed_ = false;
      // If the pivot tolerance has been changed but the matrix is not
      // new, we have to request the values for the matrix again to do
      // the factorization again.
      if (!new_matrix) {
        DBG_PRINT((1,"Ask caller to call again.\n"));
        refactorize_ = true;
        return SYMSOLVER_CALL_AGAIN;
      }
    }

    // check if a factorization has to be done
    DBG_PRINT((1, "new_matrix = %d\n", new_matrix));
    if (new_matrix || refactorize_) {
      // perform the factorization
      ESymSolverStatus retval;
      retval = Factorization(check_NegEVals, numberOfNegEVals);
      if (retval != SYMSOLVER_SUCCESS)  {
        DBG_PRINT((1, "FACTORIZATION FAILED!\n"));
        return retval;  // Matrix singular or error occurred
      }
      refactorize_ = false;
    }
    // do the solve
    return Solve(nrhs, rhs_vals);
  }


  double* MumpsSolverInterface::GetValuesArrayPtr()
  {
    DMUMPS_STRUC_C* mumps_ = (DMUMPS_STRUC_C*)mumps_ptr_;
    DBG_START_METH("MumpsSolverInterface::GetValuesArrayPtr",dbg_verbosity)
    DBG_ASSERT(initialized_);
    return mumps_->a;
  }

  void dump_matrix(DMUMPS_STRUC_C* mumps_data)
  {
#ifdef write_matrices
    // Dump the matrix
    for (int i=0; i<40; i++) {
      printf("%d\n", mumps_data->icntl[i]);
    }
    for (int i=0; i<5; i++) {
      printf("%25.15e\n", mumps_data->cntl[i]);
    }
    printf("%-15d :N\n",mumps_data->n);
    printf("%-15d :NZ", mumps_data->nz);
    for (int i=0; i<mumps_data->nz; i++) {
      printf("\n%d %d %25.15e", mumps_data->irn[i], mumps_data->jcn[i], mumps_data->a[i]);
    }
    printf("       :values");
    // Dummy RHS for now
    for (int i=0; i<mumps_data->n; i++) {
      printf("\n%25.15e", 0.);
    }
    printf("    :RHS\n");
#endif

  }

  /* Initialize the local copy of the positions of the nonzero
      elements */
  ESymSolverStatus MumpsSolverInterface::InitializeStructure(Index dim,
      Index nonzeros,
      const Index* ia,
      const Index* ja)
  {
    DMUMPS_STRUC_C* mumps_ = (DMUMPS_STRUC_C*)mumps_ptr_;
    DBG_START_METH("MumpsSolverInterface::InitializeStructure", dbg_verbosity);

    ESymSolverStatus retval = SYMSOLVER_SUCCESS;
    if (!warm_start_same_structure_) {
      mumps_->n = dim;
      mumps_->nz = nonzeros;
      delete [] mumps_->a;
      mumps_->a = NULL;

      mumps_->a = new double[nonzeros];
      mumps_->irn = const_cast<int*>(ia);
      mumps_->jcn = const_cast<int*>(ja);

      // Do the symbolic facotrization
      retval = SymbolicFactorization();
      if (retval != SYMSOLVER_SUCCESS ) {
        return retval;
      }
    }
    else {
      ASSERT_EXCEPTION(mumps_->n==dim && mumps_->nz==nonzeros,
                       INVALID_WARMSTART,"MumpsSolverInterface called with warm_start_same_structure, but the problem size has changed.");
    }

    initialized_ = true;
    return retval;
  }


  ESymSolverStatus MumpsSolverInterface::SymbolicFactorization()
  {
    DBG_START_METH("MumpsSolverInterface::Factorization", dbg_verbosity);
    DMUMPS_STRUC_C* mumps_data = (DMUMPS_STRUC_C*)mumps_ptr_;

    IpData().TimingStats().LinearSystemSymbolicFactorization().Start();

    mumps_data->job = 1;//symbolic ordering pass

    mumps_data->icntl[1] = 0;
    mumps_data->icntl[2] = 0;//QUIETLY!
    mumps_data->icntl[3] = 0;

    //mumps_data->icntl[1] = 6;
    //mumps_data->icntl[2] = 6;//QUIETLY!
    //mumps_data->icntl[3] = 4;

    //Todo: reveal and tune these options
    mumps_data->icntl[5] = 0;//no column permutation
    mumps_data->icntl[6] = 0;//AMD ordering
    //orig mumps_data->icntl[7] = 0;//no scalling
    mumps_data->icntl[7] = 1;//no scalling
    mumps_data->icntl[9] = 0;//no iterative refinement iterations


    mumps_data->icntl[12] = 1;//avoid lapack bug, ensures proper inertia
    mumps_data->icntl[13] = mem_percent_; //% memory to allocate over expected
    mumps_data->cntl[0] = pivtol_;  // Set pivot tolerance

    dump_matrix(mumps_data);

    dmumps_c(mumps_data);
    int error = mumps_data->info[0];

    //return appropriat value
    if (error == -6) {//system is singular
      Jnlst().Printf(J_DETAILED, J_LINEAR_ALGEBRA,
                     "MUMPS returned INFO(1) = %d matrix is singular.\n",error);
      return SYMSOLVER_SINGULAR;
    }
    if (error < 0) {
      Jnlst().Printf(J_ERROR, J_LINEAR_ALGEBRA,
                     "Error=%d returned from MUMPS in Factorization.\n",
                     error);
      return SYMSOLVER_FATAL_ERROR;
    }

    IpData().TimingStats().LinearSystemSymbolicFactorization().End();

    return SYMSOLVER_SUCCESS;
  }

  ESymSolverStatus MumpsSolverInterface::Factorization(
    bool check_NegEVals, Index numberOfNegEVals)
  {
    DBG_START_METH("MumpsSolverInterface::Factorization", dbg_verbosity);
    DMUMPS_STRUC_C* mumps_data = (DMUMPS_STRUC_C*)mumps_ptr_;

    mumps_data->job = 2;//numerical factorization

    dump_matrix(mumps_data);
    dmumps_c(mumps_data);
    int error = mumps_data->info[0];

    //Check for errors
    if (error == -8 || error == -9) {//not enough memory
      const Index trycount_max = 20;
      for(int trycount=0; trycount<trycount_max; trycount++) {
        Jnlst().Printf(J_WARNING, J_LINEAR_ALGEBRA,
                       "MUMPS returned INFO(1) = %d and requires more memory, reallocating.  Attempt %d\n",
                       error,trycount+1);
        Jnlst().Printf(J_WARNING, J_LINEAR_ALGEBRA,
                       "  Increasing icntl[13] from %d to ", mumps_data->icntl[13]);
        double mem_percent = mumps_data->icntl[13];
        mumps_data->icntl[13] = (Index)(2.0 * mem_percent);
        Jnlst().Printf(J_WARNING, J_LINEAR_ALGEBRA, "%d.\n", mumps_data->icntl[13]);

        dump_matrix(mumps_data);
        dmumps_c(mumps_data);
        error = mumps_data->info[0];
        if (error != -8 & error != -9)
          break;
      }
      if (error == -8 || error == -9) {
        Jnlst().Printf(J_ERROR, J_LINEAR_ALGEBRA,
                       "MUMPS was not able to obtain enough memory.\n");
        return SYMSOLVER_FATAL_ERROR;
      }
    }


    if (error == -10) {//system is singular
      Jnlst().Printf(J_DETAILED, J_LINEAR_ALGEBRA,
                     "MUMPS returned INFO(1) = %d matrix is singular.\n",error);
      return SYMSOLVER_SINGULAR;
    }

    negevals_ = mumps_data->infog[11];

    if (check_NegEVals && (numberOfNegEVals!=negevals_)) {
      Jnlst().Printf(J_DETAILED, J_LINEAR_ALGEBRA,
                     "In MumpsSolverInterface::Factorization: negevals_ = %d, but numberOfNegEVals = %d\n",
                     negevals_, numberOfNegEVals);
      return SYMSOLVER_WRONG_INERTIA;
    }

    if (error < 0) {//some other error
      Jnlst().Printf(J_ERROR, J_LINEAR_ALGEBRA,
                     "MUMPS returned INFO(1) =%d MUMPS failure.\n",
                     error);
      return SYMSOLVER_FATAL_ERROR;
    }

    return SYMSOLVER_SUCCESS;
  }

  ESymSolverStatus MumpsSolverInterface::Solve(Index nrhs, double *rhs_vals)
  {
    DBG_START_METH("MumpsSolverInterface::Solve", dbg_verbosity);
    DMUMPS_STRUC_C* mumps_data = (DMUMPS_STRUC_C*)mumps_ptr_;
    ESymSolverStatus retval = SYMSOLVER_SUCCESS;
    IpData().TimingStats().LinearSystemBackSolve().Start();
    for (Index i = 0; i < nrhs; i++) {
      Index offset = i * mumps_data->n;
      mumps_data->rhs = &(rhs_vals[offset]);
      mumps_data->job = 3;//solve
      dmumps_c(mumps_data);
      int error = mumps_data->info[0];
      if (error < 0) {
        Jnlst().Printf(J_ERROR, J_LINEAR_ALGEBRA,
                       "Error=%d returned from MUMPS in Solve.\n",
                       error);
        retval = SYMSOLVER_FATAL_ERROR;
      }
    }
    IpData().TimingStats().LinearSystemBackSolve().End();
    return retval;
  }

  Index MumpsSolverInterface::NumberOfNegEVals() const
  {
    DBG_START_METH("MumpsSolverInterface::NumberOfNegEVals", dbg_verbosity);
    DBG_ASSERT(negevals_ >= 0);
    return negevals_;
  }

  bool MumpsSolverInterface::IncreaseQuality()
  {
    DBG_START_METH("MumpsTSolverInterface::IncreaseQuality",dbg_verbosity);
    if (pivtol_ == pivtolmax_) {
      return false;
    }
    pivtol_changed_ = true;

    Jnlst().Printf(J_DETAILED, J_LINEAR_ALGEBRA,
                   "Increasing pivot tolerance for MUMPS from %7.2e ",
                   pivtol_);

    //this is a more aggresive update then MA27
    //this should be tuned
    pivtol_ = Min(pivtolmax_, pow(pivtol_,0.5));
    Jnlst().Printf(J_DETAILED, J_LINEAR_ALGEBRA,
                   "to %7.2e.\n",
                   pivtol_);
    return true;
  }

}//end Ipopt namespace


