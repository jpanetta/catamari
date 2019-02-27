/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_DENSE_FACTORIZATIONS_H_
#define CATAMARI_DENSE_FACTORIZATIONS_H_

#include <random>

#include "catamari/blas_matrix_view.hpp"
#include "catamari/buffer.hpp"
#include "catamari/complex.hpp"
#include "catamari/integers.hpp"

namespace catamari {

// Attempts to overwrite the lower triangle of an (implicitly) Hermitian
// Positive-Definite matrix with its lower-triangular Cholesky factor. The
// return value is the number of successful pivots; the factorization was
// successful if and only if the return value is the height of the input
// matrix.
template <class Field>
Int LowerCholeskyFactorization(Int block_size, BlasMatrixView<Field>* matrix);

#ifdef _OPENMP
template <class Field>
Int MultithreadedLowerCholeskyFactorization(Int tile_size, Int block_size,
                                            BlasMatrixView<Field>* matrix);
#endif  // ifdef _OPENMP

// Attempts to overwrite the lower triangle of an (implicitly) Hermitian
// matrix with its L D L^H factorization, where L is lower-triangular with
// unit diagonal and D is diagonal (D is stored in place of the implicit
// unit diagonal). The return value is the number of successful pivots; the
// factorization was successful if and only if the return value is the height
// of the input matrix.
template <class Field>
Int LowerLDLAdjointFactorization(Int block_size, BlasMatrixView<Field>* matrix);

#ifdef _OPENMP
template <class Field>
Int MultithreadedLowerLDLAdjointFactorization(Int tile_size, Int block_size,
                                              BlasMatrixView<Field>* matrix,
                                              Buffer<Field>* buffer);
#endif  // ifdef _OPENMP

// Attempts to overwrite the lower triangle of an (implicitly) symmetric
// matrix with its L D L^T factorization, where L is lower-triangular with
// unit diagonal and D is diagonal (D is stored in place of the implicit
// unit diagonal). The return value is the number of successful pivots; the
// factorization was successful if and only if the return value is the height
// of the input matrix.
template <class Field>
Int LowerLDLTransposeFactorization(Int block_size,
                                   BlasMatrixView<Field>* matrix);

#ifdef _OPENMP
template <class Field>
Int MultithreadedLowerLDLTransposeFactorization(Int tile_size, Int block_size,
                                                BlasMatrixView<Field>* matrix,
                                                Buffer<Field>* buffer);
#endif  // ifdef _OPENMP

// Returns a sample from the Determinantal Point Process implied by the
// marginal kernel matrix (i.e., a Hermitian matrix with all eigenvalues
// contained in [0, 1]). The input matrix is overwritten with the associated
// L D L^H factorization of the modified kernel (each diagonal pivot with a
// failed coin-flip is decremented by one).
template <class Field>
std::vector<Int> LowerFactorAndSampleDPP(
    Int block_size, bool maximum_likelihood, BlasMatrixView<Field>* matrix,
    std::mt19937* generator,
    std::uniform_real_distribution<ComplexBase<Field>>* uniform_dist);

#ifdef _OPENMP
template <class Field>
std::vector<Int> MultithreadedLowerFactorAndSampleDPP(
    Int tile_size, Int block_size, bool maximum_likelihood,
    BlasMatrixView<Field>* matrix, std::mt19937* generator,
    std::uniform_real_distribution<ComplexBase<Field>>* uniform_dist,
    std::vector<Field>* buffer);
#endif  // ifdef _OPENMP

}  // namespace catamari

#include "catamari/dense_factorizations-impl.hpp"

#endif  // ifndef CATAMARI_DENSE_FACTORIZATIONS_H_
