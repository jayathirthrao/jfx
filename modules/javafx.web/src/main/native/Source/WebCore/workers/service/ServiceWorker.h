/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include "ActiveDOMObject.h"
#include "ContextDestructionObserver.h"
#include "EventTarget.h"
#include "ServiceWorkerData.h"
#include <JavaScriptCore/Strong.h>
#include <wtf/RefCounted.h>
#include <wtf/URL.h>

namespace JSC {
class JSGlobalObject;
class JSValue;
}

namespace WebCore {

class LocalFrame;
class SWClientConnection;

struct StructuredSerializeOptions;

class ServiceWorker final : public RefCounted<ServiceWorker>, public EventTarget, public ActiveDOMObject {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED_EXPORT(ServiceWorker, WEBCORE_EXPORT);
public:
    using State = ServiceWorkerState;
    static Ref<ServiceWorker> getOrCreate(ScriptExecutionContext&, ServiceWorkerData&&);

    WEBCORE_EXPORT virtual ~ServiceWorker();

    const URL& scriptURL() const { return m_data.scriptURL; }

    State state() const { return m_data.state; }

    void updateState(State);

    ExceptionOr<void> postMessage(JSC::JSGlobalObject&, JSC::JSValue message, StructuredSerializeOptions&&);

    ServiceWorkerIdentifier identifier() const { return m_data.identifier; }
    ServiceWorkerRegistrationIdentifier registrationIdentifier() const { return m_data.registrationIdentifier; }
    WorkerType workerType() const { return m_data.type; }

    // ActiveDOMObject.
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    const ServiceWorkerData& data() const { return m_data; }

private:
    ServiceWorker(ScriptExecutionContext&, ServiceWorkerData&&);
    void updatePendingActivityForEventDispatch();

    enum EventTargetInterfaceType eventTargetInterface() const final;
    ScriptExecutionContext* scriptExecutionContext() const final;
    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }

    // ActiveDOMObject.
    void stop() final;

    SWClientConnection& swConnection();

    ServiceWorkerData m_data;
    bool m_isStopped { false };
    RefPtr<PendingActivity<ServiceWorker>> m_pendingActivityForEventDispatch;
};

} // namespace WebCore
