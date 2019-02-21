/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_H_
#define CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_H_

#include "catamari/buffer.hpp"
#include "catamari/ldl/supernodal_ldl/diagonal_factor.hpp"
#include "catamari/ldl/supernodal_ldl/lower_factor.hpp"
#include "catamari/ldl/supernodal_ldl/supernode_utils.hpp"

namespace catamari {
namespace supernodal_ldl {

// Configuration options for supernodal LDL' factorization.
struct Control {
  // Determines the style of the factorization.
  SymmetricFactorizationType factorization_type;

  // Configuration for the supernodal relaxation.
  SupernodalRelaxationControl relaxation_control;

  // The choice of either left-looking or right-looking LDL' factorization.
  // There is currently no supernodal up-looking support.
  LDLAlgorithm algorithm = kRightLookingLDL;

  // The minimal supernode size for an out-of-place trapezoidal solve to be
  // used.
  Int forward_solve_out_of_place_supernode_threshold = 10;

  // The minimal supernode size for an out-of-place trapezoidal solve to be
  // used.
  Int backward_solve_out_of_place_supernode_threshold = 10;

  // The algorithmic block size for the factorization.
  Int block_size = 64;

#ifdef _OPENMP
  // The size of the matrix tiles for factorization OpenMP tasks.
  Int factor_tile_size = 128;

  // The size of the matrix tiles for dense outer product OpenMP tasks.
  Int outer_product_tile_size = 240;

  // The number of columns to group into a single task when multithreading
  // the addition of child Schur complement updates onto the parent.
  Int merge_grain_size = 500;

  // The number of columns to group into a single task when multithreading
  // the scalar structure formation.
  Int sort_grain_size = 200;
#endif  // ifdef _OPENMP
};

// The user-facing data structure for storing a supernodal LDL' factorization.
template <class Field>
class Factorization {
 public:
  // Factors the given matrix using the prescribed permutation.
  LDLResult Factor(const CoordinateMatrix<Field>& matrix,
                   const SymmetricOrdering& manual_ordering,
                   const Control& control);

  // Solve a set of linear systems using the factorization.
  void Solve(BlasMatrix<Field>* matrix) const;

  // Solves a set of linear systems using the lower-triangular factor.
  void LowerTriangularSolve(BlasMatrix<Field>* matrix) const;

#ifdef _OPENMP
  void MultithreadedLowerTriangularSolve(BlasMatrix<Field>* matrix) const;
#endif  // ifdef _OPENMP

  // Solves a set of linear systems using the diagonal factor.
  void DiagonalSolve(BlasMatrix<Field>* matrix) const;

#ifdef _OPENMP
  void MultithreadedDiagonalSolve(BlasMatrix<Field>* matrix) const;
#endif  // ifdef _OPENMP

  // Solves a set of linear systems using the trasnpose (or adjoint) of the
  // lower-triangular factor.
  void LowerTransposeTriangularSolve(BlasMatrix<Field>* matrix) const;

#ifdef _OPENMP
  void MultithreadedLowerTransposeTriangularSolve(
      BlasMatrix<Field>* matrix) const;
#endif  // ifdef _OPENMP

  // Prints the diagonal of the factorization.
  void PrintDiagonalFactor(const std::string& label, std::ostream& os) const;

  // Prints the unit lower-triangular matrix.
  void PrintLowerFactor(const std::string& label, std::ostream& os) const;

  // Incorporates the details and work required to process the supernode with
  // the given size and degree into the factorization result.
  static void IncorporateSupernodeIntoLDLResult(Int supernode_size, Int degree,
                                                LDLResult* result);

  // Adds in the contribution of a subtree into an overall result.
  static void MergeContribution(const LDLResult& contribution,
                                LDLResult* result);

 private:
  // The representation of the permutation matrix P so that P A P' should be
  // factored. Typically, this permutation is the composition of a
  // fill-reducing ordering and a supernodal relaxation permutation.
  SymmetricOrdering ordering_;

  // Marks the type of factorization employed.
  SymmetricFactorizationType factorization_type_;

  // Whether a left-looking or right-looking factorization is to be used.
  LDLAlgorithm algorithm_;

  // An array of length 'num_rows'; the i'th member is the index of the
  // supernode containing column 'i'.
  Buffer<Int> supernode_member_to_index_;

  // The largest supernode size in the factorization.
  Int max_supernode_size_;

  // The largest degree of a supernode in the factorization.
  Int max_degree_;

  // The algorithmic block size for diagonal block factorizations.
  Int block_size_;

#ifdef _OPENMP
  // The size of the matrix tiles for factorization OpenMP tasks.
  Int factor_tile_size_;

  // The size of the matrix tiles for dense outer product OpenMP tasks.
  Int outer_product_tile_size_;

  // The number of columns to group into a single task when multithreading
  // the addition of child Schur complement updates onto the parent.
  Int merge_grain_size_;

  // The number of columns to group into a single task when multithreading
  // the scalar structure formation.
  Int sort_grain_size_;
#endif  // ifdef _OPENMP

  // The minimal supernode size for an out-of-place trapezoidal solve to be
  // used.
  Int forward_solve_out_of_place_supernode_threshold_;

  // The minimal supernode size for an out-of-place trapezoidal solve to be
  // used.
  Int backward_solve_out_of_place_supernode_threshold_;

  // The subdiagonal-block portion of the lower-triangular factor.
  std::unique_ptr<LowerFactor<Field>> lower_factor_;

  // The block-diagonal factor.
  std::unique_ptr<DiagonalFactor<Field>> diagonal_factor_;

  // Form the (possibly relaxed) supernodes for the factorization.
  void FormSupernodes(const CoordinateMatrix<Field>& matrix,
                      const SupernodalRelaxationControl& control,
                      AssemblyForest* forest, Buffer<Int>* supernode_degrees);
#ifdef _OPENMP
  void MultithreadedFormSupernodes(const CoordinateMatrix<Field>& matrix,
                                   const SupernodalRelaxationControl& control,
                                   AssemblyForest* forest,
                                   Buffer<Int>* supernode_degrees);
#endif  // ifdef _OPENMP

  void InitializeFactors(const CoordinateMatrix<Field>& matrix,
                         const AssemblyForest& forest,
                         const Buffer<Int>& supernode_degrees);
#ifdef _OPENMP
  void MultithreadedInitializeFactors(const CoordinateMatrix<Field>& matrix,
                                      const AssemblyForest& forest,
                                      const Buffer<Int>& supernode_degrees);
#endif  // ifdef _OPENMP

  LDLResult LeftLooking(const CoordinateMatrix<Field>& matrix,
                        const Control& control);
#ifdef _OPENMP
  LDLResult MultithreadedLeftLooking(const CoordinateMatrix<Field>& matrix,
                                     const Control& control);
#endif  // ifdef _OPENMP

  LDLResult RightLooking(const CoordinateMatrix<Field>& matrix,
                         const Control& control);
#ifdef _OPENMP
  LDLResult MultithreadedRightLooking(const CoordinateMatrix<Field>& matrix,
                                      const Control& control);
#endif  // ifdef _OPENMP

  bool LeftLookingSubtree(Int supernode, const CoordinateMatrix<Field>& matrix,
                          LeftLookingSharedState* shared_state,
                          PrivateState<Field>* private_state,
                          LDLResult* result);
#ifdef _OPENMP
  bool MultithreadedLeftLookingSubtree(
      Int level, Int max_parallel_levels, Int supernode,
      const CoordinateMatrix<Field>& matrix,
      LeftLookingSharedState* shared_state,
      Buffer<PrivateState<Field>>* private_states, LDLResult* result);
#endif  // ifdef _OPENMP

  bool RightLookingSubtree(Int supernode, const CoordinateMatrix<Field>& matrix,
                           RightLookingSharedState<Field>* shared_state,
                           LDLResult* result);
#ifdef _OPENMP
  bool MultithreadedRightLookingSubtree(
      Int level, Int max_parallel_levels, Int supernode,
      const CoordinateMatrix<Field>& matrix,
      const Buffer<double>& work_estimates,
      RightLookingSharedState<Field>* shared_state, LDLResult* result);
#endif  // ifdef _OPENMP

  void LeftLookingSupernodeUpdate(Int main_supernode,
                                  const CoordinateMatrix<Field>& matrix,
                                  LeftLookingSharedState* shared_state,
                                  PrivateState<Field>* private_state);
#ifdef _OPENMP
  void MultithreadedLeftLookingSupernodeUpdate(
      Int main_supernode, const CoordinateMatrix<Field>& matrix,
      LeftLookingSharedState* shared_state,
      Buffer<PrivateState<Field>>* private_states);
#endif  // ifdef _OPENMP

  bool LeftLookingSupernodeFinalize(Int main_supernode, LDLResult* result);
#ifdef _OPENMP
  bool MultithreadedLeftLookingSupernodeFinalize(
      Int supernode, Buffer<PrivateState<Field>>* private_states,
      LDLResult* result);
#endif  // ifdef _OPENMP

  bool RightLookingSupernodeFinalize(
      Int supernode, RightLookingSharedState<Field>* shared_state,
      LDLResult* result);
#ifdef _OPENMP
  bool MultithreadedRightLookingSupernodeFinalize(
      Int supernode, RightLookingSharedState<Field>* shared_state,
      LDLResult* result);
#endif  // ifdef _OPENMP

  // Performs the portion of the lower-triangular solve corresponding to the
  // subtree with the given root supernode.
  void LowerTriangularSolveRecursion(Int supernode, BlasMatrix<Field>* matrix,
                                     Buffer<Field>* workspace) const;

#ifdef _OPENMP
  void MultithreadedLowerTriangularSolveRecursion(
      Int supernode, BlasMatrix<Field>* matrix,
      Buffer<Buffer<Field>>* private_workspaces) const;
#endif  // ifdef _OPENMP

  // Performs the trapezoidal solve associated with a particular supernode.
  void LowerSupernodalTrapezoidalSolve(Int supernode, BlasMatrix<Field>* matrix,
                                       Buffer<Field>* workspace) const;

  // Performs the portion of the transposed lower-triangular solve
  // corresponding to the subtree with the given root supernode.
  void LowerTransposeTriangularSolveRecursion(
      Int supernode, BlasMatrix<Field>* matrix,
      Buffer<Field>* packed_input_buf) const;

#ifdef _OPENMP
  void MultithreadedLowerTransposeTriangularSolveRecursion(
      Int supernode, BlasMatrix<Field>* matrix,
      Buffer<Buffer<Field>>* private_packed_input_bufs) const;
#endif  // ifdef _OPENMP

  // Performs the trapezoidal solve associated with a particular supernode.
  void LowerTransposeSupernodalTrapezoidalSolve(Int supernode,
                                                BlasMatrix<Field>* matrix,
                                                Buffer<Field>* workspace) const;
};

}  // namespace supernodal_ldl
}  // namespace catamari

#include "catamari/ldl/supernodal_ldl/factorization-impl.hpp"

#endif  // ifndef CATAMARI_LDL_SUPERNODAL_LDL_FACTORIZATION_H_
