/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_SUPERNODAL_LDL_IMPL_H_
#define CATAMARI_SUPERNODAL_LDL_IMPL_H_

#include "catamari/dense_basic_linear_algebra.hpp"
#include "catamari/dense_factorizations.hpp"
#include "catamari/supernodal_ldl.hpp"

namespace catamari {

template <class Field>
SupernodalLowerFactor<Field>::SupernodalLowerFactor(
    const std::vector<Int>& supernode_sizes,
    const std::vector<Int>& supernode_degrees) {
  const Int num_supernodes = supernode_sizes.size();

  Int degree_sum = 0;
  Int num_entries = 0;
  structure_index_offsets_.resize(num_supernodes + 1);
  std::vector<Int> lower_value_offsets(num_supernodes + 1);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int degree = supernode_degrees[supernode];
    const Int supernode_size = supernode_sizes[supernode];

    structure_index_offsets_[supernode] = degree_sum;
    lower_value_offsets[supernode] = num_entries;

    degree_sum += degree;
    num_entries += degree * supernode_size;
  }
  structure_index_offsets_[num_supernodes] = degree_sum;
  lower_value_offsets[num_supernodes] = num_entries;

  structure_indices_.resize(degree_sum);
  values_.resize(num_entries, Field{0});

  blocks.resize(num_supernodes);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int degree = supernode_degrees[supernode];
    const Int supernode_size = supernode_sizes[supernode];

    blocks[supernode].height = degree;
    blocks[supernode].width = supernode_size;
    blocks[supernode].leading_dim = degree;
    blocks[supernode].data = &values_[lower_value_offsets[supernode]];
  }
}

template <class Field>
Int* SupernodalLowerFactor<Field>::Structure(Int supernode) {
  return &structure_indices_[structure_index_offsets_[supernode]];
}

template <class Field>
const Int* SupernodalLowerFactor<Field>::Structure(Int supernode) const {
  return &structure_indices_[structure_index_offsets_[supernode]];
}

template <class Field>
Int* SupernodalLowerFactor<Field>::IntersectionSizes(Int supernode) {
  return &intersect_sizes_[intersect_size_offsets_[supernode]];
}

template <class Field>
const Int* SupernodalLowerFactor<Field>::IntersectionSizes(
    Int supernode) const {
  return &intersect_sizes_[intersect_size_offsets_[supernode]];
}

template <class Field>
void SupernodalLowerFactor<Field>::FillIntersectionSizes(
    const std::vector<Int>& supernode_sizes,
    const std::vector<Int>& supernode_member_to_index,
    Int* max_descendant_entries) {
  const Int num_supernodes = blocks.size();

  // Compute the supernode offsets.
  Int num_supernode_intersects = 0;
  intersect_size_offsets_.resize(num_supernodes + 1);
  for (Int column_supernode = 0; column_supernode < num_supernodes;
       ++column_supernode) {
    intersect_size_offsets_[column_supernode] = num_supernode_intersects;
    Int last_supernode = -1;

    const Int* index_beg = Structure(column_supernode);
    const Int* index_end = Structure(column_supernode + 1);
    for (const Int* row_ptr = index_beg; row_ptr != index_end; ++row_ptr) {
      const Int row = *row_ptr;
      const Int supernode = supernode_member_to_index[row];
      if (supernode != last_supernode) {
        last_supernode = supernode;
        ++num_supernode_intersects;
      }
    }
  }
  intersect_size_offsets_[num_supernodes] = num_supernode_intersects;

  // Fill the supernode intersection sizes (and simultaneously compute the
  // number of intersecting descendant entries for each supernode).
  intersect_sizes_.resize(num_supernode_intersects);
  num_supernode_intersects = 0;
  std::vector<Int> num_descendant_entries(num_supernodes, 0);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    Int last_supernode = -1;
    Int intersect_size = 0;
    const Int supernode_size = supernode_sizes[supernode];

    const Int* index_beg = Structure(supernode);
    const Int* index_end = Structure(supernode + 1);
    for (const Int* row_ptr = index_beg; row_ptr != index_end; ++row_ptr) {
      const Int row = *row_ptr;
      const Int row_supernode = supernode_member_to_index[row];
      if (row_supernode != last_supernode) {
        if (last_supernode != -1) {
          // Close out the supernodal intersection.
          intersect_sizes_[num_supernode_intersects++] = intersect_size;
          num_descendant_entries[row_supernode] +=
              intersect_size * supernode_size;
        }
        last_supernode = row_supernode;
        intersect_size = 0;
      }
      ++intersect_size;
    }
    if (last_supernode != -1) {
      // Close out the last intersection count for this column supernode.
      intersect_sizes_[num_supernode_intersects++] = intersect_size;
    }
  }
  CATAMARI_ASSERT(
      num_supernode_intersects == static_cast<Int>(intersect_sizes_.size()),
      "Incorrect number of supernode intersections");

  // NOTE: This only needs to be computed for multithreaded factorizations,
  // and the same applies to the num_descendant_entries array.
  *max_descendant_entries = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    *max_descendant_entries =
        std::max(*max_descendant_entries, num_descendant_entries[supernode]);
  }
}

template <class Field>
SupernodalDiagonalFactor<Field>::SupernodalDiagonalFactor(
    const std::vector<Int>& supernode_sizes) {
  const Int num_supernodes = supernode_sizes.size();

  std::vector<Int> diag_value_offsets(num_supernodes + 1);
  Int num_diagonal_entries = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int supernode_size = supernode_sizes[supernode];
    diag_value_offsets[supernode] = num_diagonal_entries;
    num_diagonal_entries += supernode_size * supernode_size;
  }
  diag_value_offsets[num_supernodes] = num_diagonal_entries;
  values_.resize(num_diagonal_entries, Field{0});

  blocks.resize(num_supernodes);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int supernode_size = supernode_sizes[supernode];
    blocks[supernode].height = supernode_size;
    blocks[supernode].width = supernode_size;
    blocks[supernode].leading_dim = supernode_size;
    blocks[supernode].data = &values_[diag_value_offsets[supernode]];
  }
}

namespace supernodal_ldl {

// Fills 'member_to_index' with a length 'num_rows' array whose i'th index
// is the index of the supernode containing column 'i'.
inline void MemberToIndex(Int num_rows,
                          const std::vector<Int>& supernode_starts,
                          std::vector<Int>* member_to_index) {
  member_to_index->resize(num_rows);

  Int supernode = 0;
  for (Int column = 0; column < num_rows; ++column) {
    if (column == supernode_starts[supernode + 1]) {
      ++supernode;
    }
    CATAMARI_ASSERT(column >= supernode_starts[supernode] &&
                        column < supernode_starts[supernode + 1],
                    "Column was not in the marked supernode.");
    (*member_to_index)[column] = supernode;
  }
  CATAMARI_ASSERT(supernode == static_cast<Int>(supernode_starts.size()) - 2,
                  "Did not end on the last supernode.");
}

// Builds an elimination forest over the supernodes from an elimination forest
// over the nodes.
inline void ConvertFromScalarToSupernodalEliminationForest(
    Int num_supernodes, const std::vector<Int>& parents,
    const std::vector<Int>& member_to_index,
    std::vector<Int>* supernode_parents) {
  const Int num_rows = parents.size();
  supernode_parents->resize(num_supernodes, -1);
  for (Int row = 0; row < num_rows; ++row) {
    const Int supernode = member_to_index[row];
    const Int parent = parents[row];
    if (parent == -1) {
      (*supernode_parents)[supernode] = -1;
    } else {
      const Int parent_supernode = member_to_index[parent];
      if (parent_supernode != supernode) {
        (*supernode_parents)[supernode] = parent_supernode;
      }
    }
  }
}

// Initialize the last member of each supernode as unset, but other supernode
// members as having the member to their right as their parent.
inline void InitializeEliminationForest(
    const std::vector<Int>& supernode_starts,
    const std::vector<Int>& member_to_index, std::vector<Int>* parents) {
  const Int num_rows = member_to_index.size();
  parents->resize(num_rows, -1);
  for (Int column = 0; column < num_rows; ++column) {
    const Int supernode = member_to_index[column];
    if (column != supernode_starts[supernode + 1] - 1) {
      (*parents)[column] = column + 1;
    }
  }
}

// Computes the sizes of the structures of a supernodal LDL' factorization.
template <class Field>
void SupernodalDegrees(const CoordinateMatrix<Field>& matrix,
                       const std::vector<Int>& permutation,
                       const std::vector<Int>& inverse_permutation,
                       const std::vector<Int>& supernode_sizes,
                       const std::vector<Int>& supernode_starts,
                       const std::vector<Int>& member_to_index,
                       const std::vector<Int>& parents,
                       std::vector<Int>* supernode_degrees) {
  const Int num_rows = matrix.NumRows();
  const Int num_supernodes = supernode_sizes.size();
  const bool have_permutation = !permutation.empty();

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // A data structure for marking whether or not a supernode is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> supernode_pattern_flags(num_supernodes);

  // Initialize the number of entries that will be stored into each supernode
  supernode_degrees->clear();
  supernode_degrees->resize(num_supernodes, 0);

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int main_supernode = member_to_index[row];
    pattern_flags[row] = row;
    supernode_pattern_flags[main_supernode] = row;

    const Int orig_row = have_permutation ? inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int descendant =
          have_permutation ? permutation[entry.column] : entry.column;
      Int descendant_supernode = member_to_index[descendant];

      // We are traversing the strictly lower triangle and know that the
      // indices are sorted.
      if (descendant >= row) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest from index 'descendant'. Any unset
      // parent pointers can be filled in during the traversal, as the current
      // row index would then be the parent.
      while (pattern_flags[descendant] != row) {
        // Mark index 'descendant' as in the pattern of row 'row'.
        pattern_flags[descendant] = row;

        descendant_supernode = member_to_index[descendant];
        CATAMARI_ASSERT(descendant_supernode <= main_supernode,
                        "Descendant supernode was larger than main supernode.");
        if (descendant_supernode < main_supernode) {
          if (supernode_pattern_flags[descendant_supernode] != row) {
            supernode_pattern_flags[descendant_supernode] = row;
            ++(*supernode_degrees)[descendant_supernode];
          }
        }

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        descendant = parents[descendant];
      }
    }
  }
}

// Fills in the structure indices for the lower factor.
template <class Field>
void FillStructureIndices(const CoordinateMatrix<Field>& matrix,
                          const std::vector<Int>& permutation,
                          const std::vector<Int>& inverse_permutation,
                          const std::vector<Int>& parents,
                          const std::vector<Int>& supernode_sizes,
                          const std::vector<Int>& supernode_member_to_index,
                          SupernodalLowerFactor<Field>* lower_factor,
                          Int* max_descendant_entries) {
  const Int num_rows = matrix.NumRows();
  const Int num_supernodes = supernode_sizes.size();
  const bool have_permutation = !permutation.empty();

  // A data structure for marking whether or not a node is in the pattern of
  // the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_rows);

  // A data structure for marking whether or not a supernode is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> supernode_pattern_flags(num_supernodes);

  // A set of pointers for keeping track of where to insert supernode pattern
  // indices.
  std::vector<Int*> supernode_ptrs(num_supernodes);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    supernode_ptrs[supernode] = lower_factor->Structure(supernode);
  }

  // Fill in the structure indices.
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int main_supernode = supernode_member_to_index[row];
    pattern_flags[row] = row;
    supernode_pattern_flags[main_supernode] = row;

    const Int orig_row = have_permutation ? inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int descendant =
          have_permutation ? permutation[entry.column] : entry.column;
      Int descendant_supernode = supernode_member_to_index[descendant];

      if (descendant >= row) {
        if (have_permutation) {
          continue;
        } else {
          // We are traversing the strictly lower triangle and know that the
          // indices are sorted.
          break;
        }
      }

      // Look for new entries in the pattern by walking up to the root of this
      // subtree of the elimination forest.
      while (pattern_flags[descendant] != row) {
        // Mark 'descendant' as in the pattern of this row.
        pattern_flags[descendant] = row;

        descendant_supernode = supernode_member_to_index[descendant];
        CATAMARI_ASSERT(descendant_supernode <= main_supernode,
                        "Descendant supernode was larger than main supernode.");
        if (descendant_supernode == main_supernode) {
          break;
        }

        if (supernode_pattern_flags[descendant_supernode] != row) {
          supernode_pattern_flags[descendant_supernode] = row;
          CATAMARI_ASSERT(
              descendant_supernode < main_supernode,
              "Descendant supernode was as large as main supernode.");
          CATAMARI_ASSERT(
              supernode_ptrs[descendant_supernode] >=
                      lower_factor->Structure(descendant_supernode) &&
                  supernode_ptrs[descendant_supernode] <
                      lower_factor->Structure(descendant_supernode + 1),
              "Left supernode's indices.");
          *supernode_ptrs[descendant_supernode] = row;
          ++supernode_ptrs[descendant_supernode];
        }

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        descendant = parents[descendant];
      }
    }
  }

#ifdef CATAMARI_DEBUG
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int* index_beg = lower_factor->Structure(supernode);
    const Int* index_end = lower_factor->Structure(supernode + 1);
    CATAMARI_ASSERT(supernode_ptrs[supernode] == index_end,
                    "Supernode pointers did not match index offsets.");
    bool sorted = true;
    Int last_row = -1;
    for (const Int* row_ptr = index_beg; row_ptr != index_end; ++row_ptr) {
      const Int row = *row_ptr;
      if (row <= last_row) {
        sorted = false;
        break;
      }
      last_row = row;
    }

    if (!sorted) {
      std::cerr << "Supernode " << supernode << " did not have sorted indices."
                << std::endl;
      for (const Int* row_ptr = index_beg; row_ptr != index_end; ++row_ptr) {
        std::cout << *row_ptr << " ";
      }
      std::cout << std::endl;
    }
  }
#endif

  lower_factor->FillIntersectionSizes(
      supernode_sizes, supernode_member_to_index, max_descendant_entries);
}

// Fill in the nonzeros from the original sparse matrix.
template <class Field>
void FillNonzeros(const CoordinateMatrix<Field>& matrix,
                  const std::vector<Int>& permutation,
                  const std::vector<Int>& inverse_permutation,
                  const std::vector<Int>& supernode_starts,
                  const std::vector<Int>& supernode_sizes,
                  const std::vector<Int>& supernode_member_to_index,
                  SupernodalLowerFactor<Field>* lower_factor,
                  SupernodalDiagonalFactor<Field>* diagonal_factor) {
  const Int num_rows = matrix.NumRows();
  const bool have_permutation = !permutation.empty();

  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int supernode = supernode_member_to_index[row];
    const Int supernode_start = supernode_starts[supernode];

    const Int orig_row = have_permutation ? inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      const Int column =
          have_permutation ? permutation[entry.column] : entry.column;
      const Int column_supernode = supernode_member_to_index[column];

      if (column_supernode == supernode) {
        // Insert the value into the diagonal block.
        const Int rel_row = row - supernode_start;
        const Int rel_column = column - supernode_start;
        diagonal_factor->blocks[supernode](rel_row, rel_column) = entry.value;
        continue;
      }

      if (column_supernode > supernode) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
      }

      // Insert the value into the subdiagonal block.
      const Int* column_index_beg = lower_factor->Structure(column_supernode);
      const Int* column_index_end =
          lower_factor->Structure(column_supernode + 1);
      const Int* iter =
          std::lower_bound(column_index_beg, column_index_end, row);
      CATAMARI_ASSERT(iter != column_index_end, "Exceeded column indices.");
      CATAMARI_ASSERT(*iter == row, "Entry (" + std::to_string(row) + ", " +
                                        std::to_string(column) +
                                        ") wasn't in the structure.");
      const Int rel_row = std::distance(column_index_beg, iter);
      const Int rel_column = column - supernode_starts[column_supernode];
      lower_factor->blocks[column_supernode](rel_row, rel_column) = entry.value;
    }
  }
}

template <class Field>
void InitializeLeftLookingFactors(
    const CoordinateMatrix<Field>& matrix, const std::vector<Int>& parents,
    const std::vector<Int>& supernode_degrees,
    SupernodalLDLFactorization<Field>* factorization) {
  factorization->lower_factor.reset(new SupernodalLowerFactor<Field>(
      factorization->supernode_sizes, supernode_degrees));
  factorization->diagonal_factor.reset(
      new SupernodalDiagonalFactor<Field>(factorization->supernode_sizes));

  const Int num_supernodes = factorization->supernode_sizes.size();
  CATAMARI_ASSERT(static_cast<Int>(supernode_degrees.size()) == num_supernodes,
                  "Invalid supernode degrees size.");

  // Store the largest degree of the factorization for use in the solve phase.
  factorization->max_degree = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int degree = supernode_degrees[supernode];
    factorization->max_degree = std::max(factorization->max_degree, degree);
  }

  FillStructureIndices(matrix, factorization->permutation,
                       factorization->inverse_permutation, parents,
                       factorization->supernode_sizes,
                       factorization->supernode_member_to_index,
                       factorization->lower_factor.get(),
                       &factorization->max_descendant_entries);

  FillNonzeros(
      matrix, factorization->permutation, factorization->inverse_permutation,
      factorization->supernode_starts, factorization->supernode_sizes,
      factorization->supernode_member_to_index,
      factorization->lower_factor.get(), factorization->diagonal_factor.get());
}

// Computes the supernodal nonzero pattern of L(row, :) in
// row_structure[0 : num_packed - 1].
template <class Field>
Int ComputeRowPattern(const CoordinateMatrix<Field>& matrix,
                      const std::vector<Int>& permutation,
                      const std::vector<Int>& inverse_permutation,
                      const std::vector<Int>& supernode_sizes,
                      const std::vector<Int>& supernode_starts,
                      const std::vector<Int>& member_to_index,
                      const std::vector<Int>& supernode_parents,
                      Int main_supernode, Int* pattern_flags,
                      Int* row_structure) {
  Int num_packed = 0;
  const bool have_permutation = !permutation.empty();
  const std::vector<MatrixEntry<Field>>& entries = matrix.Entries();

  // Take the union of the row patterns of each row in the supernode.
  const Int main_supernode_size = supernode_sizes[main_supernode];
  const Int main_supernode_start = supernode_starts[main_supernode];
  for (Int row = main_supernode_start;
       row < main_supernode_start + main_supernode_size; ++row) {
    const Int orig_row = have_permutation ? inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      const Int descendant =
          have_permutation ? permutation[entry.column] : entry.column;

      Int descendant_supernode = member_to_index[descendant];
      if (descendant_supernode >= main_supernode) {
        if (have_permutation) {
          continue;
        } else {
          break;
        }
      }

      // Walk up to the root of the current subtree of the elimination
      // forest, stopping if we encounter a member already marked as in the
      // row pattern.
      while (pattern_flags[descendant_supernode] != main_supernode) {
        // Place 'descendant_supernode' into the pattern of this supernode.
        row_structure[num_packed++] = descendant_supernode;
        pattern_flags[descendant_supernode] = main_supernode;

        descendant_supernode = supernode_parents[descendant_supernode];
      }
    }
  }

  return num_packed;
}

// Store the scaled adjoint update matrix, Z(d, m) = D(d, d) L(m, d)', or
// the scaled transpose, Z(d, m) = D(d, d) L(m, d)^T.
template <class Field>
void FormScaledTranspose(SymmetricFactorizationType factorization_type,
                         const ConstBlasMatrix<Field>& diagonal_block,
                         const ConstBlasMatrix<Field>& matrix,
                         BlasMatrix<Field>* scaled_transpose) {
  if (factorization_type == kCholeskyFactorization) {
    for (Int j = 0; j < matrix.width; ++j) {
      for (Int i = 0; i < matrix.height; ++i) {
        scaled_transpose->Entry(j, i) = Conjugate(matrix(i, j));
      }
    }
  } else if (factorization_type == kLDLAdjointFactorization) {
    for (Int j = 0; j < matrix.width; ++j) {
      const Field& delta = diagonal_block(j, j);
      for (Int i = 0; i < matrix.height; ++i) {
        scaled_transpose->Entry(j, i) = delta * Conjugate(matrix(i, j));
      }
    }
  } else {
    for (Int j = 0; j < matrix.width; ++j) {
      const Field& delta = diagonal_block(j, j);
      for (Int i = 0; i < matrix.height; ++i) {
        scaled_transpose->Entry(j, i) = delta * matrix(i, j);
      }
    }
  }
}

// L(m, m) -= L(m, d) * (D(d, d) * L(m, d)')
//          = L(m, d) * Z(:, m).
//
// The update is to the main_supernode_size x main_supernode_size dense
// diagonal block, L(m, m), with the densified L(m, d) matrix being
// descendant_main_intersect_size x descendant_supernode_size, and Z(:, m)
// being descendant_supernode_size x descendant_main_intersect_size.
template <class Field>
void UpdateDiagonalBlock(SymmetricFactorizationType factorization_type,
                         const std::vector<Int>& supernode_starts,
                         const SupernodalLowerFactor<Field>& lower_factor,
                         Int main_supernode, Int descendant_supernode,
                         Int descendant_main_rel_row,
                         const ConstBlasMatrix<Field>& descendant_main_matrix,
                         const ConstBlasMatrix<Field>& scaled_transpose,
                         BlasMatrix<Field>* main_diag_block,
                         BlasMatrix<Field>* workspace_matrix) {
  typedef ComplexBase<Field> Real;
  const Int main_supernode_size = main_diag_block->height;
  const Int descendant_main_intersect_size = scaled_transpose.width;

  const bool inplace_update =
      descendant_main_intersect_size == main_supernode_size;
  BlasMatrix<Field>* accumulation_block =
      inplace_update ? main_diag_block : workspace_matrix;

  if (factorization_type == kCholeskyFactorization) {
    LowerNormalHermitianOuterProduct(Real{-1}, descendant_main_matrix, Real{1},
                                     accumulation_block);
  } else {
    MatrixMultiplyLowerNormalNormal(Field{-1}, descendant_main_matrix,
                                    scaled_transpose, Field{1},
                                    accumulation_block);
  }

  if (!inplace_update) {
    // Apply the out-of-place update and zero the buffer.
    const Int main_supernode_start = supernode_starts[main_supernode];
    const Int* descendant_main_indices =
        lower_factor.Structure(descendant_supernode) + descendant_main_rel_row;

    for (Int j = 0; j < descendant_main_intersect_size; ++j) {
      const Int column = descendant_main_indices[j];
      const Int j_rel = column - main_supernode_start;
      for (Int i = j; i < descendant_main_intersect_size; ++i) {
        const Int row = descendant_main_indices[i];
        const Int i_rel = row - main_supernode_start;
        main_diag_block->Entry(i_rel, j_rel) += workspace_matrix->Entry(i, j);
        workspace_matrix->Entry(i, j) = 0;
      }
    }
  }
}

// Moves the pointers for the main supernode down to the active supernode of
// the descendant column block.
template <class Field>
void SeekForMainActiveRelativeRow(
    Int main_supernode, Int descendant_supernode, Int descendant_active_rel_row,
    const std::vector<Int>& supernode_member_to_index,
    const SupernodalLowerFactor<Field>& lower_factor, Int* main_active_rel_row,
    const Int** main_active_intersect_sizes) {
  const Int* main_indices = lower_factor.Structure(main_supernode);
  const Int* descendant_indices = lower_factor.Structure(descendant_supernode);
  const Int descendant_active_supernode_start =
      descendant_indices[descendant_active_rel_row];
  const Int active_supernode =
      supernode_member_to_index[descendant_active_supernode_start];
  CATAMARI_ASSERT(active_supernode > main_supernode,
                  "Active supernode was <= the main supernode in update.");

  Int main_active_intersect_size = **main_active_intersect_sizes;
  Int main_active_first_row = main_indices[*main_active_rel_row];
  while (supernode_member_to_index[main_active_first_row] < active_supernode) {
    *main_active_rel_row += main_active_intersect_size;
    ++*main_active_intersect_sizes;

    main_active_first_row = main_indices[*main_active_rel_row];
    main_active_intersect_size = **main_active_intersect_sizes;
  }
#ifdef CATAMARI_DEBUG
  const Int main_active_supernode =
      supernode_member_to_index[main_active_first_row];
  CATAMARI_ASSERT(main_active_supernode == active_supernode,
                  "Did not find active supernode.");
#endif
}

// L(a, m) -= L(a, d) * (D(d, d) * L(m, d)')
//          = L(a, d) * Z(:, m).
//
// The update is to the main_active_intersect_size x main_supernode_size
// densified matrix L(a, m) using the
// descendant_active_intersect_size x descendant_supernode_size densified
// L(a, d) and thd escendant_supernode_size x descendant_main_intersect_size
// Z(:, m).
template <class Field>
void UpdateSubdiagonalBlock(
    Int main_supernode, Int descendant_supernode, Int main_active_rel_row,
    Int descendant_main_rel_row, Int descendant_active_rel_row,
    const std::vector<Int>& supernode_starts,
    const std::vector<Int>& supernode_member_to_index,
    const ConstBlasMatrix<Field>& scaled_transpose,
    const ConstBlasMatrix<Field>& descendant_active_matrix,
    const SupernodalLowerFactor<Field>& lower_factor,
    BlasMatrix<Field>* main_active_block, BlasMatrix<Field>* workspace_matrix) {
  const Int main_supernode_size = lower_factor.blocks[main_supernode].width;
  const Int main_active_intersect_size = main_active_block->height;
  const Int descendant_main_intersect_size = scaled_transpose.width;
  const Int descendant_active_intersect_size = descendant_active_matrix.height;
  const bool inplace_update =
      main_active_intersect_size == descendant_active_intersect_size &&
      main_supernode_size == descendant_main_intersect_size;

  BlasMatrix<Field>* accumulation_matrix =
      inplace_update ? main_active_block : workspace_matrix;
  MatrixMultiplyNormalNormal(Field{-1}, descendant_active_matrix,
                             scaled_transpose, Field{1}, accumulation_matrix);

  if (!inplace_update) {
    const Int main_supernode_start = supernode_starts[main_supernode];

    const Int* main_indices = lower_factor.Structure(main_supernode);
    const Int* main_active_indices = main_indices + main_active_rel_row;

    const Int* descendant_indices =
        lower_factor.Structure(descendant_supernode);
    const Int* descendant_main_indices =
        descendant_indices + descendant_main_rel_row;
    const Int* descendant_active_indices =
        descendant_indices + descendant_active_rel_row;

    Int i_rel = 0;
    for (Int i = 0; i < descendant_active_intersect_size; ++i) {
      const Int row = descendant_active_indices[i];

      // Both the main and descendant supernodal intersections can be sparse,
      // and different. Thus, we must scan through the intersection indices to
      // find the appropriate relative index here.
      //
      // Scan downwards in the main active indices until we find the equivalent
      // row from the descendant active indices.
      while (main_active_indices[i_rel] != row) {
        ++i_rel;
      }

      for (Int j = 0; j < descendant_main_intersect_size; ++j) {
        const Int column = descendant_main_indices[j];
        const Int j_rel = column - main_supernode_start;

        main_active_block->Entry(i_rel, j_rel) += workspace_matrix->Entry(i, j);
        workspace_matrix->Entry(i, j) = 0;
      }
    }
  }
}

// Perform an in-place LDL' factorization of the supernodal diagonal block.
template <class Field>
Int FactorDiagonalBlock(SymmetricFactorizationType factorization_type,
                        BlasMatrix<Field>* diagonal_block) {
  Int num_pivots;
  if (factorization_type == kCholeskyFactorization) {
    num_pivots = LowerCholeskyFactorization(diagonal_block);
  } else if (factorization_type == kLDLAdjointFactorization) {
    num_pivots = LowerLDLAdjointFactorization(diagonal_block);
  } else {
    num_pivots = LowerLDLTransposeFactorization(diagonal_block);
  }
  return num_pivots;
}

// L(KNext:n, K) /= D(K, K) L(K, K)', or /= D(K, K) L(K, K)^T.
template <class Field>
void SolveAgainstDiagonalBlock(SymmetricFactorizationType factorization_type,
                               const ConstBlasMatrix<Field>& triangular_matrix,
                               BlasMatrix<Field>* lower_matrix) {
  if (!lower_matrix->height) {
    return;
  }
  if (factorization_type == kCholeskyFactorization) {
    // Solve against the lower-triangular matrix L(K, K)' from the right.
    RightLowerAdjointTriangularSolves(triangular_matrix, lower_matrix);
  } else if (factorization_type == kLDLAdjointFactorization) {
    // Solve against D(K, K) L(K, K)' from the right.
    RightDiagonalTimesLowerAdjointUnitTriangularSolves(triangular_matrix,
                                                       lower_matrix);
  } else {
    // Solve against D(K, K) L(K, K)^T from the right.
    RightDiagonalTimesLowerTransposeUnitTriangularSolves(triangular_matrix,
                                                         lower_matrix);
  }
}

// Checks that a valid set of supernodes has been provided by explicitly
// computing each row pattern and ensuring that each intersects entire
// supernodes.
template <class Field>
bool ValidFundamentalSupernodes(const CoordinateMatrix<Field>& matrix,
                                const std::vector<Int>& permutation,
                                const std::vector<Int>& inverse_permutation,
                                const std::vector<Int>& supernode_sizes) {
  const Int num_rows = matrix.NumRows();

  std::vector<Int> parents, degrees;
  ldl::EliminationForestAndDegrees(matrix, permutation, inverse_permutation,
                                   &parents, &degrees);

  std::vector<Int> supernode_starts, member_to_index;
  ldl::OffsetScan(supernode_sizes, &supernode_starts);
  MemberToIndex(num_rows, supernode_starts, &member_to_index);

  std::vector<Int> row_structure(num_rows);
  std::vector<Int> pattern_flags(num_rows);

  bool valid = true;
  for (Int row = 0; row < num_rows; ++row) {
    const Int row_supernode = member_to_index[row];

    pattern_flags[row] = row;
    const Int num_packed = ldl::ComputeRowPattern(
        matrix, permutation, inverse_permutation, parents, row,
        pattern_flags.data(), row_structure.data());
    std::sort(row_structure.data(), row_structure.data() + num_packed);

    // TODO(Jack Poulson): Extend the tests to ensure that the diagonal blocks
    // of the supernodes are dense.

    // Check that the pattern of this row intersects entire supernodes that
    // are not the current row's supernode.
    Int index = 0;
    while (index < num_packed) {
      const Int column = row_structure[index];
      const Int supernode = member_to_index[column];
      if (supernode == row_supernode) {
        break;
      }

      const Int supernode_start = supernode_starts[supernode];
      const Int supernode_size = supernode_sizes[supernode];
      if (num_packed < index + supernode_size) {
        std::cerr << "Did not pack enough indices to hold supernode "
                  << supernode << " of size " << supernode_size << " in row "
                  << row << std::endl;
        return false;
      }
      for (Int j = 0; j < supernode_size; ++j) {
        if (row_structure[index++] != supernode_start + j) {
          std::cerr << "Missed column " << supernode_start + j << " in row "
                    << row << " and supernode " << supernode_start << ":"
                    << supernode_start + supernode_size << std::endl;
          valid = false;
        }
      }
    }
  }
  return valid;
}

// Compute an unrelaxed supernodal partition using the existing ordering.
// We require that supernodes have dense diagonal blocks and equal structures
// below the diagonal block.
template <class Field>
void FormFundamentalSupernodes(const CoordinateMatrix<Field>& matrix,
                               const std::vector<Int>& permutation,
                               const std::vector<Int>& inverse_permutation,
                               const std::vector<Int>& parents,
                               const std::vector<Int>& degrees,
                               std::vector<Int>* supernode_sizes,
                               ScalarLowerStructure* scalar_structure) {
  const Int num_rows = matrix.NumRows();

  // We will only fill the indices and offsets of the factorization.
  ldl::FillStructureIndices(matrix, permutation, inverse_permutation, parents,
                            degrees, scalar_structure);

  supernode_sizes->clear();
  if (!num_rows) {
    return;
  }

  // We will iterate down each column structure to determine the supernodes.
  std::vector<Int> column_ptrs = scalar_structure->column_offsets;

  Int supernode_start = 0;
  supernode_sizes->reserve(num_rows);
  supernode_sizes->push_back(1);
  for (Int column = 1; column < num_rows; ++column) {
    // Ensure that the diagonal block would be fully-connected. Due to the
    // loop invariant, each column pointer in the current supernode would need
    // to be pointing to index 'column'.
    bool dense_diagonal_block = true;
    for (Int j = supernode_start; j < column; ++j) {
      const Int index = column_ptrs[j];
      const Int next_column_beg = scalar_structure->column_offsets[j + 1];
      if (index < next_column_beg &&
          scalar_structure->indices[index] == column) {
        ++column_ptrs[j];
      } else {
        dense_diagonal_block = false;
        break;
      }
    }
    if (!dense_diagonal_block) {
      // This column begins a new supernode.
      supernode_start = column;
      supernode_sizes->push_back(1);
      continue;
    }

    // Test if the structure of this supernode matches that of the previous
    // column (with all indices up to this column removed). We first test that
    // the set sizes are equal and then test the individual entries.

    // Test that the set sizes match.
    const Int column_beg = column_ptrs[column];
    const Int structure_size = column_ptrs[column + 1] - column_beg;
    const Int prev_column_ptr = column_ptrs[column - 1];
    const Int prev_remaining_structure_size = column_beg - prev_column_ptr;
    if (structure_size != prev_remaining_structure_size) {
      // This column begins a new supernode.
      supernode_start = column;
      supernode_sizes->push_back(1);
      continue;
    }

    // Test that the individual entries match.
    bool equal_structures = true;
    for (Int i = 0; i < structure_size; ++i) {
      const Int row = scalar_structure->indices[column_beg + i];
      const Int prev_row = scalar_structure->indices[prev_column_ptr + i];
      if (prev_row != row) {
        equal_structures = false;
        break;
      }
    }
    if (!equal_structures) {
      // This column begins a new supernode.
      supernode_start = column;
      supernode_sizes->push_back(1);
      continue;
    }

    // All tests passed, so we may extend the current supernode to incorporate
    // this column.
    ++supernode_sizes->back();
  }

#ifdef CATAMARI_DEBUG
  if (!ValidFundamentalSupernodes(matrix, permutation, inverse_permutation,
                                  *supernode_sizes)) {
    std::cerr << "Invalid fundamental supernodes." << std::endl;
    return;
  }
#endif
}

// A data structure for representing whether or not a child supernode can be
// merged with its parent and, if so, how many explicit zeros would be in the
// combined supernode's block column.
struct MergableStatus {
  // Whether or not the supernode can be merged.
  bool mergable;

  // How many explicit zeros would be stored in the merged supernode.
  Int num_merged_zeros;
};

// Returns whether or not the child supernode can be merged into its parent by
// counting the number of explicit zeros that would be introduced by the
// merge.
//
// Consider the possibility of merging a child supernode '0' with parent
// supernode '1', which would result in an expanded supernode of the form:
//
//    -------------
//   | L00  |      |
//   |------|------|
//   | L10  | L11  |
//   ---------------
//   |      |      |
//   | L20  | L21  |
//   |      |      |
//    -------------
//
// Because it is assumed that supernode 1 is the parent of supernode 0,
// the only places explicit nonzeros can be introduced are in:
//   * L10: if supernode 0 was not fully-connected to supernode 1.
//   * L20: if supernode 0's structure (with supernode 1 removed) does not
//     contain every member of the structure of supernode 1.
//
// Counting the number of explicit zeros that would be introduced is thus
// simply a matter of counting these mismatches.
//
// The reason that downstream explicit zeros would not be introduced is the
// same as the reason explicit zeros are not introduced into L21; supernode
// 1 is the parent of supernode 0.
//
inline MergableStatus MergableSupernode(
    Int child_tail, Int parent_tail, Int child_size, Int parent_size,
    Int num_child_explicit_zeros, Int num_parent_explicit_zeros,
    const std::vector<Int>& orig_member_to_index,
    const ScalarLowerStructure& scalar_structure,
    const SupernodalRelaxationControl& control) {
  const Int parent = orig_member_to_index[parent_tail];
  const Int child_structure_beg = scalar_structure.column_offsets[child_tail];
  const Int child_structure_end =
      scalar_structure.column_offsets[child_tail + 1];
  const Int parent_structure_beg = scalar_structure.column_offsets[parent_tail];
  const Int parent_structure_end =
      scalar_structure.column_offsets[parent_tail + 1];
  const Int child_structure_size = child_structure_end - child_structure_beg;
  const Int parent_structure_size = parent_structure_end - parent_structure_beg;

  MergableStatus status;

  // Count the number of intersections of the child's structure with the
  // parent supernode (and then leave the child structure pointer after the
  // parent supernode.
  //
  // We know that any intersections of the child with the parent occur at the
  // beginning of the child's structure.
  Int num_child_parent_intersections = 0;
  {
    Int child_structure_ptr = child_structure_beg;
    while (child_structure_ptr < child_structure_end) {
      const Int row = scalar_structure.indices[child_structure_ptr];
      const Int orig_row_supernode = orig_member_to_index[row];
      CATAMARI_ASSERT(orig_row_supernode >= parent,
                      "There was an intersection before the parent.");
      if (orig_row_supernode == parent) {
        ++child_structure_ptr;
        ++num_child_parent_intersections;
      } else {
        break;
      }
    }
  }
  const Int num_missing_parent_intersections =
      parent_size - num_child_parent_intersections;

  // Since the structure of L21 contains the structure of L20, we need only
  // compare the sizes of the structure of the parent supernode to the
  // remaining structure size to know how many indices will need to be
  // introduced into L20.
  const Int remaining_child_structure_size =
      child_structure_size - num_child_parent_intersections;
  const Int num_missing_structure_indices =
      parent_structure_size - remaining_child_structure_size;

  const Int num_new_zeros =
      (num_missing_parent_intersections + num_missing_structure_indices) *
      child_size;
  const Int num_old_zeros =
      num_child_explicit_zeros + num_parent_explicit_zeros;
  const Int num_zeros = num_new_zeros + num_old_zeros;
  status.num_merged_zeros = num_zeros;

  // Check if the merge would meet the absolute merge criterion.
  if (num_zeros <= control.allowable_supernode_zeros) {
    status.mergable = true;
    return status;
  }

  // Check if the merge would meet the relative merge criterion.
  const Int num_expanded_entries =
      /* num_nonzeros(L00) */
      (child_size + (child_size + 1)) / 2 +
      /* num_nonzeros(L10) */
      parent_size * child_size +
      /* num_nonzeros(L20) */
      remaining_child_structure_size * child_size +
      /* num_nonzeros(L11) */
      (parent_size + (parent_size + 1)) / 2 +
      /* num_nonzeros(L21) */
      parent_structure_size * parent_size;
  if (num_zeros <=
      control.allowable_supernode_zero_ratio * num_expanded_entries) {
    status.mergable = true;
    return status;
  }

  status.mergable = false;
  return status;
}

inline void MergeChildren(
    Int parent, const std::vector<Int>& orig_supernode_starts,
    const std::vector<Int>& orig_supernode_sizes,
    const std::vector<Int>& orig_member_to_index,
    const std::vector<Int>& children, const std::vector<Int>& child_offsets,
    const ScalarLowerStructure& scalar_structure,
    const SupernodalRelaxationControl& control,
    std::vector<Int>* supernode_sizes, std::vector<Int>* num_explicit_zeros,
    std::vector<Int>* last_merged_child, std::vector<Int>* merge_parents) {
  const Int child_beg = child_offsets[parent];
  const Int num_children = child_offsets[parent + 1] - child_beg;

  // TODO(Jack Poulson): Reserve a default size for these arrays.
  std::vector<Int> mergable_children;
  std::vector<Int> num_merged_zeros;

  // The following loop can execute at most 'num_children' times.
  while (true) {
    // Compute the list of child indices that can be merged.
    for (Int child_index = 0; child_index < num_children; ++child_index) {
      const Int child = children[child_beg + child_index];
      if ((*merge_parents)[child] != -1) {
        continue;
      }

      const Int child_tail =
          orig_supernode_starts[child] + orig_supernode_sizes[child] - 1;
      const Int parent_tail =
          orig_supernode_starts[parent] + orig_supernode_sizes[parent] - 1;

      const Int child_size = (*supernode_sizes)[child];
      const Int parent_size = (*supernode_sizes)[parent];

      const Int num_child_explicit_zeros = (*num_explicit_zeros)[child];
      const Int num_parent_explicit_zeros = (*num_explicit_zeros)[parent];

      const MergableStatus status =
          MergableSupernode(child_tail, parent_tail, child_size, parent_size,
                            num_child_explicit_zeros, num_parent_explicit_zeros,
                            orig_member_to_index, scalar_structure, control);
      if (status.mergable) {
        mergable_children.push_back(child_index);
        num_merged_zeros.push_back(status.num_merged_zeros);
      }
    }

    // Skip this supernode if no children can be merged into it.
    if (mergable_children.empty()) {
      break;
    }
    const Int first_mergable_child = children[child_beg + mergable_children[0]];

    // Select the largest mergable supernode.
    Int merging_index = 0;
    Int largest_mergable_size = (*supernode_sizes)[first_mergable_child];
    for (std::size_t mergable_index = 1;
         mergable_index < mergable_children.size(); ++mergable_index) {
      const Int child_index = mergable_children[mergable_index];
      const Int child = children[child_beg + child_index];
      const Int child_size = (*supernode_sizes)[child];
      if (child_size > largest_mergable_size) {
        merging_index = mergable_index;
        largest_mergable_size = child_size;
      }
    }
    const Int child_index = mergable_children[merging_index];
    const Int child = children[child_beg + child_index];

    // Absorb the child size into the parent.
    (*supernode_sizes)[parent] += (*supernode_sizes)[child];
    (*supernode_sizes)[child] = 0;

    // Update the number of explicit zeros in the merged supernode.
    (*num_explicit_zeros)[parent] = num_merged_zeros[merging_index];

    // Mark the child as merged.
    //
    // TODO(Jack Poulson): Consider following a similar strategy as quotient
    // and using SYMMETRIC_INDEX to pack a parent into the negative indices.

    if ((*last_merged_child)[parent] == -1) {
      (*merge_parents)[child] = parent;
    } else {
      (*merge_parents)[child] = (*last_merged_child)[parent];
    }

    if ((*last_merged_child)[child] == -1) {
      (*last_merged_child)[parent] = child;
    } else {
      (*last_merged_child)[parent] = (*last_merged_child)[child];
    }

    // Clear the mergable children information since it is now stale.
    mergable_children.clear();
    num_merged_zeros.clear();
  }
}

// Builds a packed set of child links for an elimination forest given the
// parent links (with root nodes having their parent set to -1).
inline void EliminationForestFromParents(const std::vector<Int>& parents,
                                         std::vector<Int>* children,
                                         std::vector<Int>* child_offsets) {
  const Int num_indices = parents.size();

  std::vector<Int> num_children(num_indices, 0);
  for (Int index = 0; index < num_indices; ++index) {
    const Int parent = parents[index];
    if (parent >= 0) {
      ++num_children[parent];
    }
  }

  ldl::OffsetScan(num_children, child_offsets);

  children->resize(num_indices);
  auto offsets_copy = *child_offsets;
  for (Int index = 0; index < num_indices; ++index) {
    const Int parent = parents[index];
    if (parent >= 0) {
      (*children)[offsets_copy[parent]++] = index;
    }
  }
}

// Walk up the tree in the original postordering, merging supernodes as we
// progress. The 'relaxed_permutation' and 'relaxed_inverse_permutation'
// variables are also inputs.
inline void RelaxSupernodes(
    const std::vector<Int>& orig_parents,
    const std::vector<Int>& orig_supernode_sizes,
    const std::vector<Int>& orig_supernode_starts,
    const std::vector<Int>& orig_supernode_parents,
    const std::vector<Int>& orig_supernode_degrees,
    const std::vector<Int>& orig_member_to_index,
    const ScalarLowerStructure& scalar_structure,
    const SupernodalRelaxationControl& control,
    std::vector<Int>* relaxed_permutation,
    std::vector<Int>* relaxed_inverse_permutation,
    std::vector<Int>* relaxed_parents,
    std::vector<Int>* relaxed_supernode_parents,
    std::vector<Int>* relaxed_supernode_degrees,
    std::vector<Int>* relaxed_supernode_sizes,
    std::vector<Int>* relaxed_supernode_starts,
    std::vector<Int>* relaxed_supernode_member_to_index) {
  const Int num_rows = orig_supernode_starts.back();
  const Int num_supernodes = orig_supernode_sizes.size();

  // Construct the down-links for the elimination forest.
  std::vector<Int> children;
  std::vector<Int> child_offsets;
  EliminationForestFromParents(orig_supernode_parents, &children,
                               &child_offsets);

  // Initialize the sizes of the merged supernodes, using the original indexing
  // and absorbing child sizes into the parents.
  std::vector<Int> supernode_sizes = orig_supernode_sizes;

  // Initialize the number of explicit zeros stored in each original supernode.
  std::vector<Int> num_explicit_zeros(num_supernodes, 0);

  std::vector<Int> last_merged_children(num_supernodes, -1);
  std::vector<Int> merge_parents(num_supernodes, -1);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    MergeChildren(supernode, orig_supernode_starts, orig_supernode_sizes,
                  orig_member_to_index, children, child_offsets,
                  scalar_structure, control, &supernode_sizes,
                  &num_explicit_zeros, &last_merged_children, &merge_parents);
  }

  // Count the number of remaining supernodes and construct a map from the
  // original to relaxed indices.
  std::vector<Int> original_to_relaxed(num_supernodes, -1);
  Int num_relaxed_supernodes = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    if (merge_parents[supernode] != -1) {
      continue;
    }
    original_to_relaxed[supernode] = num_relaxed_supernodes++;
  }

  // Fill in the parents for the relaxed supernodal elimination forest.
  // Simultaneously, fill in the degrees of the relaxed supernodes (which are
  // the same as the degrees of the parents of each merge tree).
  relaxed_supernode_parents->resize(num_relaxed_supernodes);
  relaxed_supernode_degrees->resize(num_relaxed_supernodes);
  Int relaxed_offset = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    if (merge_parents[supernode] != -1) {
      continue;
    }
    (*relaxed_supernode_degrees)[relaxed_offset] =
        orig_supernode_degrees[supernode];

    Int parent = orig_supernode_parents[supernode];
    if (parent == -1) {
      // This is a root node, so mark it as such in the relaxation.
      (*relaxed_supernode_parents)[relaxed_offset++] = -1;
      continue;
    }

    // Compute the root of the merge sequence of our parent.
    while (merge_parents[parent] != -1) {
      parent = merge_parents[parent];
    }

    // Convert the root of the merge sequence of our parent from the original
    // to the relaxed indexing.
    const Int relaxed_parent = original_to_relaxed[parent];
    (*relaxed_supernode_parents)[relaxed_offset++] = relaxed_parent;
  }

  // Fill the inverse permutation, the supernode sizes, and the supernode
  // offsets.
  std::vector<Int> relaxation_inverse_permutation(num_rows);
  {
    relaxed_supernode_sizes->resize(num_relaxed_supernodes);
    relaxed_supernode_starts->resize(num_relaxed_supernodes + 1);
    relaxed_supernode_member_to_index->resize(num_rows);
    Int pack_offset = 0;
    for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
      if (merge_parents[supernode] != -1) {
        continue;
      }
      const Int relaxed_supernode = original_to_relaxed[supernode];

      // Get the leaf of the merge sequence to pack.
      Int leaf_of_merge = supernode;
      if (last_merged_children[supernode] != -1) {
        leaf_of_merge = last_merged_children[supernode];
      }

      // Pack the merge sequence and count its total size.
      (*relaxed_supernode_starts)[relaxed_supernode] = pack_offset;
      Int supernode_size = 0;
      Int supernode_to_pack = leaf_of_merge;
      while (true) {
        const Int start = orig_supernode_starts[supernode_to_pack];
        const Int size = orig_supernode_sizes[supernode_to_pack];
        for (Int j = 0; j < size; ++j) {
          (*relaxed_supernode_member_to_index)[pack_offset] = relaxed_supernode;
          relaxation_inverse_permutation[pack_offset++] = start + j;
        }
        supernode_size += size;

        if (merge_parents[supernode_to_pack] == -1) {
          break;
        }
        supernode_to_pack = merge_parents[supernode_to_pack];
      }
      (*relaxed_supernode_sizes)[relaxed_supernode] = supernode_size;
    }
    CATAMARI_ASSERT(num_rows == pack_offset, "Did not pack num_rows indices.");
    (*relaxed_supernode_starts)[num_relaxed_supernodes] = num_rows;
  }

  // Compute the relaxation permutation.
  std::vector<Int> relaxation_permutation(num_rows);
  for (Int row = 0; row < num_rows; ++row) {
    relaxation_permutation[relaxation_inverse_permutation[row]] = row;
  }

  // Permute the 'orig_parents' array to form the 'parents' array.
  relaxed_parents->resize(num_rows);
  for (Int row = 0; row < num_rows; ++row) {
    const Int orig_parent = orig_parents[row];
    if (orig_parent == -1) {
      (*relaxed_parents)[relaxation_permutation[row]] = -1;
    } else {
      (*relaxed_parents)[relaxation_permutation[row]] =
          relaxation_permutation[orig_parent];
    }
  }

  // Compose the relaxation permutation with the original permutation.
  if (relaxed_permutation->empty()) {
    *relaxed_permutation = relaxation_permutation;
    *relaxed_inverse_permutation = relaxation_inverse_permutation;
  } else {
    std::vector<Int> perm_copy = *relaxed_permutation;
    for (Int row = 0; row < num_rows; ++row) {
      (*relaxed_permutation)[row] = relaxation_permutation[perm_copy[row]];
    }

    // Invert the composed permutation.
    for (Int row = 0; row < num_rows; ++row) {
      (*relaxed_inverse_permutation)[(*relaxed_permutation)[row]] = row;
    }
  }
}

// Form the (possibly relaxed) supernodes for the factorization.
template <class Field>
void FormSupernodes(const CoordinateMatrix<Field>& matrix,
                    const SupernodalLDLControl& control,
                    std::vector<Int>* parents,
                    std::vector<Int>* supernode_degrees,
                    std::vector<Int>* supernode_parents,
                    SupernodalLDLFactorization<Field>* factorization) {
  // Compute the non-supernodal elimination tree using the original ordering.
  std::vector<Int> orig_parents, orig_degrees;
  ldl::EliminationForestAndDegrees(matrix, factorization->permutation,
                                   factorization->inverse_permutation,
                                   &orig_parents, &orig_degrees);

  // Greedily compute a supernodal partition using the original ordering.
  std::vector<Int> orig_supernode_sizes;
  ScalarLowerStructure scalar_structure;
  FormFundamentalSupernodes(
      matrix, factorization->permutation, factorization->inverse_permutation,
      orig_parents, orig_degrees, &orig_supernode_sizes, &scalar_structure);

#ifdef CATAMARI_DEBUG
  {
    Int supernode_size_sum = 0;
    for (const Int& supernode_size : orig_supernode_sizes) {
      supernode_size_sum += supernode_size;
    }
    CATAMARI_ASSERT(supernode_size_sum == matrix.NumRows(),
                    "Supernodes did not sum to the matrix size.");
  }
#endif

  std::vector<Int> orig_supernode_starts;
  ldl::OffsetScan(orig_supernode_sizes, &orig_supernode_starts);

  std::vector<Int> orig_member_to_index;
  MemberToIndex(matrix.NumRows(), orig_supernode_starts, &orig_member_to_index);

  std::vector<Int> orig_supernode_degrees;
  SupernodalDegrees(matrix, factorization->permutation,
                    factorization->inverse_permutation, orig_supernode_sizes,
                    orig_supernode_starts, orig_member_to_index, orig_parents,
                    &orig_supernode_degrees);

  const Int num_orig_supernodes = orig_supernode_sizes.size();
  std::vector<Int> orig_supernode_parents;
  ConvertFromScalarToSupernodalEliminationForest(
      num_orig_supernodes, orig_parents, orig_member_to_index,
      &orig_supernode_parents);

  if (control.relaxation_control.relax_supernodes) {
    RelaxSupernodes(
        orig_parents, orig_supernode_sizes, orig_supernode_starts,
        orig_supernode_parents, orig_supernode_degrees, orig_member_to_index,
        scalar_structure, control.relaxation_control,
        &factorization->permutation, &factorization->inverse_permutation,
        parents, supernode_parents, supernode_degrees,
        &factorization->supernode_sizes, &factorization->supernode_starts,
        &factorization->supernode_member_to_index);
  } else {
    *parents = orig_parents;
    *supernode_degrees = orig_supernode_degrees;
    *supernode_parents = orig_supernode_parents;
    factorization->supernode_sizes = orig_supernode_sizes;
    factorization->supernode_starts = orig_supernode_starts;
    factorization->supernode_member_to_index = orig_member_to_index;
  }
}

template <class Field>
LDLResult LeftLooking(const CoordinateMatrix<Field>& matrix,
                      const SupernodalLDLControl& control,
                      SupernodalLDLFactorization<Field>* factorization) {
  const SymmetricFactorizationType factorization_type =
      factorization->factorization_type;

  std::vector<Int> parents;
  std::vector<Int> supernode_degrees;
  std::vector<Int> supernode_parents;
  FormSupernodes(matrix, control, &parents, &supernode_degrees,
                 &supernode_parents, factorization);
  InitializeLeftLookingFactors(matrix, parents, supernode_degrees,
                               factorization);

  const Int num_supernodes = factorization->supernode_sizes.size();
  SupernodalLowerFactor<Field>& lower_factor = *factorization->lower_factor;
  SupernodalDiagonalFactor<Field>& diagonal_factor =
      *factorization->diagonal_factor;

  // Set up a buffer for supernodal updates.
  Int max_supernode_size = 0;
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    max_supernode_size =
        std::max(max_supernode_size, diagonal_factor.blocks[supernode].height);
  }
  std::vector<Field> scaled_transpose_buffer(
      max_supernode_size * max_supernode_size, Field{0});
  std::vector<Field> workspace_buffer(
      max_supernode_size * (max_supernode_size - 1), Field{0});

  // A data structure for marking whether or not a supernode is in the pattern
  // of the active row of the lower-triangular factor.
  std::vector<Int> pattern_flags(num_supernodes);

  // An integer workspace for storing the supernodes in the current row
  // pattern.
  std::vector<Int> row_structure(num_supernodes);

  // Since we will sequentially access each of the entries in each block column
  // of  L during the updates of a supernode, we can avoid the need for binary
  // search by maintaining a separate counter for each supernode.
  std::vector<const Int*> intersect_ptrs(num_supernodes);
  std::vector<Int> rel_rows(num_supernodes);

  LDLResult result;
  for (Int main_supernode = 0; main_supernode < num_supernodes;
       ++main_supernode) {
    BlasMatrix<Field>& main_diagonal_block =
        diagonal_factor.blocks[main_supernode];
    BlasMatrix<Field>& main_lower_block = lower_factor.blocks[main_supernode];
    const Int main_degree = main_lower_block.height;
    const Int main_supernode_size = main_lower_block.width;

    pattern_flags[main_supernode] = main_supernode;

    intersect_ptrs[main_supernode] =
        lower_factor.IntersectionSizes(main_supernode);
    rel_rows[main_supernode] = 0;

    // Compute the supernodal row pattern.
    const Int num_packed = ComputeRowPattern(
        matrix, factorization->permutation, factorization->inverse_permutation,
        factorization->supernode_sizes, factorization->supernode_starts,
        factorization->supernode_member_to_index, supernode_parents,
        main_supernode, pattern_flags.data(), row_structure.data());

    // for J = find(L(K, :))
    //   L(K:n, K) -= L(K:n, J) * (D(J, J) * L(K, J)')
    for (Int index = 0; index < num_packed; ++index) {
      const Int descendant_supernode = row_structure[index];
      CATAMARI_ASSERT(descendant_supernode < main_supernode,
                      "Looking into upper triangle.");
      const ConstBlasMatrix<Field>& descendant_lower_block =
          lower_factor.blocks[descendant_supernode];
      const Int descendant_degree = descendant_lower_block.height;
      const Int descendant_supernode_size = descendant_lower_block.width;

      const Int descendant_main_intersect_size =
          *intersect_ptrs[descendant_supernode];

      const Int descendant_main_rel_row = rel_rows[descendant_supernode];
      ConstBlasMatrix<Field> descendant_main_matrix =
          descendant_lower_block.Submatrix(descendant_main_rel_row, 0,
                                           descendant_main_intersect_size,
                                           descendant_supernode_size);

      BlasMatrix<Field> scaled_transpose;
      scaled_transpose.height = descendant_supernode_size;
      scaled_transpose.width = descendant_main_intersect_size;
      scaled_transpose.leading_dim = descendant_supernode_size;
      scaled_transpose.data = scaled_transpose_buffer.data();

      FormScaledTranspose(
          factorization_type,
          diagonal_factor.blocks[descendant_supernode].ToConst(),
          descendant_main_matrix, &scaled_transpose);

      BlasMatrix<Field> workspace_matrix;
      workspace_matrix.height = descendant_main_intersect_size;
      workspace_matrix.width = descendant_main_intersect_size;
      workspace_matrix.leading_dim = descendant_main_intersect_size;
      workspace_matrix.data = workspace_buffer.data();

      UpdateDiagonalBlock(factorization_type, factorization->supernode_starts,
                          *factorization->lower_factor, main_supernode,
                          descendant_supernode, descendant_main_rel_row,
                          descendant_main_matrix, scaled_transpose.ToConst(),
                          &main_diagonal_block, &workspace_matrix);

      intersect_ptrs[descendant_supernode]++;
      rel_rows[descendant_supernode] += descendant_main_intersect_size;

      // L(KNext:n, K) -= L(KNext:n, J) * (D(J, J) * L(K, J)')
      //                = L(KNext:n, J) * Z(J, K).
      const Int* descendant_active_intersect_size_beg =
          intersect_ptrs[descendant_supernode];
      Int descendant_active_rel_row = rel_rows[descendant_supernode];
      const Int* main_active_intersect_sizes =
          lower_factor.IntersectionSizes(main_supernode);
      Int main_active_rel_row = 0;
      while (descendant_active_rel_row != descendant_degree) {
        const Int descendant_active_intersect_size =
            *descendant_active_intersect_size_beg;

        ConstBlasMatrix<Field> descendant_active_matrix =
            descendant_lower_block.Submatrix(descendant_active_rel_row, 0,
                                             descendant_active_intersect_size,
                                             descendant_supernode_size);

        // The width of the workspace matrix and pointer are already correct.
        workspace_matrix.height = descendant_active_intersect_size;
        workspace_matrix.leading_dim = descendant_active_intersect_size;

        SeekForMainActiveRelativeRow(
            main_supernode, descendant_supernode, descendant_active_rel_row,
            factorization->supernode_member_to_index, lower_factor,
            &main_active_rel_row, &main_active_intersect_sizes);
        const Int main_active_intersect_size = *main_active_intersect_sizes;

        BlasMatrix<Field> main_active_block = main_lower_block.Submatrix(
            main_active_rel_row, 0, main_active_intersect_size,
            main_supernode_size);

        UpdateSubdiagonalBlock(
            main_supernode, descendant_supernode, main_active_rel_row,
            descendant_main_rel_row, descendant_active_rel_row,
            factorization->supernode_starts,
            factorization->supernode_member_to_index,
            scaled_transpose.ToConst(), descendant_active_matrix, lower_factor,
            &main_active_block, &workspace_matrix);

        ++descendant_active_intersect_size_beg;
        descendant_active_rel_row += descendant_active_intersect_size;
      }
    }

    const Int num_supernode_pivots =
        FactorDiagonalBlock(factorization_type, &main_diagonal_block);
    result.num_successful_pivots += num_supernode_pivots;
    if (num_supernode_pivots < main_supernode_size) {
      return result;
    }

    SolveAgainstDiagonalBlock(factorization_type, main_diagonal_block.ToConst(),
                              &main_lower_block);

    // Finish updating the result structure.
    result.largest_supernode =
        std::max(result.largest_supernode, main_supernode_size);
    result.num_factorization_entries +=
        (main_supernode_size * (main_supernode_size + 1)) / 2 +
        main_supernode_size * main_degree;
    result.num_factorization_flops +=
        std::pow(1. * main_supernode_size, 3.) / 3. +
        std::pow(1. * main_degree, 2.) * main_supernode_size;
  }

  return result;
}

}  // namespace supernodal_ldl

template <class Field>
LDLResult LDL(const CoordinateMatrix<Field>& matrix,
              const std::vector<Int>& permutation,
              const std::vector<Int>& inverse_permutation,
              const SupernodalLDLControl& control,
              SupernodalLDLFactorization<Field>* factorization) {
  factorization->permutation = permutation;
  factorization->inverse_permutation = inverse_permutation;
  factorization->factorization_type = control.factorization_type;
  factorization->forward_solve_out_of_place_supernode_threshold =
      control.forward_solve_out_of_place_supernode_threshold;
  factorization->backward_solve_out_of_place_supernode_threshold =
      control.backward_solve_out_of_place_supernode_threshold;
  return supernodal_ldl::LeftLooking(matrix, control, factorization);
}

template <class Field>
LDLResult LDL(const CoordinateMatrix<Field>& matrix,
              const SupernodalLDLControl& control,
              SupernodalLDLFactorization<Field>* factorization) {
  std::vector<Int> permutation, inverse_permutation;
  return LDL(matrix, permutation, inverse_permutation, control, factorization);
}

template <class Field>
void LDLSolve(const SupernodalLDLFactorization<Field>& factorization,
              BlasMatrix<Field>* matrix) {
  const bool have_permutation = !factorization.permutation.empty();

  // Reorder the input into the permutation of the factorization.
  if (have_permutation) {
    Permute(factorization.permutation, matrix);
  }

  LowerTriangularSolve(factorization, matrix);
  DiagonalSolve(factorization, matrix);
  LowerTransposeTriangularSolve(factorization, matrix);

  // Reverse the factorization permutation.
  if (have_permutation) {
    Permute(factorization.inverse_permutation, matrix);
  }
}

template <class Field>
void LowerTriangularSolve(
    const SupernodalLDLFactorization<Field>& factorization,
    BlasMatrix<Field>* matrix) {
  const Int num_rhs = matrix->width;
  const Int num_supernodes = factorization.supernode_sizes.size();
  const SupernodalLowerFactor<Field>& lower_factor =
      *factorization.lower_factor;
  const SupernodalDiagonalFactor<Field>& diagonal_factor =
      *factorization.diagonal_factor;
  const bool is_cholesky =
      factorization.factorization_type == kCholeskyFactorization;

  std::vector<Field> workspace(factorization.max_degree * num_rhs, Field{0});

  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const ConstBlasMatrix<Field> triangular_matrix =
        diagonal_factor.blocks[supernode];

    const Int supernode_size = factorization.supernode_sizes[supernode];
    const Int supernode_start = factorization.supernode_starts[supernode];
    BlasMatrix<Field> matrix_supernode =
        matrix->Submatrix(supernode_start, 0, supernode_size, num_rhs);

    // Solve against the diagonal block of the supernode.
    if (is_cholesky) {
      LeftLowerTriangularSolves(triangular_matrix, &matrix_supernode);
    } else {
      LeftLowerUnitTriangularSolves(triangular_matrix, &matrix_supernode);
    }

    const ConstBlasMatrix<Field> subdiagonal = lower_factor.blocks[supernode];
    if (!subdiagonal.height) {
      continue;
    }

    // Handle the external updates for this supernode.
    const Int* indices = lower_factor.Structure(supernode);
    if (supernode_size >=
        factorization.forward_solve_out_of_place_supernode_threshold) {
      // Perform an out-of-place GEMV.
      BlasMatrix<Field> work_matrix;
      work_matrix.height = subdiagonal.height;
      work_matrix.width = num_rhs;
      work_matrix.leading_dim = subdiagonal.height;
      work_matrix.data = workspace.data();

      // Store the updates in the workspace.
      MatrixMultiplyNormalNormal(Field{-1}, subdiagonal,
                                 matrix_supernode.ToConst(), Field{0},
                                 &work_matrix);

      // Accumulate the workspace into the solution matrix.
      for (Int j = 0; j < num_rhs; ++j) {
        for (Int i = 0; i < subdiagonal.height; ++i) {
          const Int row = indices[i];
          matrix->Entry(row, j) += work_matrix(i, j);
        }
      }
    } else {
      for (Int j = 0; j < num_rhs; ++j) {
        for (Int k = 0; k < supernode_size; ++k) {
          const Field& eta = matrix_supernode(k, j);
          for (Int i = 0; i < subdiagonal.height; ++i) {
            const Int row = indices[i];
            matrix->Entry(row, j) -= subdiagonal(i, k) * eta;
          }
        }
      }
    }
  }
}

template <class Field>
void DiagonalSolve(const SupernodalLDLFactorization<Field>& factorization,
                   BlasMatrix<Field>* matrix) {
  const Int num_rhs = matrix->width;
  const Int num_supernodes = factorization.supernode_sizes.size();
  const SupernodalDiagonalFactor<Field>& diagonal_factor =
      *factorization.diagonal_factor;
  const bool is_cholesky =
      factorization.factorization_type == kCholeskyFactorization;
  if (is_cholesky) {
    // D is the identity.
    return;
  }

  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const ConstBlasMatrix<Field> diagonal_matrix =
        diagonal_factor.blocks[supernode];

    const Int supernode_size = factorization.supernode_sizes[supernode];
    const Int supernode_start = factorization.supernode_starts[supernode];
    BlasMatrix<Field> matrix_supernode =
        matrix->Submatrix(supernode_start, 0, supernode_size, num_rhs);

    // Handle the diagonal-block portion of the supernode.
    for (Int j = 0; j < num_rhs; ++j) {
      for (Int i = 0; i < supernode_size; ++i) {
        matrix_supernode(i, j) /= diagonal_matrix(i, i);
      }
    }
  }
}

template <class Field>
void LowerTransposeTriangularSolve(
    const SupernodalLDLFactorization<Field>& factorization,
    BlasMatrix<Field>* matrix) {
  const Int num_rhs = matrix->width;
  const Int num_supernodes = factorization.supernode_sizes.size();
  const SupernodalLowerFactor<Field>& lower_factor =
      *factorization.lower_factor;
  const SupernodalDiagonalFactor<Field>& diagonal_factor =
      *factorization.diagonal_factor;
  const SymmetricFactorizationType factorization_type =
      factorization.factorization_type;
  const bool is_selfadjoint = factorization_type != kLDLTransposeFactorization;

  std::vector<Field> packed_input_buf(factorization.max_degree * num_rhs);

  for (Int supernode = num_supernodes - 1; supernode >= 0; --supernode) {
    const Int supernode_size = factorization.supernode_sizes[supernode];
    const Int supernode_start = factorization.supernode_starts[supernode];
    const Int* indices = lower_factor.Structure(supernode);

    BlasMatrix<Field> matrix_supernode =
        matrix->Submatrix(supernode_start, 0, supernode_size, num_rhs);

    const ConstBlasMatrix<Field> subdiagonal = lower_factor.blocks[supernode];
    if (subdiagonal.height) {
      // Handle the external updates for this supernode.
      if (supernode_size >=
          factorization.backward_solve_out_of_place_supernode_threshold) {
        // Fill the work matrix.
        BlasMatrix<Field> work_matrix;
        work_matrix.height = subdiagonal.height;
        work_matrix.width = num_rhs;
        work_matrix.leading_dim = subdiagonal.height;
        work_matrix.data = packed_input_buf.data();
        for (Int j = 0; j < num_rhs; ++j) {
          for (Int i = 0; i < subdiagonal.height; ++i) {
            const Int row = indices[i];
            work_matrix(i, j) = matrix->Entry(row, j);
          }
        }

        if (is_selfadjoint) {
          MatrixMultiplyAdjointNormal(Field{-1}, subdiagonal,
                                      work_matrix.ToConst(), Field{1},
                                      &matrix_supernode);
        } else {
          MatrixMultiplyTransposeNormal(Field{-1}, subdiagonal,
                                        work_matrix.ToConst(), Field{1},
                                        &matrix_supernode);
        }
      } else {
        for (Int k = 0; k < supernode_size; ++k) {
          for (Int i = 0; i < subdiagonal.height; ++i) {
            const Int row = indices[i];
            for (Int j = 0; j < num_rhs; ++j) {
              if (is_selfadjoint) {
                matrix_supernode(k, j) -=
                    Conjugate(subdiagonal(i, k)) * matrix->Entry(row, j);
              } else {
                matrix_supernode(k, j) -=
                    subdiagonal(i, k) * matrix->Entry(row, j);
              }
            }
          }
        }
      }
    }

    // Solve against the diagonal block of this supernode.
    const ConstBlasMatrix<Field> triangular_matrix =
        diagonal_factor.blocks[supernode];
    if (factorization_type == kCholeskyFactorization) {
      LeftLowerAdjointTriangularSolves(triangular_matrix, &matrix_supernode);
    } else if (factorization_type == kLDLAdjointFactorization) {
      LeftLowerAdjointUnitTriangularSolves(triangular_matrix,
                                           &matrix_supernode);
    } else {
      LeftLowerTransposeUnitTriangularSolves(triangular_matrix,
                                             &matrix_supernode);
    }
  }
}

template <class Field>
void PrintLowerFactor(const SupernodalLDLFactorization<Field>& factorization,
                      const std::string& label, std::ostream& os) {
  const SupernodalLowerFactor<Field>& lower_factor =
      *factorization.lower_factor;
  const SupernodalDiagonalFactor<Field>& diag_factor =
      *factorization.diagonal_factor;
  const bool is_cholesky =
      factorization.factorization_type == kCholeskyFactorization;

  auto print_entry = [&](const Int& row, const Int& column,
                         const Field& value) {
    os << row << " " << column << " " << value << "\n";
  };

  os << label << ": \n";
  const Int num_supernodes = factorization.supernode_sizes.size();
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int supernode_start = factorization.supernode_starts[supernode];
    const Int* indices = lower_factor.Structure(supernode);

    const ConstBlasMatrix<Field>& diag_matrix = diag_factor.blocks[supernode];
    const ConstBlasMatrix<Field>& lower_matrix = lower_factor.blocks[supernode];

    for (Int j = 0; j < diag_matrix.height; ++j) {
      const Int column = supernode_start + j;

      // Print the portion in the diagonal block.
      if (is_cholesky) {
        print_entry(column, column, diag_matrix(j, j));
      } else {
        print_entry(column, column, Field{1});
      }
      for (Int k = j + 1; k < diag_matrix.height; ++k) {
        const Int row = supernode_start + k;
        print_entry(row, column, diag_matrix(k, j));
      }

      // Print the portion below the diagonal block.
      for (Int i = 0; i < lower_matrix.height; ++i) {
        const Int row = indices[i];
        print_entry(row, column, lower_matrix(i, j));
      }
    }
  }
  os << std::endl;
}

template <class Field>
void PrintDiagonalFactor(const SupernodalLDLFactorization<Field>& factorization,
                         const std::string& label, std::ostream& os) {
  const SupernodalDiagonalFactor<Field>& diag_factor =
      *factorization.diagonal_factor;
  if (factorization.factorization_type == kCholeskyFactorization) {
    // TODO(Jack Poulson): Print the identity.
    return;
  }

  os << label << ": \n";
  const Int num_supernodes = factorization.supernode_sizes.size();
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const ConstBlasMatrix<Field>& diag_matrix = diag_factor.blocks[supernode];
    for (Int j = 0; j < diag_matrix.height; ++j) {
      os << diag_matrix(j, j) << " ";
    }
  }
  os << std::endl;
}

}  // namespace catamari

#endif  // ifndef CATAMARI_SUPERNODAL_LDL_IMPL_H_
