/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_DENSE_FACTORIZATIONS_CHOLESKY_IMPL_H_
#define CATAMARI_DENSE_FACTORIZATIONS_CHOLESKY_IMPL_H_

#include <cmath>

#include "catamari/dense_basic_linear_algebra.hpp"
#include "catamari/lapack.hpp"

#include "catamari/dense_factorizations.hpp"
#include <stdexcept>
#include <type_traits>

namespace catamari {

template <class Field>
Int UnblockedLowerCholeskyFactorization(BlasMatrixView<Field>* matrix) {
  typedef ComplexBase<Field> Real;
  const Int height = matrix->height;
  for (Int i = 0; i < height; ++i) {
    Field *col_i = matrix->Pointer(0, i);
    const Real delta = RealPart(col_i[i]);
    if (delta <= Real{0}) {
      if constexpr (!std::is_same<Real, Field>::value)
          col_i[i] = delta;
      return i;
    }

    // TODO(Jack Poulson): Switch to a custom square-root function so that
    // more general datatypes can be supported.
    const Real delta_sqrt = std::sqrt(delta);
    col_i[i] = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      col_i[k] /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    for (Int j = i + 1; j < height; ++j) {
      const Field eta = Conjugate(col_i[j]);
      Field * col_j = matrix->Pointer(0, j);
      for (Int k = j; k < height; ++k) {
        col_j[k] -= col_i[k] * eta;
      }
    }
  }
  return height;
}

// TODO(Jack Poulson): Also implement specializations which call BLAS for the
// rank-one updates.
template <class Field>
Int UnblockedDynamicallyRegularizedLowerCholeskyFactorization(
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization) {
  typedef ComplexBase<Field> Real;
  const Int height = matrix->height;
  const Int offset = dynamic_reg_params.offset;

  for (Int i = 0; i < height; ++i) {
    Real delta = RealPart(matrix->Entry(i, i));
    if (delta <= -dynamic_reg_params.positive_threshold) {
      return i;
    } else if (delta < dynamic_reg_params.positive_threshold) {
      const Int orig_index =
          dynamic_reg_params.inverse_permutation
              ? (*dynamic_reg_params.inverse_permutation)[i + offset]
              : i + offset;
      CATAMARI_ASSERT((*dynamic_reg_params.signatures)[orig_index],
                      "Spurious negative pivot.");
      const Real regularization = dynamic_reg_params.positive_threshold - delta;
      dynamic_regularization->emplace_back(orig_index, regularization);
      delta = dynamic_reg_params.positive_threshold;
    }

    // TODO(Jack Poulson): Switch to a custom square-root function so that
    // more general datatypes can be supported.
    const Real delta_sqrt = std::sqrt(delta);
    matrix->Entry(i, i) = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    for (Int j = i + 1; j < height; ++j) {
      const Field eta = Conjugate(matrix->Entry(j, i));
      for (Int k = j; k < height; ++k) {
        const Field& lambda_left = matrix->Entry(k, i);
        matrix->Entry(k, j) -= lambda_left * eta;
      }
    }
  }
  return height;
}

#if 0 // The unblocked versions are only called for matrices so small that the BLAS overhead is not worth it...
      // TODO(Julian Panetta): experiment with dynamically dispatching to BLAS' `*syr` below...
template <>
inline Int UnblockedLowerCholeskyFactorization(BlasMatrixView<float>* matrix) {
  const Int height = matrix->height;
  const Int leading_dim = matrix->leading_dim;
  for (Int i = 0; i < height; ++i) {
    const float delta = matrix->Entry(i, i);
    if (delta <= 0) {
      return i;
    }

    const float delta_sqrt = std::sqrt(delta);
    matrix->Entry(i, i) = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    {
      const BlasInt rem_height_blas = height - (i + 1);
      const char lower = 'L';
      const float alpha = -1;
      const BlasInt unit_inc_blas = 1;
      const BlasInt leading_dim_blas = leading_dim;
      BLAS_SYMBOL(ssyr)
      (&lower, &rem_height_blas, &alpha, matrix->Pointer(i + 1, i),
       &unit_inc_blas, matrix->Pointer(i + 1, i + 1), &leading_dim_blas);
    }
  }
  return height;
}

template <>
inline Int UnblockedLowerCholeskyFactorization(BlasMatrixView<double>* matrix) {
  const Int height = matrix->height;
  const Int leading_dim = matrix->leading_dim;
  for (Int i = 0; i < height; ++i) {
    const double delta = matrix->Entry(i, i);
    if (delta <= 0) {
      return i;
    }

    const double delta_sqrt = std::sqrt(delta);
    matrix->Entry(i, i) = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    {
      const BlasInt rem_height_blas = height - (i + 1);
      const char lower = 'L';
      const double alpha = -1;
      const BlasInt unit_inc_blas = 1;
      const BlasInt leading_dim_blas = leading_dim;
      BLAS_SYMBOL(dsyr)
      (&lower, &rem_height_blas, &alpha, matrix->Pointer(i + 1, i),
       &unit_inc_blas, matrix->Pointer(i + 1, i + 1), &leading_dim_blas);
    }
  }
  return height;
}

template <>
inline Int UnblockedLowerCholeskyFactorization(
    BlasMatrixView<Complex<float>>* matrix) {
  const Int height = matrix->height;
  const Int leading_dim = matrix->leading_dim;
  for (Int i = 0; i < height; ++i) {
    const float delta = RealPart(matrix->Entry(i, i));
    if (delta <= 0) {
      return i;
    }

    const float delta_sqrt = std::sqrt(delta);
    matrix->Entry(i, i) = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    {
      const BlasInt rem_height_blas = height - (i + 1);
      const char lower = 'L';
      const float alpha = -1;
      const BlasInt unit_inc_blas = 1;
      const BlasInt leading_dim_blas = leading_dim;
      BLAS_SYMBOL(cher)
      (&lower, &rem_height_blas, &alpha,
       reinterpret_cast<const BlasComplexFloat*>(matrix->Pointer(i + 1, i)),
       &unit_inc_blas,
       reinterpret_cast<BlasComplexFloat*>(matrix->Pointer(i + 1, i + 1)),
       &leading_dim_blas);
    }
  }
  return height;
}

template <>
inline Int UnblockedLowerCholeskyFactorization(
    BlasMatrixView<Complex<double>>* matrix) {
  const Int height = matrix->height;
  const Int leading_dim = matrix->leading_dim;
  for (Int i = 0; i < height; ++i) {
    const double delta = RealPart(matrix->Entry(i, i));
    if (delta <= 0) {
      return i;
    }

    const double delta_sqrt = std::sqrt(delta);
    matrix->Entry(i, i) = delta_sqrt;

    // Solve for the remainder of the i'th column of L.
    for (Int k = i + 1; k < height; ++k) {
      matrix->Entry(k, i) /= delta_sqrt;
    }

    // Perform the Hermitian rank-one update.
    {
      const BlasInt rem_height_blas = height - (i + 1);
      const char lower = 'L';
      const double alpha = -1;
      const BlasInt unit_inc_blas = 1;
      const BlasInt leading_dim_blas = leading_dim;
      BLAS_SYMBOL(zher)
      (&lower, &rem_height_blas, &alpha,
       reinterpret_cast<const BlasComplexDouble*>(matrix->Pointer(i + 1, i)),
       &unit_inc_blas,
       reinterpret_cast<BlasComplexDouble*>(matrix->Pointer(i + 1, i + 1)),
       &leading_dim_blas);
    }
  }
  return height;
}
#endif  // ifdef CATAMARI_HAVE_BLAS

template <class Field>
Int BlockedLowerCholeskyFactorization(Int block_size,
                                      BlasMatrixView<Field>* matrix) {
  typedef ComplexBase<Field> Real;
  const Int height = matrix->height;
  for (Int i = 0; i < height; i += block_size) {
    const Int bsize = std::min(height - i, block_size);

    // Overwrite the diagonal block with its Cholesky factor.
    BlasMatrixView<Field> diagonal_block =
        matrix->Submatrix(i, i, bsize, bsize);
    const Int num_diag_pivots =
        UnblockedLowerCholeskyFactorization(&diagonal_block);
    if (num_diag_pivots < bsize) {
      return i + num_diag_pivots;
    }
    if (height == i + bsize) {
      break;
    }

    // Solve for the remainder of the block column of L.
    BlasMatrixView<Field> subdiagonal =
        matrix->Submatrix(i + bsize, i, height - (i + bsize), bsize);
    RightLowerAdjointTriangularSolves(diagonal_block.ToConst(), &subdiagonal);

    // Perform the Hermitian rank-bsize update.
    BlasMatrixView<Field> submatrix = matrix->Submatrix(
        i + bsize, i + bsize, height - (i + bsize), height - (i + bsize));
    LowerNormalHermitianOuterProduct(Real{-1}, subdiagonal.ToConst(), Real{1},
                                     &submatrix);
  }
  return height;
}

template <class Field>
Int BlockedDynamicallyRegularizedLowerCholeskyFactorization(
    Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization) {
  typedef ComplexBase<Field> Real;
  const Int height = matrix->height;

  DynamicRegularizationParams<Field> subparams = dynamic_reg_params;

  for (Int i = 0; i < height; i += block_size) {
    const Int bsize = std::min(height - i, block_size);
    subparams.offset = dynamic_reg_params.offset + i;

    // Overwrite the diagonal block with its Cholesky factor.
    BlasMatrixView<Field> diagonal_block =
        matrix->Submatrix(i, i, bsize, bsize);
    const Int num_diag_pivots =
        UnblockedDynamicallyRegularizedLowerCholeskyFactorization(
            subparams, &diagonal_block, dynamic_regularization);
    if (num_diag_pivots < bsize) {
      return i + num_diag_pivots;
    }
    if (height == i + bsize) {
      break;
    }

    // Solve for the remainder of the block column of L.
    BlasMatrixView<Field> subdiagonal =
        matrix->Submatrix(i + bsize, i, height - (i + bsize), bsize);
    RightLowerAdjointTriangularSolves(diagonal_block.ToConst(), &subdiagonal);

    // Perform the Hermitian rank-bsize update.
    BlasMatrixView<Field> submatrix = matrix->Submatrix(
        i + bsize, i + bsize, height - (i + bsize), height - (i + bsize));
    LowerNormalHermitianOuterProduct(Real{-1}, subdiagonal.ToConst(), Real{1},
                                     &submatrix);
  }
  return height;
}

template <class Field>
Int LowerCholeskyFactorizationDynamicBLASDispatch(Int block_size, BlasMatrixView<Field>* matrix) {
  if (matrix->height < block_size / 2)
      return UnblockedLowerCholeskyFactorization(matrix);
  return LowerCholeskyFactorization(block_size, matrix);
}

template <class Field>
inline Int LowerCholeskyFactorization(Int block_size,
                                      BlasMatrixView<float>* matrix) {
  if (matrix->height < 2 * block_size)
      return UnblockedLowerCholeskyFactorization(matrix);
  return BlockedLowerCholeskyFactorization(block_size, matrix);
}

#ifdef CATAMARI_HAVE_LAPACK
template <>
inline Int LowerCholeskyFactorization(Int /* block_size */,
                                      BlasMatrixView<float>* matrix) {
  const char uplo = 'L';
  const BlasInt height_blas = matrix->height;
  const BlasInt leading_dim_blas = matrix->leading_dim;
  BlasInt info;
  LAPACK_SYMBOL(spotrf)
  (&uplo, &height_blas, matrix->data, &leading_dim_blas, &info);
  if (info < 0) {
    std::cerr << "Argument " << -info << " had an illegal value." << std::endl;
    return 0;
  } else if (info == 0) {
    return matrix->height;
  } else {
    return info - 1; // Leading minor `info` is not positive definite; report `info - 1` successful pivots.
  }
}

template <>
inline Int LowerCholeskyFactorization(Int /* block_size */,
                                      BlasMatrixView<double>* matrix) {
  const char uplo = 'L';
  const BlasInt height_blas = matrix->height;
  const BlasInt leading_dim_blas = matrix->leading_dim;
  BlasInt info;
  LAPACK_SYMBOL(dpotrf)
  (&uplo, &height_blas, matrix->data, &leading_dim_blas, &info);
  if (info < 0) {
    std::cerr << "Argument " << -info << " had an illegal value." << std::endl;
    return 0;
  } else if (info == 0) {
    return matrix->height;
  } else {
    return info - 1; // Leading minor `info` is not positive definite; report `info - 1` successful pivots.
  }
}

template <>
inline Int LowerCholeskyFactorization(Int /* block_size */,
                                      BlasMatrixView<Complex<float>>* matrix) {
  const char uplo = 'L';
  const BlasInt height_blas = matrix->height;
  const BlasInt leading_dim_blas = matrix->leading_dim;
  BlasInt info;
  LAPACK_SYMBOL(cpotrf)
  (&uplo, &height_blas, reinterpret_cast<BlasComplexFloat*>(matrix->data),
   &leading_dim_blas, &info);
  if (info < 0) {
    std::cerr << "Argument " << -info << " had an illegal value." << std::endl;
    return 0;
  } else if (info == 0) {
    return matrix->height;
  } else {
    return info - 1; // Leading minor `info` is not positive definite; report `info - 1` successful pivots.
  }
}

template <>
inline Int LowerCholeskyFactorization(Int /* block_size */,
                                      BlasMatrixView<Complex<double>>* matrix) {
  const char uplo = 'L';
  const BlasInt height_blas = matrix->height;
  const BlasInt leading_dim_blas = matrix->leading_dim;
  BlasInt info;
  LAPACK_SYMBOL(zpotrf)
  (&uplo, &height_blas, reinterpret_cast<BlasComplexDouble*>(matrix->data),
   &leading_dim_blas, &info);
  if (info < 0) {
    std::cerr << "Argument " << -info << " had an illegal value." << std::endl;
    return 0;
  } else if (info == 0) {
    return matrix->height;
  } else {
    return info - 1; // Leading minor `info` is not positive definite; report `info - 1` successful pivots.
  }
}
#endif  // ifdef CATAMARI_HAVE_LAPACK

template <class Field>
Int DynamicallyRegularizedLowerCholeskyFactorization(
    Int block_size,
    const DynamicRegularizationParams<Field>& dynamic_reg_params,
    BlasMatrixView<Field>* matrix,
    std::vector<std::pair<Int, ComplexBase<Field>>>* dynamic_regularization) {
  return BlockedDynamicallyRegularizedLowerCholeskyFactorization(
      block_size, dynamic_reg_params, matrix, dynamic_regularization);
}

}  // namespace catamari

#endif  // ifndef CATAMARI_DENSE_FACTORIZATIONS_CHOLESKY_IMPL_H_
