/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEBGPU)

#include "WHLSLBlock.h"
#include "WHLSLFunctionDeclaration.h"

namespace WebCore {

namespace WHLSL {

namespace AST {

class FunctionDefinition : public FunctionDeclaration {
public:
    FunctionDefinition(FunctionDeclaration&& functionDeclaration, Block&& block, bool restricted)
        : FunctionDeclaration(WTFMove(functionDeclaration))
        , m_block(WTFMove(block))
        , m_restricted(restricted)
    {
    }

    virtual ~FunctionDefinition() = default;

    FunctionDefinition(const FunctionDefinition&) = delete;
    FunctionDefinition(FunctionDefinition&&) = default;

    bool isFunctionDefinition() const override { return true; }

    Block& block() { return m_block; }
    bool restricted() const { return m_restricted; }

private:
    Block m_block;
    bool m_restricted;
};

} // namespace AST

}

}

SPECIALIZE_TYPE_TRAITS_WHLSL_FUNCTION_DECLARATION(FunctionDefinition, isFunctionDefinition())

#endif