/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_COORDINATE_MATRIX_H_
#define CATAMARI_COORDINATE_MATRIX_H_

#include "catamari/buffer.hpp"
#include "catamari/integers.hpp"
#include "catamari/macros.hpp"
#include "quotient/coordinate_graph.hpp"

namespace catamari {

using quotient::EntryMask;
using quotient::GraphEdge;
using quotient::MatrixEntry;
using quotient::SwapClearVector;

// A coordinate-format sparse matrix data structure. The primary storage is a
// lexicographically sorted Buffer<MatrixEntry<Ring>> and an associated
// Buffer<Int> of row offsets (which serve the same role as in a Compressed
// Sparse Row (CSR) format). Thus, this storage scheme is a superset of the CSR
// format that explicitly stores both row and column indices for each entry.
//
// The class is designed so that the sorting and offset computation overhead
// can be amortized over batches of entry additions and removals.
//
// For example, the code block:
//
//   catamari::CoordinateMatrix<double> matrix;
//   matrix.Resize(5, 5);
//   matrix.ReserveEntryAdditions(6);
//   matrix.QueueEntryAddition(3, 4, 1.);
//   matrix.QueueEntryAddition(2, 3, 2.);
//   matrix.QueueEntryAddition(2, 0, -1.);
//   matrix.QueueEntryAddition(4, 2, -2.);
//   matrix.QueueEntryAddition(4, 4, 3.);
//   matrix.QueueEntryAddition(3, 2, 4.);
//   matrix.FlushEntryQueues();
//   const catamari::Buffer<catamari::MatrixEntry<double>>& entries =
//       matrix.Entries();
//
// would return a reference to the underlying
// catamari::Buffer<catamari::MatrixEntry<double>> of 'matrix', which should
// contain the entry sequence:
//   (2, 0, -1.), (2, 3, 2.), (3, 2, 4.), (3, 4, 1.), (4, 2, -2.), (4, 4, 3.).
//
// Similarly, subsequently running the code block:
//
//   matrix.ReserveEntryRemovals(2);
//   matrix.QueueEntryRemoval(2, 3);
//   matrix.QueueEntryRemoval(0, 4);
//   matrix.FlushEntryQueues();
//
// would modify the Buffer underlying the 'entries' reference to now
// contain the entry sequence:
//   (2, 0, -1.), (3, 2, 4.), (3, 4, 1.), (4, 2, -2.), (4, 4, 3.).
//
// We also allow entry-wise value access via
//
//   Ring CoordinateMatrix<Ring>::Value(Int row, Int column) const,
//
// which returns zero if there is no nonzero entry in the given location.
//
// TODO(Jack Poulson): Add support for 'END' index marker so that ranges
// can be easily incorporated.
template <class Ring>
class CoordinateMatrix {
 public:
  // The trivial constructor.
  CoordinateMatrix();

  // The copy constructor.
  CoordinateMatrix(const CoordinateMatrix<Ring>& matrix);

  // The assignment operator.
  CoordinateMatrix<Ring>& operator=(const CoordinateMatrix<Ring>& matrix);

  // Builds and returns a CoordinateMatrix from a Matrix Market description.
  static std::unique_ptr<CoordinateMatrix<Ring>> FromMatrixMarket(
      const std::string& filename, bool skip_explicit_zeros,
      EntryMask mask = EntryMask::kEntryMaskFull);

  // Writes a copy of the CoordinateMatrix to a Matrix Market file.
  void ToMatrixMarket(const std::string& filename) const;

  // A trivial destructor.
  ~CoordinateMatrix();

  // Returns the number of rows in the matrix.
  Int NumRows() const CATAMARI_NOEXCEPT;

  // Returns the number of columns in the matrix.
  Int NumColumns() const CATAMARI_NOEXCEPT;

  // Returns the number of (usually nonzero) entries in the matrix.
  Int NumEntries() const CATAMARI_NOEXCEPT;

  // Removes all entries and changes the number of rows and columns to zero.
  void Empty();

  // Changes both the number of rows and columns.
  void Resize(Int num_rows, Int num_columns);

  // Allocates space so that up to 'max_entry_additions' calls to
  // 'QueueEntryAddition' can be performed without another memory allocation.
  void ReserveEntryAdditions(Int max_entry_additions);

  // Appends the entry to the entry addition list without putting the entry
  // list in lexicographic order or updating the row offsets.
  void QueueEntryAddition(Int row, Int column, const Ring& value);
  void QueueEntryAddition(const MatrixEntry<Ring>& entry);

  // Appends a list of entries to the entry addition list without putting the
  // entry list in lexicographic order or updating the row offsets.
  void QueueEntryAdditions(const std::vector<MatrixEntry<Ring>>& entries);
  void QueueEntryAdditions(const Buffer<MatrixEntry<Ring>>& entries);

  // Allocates space so that up to 'max_entry_removals' calls to
  // 'QueueEntryRemoval' can be performed without another memory allocation.
  void ReserveEntryRemovals(Int max_entry_removals);

  // Appends the location (row, column) to the list of entries to remove.
  void QueueEntryRemoval(Int row, Int column);

  // All queued entry additions and removals are applied, the entry list is
  // lexicographically sorted (the entries with the same locations are summed)
  // and the row offsets are updated.
  void FlushEntryQueues();

  void SetSortedEntries(const Buffer<MatrixEntry<Ring>> &sortedEntries) {
      entries_ = sortedEntries;
      UpdateRowEntryOffsets();
  }

  void SetSortedEntries(Buffer<MatrixEntry<Ring>> &&sortedEntries) {
      entries_ = std::move(sortedEntries);
      UpdateRowEntryOffsets();
  }

  // Returns true if there are no entries queued for addition or removal.
  bool EntryQueuesAreEmpty() const CATAMARI_NOEXCEPT;

  // Adds the entry (row, column, value) into the matrix.
  //
  // NOTE: This routine involves a merge sort involving all of the entries. It
  // is preferable to amortize this cost by batching together several entry
  // additions.
  void AddEntry(Int row, Int column, const Ring& value);
  void AddEntry(const MatrixEntry<Ring>& entry);

  // Adds a set of entries into the matrix.
  //
  // NOTE: This routine involves a merge sort involving all of the entries. If
  // multiple calls are to be performed in sequence, it is preferable to
  // amortize this cost by batching together several entry additions.
  void AddEntries(const std::vector<MatrixEntry<Ring>>& entries);
  void AddEntries(const Buffer<MatrixEntry<Ring>>& entries);

  // Removed the entry (row, column) from the matrix.
  //
  // NOTE: This routine involves a merge sort involving all of the entries. It
  // is preferable to amortize this cost by batching together several entry
  // removals.
  void RemoveEntry(Int row, Int column);

  // Changes a pre-existing entry at position (row, column) to the new value.
  void ReplaceEntry(Int row, Int column, const Ring& value);
  void ReplaceEntry(const MatrixEntry<Ring>& entry);

  // Changes a set of entries to the given values. Note that pre-existing
  // entries at positions not in the following list are unchanged, and that
  // the behavior is undefined if duplicate replacements are applied to the same
  // position.
  void ReplaceEntries(const std::vector<MatrixEntry<Ring>>& entries);
  void ReplaceEntries(const Buffer<MatrixEntry<Ring>>& entries);

  // Adds the given value to a pre-existing entry at position (row, column).
  void AddToEntry(Int row, Int column, const Ring& value);
  void AddToEntry(const MatrixEntry<Ring>& entry);

  // Returns a reference to the entry with the given index.
  const MatrixEntry<Ring>& Entry(Int entry_index) const CATAMARI_NOEXCEPT;

  // Returns a reference to the underlying vector of entries.
  // NOTE: Only the values are meant to be directly modified.
  Buffer<MatrixEntry<Ring>>& Entries() CATAMARI_NOEXCEPT;

  // Returns a reference to the underlying vector of entries.
  const Buffer<MatrixEntry<Ring>>& Entries() const CATAMARI_NOEXCEPT;

  // Direct access to the row entry offsets vector.
  // NOTE: Not recommended for typical usage.
  Buffer<Int>& RowEntryOffsets() CATAMARI_NOEXCEPT;

  // Immutable direct access to the row entry offsets vector.
  // NOTE: Not recommended for typical usage.
  const Buffer<Int>& RowEntryOffsets() const CATAMARI_NOEXCEPT;

  // Returns the offset into the entry vector where entries from the given row
  // begin.
  Int RowEntryOffset(Int row) const CATAMARI_NOEXCEPT;

  // Returns the offset into the entry vector where the (row, column) entry
  // would be inserted.
  Int EntryOffset(Int row, Int column) const CATAMARI_NOEXCEPT;

  // Returns true if there is an entry at position (row, column).
  bool EntryExists(Int row, Int column) const CATAMARI_NOEXCEPT;

  // Returns the (non-modifiable) value of the sparse matrix's given entry.
  Ring Value(Int row, Int column) const CATAMARI_NOEXCEPT;

  // Returns the number of columns where the given row has entries.
  Int NumRowEntries(Int row) const CATAMARI_NOEXCEPT;

  // Returns a CoordinateGraph representing the nonzero pattern of the sparse
  // matrix.
  std::unique_ptr<quotient::CoordinateGraph> CoordinateGraph() const
      CATAMARI_NOEXCEPT;

 private:
  // The height of the matrix.
  Int num_rows_;

  // The width of the matrix.
  Int num_columns_;

  // The (lexicographically sorted) list of entries in the sparse matrix.
  // TODO(Jack Poulson): Avoid unnecessary traversals of this array due to
  // MatrixEntry not satisfying std::is_trivially_copy_constructible.
  Buffer<MatrixEntry<Ring>> entries_;

  // A list of length 'num_rows_ + 1', where 'row_entry_offsets_[row]' indicates
  // the location in 'entries_' where an entry with indices (row, column) would
  // be inserted (ignoring any sorting based upon the value).
  Buffer<Int> row_entry_offsets_;

  // The list of entries currently queued for addition into the sparse matrix.
  std::vector<MatrixEntry<Ring>> entries_to_add_;

  // The list of entries currently queued for removal from the sparse matrix.
  std::vector<GraphEdge> entries_to_remove_;

  // Incorporates the entries currently residing in 'entries_to_add_' into the
  // sparse matrix (and then clears 'entries_to_add_').
  void FlushEntryAdditionQueue(bool update_row_entry_offsets);

  // Removes the entries residing in 'entries_to_remove_' from the sparse matrix
  // (and then clears 'entries_to_remove_').
  void FlushEntryRemovalQueue(bool update_row_entry_offsets);

  // Recomputes 'row_entry_offsets_' based upon the current value of 'entries_'.
  void UpdateRowEntryOffsets();

  // Packs a sorted list of entries by summing the floating-point values of
  // entries with the same indices.
  static void CombineSortedEntries(std::vector<MatrixEntry<Ring>>* entries);
  static void CombineSortedEntries(Buffer<MatrixEntry<Ring>>* entries);
};

// Pretty-prints the CoordinateMatrix.
template <class Ring>
std::ostream& operator<<(std::ostream& os,
                         const CoordinateMatrix<Ring>& matrix);
template <class Ring>
void Print(const CoordinateMatrix<Ring>& matrix, const std::string& label,
           std::ostream& os);

}  // namespace catamari

#include "catamari/coordinate_matrix-impl.hpp"

#endif  // ifndef CATAMARI_COORDINATE_MATRIX_H_
