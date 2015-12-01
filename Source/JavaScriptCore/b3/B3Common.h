/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef B3Common_h
#define B3Common_h

#if ENABLE(B3_JIT)

namespace JSC { namespace B3 {

inline bool is64Bit() { return sizeof(void*) == 8; }
inline bool is32Bit() { return !is64Bit(); }

bool shouldDumpIR();
bool shouldDumpIRAtEachPhase();
bool shouldValidateIR();
bool shouldValidateIRAtEachPhase();
bool shouldSaveIRBeforePhase();
bool shouldMeasurePhaseTiming();

template<typename ResultType, typename InputType, typename BitsType>
inline bool isRepresentableAsImpl(InputType originalValue)
{
    // Get the raw bits of the original value.
    BitsType originalBits = bitwise_cast<BitsType>(originalValue);

    // Convert the original value to the desired result type.
    ResultType result = static_cast<ResultType>(originalValue);

    // Convert the converted value back to the original type. The original value is representable
    // using the new type if such round-tripping doesn't lose bits.
    InputType newValue = static_cast<InputType>(result);

    // Get the raw bits of the round-tripped value.
    BitsType newBits = bitwise_cast<BitsType>(newValue);
    
    return originalBits == newBits;
}

template<typename ResultType>
inline bool isRepresentableAs(int32_t value)
{
    return isRepresentableAsImpl<ResultType, int32_t, int32_t>(value);
}

template<typename ResultType>
inline bool isRepresentableAs(int64_t value)
{
    return isRepresentableAsImpl<ResultType, int64_t, int64_t>(value);
}

template<typename ResultType>
inline bool isRepresentableAs(double value)
{
    return isRepresentableAsImpl<ResultType, double, int64_t>(value);
}

} } // namespace JSC::B3

#endif // ENABLE(B3_JIT)

#endif // B3Common_h
