/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_SPARSE_LDL_SUPERNODAL_SUPERNODE_UTILS_IMPL_H_
#define CATAMARI_SPARSE_LDL_SUPERNODAL_SUPERNODE_UTILS_IMPL_H_

#include "catamari/sparse_ldl/supernodal/supernode_utils.hpp"

#include "quotient/index_utils.hpp"

namespace catamari {
namespace supernodal_ldl {

inline void MemberToIndex(Int num_rows, const Buffer<Int>& supernode_starts,
                          Buffer<Int>* member_to_index) {
  member_to_index->Resize(num_rows);

  std::size_t supernode = 0;
  for (Int column = 0; column < num_rows; ++column) {
    if (column == supernode_starts[supernode + 1]) {
      ++supernode;
    }
    CATAMARI_ASSERT(column >= supernode_starts[supernode] &&
                        column < supernode_starts[supernode + 1],
                    "Column was not in the marked supernode.");
    (*member_to_index)[column] = supernode;
  }
  CATAMARI_ASSERT(supernode == supernode_starts.Size() - 2,
                  "Ended on supernode " + std::to_string(supernode) +
                      " instead of " +
                      std::to_string(supernode_starts.Size() - 2));
}

inline void ConvertFromScalarToSupernodalEliminationForest(
    Int num_supernodes, const Buffer<Int>& parents,
    const Buffer<Int>& member_to_index, Buffer<Int>* supernode_parents) {
  const Int num_rows = parents.Size();
  supernode_parents->Resize(num_supernodes, -1);
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

template <class Field>
bool ValidFundamentalSupernodes(const CoordinateMatrix<Field>& matrix,
                                const SymmetricOrdering& ordering,
                                const Buffer<Int>& supernode_sizes) {
  const Int num_rows = matrix.NumRows();
  for (std::size_t index = 0; index < supernode_sizes.Size(); ++index) {
    CATAMARI_ASSERT(supernode_sizes[index] > 0,
                    "Supernode " + std::to_string(index) + " had length " +
                        std::to_string(supernode_sizes[index]));
  }
  {
    Int size_sum = 0;
    for (const Int& supernode_size : supernode_sizes) {
      size_sum += supernode_size;
    }
    CATAMARI_ASSERT(size_sum == num_rows,
                    "Supernode sizes summed to " + std::to_string(size_sum) +
                        " instead of " + std::to_string(num_rows));
  }

  Buffer<Int> parents, degrees;
  scalar_ldl::EliminationForestAndDegrees(matrix, ordering, &parents, &degrees);

  Buffer<Int> supernode_starts, member_to_index;
  OffsetScan(supernode_sizes, &supernode_starts);
  MemberToIndex(num_rows, supernode_starts, &member_to_index);

  Buffer<Int> row_structure(num_rows);
  Buffer<Int> pattern_flags(num_rows);

  bool valid = true;
  for (Int row = 0; row < num_rows; ++row) {
    const Int row_supernode = member_to_index[row];

    pattern_flags[row] = row;
    const Int num_packed = scalar_ldl::ComputeRowPattern(
        matrix, ordering, parents, row, pattern_flags.Data(),
        row_structure.Data());
    std::sort(row_structure.Data(), row_structure.Data() + num_packed);

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

template <class Field>
void FormFundamentalSupernodes(const CoordinateMatrix<Field>& matrix,
                               const SymmetricOrdering& ordering,
                               AssemblyForest* scalar_forest,
                               Buffer<Int>* supernode_sizes,
                               scalar_ldl::LowerStructure* scalar_structure) {
  const Int num_rows = matrix.NumRows();

  // Compute the non-supernodal elimination tree using the original ordering.
  Buffer<Int> scalar_degrees;
  scalar_ldl::EliminationForestAndDegrees(
      matrix, ordering, &scalar_forest->parents, &scalar_degrees);
  scalar_forest->FillFromParents();

  // We will only fill the indices and offsets of the factorization.
  scalar_ldl::FillStructureIndices(matrix, ordering, *scalar_forest,
                                   scalar_degrees, scalar_structure);

  supernode_sizes->Clear();
  if (!num_rows) {
    return;
  }

  // We will iterate down each column structure to determine the supernodes.
  Buffer<Int> column_ptrs = scalar_structure->column_offsets;

  Int supernode_start = 0;
  Int num_supernodes = 0;
  supernode_sizes->Resize(num_rows);
  (*supernode_sizes)[num_supernodes++] = 1;
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
      (*supernode_sizes)[num_supernodes++] = 1;
      continue;
    }

    // Test if the structure of this supernode matches that of the previous
    // column (with all indices up to this column removed). Because the
    // diagonal blocks are dense, each column is a child of the next, so that
    // its structures are nested and their external degrees being equal implies
    // that their structures are as well.

    // Test that the set sizes match.
    const Int column_beg = column_ptrs[column];
    const Int degree = column_ptrs[column + 1] - column_beg;
    const Int prev_column_ptr = column_ptrs[column - 1];
    const Int prev_remaining_degree = column_beg - prev_column_ptr;
    if (degree != prev_remaining_degree) {
      // This column begins a new supernode.
      supernode_start = column;
      (*supernode_sizes)[num_supernodes++] = 1;
      continue;
    }

    // All tests passed, so we may extend the current supernode to incorporate
    // this column.
    ++(*supernode_sizes)[num_supernodes - 1];
  }
  supernode_sizes->Resize(num_supernodes);

#ifdef CATAMARI_DEBUG
  if (!ValidFundamentalSupernodes(matrix, ordering, *supernode_sizes)) {
    std::cerr << "Invalid fundamental supernodes." << std::endl;
    return;
  }
#endif
}

inline MergableStatus MergableSupernode(
    Int child_tail, Int parent_tail, Int child_size, Int parent_size,
    Int num_child_explicit_zeros, Int num_parent_explicit_zeros,
    const Buffer<Int>& orig_member_to_index,
    const scalar_ldl::LowerStructure& scalar_structure,
    const SupernodalRelaxationControl& control) {
  const Int parent = orig_member_to_index[parent_tail];
  const Int child_structure_beg = scalar_structure.column_offsets[child_tail];
  const Int child_structure_end =
      scalar_structure.column_offsets[child_tail + 1];
  const Int parent_structure_beg = scalar_structure.column_offsets[parent_tail];
  const Int parent_structure_end =
      scalar_structure.column_offsets[parent_tail + 1];
  const Int child_degree = child_structure_end - child_structure_beg;
  const Int parent_degree = parent_structure_end - parent_structure_beg;

  MergableStatus status;

  // Count the number of intersections of the child's structure with the
  // parent supernode (and then leave the child structure pointer after the
  // parent supernode).
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
  const Int remaining_child_degree =
      child_degree - num_child_parent_intersections;
  const Int num_missing_structure_indices =
      parent_degree - remaining_child_degree;

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
      remaining_child_degree * child_size +
      /* num_nonzeros(L11) */
      (parent_size + (parent_size + 1)) / 2 +
      /* num_nonzeros(L21) */
      parent_degree * parent_size;
  if (num_zeros <=
      control.allowable_supernode_zero_ratio * num_expanded_entries) {
    status.mergable = true;
    return status;
  }

  status.mergable = false;
  return status;
}

inline void MergeChildren(Int parent, const Buffer<Int>& orig_supernode_starts,
                          const Buffer<Int>& orig_supernode_sizes,
                          const Buffer<Int>& orig_member_to_index,
                          const Buffer<Int>& children,
                          const Buffer<Int>& child_offsets,
                          const scalar_ldl::LowerStructure& scalar_structure,
                          const SupernodalRelaxationControl& control,
                          Buffer<Int>* supernode_sizes,
                          Buffer<Int>* num_explicit_zeros,
                          Buffer<Int>* last_merged_child,
                          Buffer<Int>* merge_parents) {
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

inline void RelaxSupernodes(
    const Buffer<Int>& orig_parents, const Buffer<Int>& orig_supernode_sizes,
    const Buffer<Int>& orig_supernode_starts,
    const Buffer<Int>& orig_supernode_parents,
    const Buffer<Int>& orig_supernode_degrees,
    const Buffer<Int>& orig_member_to_index,
    const scalar_ldl::LowerStructure& scalar_structure,
    const SupernodalRelaxationControl& control,
    Buffer<Int>* relaxed_permutation, Buffer<Int>* relaxed_inverse_permutation,
    Buffer<Int>* relaxed_parents, Buffer<Int>* relaxed_supernode_parents,
    Buffer<Int>* relaxed_supernode_degrees,
    Buffer<Int>* relaxed_supernode_sizes, Buffer<Int>* relaxed_supernode_starts,
    Buffer<Int>* relaxed_supernode_member_to_index) {
  const Int num_rows = orig_supernode_starts.Back();
  const Int num_supernodes = orig_supernode_sizes.Size();

  // Construct the down-links for the elimination forest.
  Buffer<Int> children;
  Buffer<Int> child_offsets;
  quotient::ChildrenFromParents(orig_supernode_parents, &children,
                                &child_offsets);

  // Initialize the sizes of the merged supernodes, using the original indexing
  // and absorbing child sizes into the parents.
  Buffer<Int> supernode_sizes = orig_supernode_sizes;

  // Initialize the number of explicit zeros stored in each original supernode.
  Buffer<Int> num_explicit_zeros(num_supernodes, 0);

  Buffer<Int> last_merged_children(num_supernodes, -1);
  Buffer<Int> merge_parents(num_supernodes, -1);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    MergeChildren(supernode, orig_supernode_starts, orig_supernode_sizes,
                  orig_member_to_index, children, child_offsets,
                  scalar_structure, control, &supernode_sizes,
                  &num_explicit_zeros, &last_merged_children, &merge_parents);
  }

  // Count the number of remaining supernodes and construct a map from the
  // original to relaxed indices.
  Buffer<Int> original_to_relaxed(num_supernodes, -1);
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
  relaxed_supernode_parents->Resize(num_relaxed_supernodes);
  relaxed_supernode_degrees->Resize(num_relaxed_supernodes);
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
  Buffer<Int> relaxation_inverse_permutation(num_rows);
  {
    relaxed_supernode_sizes->Resize(num_relaxed_supernodes);
    relaxed_supernode_starts->Resize(num_relaxed_supernodes + 1);
    relaxed_supernode_member_to_index->Resize(num_rows);
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
  Buffer<Int> relaxation_permutation(num_rows);
  for (Int row = 0; row < num_rows; ++row) {
    relaxation_permutation[relaxation_inverse_permutation[row]] = row;
  }

  // Permute the 'orig_parents' array to form the 'parents' array.
  relaxed_parents->Resize(num_rows);
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
  if (relaxed_permutation->Empty()) {
    *relaxed_permutation = relaxation_permutation;
    *relaxed_inverse_permutation = relaxation_inverse_permutation;
  } else {
    // Compuse the relaxation and original permutations.
    Buffer<Int> perm_copy = *relaxed_permutation;
    for (Int row = 0; row < num_rows; ++row) {
      (*relaxed_permutation)[row] = relaxation_permutation[perm_copy[row]];
    }

    // Invert the composed permutation.
    InvertPermutation(*relaxed_permutation, relaxed_inverse_permutation);
  }
}

template <class Field>
void SupernodalDegrees(const CoordinateMatrix<Field>& matrix,
                       const SymmetricOrdering& ordering,
                       const AssemblyForest& forest,
                       const Buffer<Int>& member_to_index,
                       Buffer<Int>* supernode_degrees) {
  const Int num_rows = matrix.NumRows();
  const Int num_supernodes = ordering.supernode_sizes.Size();
  const bool have_permutation = !ordering.permutation.Empty();

  // A data structure for marking whether or not an index is in the pattern
  // of the active row of the lower-triangular factor.
  Buffer<Int> pattern_flags(num_rows);

  // A data structure for marking whether or not a supernode is in the pattern
  // of the active row of the lower-triangular factor.
  Buffer<Int> supernode_pattern_flags(num_supernodes);

  // Initialize the number of entries that will be stored into each supernode
  supernode_degrees->Resize(num_supernodes, 0);

  const Buffer<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int main_supernode = member_to_index[row];
    pattern_flags[row] = row;
    supernode_pattern_flags[main_supernode] = row;

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int descendant =
          have_permutation ? ordering.permutation[entry.column] : entry.column;
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
        descendant = forest.parents[descendant];
      }
    }
  }
}

template <class Field>
void FillStructureIndices(const CoordinateMatrix<Field>& matrix,
                          const SymmetricOrdering& ordering,
                          const AssemblyForest& forest,
                          const Buffer<Int>& supernode_member_to_index,
                          LowerFactor<Field>* lower_factor) {
  const Int num_rows = matrix.NumRows();
  const Int num_supernodes = ordering.supernode_sizes.Size();
  const bool have_permutation = !ordering.permutation.Empty();

  // A data structure for marking whether or not a node is in the pattern of
  // the active row of the lower-triangular factor.
  Buffer<Int> pattern_flags(num_rows);

  // A data structure for marking whether or not a supernode is in the pattern
  // of the active row of the lower-triangular factor.
  Buffer<Int> supernode_pattern_flags(num_supernodes);

  // A set of pointers for keeping track of where to insert supernode pattern
  // indices.
  Buffer<Int*> supernode_ptrs(num_supernodes);
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    supernode_ptrs[supernode] = lower_factor->StructureBeg(supernode);
  }

  // Fill in the structure indices.
  const Buffer<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int main_supernode = supernode_member_to_index[row];
    pattern_flags[row] = row;
    supernode_pattern_flags[main_supernode] = row;

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      Int descendant =
          have_permutation ? ordering.permutation[entry.column] : entry.column;
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
                      lower_factor->StructureBeg(descendant_supernode) &&
                  supernode_ptrs[descendant_supernode] <
                      lower_factor->StructureEnd(descendant_supernode),
              "Left supernode's indices.");
          *supernode_ptrs[descendant_supernode] = row;
          ++supernode_ptrs[descendant_supernode];
        }

        // Move up to the parent in this subtree of the elimination forest.
        // Moving to the parent will increase the index (but remain bounded
        // from above by 'row').
        descendant = forest.parents[descendant];
      }
    }
  }

#ifdef CATAMARI_DEBUG
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    const Int* index_beg = lower_factor->StructureBeg(supernode);
    const Int* index_end = lower_factor->StructureEnd(supernode);
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
#endif  // ifdef CATAMARI_DEBUG
}

template <class Field>
void FillSubtreeWorkEstimates(Int root, const AssemblyForest& supernode_forest,
                              const LowerFactor<Field>& lower_factor,
                              Buffer<double>* work_estimates) {
  const Int child_beg = supernode_forest.child_offsets[root];
  const Int child_end = supernode_forest.child_offsets[root + 1];
  const Int num_children = child_end - child_beg;

  for (Int child_index = 0; child_index < num_children; ++child_index) {
    const Int child = supernode_forest.children[child_beg + child_index];
    FillSubtreeWorkEstimates(child, supernode_forest, lower_factor,
                             work_estimates);
    (*work_estimates)[root] += (*work_estimates)[child];
  }

  const ConstBlasMatrixView<Field>& lower_block =
      lower_factor.blocks[root].ToConst();
  const Int supernode_size = lower_block.width;
  const Int degree = lower_block.height;
  (*work_estimates)[root] += std::pow(1. * supernode_size, 3.) / 3;
  (*work_estimates)[root] += std::pow(1. * degree, 2.) * supernode_size;
}

template <class Field>
void FillNonzeros(const CoordinateMatrix<Field>& matrix,
                  const SymmetricOrdering& ordering,
                  const Buffer<Int>& supernode_member_to_index,
                  LowerFactor<Field>* lower_factor,
                  DiagonalFactor<Field>* diagonal_factor) {
  const Int num_rows = matrix.NumRows();
  const bool have_permutation = !ordering.permutation.Empty();

  const Buffer<MatrixEntry<Field>>& entries = matrix.Entries();
  for (Int row = 0; row < num_rows; ++row) {
    const Int supernode = supernode_member_to_index[row];
    const Int supernode_start = ordering.supernode_offsets[supernode];

    const Int orig_row =
        have_permutation ? ordering.inverse_permutation[row] : row;
    const Int row_beg = matrix.RowEntryOffset(orig_row);
    const Int row_end = matrix.RowEntryOffset(orig_row + 1);
    for (Int index = row_beg; index < row_end; ++index) {
      const MatrixEntry<Field>& entry = entries[index];
      const Int column =
          have_permutation ? ordering.permutation[entry.column] : entry.column;
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
      const Int* column_index_beg =
          lower_factor->StructureBeg(column_supernode);
      const Int* column_index_end =
          lower_factor->StructureEnd(column_supernode);
      const Int* iter =
          std::lower_bound(column_index_beg, column_index_end, row);
      CATAMARI_ASSERT(iter != column_index_end, "Exceeded column indices.");
      CATAMARI_ASSERT(*iter == row, "Entry (" + std::to_string(row) + ", " +
                                        std::to_string(column) +
                                        ") wasn't in the structure.");
      const Int rel_row = std::distance(column_index_beg, iter);
      const Int rel_column =
          column - ordering.supernode_offsets[column_supernode];
      lower_factor->blocks[column_supernode](rel_row, rel_column) = entry.value;
    }
  }
}

template <class Field>
void FillZeros(const SymmetricOrdering& ordering,
               LowerFactor<Field>* lower_factor,
               DiagonalFactor<Field>* diagonal_factor) {
  const Int num_supernodes = diagonal_factor->blocks.Size();
  for (Int supernode = 0; supernode < num_supernodes; ++supernode) {
    BlasMatrixView<Field>& diagonal_block = diagonal_factor->blocks[supernode];
    std::fill(
        diagonal_block.data,
        diagonal_block.data + diagonal_block.leading_dim * diagonal_block.width,
        Field{0});

    BlasMatrixView<Field>& lower_block = lower_factor->blocks[supernode];
    std::fill(lower_block.data,
              lower_block.data + lower_block.leading_dim * lower_block.width,
              Field{0});
  }
}

template <class Field>
Int ComputeRowPattern(const CoordinateMatrix<Field>& matrix,
                      const Buffer<Int>& permutation,
                      const Buffer<Int>& inverse_permutation,
                      const Buffer<Int>& supernode_sizes,
                      const Buffer<Int>& supernode_starts,
                      const Buffer<Int>& member_to_index,
                      const Buffer<Int>& supernode_parents, Int main_supernode,
                      Int* pattern_flags, Int* row_structure) {
  Int num_packed = 0;
  const bool have_permutation = !permutation.Empty();
  const Buffer<MatrixEntry<Field>>& entries = matrix.Entries();

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

template <class Field>
void FormScaledTranspose(SymmetricFactorizationType factorization_type,
                         const ConstBlasMatrixView<Field>& diagonal_block,
                         const ConstBlasMatrixView<Field>& matrix,
                         BlasMatrixView<Field>* scaled_transpose) {
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

template <class Field>
void UpdateDiagonalBlock(
    SymmetricFactorizationType factorization_type,
    const Buffer<Int>& supernode_starts, const LowerFactor<Field>& lower_factor,
    Int main_supernode, Int descendant_supernode, Int descendant_main_rel_row,
    const ConstBlasMatrixView<Field>& descendant_main_matrix,
    const ConstBlasMatrixView<Field>& scaled_transpose,
    BlasMatrixView<Field>* main_diag_block,
    BlasMatrixView<Field>* workspace_matrix) {
  typedef ComplexBase<Field> Real;
  const Int main_supernode_size = main_diag_block->height;
  const Int descendant_main_intersect_size = scaled_transpose.width;

  const bool inplace_update =
      descendant_main_intersect_size == main_supernode_size;
  BlasMatrixView<Field>* accumulation_block =
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
        lower_factor.StructureBeg(descendant_supernode) +
        descendant_main_rel_row;

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

template <class Field>
void UpdateSubdiagonalBlock(
    Int main_supernode, Int descendant_supernode, Int main_active_rel_row,
    Int descendant_main_rel_row, Int descendant_active_rel_row,
    const Buffer<Int>& supernode_starts,
    const Buffer<Int>& supernode_member_to_index,
    const ConstBlasMatrixView<Field>& scaled_transpose,
    const ConstBlasMatrixView<Field>& descendant_active_matrix,
    const LowerFactor<Field>& lower_factor,
    BlasMatrixView<Field>* main_active_block,
    BlasMatrixView<Field>* workspace_matrix) {
  const Int main_supernode_size = lower_factor.blocks[main_supernode].width;
  const Int main_active_intersect_size = main_active_block->height;
  const Int descendant_main_intersect_size = scaled_transpose.width;
  const Int descendant_active_intersect_size = descendant_active_matrix.height;
  const bool inplace_update =
      main_active_intersect_size == descendant_active_intersect_size &&
      main_supernode_size == descendant_main_intersect_size;

  BlasMatrixView<Field>* accumulation_matrix =
      inplace_update ? main_active_block : workspace_matrix;
  MatrixMultiplyNormalNormal(Field{-1}, descendant_active_matrix,
                             scaled_transpose, Field{1}, accumulation_matrix);

  if (!inplace_update) {
    const Int main_supernode_start = supernode_starts[main_supernode];

    const Int* main_indices = lower_factor.StructureBeg(main_supernode);
    const Int* main_active_indices = main_indices + main_active_rel_row;

    const Int* descendant_indices =
        lower_factor.StructureBeg(descendant_supernode);
    const Int* descendant_main_indices =
        descendant_indices + descendant_main_rel_row;
    const Int* descendant_active_indices =
        descendant_indices + descendant_active_rel_row;

    CATAMARI_ASSERT(
        workspace_matrix->height == descendant_active_intersect_size,
        "workspace_matrix.height != descendant_active_intersect_size");
    CATAMARI_ASSERT(workspace_matrix->width == descendant_main_intersect_size,
                    "workspace_matrix.width != descendant_main_intersect_size");

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
      CATAMARI_ASSERT(i_rel < main_active_block->height,
                      "i_rel out-of-bounds for main_active_block.");

      for (Int j = 0; j < descendant_main_intersect_size; ++j) {
        const Int column = descendant_main_indices[j];
        const Int j_rel = column - main_supernode_start;
        CATAMARI_ASSERT(j_rel < main_active_block->width,
                        "j_rel out-of-bounds for main_active_block.");

        main_active_block->Entry(i_rel, j_rel) += workspace_matrix->Entry(i, j);
        workspace_matrix->Entry(i, j) = 0;
      }
    }
  }
}

template <class Field>
void SeekForMainActiveRelativeRow(Int main_supernode, Int descendant_supernode,
                                  Int descendant_active_rel_row,
                                  const Buffer<Int>& supernode_member_to_index,
                                  const LowerFactor<Field>& lower_factor,
                                  Int* main_active_rel_row,
                                  const Int** main_active_intersect_sizes) {
  const Int* main_indices = lower_factor.StructureBeg(main_supernode);
  const Int* descendant_indices =
      lower_factor.StructureBeg(descendant_supernode);
  const Int descendant_active_supernode_start =
      descendant_indices[descendant_active_rel_row];
  const Int active_supernode =
      supernode_member_to_index[descendant_active_supernode_start];
  CATAMARI_ASSERT(active_supernode > main_supernode,
                  "Active supernode " + std::to_string(active_supernode) +
                      " was <= the main supernode " +
                      std::to_string(main_supernode));

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

template <class Field>
void MergeChildSchurComplements(Int supernode,
                                const SymmetricOrdering& ordering,
                                LowerFactor<Field>* lower_factor,
                                DiagonalFactor<Field>* diagonal_factor,
                                RightLookingSharedState<Field>* shared_state) {
  const Int child_beg = ordering.assembly_forest.child_offsets[supernode];
  const Int child_end = ordering.assembly_forest.child_offsets[supernode + 1];
  const Int num_children = child_end - child_beg;
  BlasMatrixView<Field> lower_block = lower_factor->blocks[supernode];
  BlasMatrixView<Field> diagonal_block = diagonal_factor->blocks[supernode];
  BlasMatrixView<Field> schur_complement =
      shared_state->schur_complements[supernode];

  const Int supernode_size = ordering.supernode_sizes[supernode];
  const Int supernode_start = ordering.supernode_offsets[supernode];
  const Int* main_indices = lower_factor->StructureBeg(supernode);
  for (Int child_index = 0; child_index < num_children; ++child_index) {
    const Int child =
        ordering.assembly_forest.children[child_beg + child_index];
    const Int* child_indices = lower_factor->StructureBeg(child);
    Buffer<Field>& child_schur_complement_buffer =
        shared_state->schur_complement_buffers[child];
    BlasMatrixView<Field>& child_schur_complement =
        shared_state->schur_complements[child];
    const Int child_degree = child_schur_complement.height;

    // Fill the mapping from the child structure into the parent front.
    Int num_child_diag_indices = 0;
    Buffer<Int> child_rel_indices(child_degree);
    {
      Int i_rel = supernode_size;
      for (Int i = 0; i < child_degree; ++i) {
        const Int row = child_indices[i];
        if (row < supernode_start + supernode_size) {
          child_rel_indices[i] = row - supernode_start;
          ++num_child_diag_indices;
        } else {
          while (main_indices[i_rel - supernode_size] != row) {
            ++i_rel;
            CATAMARI_ASSERT(i_rel < supernode_size + schur_complement.height,
                            "Relative index is out-of-bounds.");
          }
          child_rel_indices[i] = i_rel;
        }
      }
    }

    // Add the child Schur complement into this supernode's front.
    for (Int j = 0; j < child_degree; ++j) {
      const Int j_rel = child_rel_indices[j];
      if (j < num_child_diag_indices) {
        // Contribute into the upper-left diagonal block of the front.
        for (Int i = j; i < num_child_diag_indices; ++i) {
          const Int i_rel = child_rel_indices[i];
          diagonal_block(i_rel, j_rel) += child_schur_complement(i, j);
        }

        // Contribute into the lower-left block of the front.
        for (Int i = num_child_diag_indices; i < child_degree; ++i) {
          const Int i_rel = child_rel_indices[i];
          lower_block(i_rel - supernode_size, j_rel) +=
              child_schur_complement(i, j);
        }
      } else {
        // Contribute into the bottom-right block of the front.
        for (Int i = j; i < child_degree; ++i) {
          const Int i_rel = child_rel_indices[i];
          schur_complement(i_rel - supernode_size, j_rel - supernode_size) +=
              child_schur_complement(i, j);
        }
      }
    }

    child_schur_complement.height = 0;
    child_schur_complement.width = 0;
    child_schur_complement.data = nullptr;
    child_schur_complement_buffer.Clear();
  }
}

template <class Field>
Int FactorDiagonalBlock(Int block_size,
                        SymmetricFactorizationType factorization_type,
                        BlasMatrixView<Field>* diagonal_block) {
  Int num_pivots;
  if (factorization_type == kCholeskyFactorization) {
    num_pivots = LowerCholeskyFactorization(block_size, diagonal_block);
  } else if (factorization_type == kLDLAdjointFactorization) {
    num_pivots = LowerLDLAdjointFactorization(block_size, diagonal_block);
  } else {
    num_pivots = LowerLDLTransposeFactorization(block_size, diagonal_block);
  }
  return num_pivots;
}

template <class Field>
void SolveAgainstDiagonalBlock(
    SymmetricFactorizationType factorization_type,
    const ConstBlasMatrixView<Field>& triangular_matrix,
    BlasMatrixView<Field>* lower_matrix) {
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

}  // namespace supernodal_ldl
}  // namespace catamari

#include "catamari/sparse_ldl/supernodal/supernode_utils/openmp-impl.hpp"

#endif  // ifndef CATAMARI_SPARSE_LDL_SUPERNODAL_SUPERNODE_UTILS_IMPL_H_