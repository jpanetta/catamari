/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
// Samples a dense, nonsymmetric Determinantal Point Process defining a uniform
// distribution over domino tilings of the Aztec diamond. Please see the review
// paper:
//
//   Sunil Chhita, Kurt Johansson, and Benjamin Young,
//   "Asymptotic Domino Statistics in the Aztec Diamond",
//   Annals of Applied Probability, 25 (3), pp. 1232--1278, 2015.
//
// Note that there appears to be a mising negative sign on the down-left edge
// from 'b_0' in Fig. 4 of said publication.
//
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <typeinfo>
#include <vector>

#include "catamari/blas_matrix.hpp"
#include "catamari/dense_factorizations.hpp"
#include "catamari/io_utils.hpp"
#include "catamari/ldl.hpp"
#include "quotient/timer.hpp"
#include "specify.hpp"

// Inverting a complex symmetric matrix in place is an integral part of this
// driver, and so the entire driver is disabled if LAPACK support was not
// detected.
#ifdef CATAMARI_HAVE_LAPACK

using catamari::BlasMatrix;
using catamari::BlasMatrixView;
using catamari::Complex;
using catamari::ComplexBase;
using catamari::CoordinateMatrix;
using catamari::Int;
using quotient::Buffer;

extern "C" {

void LAPACK_SYMBOL(cgetrf)(const BlasInt* height, const BlasInt* width,
                           BlasComplexFloat* matrix, const BlasInt* leading_dim,
                           BlasInt* pivots, BlasInt* info);

void LAPACK_SYMBOL(zgetrf)(const BlasInt* height, const BlasInt* width,
                           BlasComplexDouble* matrix,
                           const BlasInt* leading_dim, BlasInt* pivots,
                           BlasInt* info);

void LAPACK_SYMBOL(cgetri)(const BlasInt* height, BlasComplexFloat* matrix,
                           const BlasInt* leading_dim, const BlasInt* pivots,
                           BlasComplexFloat* work, const BlasInt* work_size,
                           BlasInt* info);

void LAPACK_SYMBOL(zgetri)(const BlasInt* height, BlasComplexDouble* matrix,
                           const BlasInt* leading_dim, const BlasInt* pivots,
                           BlasComplexDouble* work, const BlasInt* work_size,
                           BlasInt* info);

}  // extern "C"

namespace {

#ifdef CATAMARI_HAVE_LIBTIFF
// Returns whether the given value exists within a sorted list.
template <typename Iterator, typename T>
bool IndexExists(Iterator beg, Iterator end, T value) {
  auto iter = std::lower_bound(beg, end, value);
  return iter != end && *iter == value;
}

#endif  // ifdef CATAMARI_HAVE_LIBTIFF

template <typename Field>
std::vector<Int> SampleNonsymmetricDPP(Int block_size, bool maximum_likelihood,
                                       BlasMatrixView<Field>* matrix,
                                       std::mt19937* generator) {
  const bool is_complex = catamari::IsComplex<Field>::value;
  const Int matrix_size = matrix->height;
  quotient::Timer timer;
  timer.Start();

  const std::vector<Int> sample = LowerFactorAndSampleNonsymmetricDPP(
      block_size, maximum_likelihood, matrix, generator);

  const double runtime = timer.Stop();
  const double flops =
      (is_complex ? 4 : 1) * 2 * std::pow(1. * matrix_size, 3.) / 3.;
  const double gflops_per_sec = flops / (1.e9 * runtime);
  std::cout << "Sequential DPP GFlop/s: " << gflops_per_sec << std::endl;

  return sample;
}

#ifdef CATAMARI_OPENMP
template <typename Field>
std::vector<Int> OpenMPSampleNonsymmetricDPP(Int tile_size, Int block_size,
                                             bool maximum_likelihood,
                                             BlasMatrixView<Field>* matrix,
                                             std::mt19937* generator) {
  const bool is_complex = catamari::IsComplex<Field>::value;
  const Int matrix_size = matrix->height;
  quotient::Timer timer;
  timer.Start();

  const int old_max_threads = catamari::GetMaxBlasThreads();
  catamari::SetNumBlasThreads(1);

  std::vector<Int> sample;
  #pragma omp parallel
  #pragma omp single
  sample = OpenMPLowerFactorAndSampleNonsymmetricDPP(
      tile_size, block_size, maximum_likelihood, matrix, generator);

  catamari::SetNumBlasThreads(old_max_threads);

  const double runtime = timer.Stop();
  const double flops =
      (is_complex ? 4 : 1) * 2 * std::pow(1. * matrix_size, 3.) / 3.;
  const double gflops_per_sec = flops / (1.e9 * runtime);
  std::cout << "OpenMP DPP GFlop/s: " << gflops_per_sec << std::endl;

  return sample;
}
#endif  // ifdef CATAMARI_OPENMP

// Constructs the Kasteleyn matrix for an Aztec diamond.
template <typename Real>
void KasteleynMatrix(Int diamond_size,
                     CoordinateMatrix<Complex<Real>>* matrix) {
  const Int i1_length = diamond_size + 1;
  const Int i2_length = diamond_size;
  const Int num_vertices = i1_length * i2_length;
  matrix->Resize(num_vertices, num_vertices);
  matrix->ReserveEntryAdditions(4 * num_vertices);

  // Iterate over the coordinates of the black vertices.
  for (Int i1 = 0; i1 < i1_length; ++i1) {
    for (Int i2 = 0; i2 < i2_length; ++i2) {
      const bool negate = (i1 + i2) % 2;
      const Real scale = negate ? Real{-1} : Real{1};
      const Int black_index = i1 + i2 * i1_length;

      if (i1 > 0) {
        // Connect down and left (via -e1) to (i2, i1-1) in the white
        // coordinates.
        const Int white_dl_index = i2 + (i1 - 1) * i1_length;
        matrix->QueueEntryAddition(black_index, white_dl_index, -scale);

        // Connect up and left (via -e2) to (i2+1, i1-1) in the white
        // coordinates.
        const Int white_ul_index = (i2 + 1) + (i1 - 1) * i1_length;
        matrix->QueueEntryAddition(black_index, white_ul_index,
                                   Complex<Real>(0, scale));
      }

      if (i1 < diamond_size) {
        // Connect up and right (via e1) to (i2+1, i1) in the white
        // coordinates.
        const Int white_ur_index = (i2 + 1) + i1 * i1_length;
        matrix->QueueEntryAddition(black_index, white_ur_index, scale);

        // Connect down and right (via e2) to (i2, i1) in the white coordinates.
        const Int white_dr_index = i2 + i1 * i1_length;
        matrix->QueueEntryAddition(black_index, white_dr_index,
                                   Complex<Real>(0, -scale));
      }
    }
  }

  matrix->FlushEntryQueues();
}

// Fills a dense matrix from a sparse matrix.
template <typename Field>
void ConvertToDense(const CoordinateMatrix<Field>& sparse_matrix,
                    BlasMatrix<Field>* matrix) {
  const Int num_rows = sparse_matrix.NumRows();
  const Int num_cols = sparse_matrix.NumColumns();

  matrix->Resize(num_rows, num_cols, Field{0});
  for (const catamari::MatrixEntry<Field>& entry : sparse_matrix.Entries()) {
    matrix->Entry(entry.row, entry.column) = entry.value;
  }
}

// Inverts a single-precision complex dense matrix in-place.
void InvertMatrix(BlasMatrix<Complex<float>>* matrix) {
  const BlasInt num_rows = matrix->view.height;
  const BlasInt leading_dim = matrix->view.leading_dim;

  Buffer<BlasInt> pivots(num_rows);

  const BlasInt work_size = 128 * num_rows;
  Buffer<Complex<float>> work(work_size);

  BlasInt info;

  LAPACK_SYMBOL(cgetrf)
  (&num_rows, &num_rows, matrix->view.data, &leading_dim, pivots.Data(), &info);
  if (info) {
    std::cerr << "Exited cgetrf with info=" << info << std::endl;
    return;
  }

  LAPACK_SYMBOL(cgetri)
  (&num_rows, matrix->view.data, &leading_dim, pivots.Data(), work.Data(),
   &work_size, &info);
  if (info) {
    std::cerr << "Exited cgetri with info=" << info << std::endl;
  }
}

// Inverts a double-precision complex dense matrix in-place.
void InvertMatrix(BlasMatrix<Complex<double>>* matrix) {
  const BlasInt num_rows = matrix->view.height;
  const BlasInt leading_dim = matrix->view.leading_dim;

  Buffer<BlasInt> pivots(num_rows);

  const BlasInt work_size = 128 * num_rows;
  Buffer<Complex<double>> work(work_size);

  BlasInt info;

  LAPACK_SYMBOL(zgetrf)
  (&num_rows, &num_rows, matrix->view.data, &leading_dim, pivots.Data(), &info);
  if (info) {
    std::cerr << "Exited zgetrf with info=" << info << std::endl;
    return;
  }

  LAPACK_SYMBOL(zgetri)
  (&num_rows, matrix->view.data, &leading_dim, pivots.Data(), work.Data(),
   &work_size, &info);
  if (info) {
    std::cerr << "Exited zgetri with info=" << info << std::endl;
  }
}

// Forms the dense inverse of the Kasteleyn matrix.
// Note that we are not free to use a naive LDL^T sparse-direct factorization,
// as there are several zero diagonal entries in the Kasteleyn matrix.
template <typename Real>
void InvertKasteleynMatrix(
    const CoordinateMatrix<Complex<Real>> kasteleyn_matrix,
    BlasMatrix<Complex<Real>>* inverse_kasteleyn_matrix) {
  ConvertToDense(kasteleyn_matrix, inverse_kasteleyn_matrix);
  InvertMatrix(inverse_kasteleyn_matrix);
}

template <typename Real>
void KenyonMatrix(Int diamond_size,
                  const CoordinateMatrix<Complex<Real>>& kasteleyn_matrix,
                  const BlasMatrix<Complex<Real>>& inverse_kasteleyn_matrix,
                  BlasMatrix<Complex<Real>>* kenyon_matrix) {
  const Int num_edges = 4 * diamond_size * diamond_size;
  const Int i1_length = diamond_size + 1;

  BlasMatrix<Complex<Real>> dense_kasteleyn;
  ConvertToDense(kasteleyn_matrix, &dense_kasteleyn);

  /*
    We can group the edges into diamonds in the form:

      w
     / \
    b   b,
     \ /
      w


   starting in the bottom-left and working rightward and then upward. Within
   each diamond, we order the edges as


        w
       / \
      2   3
     /     \
    b       b.
     \     /
      0   1
       \ /
        w
  */

  // The Kenyon matrix has entries of the form:
  //
  //   L(e_i, e_j) = sqrt(K(b_i, w_i)) inv(K)(w_j, b_i) sqrt(K(b_j, w_j)).
  //
  kenyon_matrix->Resize(num_edges, num_edges);
  for (Int i1 = 0; i1 < diamond_size; ++i1) {
    for (Int i2 = 0; i2 < diamond_size; ++i2) {
      const Int i_tile_offset = 4 * (i1 + i2 * diamond_size);
      const Int i_black_left = i1 + i2 * i1_length;
      const Int i_black_right = (i1 + 1) + i2 * i1_length;
      const Int i_white_bottom = i2 + i1 * i1_length;
      const Int i_white_top = (i2 + 1) + i1 * i1_length;

      const Int i_blacks[4] = {i_black_left, i_black_right, i_black_left,
                               i_black_right};
      const Int i_whites[4] = {i_white_bottom, i_white_bottom, i_white_top,
                               i_white_top};

      for (Int j2 = 0; j2 < diamond_size; ++j2) {
        for (Int j1 = 0; j1 < diamond_size; ++j1) {
          const Int j_tile_offset = 4 * (j1 + j2 * diamond_size);
          const Int j_white_bottom = j2 + j1 * i1_length;
          const Int j_white_top = (j2 + 1) + j1 * i1_length;

          const Int j_whites[4] = {j_white_bottom, j_white_bottom, j_white_top,
                                   j_white_top};

          for (Int i_edge = 0; i_edge < 4; ++i_edge) {
            const Complex<Real> i_kasteleyn_value =
                dense_kasteleyn(i_blacks[i_edge], i_whites[i_edge]);
            for (Int j_edge = 0; j_edge < 4; ++j_edge) {
              const Complex<Real> inverse_kasteleyn_value =
                  inverse_kasteleyn_matrix(j_whites[j_edge], i_blacks[i_edge]);
              kenyon_matrix->Entry(i_tile_offset + i_edge,
                                   j_tile_offset + j_edge) =
                  i_kasteleyn_value * inverse_kasteleyn_value;
            }
          }
        }
      }
    }
  }
}

// Prints the sample to the console.
void PrintSample(const std::vector<Int>& sample) {
  std::ostringstream os;
  os << "Sample: ";
  for (const Int& index : sample) {
    os << index << " ";
  }
  os << std::endl;
  std::cout << os.str();
}

// Samples the Aztec diamond domino tiling a requested number of times using
// both
// sequential and OpenMP-parallelized algorithms.
template <typename Real>
void DominoTilings(bool maximum_likelihood, Int diamond_size, Int block_size,
                   Int tile_size, Int num_rounds, unsigned int random_seed,
                   bool write_tiff) {
  CoordinateMatrix<Complex<Real>> kasteleyn_matrix;
  KasteleynMatrix(diamond_size, &kasteleyn_matrix);

  BlasMatrix<Complex<Real>> inverse_kasteleyn_matrix;
  InvertKasteleynMatrix(kasteleyn_matrix, &inverse_kasteleyn_matrix);

  BlasMatrix<Complex<Real>> kenyon_matrix;
  KenyonMatrix(diamond_size, kasteleyn_matrix, inverse_kasteleyn_matrix,
               &kenyon_matrix);

  const Int expected_sample_size = diamond_size * (diamond_size + 1);

  std::mt19937 generator(random_seed);
  BlasMatrix<Complex<Real>> kenyon_copy;
  for (Int round = 0; round < num_rounds; ++round) {
#ifdef CATAMARI_OPENMP
    // Sample using the OpenMP DPP sampler.
    kenyon_copy = kenyon_matrix;
    const std::vector<Int> omp_sample =
        OpenMPSampleNonsymmetricDPP(tile_size, block_size, maximum_likelihood,
                                    &kenyon_copy.view, &generator);
    if (Int(omp_sample.size()) != expected_sample_size) {
      std::cerr << "ERROR: Sampled " << omp_sample.size() << " instead of "
                << expected_sample_size << " dimers." << std::endl;
    }
#ifdef CATAMARI_HAVE_LIBTIFF
    if (write_tiff) {
      const std::string tag = std::string("omp-") + typeid(Real).name();
      // TODO(Jack Poulson): Write sample to TIFF using N/S/E/W dimer coloring.
    }
#endif  // ifdef CATAMARI_HAVE_LIBTIFF
    PrintSample(omp_sample);
#endif  // ifdef CATAMARI_OPENMP

    // Sample using the sequential DPP sampler.
    kenyon_copy = kenyon_matrix;
    const std::vector<Int> sample = SampleNonsymmetricDPP(
        block_size, maximum_likelihood, &kenyon_copy.view, &generator);
    if (Int(sample.size()) != expected_sample_size) {
      std::cerr << "ERROR: Sampled " << sample.size() << " instead of "
                << expected_sample_size << " dimers." << std::endl;
    }
    if (write_tiff) {
      const std::string tag = typeid(Real).name();
      // TODO(Jack Poulson): Write sample to TIFF using N/S/E/W dimer coloring.
    }
    PrintSample(sample);
  }
}

}  // anonymous namespace

int main(int argc, char** argv) {
  specify::ArgumentParser parser(argc, argv);
  const Int diamond_size = parser.OptionalInput<Int>(
      "diamond_size", "The dimension of the Aztec diamond.", 50);
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
  const bool write_tiff = parser.OptionalInput<bool>(
      "write_tiff", "Write out the results into a TIFF file?", true);
  if (!parser.OK()) {
    return 0;
  }

  std::cout << "Single-precision:" << std::endl;
  DominoTilings<float>(maximum_likelihood, diamond_size, block_size, tile_size,
                       num_rounds, random_seed, write_tiff);
  std::cout << std::endl;

  std::cout << "Double-precision:" << std::endl;
  DominoTilings<double>(maximum_likelihood, diamond_size, block_size, tile_size,
                        num_rounds, random_seed, write_tiff);

  return 0;
}
#else
int main(int CATAMARI_UNUSED argc, char* CATAMARI_UNUSED argv[]) { return 0; }
#endif  // ifdef CATAMARI_HAVE_LAPACK