/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_LDL_IMPL_H_
#define CATAMARI_LDL_IMPL_H_

#include "catamari/ldl.hpp"
#include "quotient/io_utils.hpp"

namespace catamari {

template <class Field>
void PrintLowerFactor(const LowerFactor<Field>& lower_factor,
                      const std::string& label, std::ostream& os) {
  const LowerStructure& lower_structure = lower_factor.structure;
  os << label << ":\n";
  const Int num_columns = lower_structure.column_offsets.size() - 1;
  for (Int column = 0; column < num_columns; ++column) {
    const Int column_beg = lower_structure.column_offsets[column];
    const Int column_end = lower_structure.column_offsets[column + 1];
    for (Int index = column_beg; index < column_end; ++index) {
      const Int row = lower_structure.indices[index];
      const Field& value = lower_factor.values[index];
      os << row << " " << column << " " << value << "\n";
    }
  }
  os << std::endl;
}

template <class Field>
void PrintDiagonalFactor(const DiagonalFactor<Field>& diagonal_factor,
                         const std::string& label, std::ostream& os) {
  quotient::PrintVector(diagonal_factor.values, label, os);
}

namespace ldl {

// Computes the elimination forest (via the 'parents' array) and sizes of the
// structures of a scalar (simplicial) LDL' factorization.
//
// Cf. Tim Davis's "LDL"'s symbolic factorization.
template <class Field>
void EliminationForestAndDegrees(const CoordinateMatrix<Field>& matrix,
                                 std::vector<Int>* parents,
                                 std::vector<Int>* degrees) {
  const Int num_rows = matrix.NumRows();

  // Initialize all of the parent indices as unset.
  parents->resize(num_rows, -1);

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // Initialize the number of subdiagonal entries that will be stored into
  // each column.
  degrees->clear();
  degrees->resize(num_rows, 0);

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    pattern_flags[row] = row;

    const Int row_beg = matrix.RowEntryOffset(row);
    const Int row_end = matrix.RowEntryOffset(row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column = entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        break;
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest from index 'column'. Any unset
      // parent pointers can be filled in during the traversal, as the current
      // row index would then be the parent.
      while (pattern_flags[column] != row) {
        // Mark index 'column' as in the pattern of row 'row'.
        pattern_flags[column] = row;
        ++(*degrees)[column];

        if ((*parents)[column] == -1) {
          // This is the first occurrence of 'column' in a row pattern.
          (*parents)[column] = row;
        }

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        column = (*parents)[column];
      }
    }
  }
}

// Fills in the structure indices for the lower factor.
template <class Field>
void FillStructureIndices(const CoordinateMatrix<Field>& matrix,
                          const std::vector<Int>& parents,
                          const std::vector<Int>& degrees,
                          LowerStructure* lower_structure) {
  const Int num_rows = matrix.NumRows();

  // Set up the column offsets and allocate space (initializing the values of
  // the unit-lower and diagonal and all zeros).
  lower_structure->column_offsets.resize(num_rows + 1);
  Int num_entries = 0;
  for (Int column = 0; column < num_rows; ++column) {
    lower_structure->column_offsets[column] = num_entries;
    num_entries += degrees[column];
  }
  lower_structure->column_offsets[num_rows] = num_entries;
  lower_structure->indices.resize(num_entries);

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // A set of pointers for keeping track of where to insert column pattern
  // indices.
  std::vector<Int> column_ptrs(num_rows);

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    pattern_flags[row] = row;
    column_ptrs[row] = lower_structure->column_offsets[row];

    const Int row_beg = matrix.RowEntryOffset(row);
    const Int row_end = matrix.RowEntryOffset(row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column = entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        break;
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest from index 'column'.
      while (pattern_flags[column] != row) {
        // Mark index 'column' as in the pattern of row 'row'.
        pattern_flags[column] = row;
        lower_structure->indices[column_ptrs[column]++] = row;

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        column = parents[column];
      }
    }
  }
}

// Fill the factorization with the nonzeros from the input matrix.
template <class Field>
void FillNonzeros(const CoordinateMatrix<Field>& matrix,
                  LDLFactorization<Field>* factorization) {
  LowerFactor<Field>& lower_factor = factorization->lower_factor;
  LowerStructure& lower_structure = lower_factor.structure;
  DiagonalFactor<Field>& diagonal_factor = factorization->diagonal_factor;
  const Int num_rows = matrix.NumRows();
  const Int num_entries = lower_structure.indices.size();

  lower_factor.values.resize(num_entries, Field{0});
  diagonal_factor.values.resize(num_rows, Field{0});

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int row_beg = matrix.RowEntryOffset(row);
    const Int row_end = matrix.RowEntryOffset(row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column = entry.column;
      if (column == row) {
        diagonal_factor.values[column] = entry.value;
      }
      if (column >= row) {
        break;
      }

      const Int column_beg = lower_structure.column_offsets[column];
      const Int column_end = lower_structure.column_offsets[column + 1];
      Int* iter =
          std::lower_bound(lower_structure.indices.data() + column_beg,
                           lower_structure.indices.data() + column_end, row);
      CATAMARI_ASSERT(iter != lower_structure.indices.data() + column_end,
                      "Exceeded column indices.");
      CATAMARI_ASSERT(*iter == row, "Did not find index.");
      const Int structure_index =
          std::distance(lower_structure.indices.data(), iter);
      lower_factor.values[structure_index] = entry.value;
    }
  }
}

// Fills in the structures of each column of the lower-triangular Cholesky
// factor.
template <class Field>
void InitializeLeftLookingFactors(const CoordinateMatrix<Field>& matrix,
                                  const std::vector<Int>& parents,
                                  const std::vector<Int>& degrees,
                                  LDLFactorization<Field>* factorization) {
  FillStructureIndices(
      matrix, parents, degrees, &factorization->lower_factor.structure);
  FillNonzeros(matrix, factorization);
}

// Sets up the data structures needed for a scalar up-looking LDL'
// factorization: the elimination forest and structure sizes are computed,
// and the memory for the factors is allocated.
template <class Field>
void UpLookingSetup(const CoordinateMatrix<Field>& matrix,
                    std::vector<Int>* parents,
                    LDLFactorization<Field>* factorization) {
  LowerFactor<Field>& lower_factor = factorization->lower_factor;
  LowerStructure& lower_structure = lower_factor.structure;
  DiagonalFactor<Field>& diagonal_factor = factorization->diagonal_factor;

  std::vector<Int> degrees;
  EliminationForestAndDegrees(matrix, parents, &degrees);

  const Int num_rows = matrix.NumRows();
  lower_structure.column_offsets.resize(num_rows + 1);
  Int num_entries = 0;
  for (Int column = 0; column < num_rows; ++column) {
    lower_structure.column_offsets[column] = num_entries;
    num_entries += degrees[column];
  }
  lower_structure.column_offsets[num_rows] = num_entries;
  lower_structure.indices.resize(num_entries);

  lower_factor.values.resize(num_entries);
  diagonal_factor.values.resize(num_rows);
}

template <class Field>
void LeftLookingSetup(const CoordinateMatrix<Field>& matrix,
                      std::vector<Int>* parents,
                      LDLFactorization<Field>* factorization) {
  std::vector<Int> degrees;
  EliminationForestAndDegrees(matrix, parents, &degrees);
  InitializeLeftLookingFactors(matrix, *parents, degrees, factorization);
}

// Computes the nonzero pattern of L(row, :) in
// row_structure[0 : num_packed - 1].
template <class Field>
Int ComputeRowPattern(const CoordinateMatrix<Field>& matrix,
                      const std::vector<Int>& parents, Int row,
                      Int* pattern_flags, Int* row_structure) {
  const Int row_beg = matrix.RowEntryOffset(row);
  const Int row_end = matrix.RowEntryOffset(row + 1);
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  Int num_packed = 0;
  for (Int index = row_beg; index < row_end; ++index) {
    const MatrixEntry<Field>& entry = entries[index];
    Int column = entry.column;
    if (column >= row) {
      break;
    }

    // Walk up to the root of the current subtree of the elimination
    // forest, stopping if we encounter a member already marked as in the
    // row pattern.
    while (pattern_flags[column] != row) {
      // Place 'column' into the pattern of row 'row'.
      row_structure[num_packed++] = column;
      pattern_flags[column] = row;

      // Move up to the parent in this subtree of the elimination forest.
      column = parents[column];
    }
  }
  return num_packed;
}

// Computes the nonzero pattern of L(row, :) in a topological ordering in
// row_structure[start : num_rows - 1] and spread A(row, 0 : row - 1) into
// row_workspace.
template <class Field>
Int ComputeTopologicalRowPatternAndScatterNonzeros(
    const CoordinateMatrix<Field>& matrix, const std::vector<Int>& parents,
    Int row, Int* pattern_flags, Int* row_structure, Field* row_workspace) {
  Int start = matrix.NumRows();
  const Int row_beg = matrix.RowEntryOffset(row);
  const Int row_end = matrix.RowEntryOffset(row + 1);
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int index = row_beg; index < row_end; ++index) {
    const MatrixEntry<Field>& entry = entries[index];
    Int column = entry.column;
    if (column > row) {
      break;
    }

    // Scatter matrix(row, column) into row_workspace.
    row_workspace[column] = entry.value;

    // Walk up to the root of the current subtree of the elimination
    // forest, stopping if we encounter a member already marked as in the
    // row pattern.
    Int num_packed = 0;
    while (pattern_flags[column] != row) {
      // Place 'column' into the pattern of row 'row'.
      row_structure[num_packed++] = column;
      pattern_flags[column] = row;

      // Move up to the parent in this subtree of the elimination forest.
      column = parents[column];
    }

    // Pack this ancestral sequence into the back of the pattern.
    while (num_packed > 0) {
      row_structure[--start] = row_structure[--num_packed];
    }
  }
  return start;
}

// For each index 'i' in the structure of column 'column' of L formed so far:
//   L(row, i) -= (L(row, column) * d(column)) * conj(L(i, column)).
// L(row, row) is similarly updated, within d, then L(row, column) is finalized.
template <class Field>
void UpLookingRowUpdate(Int row, Int column,
                        LDLFactorization<Field>* factorization,
                        Int* column_update_ptrs, Field* row_workspace) {
  LowerFactor<Field>& lower_factor = factorization->lower_factor;
  LowerStructure& lower_structure = lower_factor.structure;
  DiagonalFactor<Field>& diagonal_factor = factorization->diagonal_factor;

  // Load eta := L(row, column) * d(column) from the workspace.
  const Field eta = row_workspace[column];
  row_workspace[column] = Field{0};

  // Update
  //
  //   L(row, I) -= (L(row, column) * d(column)) * conj(L(I, column)),
  //
  // where I is the set of already-formed entries in the structure of column
  // 'column' of L.
  //
  // Rothberg and Gupta refer to this as a 'scatter kernel' in:
  //   "An Evaluation of Left-Looking, Right-Looking and Multifrontal
  //   Approaches to Sparse Cholesky Factorization on Hierarchical-Memory
  //   Machines".
  const Int factor_column_beg = lower_structure.column_offsets[column];
  const Int factor_column_end = column_update_ptrs[column]++;
  for (Int index = factor_column_beg; index < factor_column_end; ++index) {
    // Update L(row, i) -= (L(row, column) * d(column)) * conj(L(i, column)).
    const Int i = lower_structure.indices[index];
    const Field& value = lower_factor.values[index];
    row_workspace[i] -= eta * Conjugate(value);
  }

  // Compute L(row, column) from eta = L(row, column) * d(column).
  const Field lambda = eta / diagonal_factor.values[column];

  // Update L(row, row) -= (L(row, column) * d(column)) * conj(L(row, column)).
  diagonal_factor.values[row] -= eta * Conjugate(lambda);

  // Append L(row, column) into the structure of column 'column'.
  lower_structure.indices[factor_column_end] = row;
  lower_factor.values[factor_column_end] = lambda;
}

// Performs a non-supernodal left-looking LDL' factorization.
// Cf. Section 4.8 of Tim Davis, "Direct Methods for Sparse Linear Systems".
//
// The basic high-level algorithm is of the form:
//   for k = 1:n
//     L(k, k) = sqrt(A(k, k) - L(k, 1:k-1) * L(k, 1:k-1)');
//     L(k+1:n, k) = (A(k+1:n, k) - L(k+1:n, 1:k-1) * L(k, 1:k-1)') / L(k, k);
//   end
template <class Field>
Int LeftLooking(const CoordinateMatrix<Field>& matrix,
                LDLFactorization<Field>* factorization) {
  const Int num_rows = matrix.NumRows();

  std::vector<Int> parents;
  ldl::LeftLookingSetup(matrix, &parents, factorization);

  LowerFactor<Field>& lower_factor = factorization->lower_factor;
  DiagonalFactor<Field>& diagonal_factor = factorization->diagonal_factor;
  const LowerStructure& lower_structure = lower_factor.structure;

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // Set up an integer workspace that could hold any row nonzero pattern.
  std::vector<Int> row_structure(num_rows);

  // Since we will sequentially access each of the entries in each column of
  // L during the updates of the active column, we can avoid the need for
  // binary search by maintaining a separate counter for each column.
  std::vector<Int> column_update_ptrs(num_rows);

  for (Int column = 0; column < num_rows; ++column) {
    pattern_flags[column] = column;
    column_update_ptrs[column] = lower_structure.column_offsets[column];

    // Compute the row pattern.
    const Int num_packed = ldl::ComputeRowPattern(
        matrix, parents, column, pattern_flags.data(), row_structure.data());

    // for j = find(L(column, :))
    //   L(column:n, column) -= L(column:n, j) * (d(j) * conj(L(column, j)))
    for (Int index = 0; index < num_packed; ++index) {
      const Int j = row_structure[index];
      CATAMARI_ASSERT(j < column, "Looking into upper triangle.");

      // Find L(column, j) in the j'th column.
      Int j_ptr = column_update_ptrs[j]++;
      const Int j_end = lower_structure.column_offsets[j + 1];
      CATAMARI_ASSERT(j_ptr != j_end, "Left column looking for L(column, j)");
      CATAMARI_ASSERT(lower_structure.indices[j_ptr] == column,
                      "Did not find L(column, j)");

      const Field lambda_k_j = lower_factor.values[j_ptr];
      const Field eta = diagonal_factor.values[j] * Conjugate(lambda_k_j);

      // L(column, column) -= L(column, j) * eta.
      diagonal_factor.values[column] -= lambda_k_j * eta;
      ++j_ptr;

      // L(column+1:n, column) -= L(column+1:n, j) * eta.
      const Int column_beg = lower_structure.column_offsets[column];
      Int column_ptr = column_beg;
      for (; j_ptr != j_end; ++j_ptr) {
        const Int row = lower_structure.indices[j_ptr];
        CATAMARI_ASSERT(row >= column, "Row index was less than column.");

        // L(row, column) -= L(row, j) * eta.
        // Move the pointer for column 'column' to the equivalent index.
        //
        // TODO(Jack Poulson): Decide if this 'search kernel' (see Rothbert
        // and Gupta's "An Evaluation of Left-Looking, Right-Looking and
        // Multifrontal Approaches to Sparse Cholesky Factorization on
        // Hierarchical-Memory Machines") can be improved. Unfortunately,
        // binary search, e.g., via std::lower_bound between 'column_ptr' and
        // the end of the column, leads to a 3x slowdown of the factorization
        // of the bbmat matrix.
        while (lower_structure.indices[column_ptr] < row) {
          ++column_ptr;
        }
        CATAMARI_ASSERT(lower_structure.indices[column_ptr] == row,
                        "The column pattern did not contain the j pattern.");
        CATAMARI_ASSERT(column_ptr < lower_structure.column_offsets[column + 1],
                        "The column pointer left the column.");
        const Field update = lower_factor.values[j_ptr] * eta;
        lower_factor.values[column_ptr] -= update;
      }
    }

    // Early exit if solving would involve division by zero.
    const Field pivot = diagonal_factor.values[column];
    if (pivot == Field{0}) {
      return column;
    }

    // L(column+1:n, column) /= d(column).
    {
      const Int column_beg = lower_structure.column_offsets[column];
      const Int column_end = lower_structure.column_offsets[column + 1];
      for (Int index = column_beg; index < column_end; ++index) {
        lower_factor.values[index] /= pivot;
      }
    }
  }

  return num_rows;
}

// Performs a non-supernodal up-looking LDL' factorization.
// Cf. Section 4.7 of Tim Davis, "Direct Methods for Sparse Linear Systems".
template <class Field>
Int UpLooking(const CoordinateMatrix<Field>& matrix,
              LDLFactorization<Field>* factorization) {
  const Int num_rows = matrix.NumRows();

  std::vector<Int> parents;
  ldl::UpLookingSetup(matrix, &parents, factorization);
  const LowerStructure& lower_structure = factorization->lower_factor.structure;

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // Set up an integer workspace that could hold any row nonzero pattern.
  std::vector<Int> row_structure(num_rows);

  // An array for holding the active index to insert the new entry of each
  // column into.
  std::vector<Int> column_update_ptrs(num_rows);

  // Set up a workspace for performing a triangular solve against a row of the
  // input matrix.
  std::vector<Field> row_workspace(num_rows, Field{0});

  for (Int row = 0; row < num_rows; ++row) {
    pattern_flags[row] = row;
    column_update_ptrs[row] = lower_structure.column_offsets[row];

    // Compute the row pattern and scatter the row of the input matrix into
    // the workspace.
    const Int start = ldl::ComputeTopologicalRowPatternAndScatterNonzeros(
        matrix, parents, row, pattern_flags.data(), row_structure.data(),
        row_workspace.data());

    // Pull the diagonal entry out of the workspace.
    factorization->diagonal_factor.values[row] = row_workspace[row];
    row_workspace[row] = Field{0};

    // Compute L(row, :) using a sparse triangular solve. In particular,
    //   L(row, :) := matrix(row, :) / L(0 : row - 1, 0 : row - 1)'.
    for (Int index = start; index < num_rows; ++index) {
      const Int column = row_structure[index];
      ldl::UpLookingRowUpdate(row, column, factorization,
                              column_update_ptrs.data(), row_workspace.data());
    }

    // Early exit if solving would involve division by zero.
    if (factorization->diagonal_factor.values[row] == Field{0}) {
      return row;
    }
  }

  return num_rows;
}

}  // namespace ldl

template <class Field>
Int LDL(const CoordinateMatrix<Field>& matrix, const LDLControl& control,
        LDLFactorization<Field>* factorization) {
  if (control.algorithm == kLeftLookingLDL) {
    return ldl::LeftLooking(matrix, factorization);
  } else {
    return ldl::UpLooking(matrix, factorization);
  }
}

template <class Field>
void LDLSolve(const LDLFactorization<Field>& factorization,
              std::vector<Field>* vector) {
  UnitLowerTriangularSolve(factorization.lower_factor, vector);
  DiagonalSolve(factorization.diagonal_factor, vector);
  UnitLowerAdjointTriangularSolve(factorization.lower_factor, vector);
}

template <class Field>
void UnitLowerTriangularSolve(const LowerFactor<Field>& unit_lower_factor,
                              std::vector<Field>* vector) {
  const LowerStructure& lower_structure = unit_lower_factor.structure;
  const Int num_rows = lower_structure.column_offsets.size() - 1;
  CATAMARI_ASSERT(static_cast<Int>(vector->size()) == num_rows,
                  "Vector was of the incorrect size.");
  for (Int column = 0; column < num_rows; ++column) {
    const Field& eta = (*vector)[column];

    const Int factor_column_beg = lower_structure.column_offsets[column];
    const Int factor_column_end = lower_structure.column_offsets[column + 1];
    for (Int index = factor_column_beg; index < factor_column_end; ++index) {
      const Int i = lower_structure.indices[index];
      const Field& value = unit_lower_factor.values[index];
      (*vector)[i] -= value * eta;
    }
  }
}

template <class Field>
void DiagonalSolve(const DiagonalFactor<Field>& diagonal_factor,
                   std::vector<Field>* vector) {
  const Int num_rows = diagonal_factor.values.size();
  CATAMARI_ASSERT(static_cast<Int>(vector->size()) == num_rows,
                  "Vector was of the incorrect size.");
  for (Int column = 0; column < num_rows; ++column) {
    (*vector)[column] /= diagonal_factor.values[column];
  }
}

template <class Field>
void UnitLowerAdjointTriangularSolve(
    const LowerFactor<Field>& unit_lower_factor, std::vector<Field>* vector) {
  const LowerStructure& lower_structure = unit_lower_factor.structure;
  const Int num_rows = lower_structure.column_offsets.size() - 1;
  CATAMARI_ASSERT(static_cast<Int>(vector->size()) == num_rows,
                  "Vector was of the incorrect size.");
  for (Int column = num_rows - 1; column >= 0; --column) {
    Field& eta = (*vector)[column];

    const Int factor_column_beg = lower_structure.column_offsets[column];
    const Int factor_column_end = lower_structure.column_offsets[column + 1];
    for (Int index = factor_column_beg; index < factor_column_end; ++index) {
      const Int i = lower_structure.indices[index];
      const Field& value = unit_lower_factor.values[index];
      eta -= Conjugate(value) * (*vector)[i];
    }
  }
}

}  // namespace catamari

#endif  // ifndef CATAMARI_LDL_IMPL_H_
