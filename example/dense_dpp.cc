/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include <iostream>

#include "catamari/blas_matrix.hpp"
#include "catamari/dense_factorizations.hpp"
#include "catamari/ldl.hpp"
#include "quotient/timer.hpp"
#include "specify.hpp"

using catamari::BlasMatrix;
using catamari::BlasMatrixView;
using catamari::Complex;
using catamari::ComplexBase;
using catamari::Int;
using quotient::Buffer;

namespace {

template <typename Field>
void InitializeMatrix(Int matrix_size, BlasMatrix<Field>* matrix) {
  matrix->Resize(matrix_size, matrix_size);
  for (Int j = 0; j < matrix_size; ++j) {
    matrix->Entry(j, j) = 1.;
    for (Int i = j + 1; i < matrix_size; ++i) {
      matrix->Entry(i, j) = -1. / (2 * matrix_size);
    }
  }
}

template <typename Field>
void SampleDPP(
    Int block_size, bool maximum_likelihood, BlasMatrixView<Field>* matrix,
    std::mt19937* generator,
    std::uniform_real_distribution<ComplexBase<Field>>* uniform_dist) {
  const bool is_complex = catamari::IsComplex<Field>::value;
  const Int matrix_size = matrix->height;
  quotient::Timer timer;
  timer.Start();

  LowerFactorAndSampleDPP(block_size, maximum_likelihood, matrix, generator,
                          uniform_dist);

  const double runtime = timer.Stop();
  const double flops =
      (is_complex ? 4 : 1) * std::pow(1. * matrix_size, 3.) / 3.;
  const double gflops_per_sec = flops / (1.e9 * runtime);
  std::cout << "Sequential DPP GFlop/s: " << gflops_per_sec << std::endl;
}

#ifdef CATAMARI_OPENMP
template <typename Field>
void OpenMPSampleDPP(
    Int tile_size, Int block_size, bool maximum_likelihood,
    BlasMatrixView<Field>* matrix, std::mt19937* generator,
    std::uniform_real_distribution<ComplexBase<Field>>* uniform_dist,
    Buffer<Field>* extra_buffer) {
  const bool is_complex = catamari::IsComplex<Field>::value;
  const Int matrix_size = matrix->height;
  quotient::Timer timer;
  timer.Start();

  const int old_max_threads = catamari::GetMaxBlasThreads();
  catamari::SetNumBlasThreads(1);

  #pragma omp parallel
  #pragma omp single
  OpenMPLowerFactorAndSampleDPP(tile_size, block_size, maximum_likelihood,
                                matrix, generator, uniform_dist, extra_buffer);

  catamari::SetNumBlasThreads(old_max_threads);

  const double runtime = timer.Stop();
  const double flops =
      (is_complex ? 4 : 1) * std::pow(1. * matrix_size, 3.) / 3.;
  const double gflops_per_sec = flops / (1.e9 * runtime);
  std::cout << "OpenMP DPP GFlop/s: " << gflops_per_sec << std::endl;
}
#endif  // ifdef CATAMARI_OPENMP

template <typename Field>
void RunDPPTests(bool maximum_likelihood, Int matrix_size, Int block_size,
                 Int CATAMARI_UNUSED tile_size, Int num_rounds,
                 unsigned int random_seed) {
  typedef catamari::ComplexBase<Field> Real;
  BlasMatrix<Field> matrix;
  Buffer<Field> extra_buffer(matrix_size * matrix_size);

  std::mt19937 generator(random_seed);
  std::uniform_real_distribution<Real> uniform_dist(Real{0}, Real{1});

  for (Int round = 0; round < num_rounds; ++round) {
#ifdef CATAMARI_OPENMP
    InitializeMatrix(matrix_size, &matrix);
    OpenMPSampleDPP(tile_size, block_size, maximum_likelihood, &matrix.view,
                    &generator, &uniform_dist, &extra_buffer);
#endif  // ifdef CATAMARI_OPENMP

    InitializeMatrix(matrix_size, &matrix);
    SampleDPP(block_size, maximum_likelihood, &matrix.view, &generator,
              &uniform_dist);
  }
}

}  // anonymous namespace

int main(int argc, char** argv) {
  specify::ArgumentParser parser(argc, argv);
  const Int matrix_size = parser.OptionalInput<Int>(
      "matrix_size", "The dimension of the matrix to factor.", 1000);
  const Int tile_size = parser.OptionalInput<Int>(
      "tile_size", "The tile size for multithreaded factorization.", 128);
  const Int block_size = parser.OptionalInput<Int>(
      "block_size", "The block_size for dense factorization.", 64);
  const Int num_rounds = parser.OptionalInput<Int>(
      "num_rounds", "The number of rounds of factorizations.", 2);
  const unsigned int random_seed = parser.OptionalInput<unsigned int>(
      "random_seed", "The random seed for the DPP.", 17u);
  const bool maximum_likelihood = parser.OptionalInput<bool>(
      "maximum_likelihood", "Take a maximum likelihood DPP sample?", true);
  if (!parser.OK()) {
    return 0;
  }

  std::cout << "Single-precision:" << std::endl;
  RunDPPTests<Complex<float>>(maximum_likelihood, matrix_size, block_size,
                              tile_size, num_rounds, random_seed);
  std::cout << std::endl;

  std::cout << "Double-precision:" << std::endl;
  RunDPPTests<Complex<double>>(maximum_likelihood, matrix_size, block_size,
                               tile_size, num_rounds, random_seed);

  return 0;
}
