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

#include "config.h"

#include "APICast.h"
#include "JSCJSValueInlines.h"
#include "JSObject.h"

#include <JavaScriptCore/JSObjectRefPrivate.h>
#include <JavaScriptCore/JavaScript.h>
#include <wtf/DataLog.h>
#include <wtf/Expected.h>
#include <wtf/Noncopyable.h>
#include <wtf/NumberOfCores.h>
#include <wtf/Vector.h>

extern "C" int testCAPIViaCpp(const char* filter);

class APIString {
    WTF_MAKE_NONCOPYABLE(APIString);
public:

    APIString(const char* string)
        : m_string(JSStringCreateWithUTF8CString(string))
    {
    }

    ~APIString()
    {
        JSStringRelease(m_string);
    }

    operator JSStringRef() { return m_string; }

private:
    JSStringRef m_string;
};

class APIContext {
    WTF_MAKE_NONCOPYABLE(APIContext);
public:

    APIContext()
        : m_context(JSGlobalContextCreate(nullptr))
    {
        APIString print("print");
        JSObjectRef printFunction = JSObjectMakeFunctionWithCallback(m_context, print, [] (JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef*) {

            JSC::ExecState* exec = toJS(ctx);
            for (unsigned i = 0; i < argumentCount; i++)
                dataLog(toJS(exec, arguments[i]));
            dataLogLn();
            return JSValueMakeUndefined(ctx);
        });

        JSObjectSetProperty(m_context, JSContextGetGlobalObject(m_context), print, printFunction, kJSPropertyAttributeNone, nullptr);
    }

    ~APIContext()
    {
        JSGlobalContextRelease(m_context);
    }

    operator JSGlobalContextRef() { return m_context; }
    operator JSC::ExecState*() { return toJS(m_context); }

private:
    JSGlobalContextRef m_context;
};

template<typename T>
class APIVector : protected Vector<T> {
    using Base = Vector<T>;
public:
    APIVector(APIContext& context)
        : Base()
        , m_context(context)
    {
    }

    ~APIVector()
    {
        for (auto& value : *this)
            JSValueUnprotect(m_context, value);
    }

    using Vector<T>::operator[];
    using Vector<T>::size;
    using Vector<T>::begin;
    using Vector<T>::end;
    using typename Vector<T>::iterator;

    void append(T value)
    {
        JSValueProtect(m_context, value);
        Base::append(WTFMove(value));
    }

private:
    APIContext& m_context;
};

class TestAPI {
public:
    int run(const char* filter);

    void basicSymbol();
    void symbolsTypeof();
    void symbolsGetPropertyForKey();
    void symbolsSetPropertyForKey();
    void symbolsHasPropertyForKey();
    void symbolsDeletePropertyForKey();
    void promiseResolveTrue();
    void promiseRejectTrue();

    int failed() const { return m_failed; }

private:

    template<typename... Strings>
    bool check(bool condition, Strings... message);

    template<typename JSFunctor, typename APIFunctor>
    void checkJSAndAPIMatch(const JSFunctor&, const APIFunctor&, const char* description);

    // Helper methods.
    using ScriptResult = Expected<JSValueRef, JSValueRef>;
    ScriptResult evaluateScript(const char* script, JSObjectRef thisObject = nullptr);
    template<typename... ArgumentTypes>
    ScriptResult callFunction(const char* functionSource, ArgumentTypes... arguments);
    template<typename... ArgumentTypes>
    bool functionReturnsTrue(const char* functionSource, ArgumentTypes... arguments);

    // Ways to make sets of interesting things.
    APIVector<JSObjectRef> interestingObjects();
    APIVector<JSValueRef> interestingKeys();

    int m_failed { 0 };
    APIContext context;
};

TestAPI::ScriptResult TestAPI::evaluateScript(const char* script, JSObjectRef thisObject)
{
    APIString scriptAPIString(script);
    JSValueRef exception = nullptr;

    JSValueRef result = JSEvaluateScript(context, scriptAPIString, thisObject, nullptr, 0, &exception);
    if (exception)
        return Unexpected<JSValueRef>(exception);
    return ScriptResult(result);
}

template<typename... ArgumentTypes>
TestAPI::ScriptResult TestAPI::callFunction(const char* functionSource, ArgumentTypes... arguments)
{
    JSValueRef function;
    {
        ScriptResult functionResult = evaluateScript(functionSource);
        if (!functionResult)
            return functionResult;
        function = functionResult.value();
    }

    JSValueRef exception = nullptr;
    if (JSObjectRef functionObject = JSValueToObject(context, function, &exception)) {
        JSValueRef args[sizeof...(arguments)] { arguments... };
        JSValueRef result = JSObjectCallAsFunction(context, functionObject, functionObject, sizeof...(arguments), args, &exception);
        if (!exception)
            return ScriptResult(result);
    }

    RELEASE_ASSERT(exception);
    return Unexpected<JSValueRef>(exception);
}

template<typename... ArgumentTypes>
bool TestAPI::functionReturnsTrue(const char* functionSource, ArgumentTypes... arguments)
{
    JSValueRef trueValue = JSValueMakeBoolean(context, true);
    ScriptResult result = callFunction(functionSource, arguments...);
    if (!result)
        return false;
    return JSValueIsStrictEqual(context, trueValue, result.value());
}

template<typename... Strings>
bool TestAPI::check(bool condition, Strings... messages)
{
    if (!condition) {
        dataLogLn(messages..., ": FAILED");
        m_failed++;
    } else
        dataLogLn(messages..., ": PASSED");

    return condition;
}

template<typename JSFunctor, typename APIFunctor>
void TestAPI::checkJSAndAPIMatch(const JSFunctor& jsFunctor, const APIFunctor& apiFunctor, const char* description)
{
    JSValueRef exception = nullptr;
    JSValueRef result = apiFunctor(&exception);
    ScriptResult jsResult = jsFunctor();
    if (!jsResult) {
        check(exception, "JS and API calls should both throw an exception while ", description);
        check(functionReturnsTrue("(function(a, b) { return a.constructor === b.constructor; })", exception, jsResult.error()), "JS and API calls should both throw the same exception while ", description);
    } else {
        check(!exception, "JS and API calls should both not throw an exception while ", description);
        check(JSValueIsStrictEqual(context, result, jsResult.value()), "JS result and API calls should return the same value while ", description);
    }
}

APIVector<JSObjectRef> TestAPI::interestingObjects()
{
    APIVector<JSObjectRef> result(context);
    JSObjectRef array = JSValueToObject(context, evaluateScript(
        "[{}, [], { [Symbol.iterator]: 1 }, new Date(), new String('str'), new Map(), new Set(), new WeakMap(), new WeakSet(), new Error(), new Number(42), new Boolean(), { get length() { throw new Error(); } }];").value(), nullptr);

    APIString lengthString("length");
    unsigned length = JSValueToNumber(context, JSObjectGetProperty(context, array, lengthString, nullptr), nullptr);
    for (unsigned i = 0; i < length; i++) {
        JSObjectRef object = JSValueToObject(context, JSObjectGetPropertyAtIndex(context, array, i, nullptr), nullptr);
        ASSERT(object);
        result.append(object);
    }

    return result;
}

APIVector<JSValueRef> TestAPI::interestingKeys()
{
    APIVector<JSValueRef> result(context);
    JSObjectRef array = JSValueToObject(context, evaluateScript("[{}, [], 1, Symbol.iterator, 'length']").value(), nullptr);

    APIString lengthString("length");
    unsigned length = JSValueToNumber(context, JSObjectGetProperty(context, array, lengthString, nullptr), nullptr);
    for (unsigned i = 0; i < length; i++) {
        JSValueRef value = JSObjectGetPropertyAtIndex(context, array, i, nullptr);
        ASSERT(value);
        result.append(value);
    }

    return result;
}

static const char* isSymbolFunction = "(function isSymbol(symbol) { return typeof(symbol) === 'symbol'; })";
static const char* getFunction = "(function get(object, key) { return object[key]; })";
static const char* setFunction = "(function set(object, key, value) { object[key] = value; })";

void TestAPI::basicSymbol()
{
    // Can't call Symbol as a constructor since it's not subclassable.
    auto result = evaluateScript("Symbol('dope');");
    check(JSValueGetType(context, result.value()) == kJSTypeSymbol, "dope get type is a symbol");
    check(JSValueIsSymbol(context, result.value()), "dope is a symbol");
}

void TestAPI::symbolsTypeof()
{
    APIString description("dope");
    JSValueRef symbol = JSValueMakeSymbol(context, description);
    check(functionReturnsTrue(isSymbolFunction, symbol), "JSValueMakeSymbol makes a symbol value");
}

void TestAPI::symbolsGetPropertyForKey()
{
    auto objects = interestingObjects();
    auto keys = interestingKeys();

    for (auto& object : objects) {
        dataLogLn("\nnext object: ", toJS(context, object));
        for (auto& key : keys) {
            dataLogLn("Using key: ", toJS(context, key));
            checkJSAndAPIMatch(
            [&] {
                return callFunction(getFunction, object, key);
            }, [&] (JSValueRef* exception) {
                return JSObjectGetPropertyForKey(context, object, key, exception);
            }, "checking get property keys");
        }
    }
}

void TestAPI::symbolsSetPropertyForKey()
{
    auto jsObjects = interestingObjects();
    auto apiObjects = interestingObjects();
    auto keys = interestingKeys();

    JSValueRef theAnswer = JSValueMakeNumber(context, 42);
    for (size_t i = 0; i < jsObjects.size(); i++) {
        for (auto& key : keys) {
            JSObjectRef jsObject = jsObjects[i];
            JSObjectRef apiObject = apiObjects[i];
            checkJSAndAPIMatch(
                [&] {
                    return callFunction(setFunction, jsObject, key, theAnswer);
                } , [&] (JSValueRef* exception) {
                    JSObjectSetPropertyForKey(context, apiObject, key, theAnswer, kJSPropertyAttributeNone, exception);
                    return JSValueMakeUndefined(context);
                }, "setting property keys to the answer");
            // Check get is the same on API object.
            checkJSAndAPIMatch(
                [&] {
                    return callFunction(getFunction, apiObject, key);
                }, [&] (JSValueRef* exception) {
                    return JSObjectGetPropertyForKey(context, apiObject, key, exception);
                }, "getting property keys from API objects");
            // Check get is the same on respective objects.
            checkJSAndAPIMatch(
                [&] {
                    return callFunction(getFunction, jsObject, key);
                }, [&] (JSValueRef* exception) {
                    return JSObjectGetPropertyForKey(context, apiObject, key, exception);
                }, "getting property keys from respective objects");
        }
    }
}

void TestAPI::symbolsHasPropertyForKey()
{
    const char* hasFunction = "(function has(object, key) { return key in object; })";
    auto objects = interestingObjects();
    auto keys = interestingKeys();

    JSValueRef theAnswer = JSValueMakeNumber(context, 42);
    for (auto& object : objects) {
        dataLogLn("\nNext object: ", toJS(context, object));
        for (auto& key : keys) {
            dataLogLn("Using key: ", toJS(context, key));
            checkJSAndAPIMatch(
                [&] {
                    return callFunction(hasFunction, object, key);
                }, [&] (JSValueRef* exception) {
                    return JSValueMakeBoolean(context, JSObjectHasPropertyForKey(context, object, key, exception));
                }, "checking has property keys unset");

            check(!!callFunction(setFunction, object, key, theAnswer), "set property to the answer");

            checkJSAndAPIMatch(
                [&] {
                    return callFunction(hasFunction, object, key);
                }, [&] (JSValueRef* exception) {
                    return JSValueMakeBoolean(context, JSObjectHasPropertyForKey(context, object, key, exception));
                }, "checking has property keys set");
        }
    }
}


void TestAPI::symbolsDeletePropertyForKey()
{
    const char* deleteFunction = "(function del(object, key) { return delete object[key]; })";
    auto objects = interestingObjects();
    auto keys = interestingKeys();

    JSValueRef theAnswer = JSValueMakeNumber(context, 42);
    for (auto& object : objects) {
        dataLogLn("\nNext object: ", toJS(context, object));
        for (auto& key : keys) {
            dataLogLn("Using key: ", toJS(context, key));
            checkJSAndAPIMatch(
                [&] {
                    return callFunction(deleteFunction, object, key);
                }, [&] (JSValueRef* exception) {
                    return JSValueMakeBoolean(context, JSObjectDeletePropertyForKey(context, object, key, exception));
                }, "checking has property keys unset");

            check(!!callFunction(setFunction, object, key, theAnswer), "set property to the answer");

            checkJSAndAPIMatch(
                [&] {
                    return callFunction(deleteFunction, object, key);
                }, [&] (JSValueRef* exception) {
                    return JSValueMakeBoolean(context, JSObjectDeletePropertyForKey(context, object, key, exception));
                }, "checking has property keys set");
        }
    }
}

void TestAPI::promiseResolveTrue()
{
    JSObjectRef resolve;
    JSObjectRef reject;
    JSValueRef exception = nullptr;
    JSObjectRef promise = JSObjectMakeDeferredPromise(context, &resolve, &reject, &exception);
    check(!exception, "No exception should be thrown creating a deferred promise");

    // Ugh, we don't have any C API that takes blocks... so we do this hack to capture the runner.
    static TestAPI* tester = this;
    static bool passedTrueCalled = false;

    APIString trueString("passedTrue");
    auto passedTrue = [](JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef*) -> JSValueRef {
        tester->check(argumentCount && JSValueIsStrictEqual(ctx, arguments[0], JSValueMakeBoolean(ctx, true)), "function should have been called back with true");
        passedTrueCalled = true;
        return JSValueMakeUndefined(ctx);
    };

    APIString thenString("then");
    JSValueRef thenFunction = JSObjectGetProperty(context, promise, thenString, &exception);
    check(!exception && thenFunction && JSValueIsObject(context, thenFunction), "Promise should have a then object property");

    JSValueRef passedTrueFunction = JSObjectMakeFunctionWithCallback(context, trueString, passedTrue);
    JSObjectCallAsFunction(context, const_cast<JSObjectRef>(thenFunction), promise, 1, &passedTrueFunction, &exception);
    check(!exception, "No exception should be thrown setting up callback");

    auto trueValue = JSValueMakeBoolean(context, true);
    JSObjectCallAsFunction(context, resolve, resolve, 1, &trueValue, &exception);
    check(!exception, "No exception should be thrown resolve promise");
    check(passedTrueCalled, "then response function should have been called.");
}

void TestAPI::promiseRejectTrue()
{
    JSObjectRef resolve;
    JSObjectRef reject;
    JSValueRef exception = nullptr;
    JSObjectRef promise = JSObjectMakeDeferredPromise(context, &resolve, &reject, &exception);
    check(!exception, "No exception should be thrown creating a deferred promise");

    // Ugh, we don't have any C API that takes blocks... so we do this hack to capture the runner.
    static TestAPI* tester = this;
    static bool passedTrueCalled = false;

    APIString trueString("passedTrue");
    auto passedTrue = [](JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef*) -> JSValueRef {
        tester->check(argumentCount && JSValueIsStrictEqual(ctx, arguments[0], JSValueMakeBoolean(ctx, true)), "function should have been called back with true");
        passedTrueCalled = true;
        return JSValueMakeUndefined(ctx);
    };

    APIString catchString("catch");
    JSValueRef catchFunction = JSObjectGetProperty(context, promise, catchString, &exception);
    check(!exception && catchFunction && JSValueIsObject(context, catchFunction), "Promise should have a then object property");

    JSValueRef passedTrueFunction = JSObjectMakeFunctionWithCallback(context, trueString, passedTrue);
    JSObjectCallAsFunction(context, const_cast<JSObjectRef>(catchFunction), promise, 1, &passedTrueFunction, &exception);
    check(!exception, "No exception should be thrown setting up callback");

    auto trueValue = JSValueMakeBoolean(context, true);
    JSObjectCallAsFunction(context, reject, reject, 1, &trueValue, &exception);
    check(!exception, "No exception should be thrown resolve promise");
    check(passedTrueCalled, "then response function should have been called.");
}

#define RUN(test) do {                                 \
        if (!shouldRun(#test))                         \
            break;                                     \
        tasks.append(                                  \
            createSharedTask<void(TestAPI&)>(          \
                [&] (TestAPI& tester) {                \
                    tester.test;                       \
                    dataLog(#test ": OK!\n");          \
                }));                                   \
    } while (false)

int testCAPIViaCpp(const char* filter)
{
    dataLogLn("Starting C-API tests in C++");

    Deque<RefPtr<SharedTask<void(TestAPI&)>>> tasks;

#if OS(DARWIN)
    auto shouldRun = [&] (const char* testName) -> bool {
        return !filter || !!strcasestr(testName, filter);
    };
#else
    auto shouldRun = [] (const char*) -> bool { return true; };
#endif

    RUN(basicSymbol());
    RUN(symbolsTypeof());
    RUN(symbolsGetPropertyForKey());
    RUN(symbolsSetPropertyForKey());
    RUN(symbolsHasPropertyForKey());
    RUN(symbolsDeletePropertyForKey());
    RUN(promiseResolveTrue());
    RUN(promiseRejectTrue());

    if (tasks.isEmpty()) {
        dataLogLn("Filtered all tests: ERROR");
        return 1;
    }

    Lock lock;

    static Atomic<int> failed { 0 };
    Vector<Ref<Thread>> threads;
    for (unsigned i = filter ? 1 : WTF::numberOfProcessorCores(); i--;) {
        threads.append(Thread::create(
            "Testapi via C++ thread",
            [&] () {
                TestAPI tester;
                for (;;) {
                    RefPtr<SharedTask<void(TestAPI&)>> task;
                    {
                        LockHolder locker(lock);
                        if (tasks.isEmpty())
                            break;
                        task = tasks.takeFirst();
                    }

                    task->run(tester);
                }
                failed.exchangeAdd(tester.failed());
            }));
    }

    for (auto& thread : threads)
        thread->waitForCompletion();

    dataLogLn("C-API tests in C++ had ", failed.load(), " failures");
    return failed.load();
}
