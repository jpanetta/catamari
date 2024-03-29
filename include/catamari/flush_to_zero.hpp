/*
 * Copyright (c) 2019 Jack Poulson <jack@hodgestar.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef CATAMARI_FLUSH_TO_ZERO_H_
#define CATAMARI_FLUSH_TO_ZERO_H_

#ifdef CATAMARI_HAVE_XMMINTRIN
#include <xmmintrin.h>
#elif CATAMARI_HAVE_FENV_DISABLE_DENORMS
#include <cfenv>
#endif  // ifdef CATAMARI_HAVE_XMMINTRIN

namespace catamari {

// Avoid the potential for order-of-magnitude performance degradation from
// slow subnormal processing.
//
// Note that this flushing is not guaranteed on all platforms.
inline void EnableFlushToZero() {
#ifdef CATAMARI_HAVE_XMMINTRIN
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#elif CATAMARI_HAVE_FENV_DISABLE_DENORMS
#pragma FENV_ACCESS ON
#ifdef X86
  fesetenv(FE_DFL_DISABLE_SSE_DENORMS_ENV);
#elif defined(ARM)
  fesetenv(FE_DFL_DISABLE_DENORMS_ENV);
#endif  // ifdef X86
#endif  // ifdef CATAMARI_HAVE_XMMINTRIN
}

struct ScopedEnableFlushToZero {
    ScopedEnableFlushToZero() {
#ifdef CATAMARI_HAVE_XMMINTRIN
        m_mode = _MM_GET_FLUSH_ZERO_MODE();
#else
        fegetenv(&m_env);
#endif
        EnableFlushToZero();
    }

    ~ScopedEnableFlushToZero() {
#ifdef CATAMARI_HAVE_XMMINTRIN
        _MM_SET_FLUSH_ZERO_MODE(m_mode);
#else
        fesetenv(&m_env);
#endif
    }

private:
#ifdef CATAMARI_HAVE_XMMINTRIN
    int m_mode;
#else
    fenv_t m_env;
#endif
};

}  // namespace catamari

#endif  // ifndef CATAMARI_FLUSH_TO_ZERO_H_
