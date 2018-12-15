/*
 * Copyright (c) 2018 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_LDL_IMPL_H_
#define CATAMARI_LDL_IMPL_H_

#include <memory>

#include "catamari/supernodal_ldl.hpp"

namespace catamari {

template <class Field>
Int LDL(
    const CoordinateMatrix<Field>& matrix, const LDLControl& control,
    LDLFactorization<Field>* factorization) {
  bool use_supernodal;
  if (control.supernodal_strategy == kScalarFactorization) {
    use_supernodal = false;
  } else if (control.supernodal_strategy == kSupernodalFactorization) {
    use_supernodal = true;
  } else {
    // TODO(Jack Poulson): Use a more intelligent means of selecting.
    use_supernodal = true;
  }

  factorization->is_supernodal = use_supernodal;
  if (use_supernodal) {
    factorization->supernodal_factorization.reset(
        new SupernodalLDLFactorization<Field>);
    return LDL(
        matrix, control.supernodal_control,
        factorization->supernodal_factorization.get());
  } else {
    factorization->scalar_factorization.reset(
        new ScalarLDLFactorization<Field>);
    return LDL(matrix, control.scalar_control,
        factorization->scalar_factorization.get());
  }
}

template <class Field>
void LDLSolve(
    const LDLFactorization<Field>& factorization, std::vector<Field>* vector) {
  if (factorization.is_supernodal) {
    LDLSolve(*factorization.supernodal_factorization.get(), vector);
  } else {
    LDLSolve(*factorization.scalar_factorization.get(), vector);
  }
}

template <class Field>
void LowerTriangularSolve(
    const LDLFactorization<Field>& factorization, std::vector<Field>* vector) {
  if (factorization.is_supernodal) {
    LowerTriangularSolve(*factorization.supernodal_factorization.get(), vector);
  } else {
    LowerTriangularSolve(*factorization.scalar_factorization.get(), vector);
  }
}

template <class Field>
void DiagonalSolve(
    const LDLFactorization<Field>& factorization, std::vector<Field>* vector) {
  if (factorization.is_supernodal) {
    DiagonalSolve(*factorization.supernodal_factorization.get(), vector);
  } else {
    DiagonalSolve(*factorization.scalar_factorization.get(), vector);
  }
}

template <class Field>
void LowerAdjointTriangularSolve(
    const LDLFactorization<Field>& factorization, std::vector<Field>* vector) {
  if (factorization.is_supernodal) {
    LowerAdjointTriangularSolve(
        *factorization.supernodal_factorization.get(), vector);
  } else {
    LowerAdjointTriangularSolve(
        *factorization.scalar_factorization.get(), vector);
  }
}

template <class Field>
void PrintLowerFactor(
    const LDLFactorization<Field>& factorization, const std::string& label,
    std::ostream& os) {
  if (factorization.is_supernodal) {
    PrintLowerFactor(*factorization.supernodal_factorization.get(), label, os);
  } else {
    PrintLowerFactor(*factorization.scalar_factorization.get(), label, os);
  }
}

template <class Field>
void PrintDiagonalFactor(
    const LDLFactorization<Field>& factorization, const std::string& label,
    std::ostream& os) {
  if (factorization.is_supernodal) {
    PrintDiagonalFactor(
        *factorization.supernodal_factorization.get(), label, os);
  } else {
    PrintDiagonalFactor(*factorization.scalar_factorization.get(), label, os);
  }
}

}  // namespace catamari

#endif  // ifndef CATAMARI_LDL_IMPL_H_
