/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_RIGHT_LOOKING_OPENMP_IMPL_H_
#define CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_RIGHT_LOOKING_OPENMP_IMPL_H_
#ifdef CATAMARI_OPENMP

#include <algorithm>

#include "catamari/dense_basic_linear_algebra.hpp"
#include "catamari/dense_factorizations.hpp"

#include "catamari/ldl/supernodal_ldl/factorization.hpp"

namespace catamari {
namespace supernodal_ldl {

template <class Field>
bool Factorization<Field>::OpenMPRightLookingSupernodeFinalize(
    Int supernode, RightLookingSharedState<Field>* shared_state,
    Buffer<PrivateState<Field>>* private_states, LDLResult* result) {
  typedef ComplexBase<Field> Real;
  BlasMatrixView<Field> diagonal_block = diagonal_factor_->blocks[supernode];
  BlasMatrixView<Field> lower_block = lower_factor_->blocks[supernode];
  const Int degree = lower_block.height;
  const Int supernode_size = lower_block.width;

  const int thread = omp_get_thread_num();
  PrivateState<Field>* private_state = &(*private_states)[thread];

  // Initialize this supernode's Schur complement as the zero matrix.
  Buffer<Field>& schur_complement_buffer =
      shared_state->schur_complement_buffers[supernode];
  {
    BlasMatrixView<Field>& schur_complement =
        shared_state->schur_complements[supernode];
    schur_complement_buffer.Resize(degree * degree, Field{0});
    schur_complement.height = degree;
    schur_complement.width = degree;
    schur_complement.leading_dim = degree;
    schur_complement.data = schur_complement_buffer.Data();
  }
  BlasMatrixView<Field> schur_complement =
      shared_state->schur_complements[supernode];

  OpenMPMergeChildSchurComplements(control_.merge_grain_size, supernode,
                                   ordering_, lower_factor_.get(),
                                   diagonal_factor_.get(), shared_state);

  Int num_supernode_pivots;
  {
    // TODO(Jack Poulson): Preallocate this buffer.
    Buffer<Field> multithreaded_buffer(supernode_size * supernode_size);
    #pragma omp taskgroup
    {
      num_supernode_pivots = OpenMPFactorDiagonalBlock(
          control_.factor_tile_size, control_.block_size,
          control_.factorization_type, &diagonal_block, &multithreaded_buffer);
      result->num_successful_pivots += num_supernode_pivots;
    }
  }
  if (num_supernode_pivots < supernode_size) {
    return false;
  }
  IncorporateSupernodeIntoLDLResult(supernode_size, degree, result);

  if (!degree) {
    // We can early exit.
    return true;
  }

  CATAMARI_ASSERT(supernode_size > 0, "Supernode size was non-positive.");
  #pragma omp taskgroup
  OpenMPSolveAgainstDiagonalBlock(control_.outer_product_tile_size,
                                  control_.factorization_type,
                                  diagonal_block.ToConst(), &lower_block);

  if (control_.factorization_type == kCholeskyFactorization) {
    #pragma omp taskgroup
    OpenMPLowerNormalHermitianOuterProduct(control_.outer_product_tile_size,
                                           Real{-1}, lower_block.ToConst(),
                                           Real{1}, &schur_complement);
  } else {
    BlasMatrixView<Field> scaled_transpose;
    scaled_transpose.height = supernode_size;
    scaled_transpose.width = degree;
    scaled_transpose.leading_dim = supernode_size;
    scaled_transpose.data = private_state->scaled_transpose_buffer.Data();

    #pragma omp taskgroup
    OpenMPFormScaledTranspose(
        control_.outer_product_tile_size, control_.factorization_type,
        diagonal_block.ToConst(), lower_block.ToConst(), &scaled_transpose);

    // Perform the multi-threaded MatrixMultiplyLowerNormalNormal.
    #pragma omp taskgroup
    OpenMPMatrixMultiplyLowerNormalNormal(
        control_.outer_product_tile_size, Field{-1}, lower_block.ToConst(),
        scaled_transpose.ToConst(), Field{1}, &schur_complement);
  }

  return true;
}

template <class Field>
bool Factorization<Field>::OpenMPRightLookingSubtree(
    Int level, Int max_parallel_levels, Int supernode,
    const CoordinateMatrix<Field>& matrix, const Buffer<double>& work_estimates,
    RightLookingSharedState<Field>* shared_state,
    Buffer<PrivateState<Field>>* private_states, LDLResult* result) {
  if (level >= max_parallel_levels) {
    const int thread = omp_get_thread_num();
    return RightLookingSubtree(supernode, matrix, shared_state,
                               &(*private_states)[thread], result);
  }

  const Int child_beg = ordering_.assembly_forest.child_offsets[supernode];
  const Int child_end = ordering_.assembly_forest.child_offsets[supernode + 1];
  const Int num_children = child_end - child_beg;

  Buffer<int> successes(num_children);
  Buffer<LDLResult> result_contributions(num_children);

  // Recurse on the children.
  #pragma omp taskgroup
  for (Int child_index = 0; child_index < num_children; ++child_index) {
    const Int child =
        ordering_.assembly_forest.children[child_beg + child_index];
    #pragma omp task default(none)                                            \
      firstprivate(level, max_parallel_levels, supernode, child, child_index, \
          shared_state, private_states)                                       \
      shared(successes, matrix, result_contributions, work_estimates)
    {
      LDLResult& result_contribution = result_contributions[child_index];
      successes[child_index] = OpenMPRightLookingSubtree(
          level + 1, max_parallel_levels, child, matrix, work_estimates,
          shared_state, private_states, &result_contribution);
    }
  }

  // Merge the child results (stopping if a failure is detected).
  bool succeeded = true;
  for (Int child_index = 0; child_index < num_children; ++child_index) {
    if (!successes[child_index]) {
      succeeded = false;
      break;
    }
    MergeContribution(result_contributions[child_index], result);
  }

  if (succeeded) {
    #pragma omp taskgroup
    succeeded = OpenMPRightLookingSupernodeFinalize(supernode, shared_state,
                                                    private_states, result);
  } else {
    // Clear the child fronts.
    for (Int child_index = 0; child_index < num_children; ++child_index) {
      const Int child =
          ordering_.assembly_forest.children[child_beg + child_index];
      Buffer<Field>& child_schur_complement_buffer =
          shared_state->schur_complement_buffers[child];
      BlasMatrixView<Field>& child_schur_complement =
          shared_state->schur_complements[child];
      child_schur_complement.height = 0;
      child_schur_complement.width = 0;
      child_schur_complement.data = nullptr;
      child_schur_complement_buffer.Clear();
    }
  }

  return succeeded;
}

template <class Field>
LDLResult Factorization<Field>::OpenMPRightLooking(
    const CoordinateMatrix<Field>& matrix) {
  const Int num_supernodes = ordering_.supernode_sizes.Size();
  const Int num_roots = ordering_.assembly_forest.roots.Size();
  const Int max_threads = omp_get_max_threads();

  RightLookingSharedState<Field> shared_state;
  shared_state.schur_complement_buffers.Resize(num_supernodes);
  shared_state.schur_complements.Resize(num_supernodes);

  Buffer<PrivateState<Field>> private_states(max_threads);
  if (control_.factorization_type != kCholeskyFactorization) {
    const Int workspace_size = max_lower_block_size_;
    #pragma omp taskgroup
    for (int t = 0; t < max_threads; ++t) {
      #pragma omp task default(none) firstprivate(t, workspace_size) \
          shared(private_states)
      private_states[t].scaled_transpose_buffer.Resize(workspace_size);
    }
  }

  // Compute flop-count estimates so that we may prioritize the expensive
  // tasks before the cheaper ones.
  Buffer<double> work_estimates(num_supernodes);
  for (const Int& root : ordering_.assembly_forest.roots) {
    FillSubtreeWorkEstimates(root, ordering_.assembly_forest, *lower_factor_,
                             &work_estimates);
  }

  LDLResult result;

  Buffer<int> successes(num_roots);
  Buffer<LDLResult> result_contributions(num_roots);

  // TODO(Jack Poulson): Make this value configurable.
  const Int max_parallel_levels = std::ceil(std::log2(max_threads)) + 3;

  // Recurse on each tree in the elimination forest.
  const Int level = 0;
  if (max_parallel_levels == 0) {
    const int thread = omp_get_thread_num();
    for (Int root_index = 0; root_index < num_roots; ++root_index) {
      const Int root = ordering_.assembly_forest.roots[root_index];
      LDLResult& result_contribution = result_contributions[root_index];
      successes[root_index] =
          RightLookingSubtree(root, matrix, &shared_state,
                              &private_states[thread], &result_contribution);
    }
  } else {
    const int old_max_threads = GetMaxBlasThreads();
    SetNumBlasThreads(1);

    #pragma omp parallel
    #pragma omp single
    for (Int root_index = 0; root_index < num_roots; ++root_index) {
      const Int root = ordering_.assembly_forest.roots[root_index];

      // As above, one could make use of OpenMP task priorities, e.g., with an
      // integer priority of:
      //
      //   const Int task_priority = std::pow(work_estimates[child], 0.25);
      //
      #pragma omp task default(none) firstprivate(root, root_index, level) \
          shared(successes, matrix, result_contributions, shared_state,    \
              private_states, work_estimates)
      {
        LDLResult& result_contribution = result_contributions[root_index];
        successes[root_index] = OpenMPRightLookingSubtree(
            level + 1, max_parallel_levels, root, matrix, work_estimates,
            &shared_state, &private_states, &result_contribution);
      }
    }

    SetNumBlasThreads(old_max_threads);
  }

  for (Int index = 0; index < num_roots; ++index) {
    if (!successes[index]) {
      break;
    }
    MergeContribution(result_contributions[index], &result);
  }

  return result;
}

}  // namespace supernodal_ldl
}  // namespace catamari

#endif  // ifdef CATAMARI_OPENMP
#endif  // ifndef
// CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_RIGHT_LOOKING_OPENMP_IMPL_H_
