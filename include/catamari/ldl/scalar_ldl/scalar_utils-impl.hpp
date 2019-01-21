/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_LDL_SCALAR_LDL_SCALAR_UTILS_IMPL_H_
#define CATAMARI_LDL_SCALAR_LDL_SCALAR_UTILS_IMPL_H_

#include "catamari/ldl/scalar_ldl/scalar_utils.hpp"

namespace catamari {
namespace scalar_ldl {

template <class Field>
void EliminationForestAndDegrees(const CoordinateMatrix<Field>& matrix,
                                 const SymmetricOrdering& ordering,
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
  const bool have_permutation = !ordering.permutation.empty();
  for (Int row = 0; row < num_rows; ++row) {
    pattern_flags[row] = row;

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column =
          have_permutation ? ordering.permutation[entry.column] : entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
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

#ifdef _OPENMP
template <class Field>
void MultithreadedEliminationForestAndDegreesRecursion(
    const CoordinateMatrix<Field>& matrix, const SymmetricOrdering& ordering,
    Int root, std::vector<Int>* parents, std::vector<Int>* degrees,
    std::vector<Int>* pattern_flags) {
  const Int child_beg = ordering.assembly_forest.child_offsets[root];
  const Int child_end = ordering.assembly_forest.child_offsets[root + 1];
  #pragma omp taskgroup
  for (Int index = child_beg; index < child_end; ++index) {
    const Int child = ordering.assembly_forest.children[index];

    #pragma omp task default(none) firstprivate(child) \
        shared(matrix, ordering, parents, degrees, pattern_flags)
    MultithreadedEliminationForestAndDegreesRecursion(
        matrix, ordering, child, parents, degrees, pattern_flags);
  }

  const bool have_permutation = !ordering.permutation.empty();
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  const Int supernode_size = ordering.supernode_sizes[root];
  const Int supernode_offset = ordering.supernode_offsets[root];
  for (Int index = 0; index < supernode_size; ++index) {
    const Int row = supernode_offset + index;
    (*pattern_flags)[row] = row;

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column =
          have_permutation ? ordering.permutation[entry.column] : entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest from index 'column'. Any unset
      // parent pointers can be filled in during the traversal, as the current
      // row index would then be the parent.
      while ((*pattern_flags)[column] != row) {
        // Mark index 'column' as in the pattern of row 'row'.
        (*pattern_flags)[column] = row;
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

template <class Field>
void MultithreadedEliminationForestAndDegrees(
    const CoordinateMatrix<Field>& matrix, const SymmetricOrdering& ordering,
    std::vector<Int>* parents, std::vector<Int>* degrees) {
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

  #pragma omp taskgroup
  for (const Int root : ordering.assembly_forest.roots) {
    #pragma omp task default(none) firstprivate(root) \
        shared(matrix, ordering, parents, degrees, pattern_flags)
    MultithreadedEliminationForestAndDegreesRecursion(
        matrix, ordering, root, parents, degrees, &pattern_flags);
  }
}
#endif  // ifdef _OPENMP

template <class Field>
Int ComputeRowPattern(const CoordinateMatrix<Field>& matrix,
                      const SymmetricOrdering& ordering,
                      const std::vector<Int>& parents, Int row,
                      Int* pattern_flags, Int* row_structure) {
  const bool have_permutation = !ordering.permutation.empty();

  const Int orig_row =
      have_permutation ? ordering.inverse_permutation[row] : row;
  const Int row_beg = matrix.RowEntryOffset(orig_row);
  const Int row_end = matrix.RowEntryOffset(orig_row + 1);
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  Int num_packed = 0;
  for (Int index = row_beg; index < row_end; ++index) {
    const MatrixEntry<Field>& entry = entries[index];
    Int column =
        have_permutation ? ordering.permutation[entry.column] : entry.column;

    if (column >= row) {
      if (have_permutation) {
        continue;
      } else {
        break;
      }
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

template <class Field>
Int ComputeTopologicalRowPatternAndScatterNonzeros(
    const CoordinateMatrix<Field>& matrix, const SymmetricOrdering& ordering,
    const std::vector<Int>& parents, Int row, Int* pattern_flags,
    Int* row_structure, Field* row_workspace) {
  const bool have_permutation = !ordering.permutation.empty();
  Int start = matrix.NumRows();

  const Int orig_row =
      have_permutation ? ordering.inverse_permutation[row] : row;
  const Int row_beg = matrix.RowEntryOffset(orig_row);
  const Int row_end = matrix.RowEntryOffset(orig_row + 1);
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int index = row_beg; index < row_end; ++index) {
    const MatrixEntry<Field>& entry = entries[index];
    Int column =
        have_permutation ? ordering.permutation[entry.column] : entry.column;

    if (column > row) {
      if (have_permutation) {
        continue;
      } else {
        break;
      }
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

template <class Field>
void FillStructureIndices(const CoordinateMatrix<Field>& matrix,
                          const SymmetricOrdering& ordering,
                          const std::vector<Int>& parents,
                          const std::vector<Int>& degrees,
                          LowerStructure* lower_structure) {
  const Int num_rows = matrix.NumRows();
  const bool have_permutation = !ordering.permutation.empty();

  // Set up the column offsets and allocate space (initializing the values of
  // the unit-lower and diagonal and all zeros).
  OffsetScan(degrees, &lower_structure->column_offsets);
  lower_structure->indices.resize(lower_structure->column_offsets.back());

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

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column =
          have_permutation ? ordering.permutation[entry.column] : entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
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

#ifdef _OPENMP
template <class Field>
void MultithreadedFillStructureIndicesRecursion(
    const CoordinateMatrix<Field>& matrix, const SymmetricOrdering& ordering,
    const std::vector<Int>& parents, const std::vector<Int>& degrees, Int root,
    LowerStructure* lower_structure, std::vector<Int>* pattern_flags,
    std::vector<Int>* column_ptrs) {
  const Int child_beg = ordering.assembly_forest.child_offsets[root];
  const Int child_end = ordering.assembly_forest.child_offsets[root + 1];
  #pragma omp taskgroup
  for (Int index = child_beg; index < child_end; ++index) {
    const Int child = ordering.assembly_forest.children[index];

    #pragma omp task default(none) firstprivate(child) \
        shared(matrix, ordering, parents, degrees, lower_structure, \
            pattern_flags, column_ptrs)
    MultithreadedFillStructureIndicesRecursion(matrix, ordering, parents,
                                               degrees, child, lower_structure,
                                               pattern_flags, column_ptrs);
  }

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  const bool have_permutation = !ordering.permutation.empty();
  const Int supernode_size = ordering.supernode_sizes[root];
  const Int supernode_offset = ordering.supernode_offsets[root];
  for (Int index = 0; index < supernode_size; ++index) {
    const Int row = supernode_offset + index;
    (*pattern_flags)[row] = row;
    (*column_ptrs)[row] = lower_structure->column_offsets[row];

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int column =
          have_permutation ? ordering.permutation[entry.column] : entry.column;

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (column >= row) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest from index 'column'.
      while ((*pattern_flags)[column] != row) {
        // Mark index 'column' as in the pattern of row 'row'.
        (*pattern_flags)[column] = row;
        lower_structure->indices[(*column_ptrs)[column]++] = row;

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        column = parents[column];
      }
    }
  }
}

template <class Field>
void MultithreadedFillStructureIndices(const CoordinateMatrix<Field>& matrix,
                                       const SymmetricOrdering& ordering,
                                       const std::vector<Int>& parents,
                                       const std::vector<Int>& degrees,
                                       LowerStructure* lower_structure) {
  const Int num_rows = matrix.NumRows();

  // Set up the column offsets and allocate space (initializing the values of
  // the unit-lower and diagonal and all zeros).
  OffsetScan(degrees, &lower_structure->column_offsets);
  lower_structure->indices.resize(lower_structure->column_offsets.back());

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // A set of pointers for keeping track of where to insert column pattern
  // indices.
  std::vector<Int> column_ptrs(num_rows);

  #pragma omp taskgroup
  for (const Int root : ordering.assembly_forest.roots) {
    #pragma omp task default(none) firstprivate(root)               \
        shared(matrix, ordering, parents, degrees, lower_structure, \
            pattern_flags, column_ptrs)
    MultithreadedFillStructureIndicesRecursion(matrix, ordering, parents,
                                               degrees, root, lower_structure,
                                               &pattern_flags, &column_ptrs);
  }
}
#endif  // ifdef _OPENMP

}  // namespace scalar_ldl
}  // namespace catamari

#endif  // ifndef CATAMARI_LDL_SCALAR_LDL_SCALAR_UTILS_IMPL_H_
