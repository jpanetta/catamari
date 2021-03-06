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
#include "catamari/dynamic_regularization.hpp"
#include "catamari/integers.hpp"

namespace catamari {

// Attempts to overwrite the lower triangle of an (implicitly) Hermitian
// Positive-Definite matrix with its lower-triangular Cholesky factor. The
// return value is the number of successful pivots; the factorization was
// successful if and only if the return value is the height of the input
// matrix.
template <class Field>
Int LowerCholeskyFactorization(Int block_size, BlasMatrixView<Field>* matrix);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPLowerCholeskyFactorization(Int tile_size, Int block_size,
                                     BlasMatrixView<Field>* matrix);
#endif  // ifdef CATAMARI_OPENMP

template <class Field>
Int DynamicallyRegularizedLowerCholeskyFactorization(
    Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPDynamicallyRegularizedLowerCholeskyFactorization(
    Int tile_size, Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization);
#endif  // ifdef CATAMARI_OPENMP

// Attempts to overwrite the lower triangle of an (implicitly) Hermitian
// matrix with its L D L^H factorization, where L is lower-triangular with
// unit diagonal and D is diagonal (D is stored in place of the implicit
// unit diagonal). The return value is the number of successful pivots; the
// factorization was successful if and only if the return value is the height
// of the input matrix.
template <class Field>
Int LDLAdjointFactorization(Int block_size, BlasMatrixView<Field>* matrix);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPLDLAdjointFactorization(Int tile_size, Int block_size,
                                  BlasMatrixView<Field>* matrix,
                                  Buffer<Field>* buffer);
#endif  // ifdef CATAMARI_OPENMP

template <class Field>
Int DynamicallyRegularizedLDLAdjointFactorization(
    Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPDynamicallyRegularizedLDLAdjointFactorization(
    Int tile_size, Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix, Buffer<Field>* buffer,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization);
#endif  // ifdef CATAMARI_OPENMP

// Attempts to overwrite the lower triangle of an (implicitly) symmetric
// matrix with its L D L^T factorization, where L is lower-triangular with
// unit diagonal and D is diagonal (D is stored in place of the implicit
// unit diagonal). The return value is the number of successful pivots; the
// factorization was successful if and only if the return value is the height
// of the input matrix.
template <class Field>
Int LDLTransposeFactorization(Int block_size, BlasMatrixView<Field>* matrix);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPLDLTransposeFactorization(Int tile_size, Int block_size,
                                    BlasMatrixView<Field>* matrix,
                                    Buffer<Field>* buffer);
#endif  // ifdef CATAMARI_OPENMP

// Attempts to overwrite the lower triangle of an (implicitly) Hermitian
// matrix with its diagonally-pivoted L D L^H factorization, where L is
// lower-triangular with unit diagonal and D is real diagonal (D is stored in
// place of the implicit unit diagonal). The return value is the number of
// successful pivots; the factorization was successful if and only if the return
// value is the height of the input matrix.
template <class Field>
Int PivotedLDLAdjointFactorization(Int block_size,
                                   BlasMatrixView<Field>* matrix,
                                   BlasMatrixView<Int>* permutation);

#ifdef CATAMARI_OPENMP
template <class Field>
Int OpenMPPivotedLDLAdjointFactorization(Int tile_size, Int block_size,
                                         BlasMatrixView<Field>* matrix,
                                         BlasMatrixView<Int>* permutation);
#endif  // ifdef CATAMARI_OPENMP

}  // namespace catamari

#include "catamari/dense_factorizations/cholesky-impl.hpp"
#include "catamari/dense_factorizations/cholesky_openmp-impl.hpp"
#include "catamari/dense_factorizations/ldl_adjoint-impl.hpp"
#include "catamari/dense_factorizations/ldl_adjoint_openmp-impl.hpp"
#include "catamari/dense_factorizations/ldl_transpose-impl.hpp"
#include "catamari/dense_factorizations/ldl_transpose_openmp-impl.hpp"
#include "catamari/dense_factorizations/pivoted_ldl_adjoint-impl.hpp"
#include "catamari/dense_factorizations/pivoted_ldl_adjoint_openmp-impl.hpp"

#endif  // ifndef CATAMARI_DENSE_FACTORIZATIONS_H_
