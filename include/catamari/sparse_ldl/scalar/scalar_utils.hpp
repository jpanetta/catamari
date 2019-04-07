/*
 * Copyright (c) 2018-2019 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_SPARSE_LDL_SCALAR_SCALAR_UTILS_H_
#define CATAMARI_SPARSE_LDL_SCALAR_SCALAR_UTILS_H_

#include <vector>

#include "catamari/buffer.hpp"
#include "catamari/coordinate_matrix.hpp"
#include "catamari/integers.hpp"
#include "catamari/symmetric_ordering.hpp"

#include "catamari/sparse_ldl/scalar.hpp"

namespace catamari {
namespace scalar_ldl {

// Computes the elimination forest (via the 'parents' array) and sizes of the
// structures of a scalar (simplicial) LDL' factorization.
//
// Cf. Tim Davis's "LDL"'s symbolic factorization.
template <class Field>
void EliminationForestAndDegrees(const CoordinateMatrix<Field>& matrix,
                                 const SymmetricOrdering& ordering,
                                 Buffer<Int>* parents, Buffer<Int>* degrees);
#ifdef CATAMARI_OPENMP
template <class Field>
void OpenMPEliminationForestAndDegrees(const CoordinateMatrix<Field>& matrix,
                                       const SymmetricOrdering& ordering,
                                       Buffer<Int>* parents,
                                       Buffer<Int>* degrees);
#endif  // ifdef CATAMARI_OPENMP

// Computes the nonzero pattern of L(row, :) in
// row_structure[0 : num_packed - 1].
template <class Field>
Int ComputeRowPattern(const CoordinateMatrix<Field>& matrix,
                      const SymmetricOrdering& ordering,
                      const Buffer<Int>& parents, Int row, Int* pattern_flags,
                      Int* row_structure);

// Computes the nonzero pattern of L(row, :) in a topological ordering in
// row_structure[start : num_rows - 1] and spread A(row, 0 : row - 1) into
// row_workspace.
template <class Field>
Int ComputeTopologicalRowPatternAndScatterNonzeros(
    const CoordinateMatrix<Field>& matrix, const SymmetricOrdering& ordering,
    const Buffer<Int>& parents, Int row, Int* pattern_flags, Int* row_structure,
    Field* row_workspace);

// Fills in the structure indices for the lower factor.
template <class Field>
void FillStructureIndices(const CoordinateMatrix<Field>& matrix,
                          const SymmetricOrdering& ordering,
                          const AssemblyForest& forest,
                          const Buffer<Int>& degrees,
                          LowerStructure* lower_structure);
#ifdef CATAMARI_OPENMP
template <class Field>
void OpenMPFillStructureIndices(const CoordinateMatrix<Field>& matrix,
                                const SymmetricOrdering& ordering,
                                AssemblyForest* forest,
                                LowerStructure* lower_structure,
                                bool preallocate = true,
                                int sort_grain_size = 500);
#endif  // ifdef CATAMARI_OPENMP

}  // namespace scalar_ldl
}  // namespace catamari

#include "catamari/sparse_ldl/scalar/scalar_utils-impl.hpp"

#endif  // ifndef CATAMARI_SPARSE_LDL_SCALAR_SCALAR_UTILS_H_