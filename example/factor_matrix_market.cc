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

#include "catamari/apply_sparse.hpp"
#include "catamari/ldl.hpp"
#include "specify.hpp"

using catamari::BlasMatrix;
using catamari::Buffer;
using catamari::ConstBlasMatrix;
using catamari::Int;

// A list of properties to measure from a sparse LDL factorization / solve.
struct Experiment {
  // The number of (structural) nonzeros in the associated Cholesky factor.
  Int num_nonzeros = 0;

  // The number of floating-point operations required for a standard Cholesky
  // factorization using the returned ordering.
  double num_flops = 0;

  // The number of seconds that elapsed during the factorization.
  double factorization_seconds = 0;

  // The number of seconds that elapsed during the solve.
  double solve_seconds = 0;
};

// Pretty prints the Experiment structure.
void PrintExperiment(const Experiment& experiment, const std::string& label) {
  std::cout << label << ":\n";
  std::cout << "  num_nonzeros:          " << experiment.num_nonzeros << "\n";
  std::cout << "  num_flops:             " << experiment.num_flops << "\n";
  std::cout << "  factorization_seconds: " << experiment.factorization_seconds
            << "\n";
  std::cout << "  solve_seconds:         " << experiment.solve_seconds << "\n";
  std::cout << std::endl;
}

// Returns the Frobenius norm of a real matrix.
// NOTE: Due to the direct accumulation of the squared norm, this algorithm is
// unstable. But it suffices for example purposes.
template <typename Real>
Real EuclideanNorm(const ConstBlasMatrix<Real>& matrix) {
  Real squared_norm{0};
  const Int height = matrix.height;
  const Int width = matrix.width;
  for (Int j = 0; j < width; ++j) {
    for (Int i = 0; i < height; ++i) {
      squared_norm += matrix(i, j) * matrix(i, j);
    }
  }
  return std::sqrt(squared_norm);
}

// Returns the Frobenius norm of a complex vector.
// NOTE: Due to the direct accumulation of the squared norm, this algorithm is
// unstable. But it suffices for example purposes.
template <typename Real>
Real EuclideanNorm(const ConstBlasMatrix<catamari::Complex<Real>>& matrix) {
  Real squared_norm{0};
  const Int height = matrix.height;
  const Int width = matrix.width;
  for (Int j = 0; j < width; ++j) {
    for (Int i = 0; i < height; ++i) {
      squared_norm += std::norm(matrix(i, j));
    }
  }
  return std::sqrt(squared_norm);
}

// Overwrites a matrix A with A + A' or A + A^T.
template <typename Field>
void MakeSymmetric(catamari::CoordinateMatrix<Field>* matrix, bool hermitian) {
  matrix->ReserveEntryAdditions(matrix->NumEntries());
  for (const catamari::MatrixEntry<Field>& entry : matrix->Entries()) {
    if (hermitian) {
      matrix->QueueEntryAddition(entry.column, entry.row,
                                 catamari::Conjugate(entry.value));
    } else {
      matrix->QueueEntryAddition(entry.column, entry.row, entry.value);
    }
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

// With a sufficiently large choice of 'diagonal_shift', this routine returns
// a symmetric positive-definite sparse matrix corresponding to the Matrix
// Market example living in the given file.
template <typename Field>
std::unique_ptr<catamari::CoordinateMatrix<Field>> LoadMatrix(
    const std::string& filename, bool skip_explicit_zeros,
    quotient::EntryMask mask, bool force_symmetry, bool hermitian,
    const Field& diagonal_shift, bool print_progress) {
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
    MakeSymmetric(matrix.get(), hermitian);
  }

  // Adding the diagonal shift.
  AddDiagonalShift(diagonal_shift, matrix.get());

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
ConstBlasMatrix<Field> GenerateRightHandSide(Int num_rows,
                                             Buffer<Field>* buffer) {
  BlasMatrix<Field> right_hand_side;
  right_hand_side.height = num_rows;
  right_hand_side.width = 1;
  right_hand_side.leading_dim = std::max(num_rows, Int(1));
  buffer->Clear();
  buffer->Resize(right_hand_side.leading_dim * right_hand_side.width);
  right_hand_side.data = buffer->Data();

  for (Int row = 0; row < num_rows; ++row) {
    right_hand_side(row, 0) = row % 5;
  }

  return right_hand_side.ToConst();
}

template <typename Field>
BlasMatrix<Field> CopyMatrix(const ConstBlasMatrix<Field>& matrix,
                             Buffer<Field>* buffer) {
  BlasMatrix<Field> matrix_copy;
  matrix_copy.height = matrix.height;
  matrix_copy.width = matrix.width;
  matrix_copy.leading_dim = std::max(matrix.height, Int(1));
  buffer->Clear();
  buffer->Resize(matrix_copy.leading_dim * matrix_copy.width);
  matrix_copy.data = buffer->Data();
  for (Int j = 0; j < matrix.width; ++j) {
    for (Int i = 0; i < matrix.height; ++i) {
      matrix_copy(i, j) = matrix(i, j);
    }
  }
  return matrix_copy;
}

// Returns the Experiment statistics for a single Matrix Market input matrix.
Experiment RunMatrixMarketTest(const std::string& filename,
                               bool skip_explicit_zeros,
                               quotient::EntryMask mask, bool force_symmetry,
                               double diagonal_shift,
                               const catamari::LDLControl& ldl_control,
                               bool print_progress) {
  typedef double Field;
  typedef catamari::ComplexBase<Field> BaseField;
  Experiment experiment;

  // Read the matrix from file.
  const bool hermitian = ldl_control.supernodal_control.factorization_type !=
                         catamari::kLDLTransposeFactorization;
  std::unique_ptr<catamari::CoordinateMatrix<Field>> matrix =
      LoadMatrix(filename, skip_explicit_zeros, mask, force_symmetry, hermitian,
                 diagonal_shift, print_progress);
  if (!matrix) {
    return experiment;
  }
  const Int num_rows = matrix->NumRows();

  // Factor the matrix.
  if (print_progress) {
    std::cout << "  Running factorization..." << std::endl;
  }
  quotient::Timer factorization_timer;
  factorization_timer.Start();
  catamari::LDLFactorization<Field> ldl_factorization;
  const catamari::LDLResult result =
      ldl_factorization.Factor(*matrix, ldl_control);
  experiment.factorization_seconds = factorization_timer.Stop();
  if (result.num_successful_pivots < num_rows) {
    std::cout << "  Failed factorization after " << result.num_successful_pivots
              << " pivots." << std::endl;
    return experiment;
  }
  experiment.num_nonzeros = result.num_factorization_entries;
  experiment.num_flops = result.num_factorization_flops;

  // Generate an arbitrary right-hand side.
  Buffer<Field> right_hand_side_buffer;
  const ConstBlasMatrix<Field> right_hand_side =
      GenerateRightHandSide<Field>(num_rows, &right_hand_side_buffer);
  const BaseField right_hand_side_norm = EuclideanNorm(right_hand_side);
  if (print_progress) {
    std::cout << "  || b ||_F = " << right_hand_side_norm << std::endl;
  }

  // Solve a random linear system.
  if (print_progress) {
    std::cout << "  Running solve..." << std::endl;
  }
  Buffer<Field> solution_buffer;
  BlasMatrix<Field> solution = CopyMatrix(right_hand_side, &solution_buffer);
  quotient::Timer solve_timer;
  solve_timer.Start();
  ldl_factorization.Solve(&solution);
  experiment.solve_seconds = solve_timer.Stop();

  // Compute the residual.
  Buffer<Field> residual_buffer;
  BlasMatrix<Field> residual = CopyMatrix(right_hand_side, &residual_buffer);
  catamari::ApplySparse(Field{-1}, *matrix, solution.ToConst(), Field{1},
                        &residual);
  const BaseField residual_norm = EuclideanNorm(residual.ToConst());
  std::cout << "  || B - A X ||_F / || B ||_F = "
            << residual_norm / right_hand_side_norm << std::endl;

  return experiment;
}

// Returns a map from the identifying string of each test matrix from the
// Amestoy/Davis/Duff Approximate Minimum Degree reordering 1996 paper meant
// to loosely reproduce Fig. 2.
std::unordered_map<std::string, Experiment> RunADD96Tests(
    const std::string& matrix_market_directory, bool skip_explicit_zeros,
    quotient::EntryMask mask, double diagonal_shift,
    const catamari::LDLControl& ldl_control, bool print_progress) {
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
        RunMatrixMarketTest(filename, skip_explicit_zeros, mask, force_symmetry,
                            diagonal_shift, ldl_control, print_progress);
  }

  return experiments;
}

int main(int argc, char** argv) {
  typedef double Field;
  typedef catamari::ComplexBase<Field> BaseField;

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
  const bool force_symmetry = parser.OptionalInput<bool>(
      "force_symmetry", "Use the nonzero pattern of A + A'?", true);
  const int factorization_type_int =
      parser.OptionalInput<int>("factorization_type_int",
                                "Type of the factorization.\n"
                                "0:Cholesky, 1:LDL', 2:LDL^T",
                                1);
  const int supernodal_strategy_int =
      parser.OptionalInput<int>("supernodal_strategy_int",
                                "The SupernodalStrategy int.\n"
                                "0:scalar, 1:supernodal, 2:adaptive",
                                2);
  const bool relax_supernodes = parser.OptionalInput<bool>(
      "relax_supernodes", "Relax the supernodes?", true);
  const Int allowable_supernode_zeros =
      parser.OptionalInput<Int>("allowable_supernode_zeros",
                                "Number of zeros allowed in relaxations.", 128);
  const float allowable_supernode_zero_ratio = parser.OptionalInput<float>(
      "allowable_supernode_zero_ratio",
      "Ratio of explicit zeros allowed in a relaxed supernode.", 0.01f);
  const double diagonal_shift = parser.OptionalInput<BaseField>(
      "diagonal_shift", "The value to add to the diagonal.", 1e6);
  const int ldl_algorithm_int =
      parser.OptionalInput<int>("ldl_algorithm_int",
                                "The LDL algorithm type.\n"
                                "0:left-looking, 1:up-looking, 2:right-looking",
                                2);
  const bool print_progress = parser.OptionalInput<bool>(
      "print_progress", "Print the progress of the experiments?", false);
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

  catamari::LDLControl ldl_control;
  ldl_control.md_control.degree_type =
      static_cast<quotient::DegreeType>(degree_type_int);
  ldl_control.md_control.aggressive_absorption = aggressive_absorption;
  ldl_control.md_control.min_dense_threshold = min_dense_threshold;
  ldl_control.md_control.dense_sqrt_multiple = dense_sqrt_multiple;
  ldl_control.supernodal_strategy =
      static_cast<catamari::SupernodalStrategy>(supernodal_strategy_int);
  ldl_control.scalar_control.factorization_type =
      static_cast<catamari::SymmetricFactorizationType>(factorization_type_int);
  ldl_control.scalar_control.algorithm =
      static_cast<catamari::LDLAlgorithm>(ldl_algorithm_int);
  ldl_control.supernodal_control.factorization_type =
      static_cast<catamari::SymmetricFactorizationType>(factorization_type_int);
  ldl_control.supernodal_control.algorithm =
      static_cast<catamari::LDLAlgorithm>(ldl_algorithm_int);
  ldl_control.supernodal_control.relaxation_control.relax_supernodes =
      relax_supernodes;
  ldl_control.supernodal_control.relaxation_control.allowable_supernode_zeros =
      allowable_supernode_zeros;
  ldl_control.supernodal_control.relaxation_control
      .allowable_supernode_zero_ratio = allowable_supernode_zero_ratio;

  if (!matrix_market_directory.empty()) {
    const std::unordered_map<std::string, Experiment> experiments =
        RunADD96Tests(matrix_market_directory, skip_explicit_zeros, mask,
                      diagonal_shift, ldl_control, print_progress);
    for (const std::pair<std::string, Experiment>& pairing : experiments) {
      PrintExperiment(pairing.second, pairing.first);
    }
  } else {
    const Experiment experiment =
        RunMatrixMarketTest(filename, skip_explicit_zeros, mask, force_symmetry,
                            diagonal_shift, ldl_control, print_progress);
    PrintExperiment(experiment, filename);
  }

  return 0;
}
