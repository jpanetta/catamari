/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_DENSE_FACTORIZATIONS_LDL_ADJOINT_IMPL_H_
#define CATAMARI_DENSE_FACTORIZATIONS_LDL_ADJOINT_IMPL_H_

#include <cmath>

#include "catamari/dense_basic_linear_algebra.hpp"
#include "catamari/lapack.hpp"

#include "catamari/dense_factorizations.hpp"

namespace catamari {

template <class Field>
Int LowerUnblockedLDLAdjointFactorization(BlasMatrixView<Field>* matrix) {
  typedef ComplexBase<Field> Real;
  const Int height = matrix->height;
  for (Int i = 0; i < height; ++i) {
    const Real delta = RealPart(matrix->Entry(i, i));
    matrix->Entry(i, i) = delta;
    if (delta == Real{0}) {
      return i;
    }

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta;
    }

    // Perform the rank-one update.
    for (Int j = i + 1; j < height; ++j) {
      const Field eta = delta * Conjugate(matrix->Entry(j, i));
      for (Int k = j; k < height; ++k) {
        const Field& lambda_left = matrix->Entry(k, i);
        matrix->Entry(k, j) -= lambda_left * eta;
      }
    }
  }
  return height;
}

template <class Field>
Int LowerBlockedLDLAdjointFactorization(Int block_size,
                                        BlasMatrixView<Field>* matrix) {
  const Int height = matrix->height;

  Buffer<Field> buffer(std::max(height - block_size, Int(0)) * block_size);
  BlasMatrixView<Field> factor;
  factor.data = buffer.Data();

  for (Int i = 0; i < height; i += block_size) {
    const Int bsize = std::min(height - i, block_size);

    // Overwrite the diagonal block with its LDL' factorization.
    BlasMatrixView<Field> diagonal_block =
        matrix->Submatrix(i, i, bsize, bsize);
    const Int num_diag_pivots =
        LowerUnblockedLDLAdjointFactorization(&diagonal_block);
    if (num_diag_pivots < bsize) {
      return i + num_diag_pivots;
    }
    if (height == i + bsize) {
      break;
    }

    // Solve for the remainder of the block column of L.
    BlasMatrixView<Field> subdiagonal =
        matrix->Submatrix(i + bsize, i, height - (i + bsize), bsize);
    RightLowerAdjointUnitTriangularSolves(diagonal_block.ToConst(),
                                          &subdiagonal);

    // Copy the conjugate of the current factor.
    factor.height = subdiagonal.height;
    factor.width = subdiagonal.width;
    factor.leading_dim = subdiagonal.height;
    for (Int j = 0; j < subdiagonal.width; ++j) {
      for (Int k = 0; k < subdiagonal.height; ++k) {
        factor(k, j) = Conjugate(subdiagonal(k, j));
      }
    }

    // Solve against the diagonal.
    for (Int j = 0; j < subdiagonal.width; ++j) {
      const ComplexBase<Field> delta = RealPart(diagonal_block(j, j));
      for (Int k = 0; k < subdiagonal.height; ++k) {
        subdiagonal(k, j) /= delta;
      }
    }

    // Perform the Hermitian rank-bsize update.
    BlasMatrixView<Field> submatrix = matrix->Submatrix(
        i + bsize, i + bsize, height - (i + bsize), height - (i + bsize));
    MatrixMultiplyLowerNormalTranspose(Field{-1}, subdiagonal.ToConst(),
                                       factor.ToConst(), Field{1}, &submatrix);
  }
  return height;
}

template <class Field>
Int LowerLDLAdjointFactorization(Int block_size,
                                 BlasMatrixView<Field>* matrix) {
  return LowerBlockedLDLAdjointFactorization(block_size, matrix);
}

}  // namespace catamari

#endif  // ifndef CATAMARI_DENSE_FACTORIZATIONS_LDL_ADJOINT_IMPL_H_
