/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

#include "catamari/supernodal_dpp.hpp"
#include "quotient/io_utils.hpp"
#include "quotient/minimum_degree.hpp"
#include "specify.hpp"

using catamari::Int;

// A list of properties to measure from DPP sampling.
struct Experiment {
  // The number of seconds that elapsed during the sample.
  double sample_seconds = 0;
};

// Pretty prints the Experiment structure.
void PrintExperiment(const Experiment& experiment, const std::string& label) {
  std::cout << label << ":\n";
  std::cout << "  sample_seconds: " << experiment.sample_seconds << "\n";
  std::cout << std::endl;
}

template <typename Real>
Real EuclideanNorm(const std::vector<Real>& vector) {
  Real squared_norm{0};
  const Int num_rows = vector.size();
  for (Int i = 0; i < num_rows; ++i) {
    squared_norm += vector[i] * vector[i];
  }
  return std::sqrt(squared_norm);
}

template <typename Real>
Real EuclideanNorm(const std::vector<catamari::Complex<Real>>& vector) {
  Real squared_norm{0};
  const Int num_rows = vector.size();
  for (Int i = 0; i < num_rows; ++i) {
    squared_norm += std::norm(vector[i]);
  }
  return std::sqrt(squared_norm);
}

template <typename Real>
Real EuclideanNorm(const catamari::CoordinateMatrix<Real>& matrix) {
  Real squared_norm{0};
  for (const catamari::MatrixEntry<Real>& entry : matrix.Entries()) {
    squared_norm += entry.value * entry.value;
  }
  return std::sqrt(squared_norm);
}

template <typename Real>
Real EuclideanNorm(
    const catamari::CoordinateMatrix<catamari::Complex<Real>>& matrix) {
  Real squared_norm{0};
  for (const catamari::MatrixEntry<catamari::Complex<Real>>& entry :
       matrix.Entries()) {
    squared_norm += std::norm(entry.value);
  }
  return std::sqrt(squared_norm);
}

// Overwrites a matrix A with A + A'.
template <typename Field>
void MakeHermitian(catamari::CoordinateMatrix<Field>* matrix) {
  matrix->ReserveEntryAdditions(matrix->NumEntries());
  for (const catamari::MatrixEntry<Field>& entry : matrix->Entries()) {
    matrix->QueueEntryAddition(entry.column, entry.row,
                               catamari::Conjugate(entry.value));
  }
  matrix->FlushEntryQueues();
}

// Adds diagonal_shift to each entry of the diagonal.
template <typename Field>
void AddDiagonalShift(Field diagonal_shift,
                      catamari::CoordinateMatrix<Field>* matrix) {
  const Int num_rows = matrix->NumRows();
  matrix->ReserveEntryAdditions(num_rows);
  for (Int i = 0; i < num_rows; ++i) {
    matrix->QueueEntryAddition(i, i, diagonal_shift);
  }
  matrix->FlushEntryQueues();
}

template <typename Field>
void Rescale(const catamari::ComplexBase<Field>& scale,
             catamari::CoordinateMatrix<Field>* matrix) {
  for (catamari::MatrixEntry<Field>& entry : matrix->Entries()) {
    entry.value *= scale;
  }
}

// Returns the densest row index and the number of entries in the row.
template <typename Field>
std::pair<Int, Int> DensestRow(
    const catamari::CoordinateMatrix<Field>& matrix) {
  Int densest_row_size = 0;
  Int densest_row_index = -1;
  for (Int i = 0; i < matrix.NumRows(); ++i) {
    if (matrix.NumRowNonzeros(i) > densest_row_size) {
      densest_row_size = matrix.NumRowNonzeros(i);
      densest_row_index = i;
    }
  }
  return std::make_pair(densest_row_size, densest_row_index);
}

template <typename Field>
std::unique_ptr<catamari::CoordinateMatrix<Field>> LoadMatrix(
    const std::string& filename, bool skip_explicit_zeros,
    quotient::EntryMask mask, bool force_symmetry, const Field& diagonal_shift,
    bool print_progress) {
  typedef catamari::ComplexBase<Field> Real;

  if (print_progress) {
    std::cout << "Reading CoordinateGraph from " << filename << "..."
              << std::endl;
  }
  std::unique_ptr<catamari::CoordinateMatrix<Field>> matrix =
      catamari::CoordinateMatrix<Field>::FromMatrixMarket(
          filename, skip_explicit_zeros, mask);
  if (!matrix) {
    std::cerr << "Could not open " << filename << "." << std::endl;
    return matrix;
  }

  // Force symmetry since many of the examples are not.
  if (force_symmetry) {
    if (print_progress) {
      std::cout << "Enforcing matrix Hermiticity..." << std::endl;
    }
    MakeHermitian(matrix.get());
  }

  // Adding the diagonal shift.
  AddDiagonalShift(diagonal_shift, matrix.get());

  const Real frob_norm = EuclideanNorm(*matrix);
  Rescale(Real{1} / frob_norm, matrix.get());

  if (print_progress) {
    std::cout << "Matrix had " << matrix->NumRows() << " rows and "
              << matrix->NumEntries() << " entries." << std::endl;

    std::pair<Int, Int> densest_row_info = DensestRow(*matrix);
    std::cout << "Densest row is index " << densest_row_info.first << " with "
              << densest_row_info.second << " connections." << std::endl;
  }
  return matrix;
}

template <typename Field>
std::vector<Field> GenerateRightHandSide(Int num_rows) {
  std::vector<Field> right_hand_side(num_rows, 0);
  for (Int row = 0; row < num_rows; ++row) {
    right_hand_side[row] = row % 5;
  }
  return right_hand_side;
}

// Returns the Experiment statistics for a single Matrix Market input matrix.
Experiment RunMatrixMarketTest(
    const std::string& filename, bool skip_explicit_zeros,
    quotient::EntryMask mask, const quotient::MinimumDegreeControl& md_control,
    bool disable_reordering, bool force_symmetry, double diagonal_shift,
    const catamari::SupernodalDPPControl& dpp_control, bool print_progress,
    bool write_permuted_matrix) {
  typedef double Field;
  Experiment experiment;

  // Read the matrix from file.
  std::unique_ptr<catamari::CoordinateMatrix<Field>> matrix =
      LoadMatrix(filename, skip_explicit_zeros, mask, force_symmetry,
                 diagonal_shift, print_progress);
  if (!matrix) {
    return experiment;
  }
  const Int num_rows = matrix->NumRows();

  // TODO(Jack Poulson): Compute the AMD reordering.
  std::unique_ptr<quotient::CoordinateGraph> graph = matrix->CoordinateGraph();
  const quotient::MinimumDegreeResult analysis =
      quotient::MinimumDegree(*graph, md_control);
  graph.reset();
  const std::vector<Int> permutation = analysis.Permutation();

  std::vector<Int> inverse_permutation(num_rows);
  for (Int row = 0; row < num_rows; ++row) {
    inverse_permutation[permutation[row]] = row;
  }

  // TODO(Jack Poulson): Get a better seed.
  unsigned int random_seed = 17;

  catamari::SupernodalDPP<double> dpp(*matrix, permutation, inverse_permutation,
                                      dpp_control, random_seed);

  // Sample the matrix.
  if (print_progress) {
    std::cout << "  Sampling DPP..." << std::endl;
  }
  quotient::Timer sample_timer;
  sample_timer.Start();
  std::vector<quotient::Int> sample = dpp.Sample();
  quotient::PrintVector(sample, "sample", std::cout);
  experiment.sample_seconds = sample_timer.Stop();

  return experiment;
}

// Returns a map from the identifying string of each test matrix from the
// Amestoy/Davis/Duff Approximate Minimum Degree reordering 1996 paper meant
// to loosely reproduce Fig. 2.
//
// It is worth noting that the LHR34 results from the paper appear to be
// incorrect, as the results shown in
//
//   https://www.cise.ufl.edu/research/sparse/matrices/Mallya/lhr34.html
//
// agree with the results observed from this code's implementation.
//
std::unordered_map<std::string, Experiment> RunADD96Tests(
    const std::string& matrix_market_directory, bool skip_explicit_zeros,
    quotient::EntryMask mask, const quotient::MinimumDegreeControl& md_control,
    bool disable_reordering, double diagonal_shift,
    const catamari::SupernodalDPPControl& dpp_control, bool print_progress,
    bool write_permuted_matrix) {
  const std::vector<std::string> matrix_names{
      "appu",     "bbmat",    "bcsstk30", "bcsstk31", "bcsstk32", "bcsstk33",
      "crystk02", "crystk03", "ct20stif", "ex11",     "ex19",     "ex40",
      "finan512", "lhr34",    "lhr71",    "nasasrb",  "olafu",    "orani678",
      "psmigr_1", "raefsky1", "raefsky3", "raefsky4", "rim",      "venkat01",
      "wang3",    "wang4",
  };
  const bool force_symmetry = true;

  std::unordered_map<std::string, Experiment> experiments;
  for (const std::string& matrix_name : matrix_names) {
    const std::string filename = matrix_market_directory + "/" + matrix_name +
                                 "/" + matrix_name + ".mtx";
    experiments[matrix_name] =
        RunMatrixMarketTest(filename, skip_explicit_zeros, mask, md_control,
                            disable_reordering, force_symmetry, diagonal_shift,
                            dpp_control, print_progress, write_permuted_matrix);
  }

  return experiments;
}

int main(int argc, char** argv) {
  typedef double Field;
  typedef catamari::ComplexBase<Field> Real;

  specify::ArgumentParser parser(argc, argv);
  const std::string filename = parser.OptionalInput<std::string>(
      "filename", "The location of a Matrix Market file.", "");
  const bool skip_explicit_zeros = parser.OptionalInput<bool>(
      "skip_explicit_zeros", "Skip explicitly zero entries?", true);
  const int entry_mask_int =
      parser.OptionalInput<int>("entry_mask_int",
                                "The quotient::EntryMask integer.\n"
                                "0:full, 1:lower-triangle, 2:upper-triangle",
                                0);
  const int degree_type_int =
      parser.OptionalInput<int>("degree_type_int",
                                "The degree approximation type.\n"
                                "0:exact, 1:Amestoy, 2:Ashcraft, 3:Gilbert",
                                1);
  const bool aggressive_absorption = parser.OptionalInput<bool>(
      "aggressive_absorption", "Eliminate elements with aggressive absorption?",
      true);
  const Int min_dense_threshold = parser.OptionalInput<Int>(
      "min_dense_threshold",
      "Lower-bound on non-diagonal nonzeros for a row to be dense. The actual "
      "threshold will be: "
      "max(min_dense_threshold, dense_sqrt_multiple * sqrt(n))",
      16);
  const float dense_sqrt_multiple = parser.OptionalInput<float>(
      "dense_sqrt_multiple",
      "The multiplier on the square-root of the number of vertices for "
      "determining if a row is dense. The actual threshold will be: "
      "max(min_dense_threshold, dense_sqrt_multiple * sqrt(n))",
      10.f);
  const bool disable_reordering = parser.OptionalInput<bool>(
      "disable_reordering", "Disable the AMD reordering?", false);
  const bool force_symmetry = parser.OptionalInput<bool>(
      "force_symmetry", "Use the nonzero pattern of A + A'?", true);
  const bool relax_supernodes = parser.OptionalInput<bool>(
      "relax_supernodes", "Relax the supernodes?", true);
  const Int allowable_supernode_zeros =
      parser.OptionalInput<Int>("allowable_supernode_zeros",
                                "Number of zeros allowed in relaxations.", 128);
  const float allowable_supernode_zero_ratio = parser.OptionalInput<float>(
      "allowable_supernode_zero_ratio",
      "Ratio of explicit zeros allowed in a relaxed supernode.", 0.01f);
  const double diagonal_shift = parser.OptionalInput<Real>(
      "diagonal_shift", "The value to add to the diagonal.", 1e6);
  const bool print_progress = parser.OptionalInput<bool>(
      "print_progress", "Print the progress of the experiments?", false);
  const bool write_permuted_matrix = parser.OptionalInput<bool>(
      "write_permuted_matrix", "Write the permuted matrix to file?", false);
  const std::string matrix_market_directory = parser.OptionalInput<std::string>(
      "matrix_market_directory",
      "The directory where the ADD96 matrix market .tar.gz's were unpacked",
      "");
#ifdef _OPENMP
  const int num_omp_threads = parser.OptionalInput<int>(
      "num_omp_threads",
      "The desired number of OpenMP threads. Uses default if <= 0.", 1);
#endif
  if (!parser.OK()) {
    return 0;
  }
  if (filename.empty() && matrix_market_directory.empty()) {
    std::cerr << "One of 'filename' or 'matrix_market_directory' must be "
                 "specified.\n"
              << std::endl;
    parser.PrintReport();
    return 0;
  }

  const quotient::EntryMask mask =
      static_cast<quotient::EntryMask>(entry_mask_int);

#ifdef _OPENMP
  if (num_omp_threads > 0) {
    const int max_omp_threads = omp_get_max_threads();
    omp_set_num_threads(num_omp_threads);
    std::cout << "Will use " << num_omp_threads << " of " << max_omp_threads
              << " OpenMP threads." << std::endl;
  } else {
    std::cout << "Will use all " << omp_get_max_threads() << " OpenMP threads."
              << std::endl;
  }
#endif

  quotient::MinimumDegreeControl md_control;
  md_control.degree_type = static_cast<quotient::DegreeType>(degree_type_int);
  md_control.aggressive_absorption = aggressive_absorption;
  md_control.min_dense_threshold = min_dense_threshold;
  md_control.dense_sqrt_multiple = dense_sqrt_multiple;

  catamari::SupernodalDPPControl dpp_control;
  dpp_control.relaxation_control.relax_supernodes = relax_supernodes;
  dpp_control.relaxation_control.allowable_supernode_zeros =
      allowable_supernode_zeros;
  dpp_control.relaxation_control.allowable_supernode_zero_ratio =
      allowable_supernode_zero_ratio;

  if (!matrix_market_directory.empty()) {
    const std::unordered_map<std::string, Experiment> experiments =
        RunADD96Tests(matrix_market_directory, skip_explicit_zeros, mask,
                      md_control, disable_reordering, diagonal_shift,
                      dpp_control, print_progress, write_permuted_matrix);
    for (const std::pair<std::string, Experiment>& pairing : experiments) {
      PrintExperiment(pairing.second, pairing.first);
    }
  } else {
    const Experiment experiment =
        RunMatrixMarketTest(filename, skip_explicit_zeros, mask, md_control,
                            disable_reordering, force_symmetry, diagonal_shift,
                            dpp_control, print_progress, write_permuted_matrix);
    PrintExperiment(experiment, filename);
  }

  return 0;
}