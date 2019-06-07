/*
 * Copyright (c) 2019 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_BLAS_MATRIX_IMPL_H_
#define CATAMARI_BLAS_MATRIX_IMPL_H_

#include "catamari/blas_matrix.hpp"

namespace catamari {

template <typename Field>
BlasMatrix<Field>::BlasMatrix() {
  view.height = 0;
  view.width = 0;
  view.leading_dim = 0;
  view.data = nullptr;
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const Int& height, const Int& width) {
  Resize(height, width);
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const Int& height, const Int& width,
                              const Field& value) {
  Resize(height, width, value);
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const Int& height, const Int& width,
                              const Int& leading_dim) {
  Resize(height, width, leading_dim);
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const Int& height, const Int& width,
                              const Int& leading_dim, const Field& value) {
  Resize(height, width, leading_dim, value);
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const BlasMatrix<Field>& matrix) {
  const Int height = matrix.view.height;
  const Int width = matrix.view.width;

  data.Resize(height * width);
  view.height = height;
  view.width = width;
  view.leading_dim = height;
  view.data = data.Data();

  // Copy each individual column so that the leading dimension does not
  // impact the copy time.
  for (Int j = 0; j < width; ++j) {
    std::copy(&matrix(0, j), &matrix(height, j), &view(0, j));
  }
}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const BlasMatrixView<Field>& matrix)
    : BlasMatrix<Field>(matrix.ToConst()) {}

template <typename Field>
BlasMatrix<Field>::BlasMatrix(const ConstBlasMatrixView<Field>& matrix) {
  const Int height = matrix.height;
  const Int width = matrix.width;

  data.Resize(height * width);
  view.height = height;
  view.width = width;
  view.leading_dim = height;
  view.data = data.Data();

  // Copy each individual column so that the leading dimension does not
  // impact the copy time.
  for (Int j = 0; j < width; ++j) {
    std::copy(&matrix(0, j), &matrix(height, j), &view(0, j));
  }
}

template <typename Field>
BlasMatrix<Field>& BlasMatrix<Field>::operator=(
    const BlasMatrix<Field>& matrix) {
  if (this != &matrix) {
    const Int height = matrix.view.height;
    const Int width = matrix.view.width;

    data.Resize(height * width);
    view.height = height;
    view.width = width;
    view.leading_dim = height;
    view.data = data.Data();

    // Copy each individual column so that the leading dimension does not
    // impact the copy time.
    for (Int j = 0; j < width; ++j) {
      std::copy(&matrix(0, j), &matrix(height, j), &view(0, j));
    }
  }
  return *this;
}

template <typename Field>
BlasMatrix<Field>& BlasMatrix<Field>::operator=(
    const BlasMatrixView<Field>& matrix) {
  *this = matrix.ToConst();
  return *this;
}

template <typename Field>
BlasMatrix<Field>& BlasMatrix<Field>::operator=(
    const ConstBlasMatrixView<Field>& matrix) {
  const Int height = matrix.height;
  const Int width = matrix.width;

  data.Resize(height * width);
  view.height = height;
  view.width = width;
  view.leading_dim = height;
  view.data = data.Data();

  // Copy each individual column so that the leading dimension does not
  // impact the copy time.
  for (Int j = 0; j < width; ++j) {
    std::copy(&matrix(0, j), &matrix(height, j), &view(0, j));
  }
  return *this;
}

template <typename Field>
void BlasMatrix<Field>::Resize(const Int& height, const Int& width) {
  if (height == view.height && width == view.width) {
    return;
  }
  // TODO(Jack Poulson): Decide on how to handle 0 leading dimensions.
  data.Resize(height * width);
  view.height = height;
  view.width = width;
  view.leading_dim = height;
  view.data = data.Data();
}

template <typename Field>
void BlasMatrix<Field>::Resize(const Int& height, const Int& width,
                               const Int& leading_dim) {
  if (height == view.height && width == view.width) {
    return;
  }
  if (leading_dim < height) {
    throw std::invalid_argument(
        "The leading dimension must be at least as large as the height.");
  }
  // TODO(Jack Poulson): Decide on how to handle 0 leading dimensions.
  data.Resize(leading_dim * width);
  view.height = height;
  view.width = width;
  view.leading_dim = leading_dim;
  view.data = data.Data();
}

template <typename Field>
void BlasMatrix<Field>::Resize(const Int& height, const Int& width,
                               const Field& value) {
  // TODO(Jack Poulson): Decide on how to handle 0 leading dimensions.
  data.Resize(height * width, value);
  view.height = height;
  view.width = width;
  view.leading_dim = height;
  view.data = data.Data();
}

template <typename Field>
void BlasMatrix<Field>::Resize(const Int& height, const Int& width,
                               const Int& leading_dim, const Field& value) {
  if (leading_dim < height) {
    throw std::invalid_argument(
        "The leading dimension must be at least as large as the height.");
  }
  // TODO(Jack Poulson): Decide on how to handle 0 leading dimensions.
  data.Resize(leading_dim * width, value);
  view.height = height;
  view.width = width;
  view.leading_dim = leading_dim;
  view.data = data.Data();
}

template <typename Field>
Int BlasMatrix<Field>::Height() const CATAMARI_NOEXCEPT {
  return view.height;
}

template <typename Field>
Int BlasMatrix<Field>::Width() const CATAMARI_NOEXCEPT {
  return view.width;
}

template <typename Field>
Int BlasMatrix<Field>::LeadingDimension() const CATAMARI_NOEXCEPT {
  return view.leading_dim;
}

template <typename Field>
const Field* BlasMatrix<Field>::Data() const CATAMARI_NOEXCEPT {
  return view.data;
}

template <typename Field>
Field* BlasMatrix<Field>::Data() CATAMARI_NOEXCEPT {
  return view.data;
}

template <typename Field>
Field& BlasMatrix<Field>::operator()(Int row, Int column) {
  return view(row, column);
}

template <typename Field>
const Field& BlasMatrix<Field>::operator()(Int row, Int column) const {
  return view(row, column);
}

template <typename Field>
Field& BlasMatrix<Field>::Entry(Int row, Int column) {
  return view(row, column);
}

template <typename Field>
const Field& BlasMatrix<Field>::Entry(Int row, Int column) const {
  return view(row, column);
}

template <typename Field>
Field* BlasMatrix<Field>::Pointer(Int row, Int column) {
  return view.Pointer(row, column);
}

template <typename Field>
const Field* BlasMatrix<Field>::Pointer(Int row, Int column) const {
  return view.Pointer(row, column);
}

template <typename Field>
BlasMatrixView<Field> BlasMatrix<Field>::View() {
  return view;
}

template <typename Field>
const ConstBlasMatrixView<Field> BlasMatrix<Field>::ConstView() const {
  return view.ToConst();
}

}  // namespace catamari

#endif  // ifndef CATAMARI_BLAS_MATRIX_IMPL_H_
