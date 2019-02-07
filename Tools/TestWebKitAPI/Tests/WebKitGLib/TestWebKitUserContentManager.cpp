/*
 * Copyright (C) 2013-2014 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2,1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "WebKitTestServer.h"
#include "WebViewTest.h"
#include <cstdarg>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>

static WebKitTestServer* kServer;

// These are all here so that they can be changed easily, if necessary.
static const char* kStyleSheetHTML = "<html><div id=\"styledElement\">Sweet stylez!</div></html>";
static const char* kInjectedStyleSheet = "#styledElement { font-weight: bold; }";
static const char* kStyleSheetTestScript = "getComputedStyle(document.getElementById('styledElement'))['font-weight']";
static const char* kStyleSheetTestScriptResult = "bold";
static const char* kInjectedScript = "document.write('<div id=\"item\">Generated by a script</div>')";
static const char* kScriptTestScript = "document.getElementById('item').innerText";
static const char* kScriptTestScriptResult = "Generated by a script";

static void testWebViewNewWithUserContentManager(Test* test, gconstpointer)
{
    GRefPtr<WebKitUserContentManager> userContentManager1 = adoptGRef(webkit_user_content_manager_new());
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(userContentManager1.get()));
    auto webView1 = Test::adoptView(Test::createWebView(userContentManager1.get()));
    g_assert_true(webkit_web_view_get_user_content_manager(webView1.get()) == userContentManager1.get());

    auto webView2 = Test::adoptView(Test::createWebView());
    g_assert_true(webkit_web_view_get_user_content_manager(webView2.get()) != userContentManager1.get());
}

static bool isStyleSheetInjectedForURLAtPath(WebViewTest* test, const char* path, const char* world = nullptr)
{
    test->loadURI(kServer->getURIForPath(path).data());
    test->waitUntilLoadFinished();

    GUniqueOutPtr<GError> error;
    WebKitJavascriptResult* javascriptResult = world ? test->runJavaScriptInWorldAndWaitUntilFinished(kStyleSheetTestScript, world, &error.outPtr())
        : test->runJavaScriptAndWaitUntilFinished(kStyleSheetTestScript, &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());

    GUniquePtr<char> resultString(WebViewTest::javascriptResultToCString(javascriptResult));
    return !g_strcmp0(resultString.get(), kStyleSheetTestScriptResult);
}

static bool isScriptInjectedForURLAtPath(WebViewTest* test, const char* path, const char* world = nullptr)
{
    test->loadURI(kServer->getURIForPath(path).data());
    test->waitUntilLoadFinished();

    GUniqueOutPtr<GError> error;
    WebKitJavascriptResult* javascriptResult = world ? test->runJavaScriptInWorldAndWaitUntilFinished(kScriptTestScript, world, &error.outPtr())
        : test->runJavaScriptAndWaitUntilFinished(kScriptTestScript, &error.outPtr());
    if (javascriptResult) {
        g_assert_no_error(error.get());

        GUniquePtr<char> resultString(WebViewTest::javascriptResultToCString(javascriptResult));
        return !g_strcmp0(resultString.get(), kScriptTestScriptResult);
    }
    return false;
}

static void fillURLListFromPaths(char** list, const char* path, ...)
{
    va_list argumentList;
    va_start(argumentList, path);

    int i = 0;
    while (path) {
        // FIXME: We must use a wildcard for the host here until http://wkbug.com/112476 is fixed.
        // Until that time patterns with port numbers in them will not properly match URLs with port numbers.
        list[i++] = g_strdup_printf("http://*/%s*", path);
        path = va_arg(argumentList, const char*);
    }
}

static void removeOldInjectedContentAndResetLists(WebKitUserContentManager* userContentManager, char** whitelist, char** blacklist)
{
    webkit_user_content_manager_remove_all_style_sheets(userContentManager);
    webkit_user_content_manager_remove_all_scripts(userContentManager);

    while (*whitelist) {
        g_free(*whitelist);
        *whitelist = 0;
        whitelist++;
    }

    while (*blacklist) {
        g_free(*blacklist);
        *blacklist = 0;
        blacklist++;
    }
}

static void testUserContentManagerInjectedStyleSheet(WebViewTest* test, gconstpointer)
{
    char* whitelist[3] = { 0, 0, 0 };
    char* blacklist[3] = { 0, 0, 0 };

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    // Without a whitelist or a blacklist all URLs should have the injected style sheet.
    static const char* randomPath = "somerandompath";
    g_assert_false(isStyleSheetInjectedForURLAtPath(test, randomPath));
    WebKitUserStyleSheet* styleSheet = webkit_user_style_sheet_new(kInjectedStyleSheet, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
    webkit_user_content_manager_add_style_sheet(test->m_userContentManager.get(), styleSheet);
    webkit_user_style_sheet_unref(styleSheet);
    g_assert_true(isStyleSheetInjectedForURLAtPath(test, randomPath));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    g_assert_false(isStyleSheetInjectedForURLAtPath(test, randomPath, "WebExtensionTestScriptWorld"));
    styleSheet = webkit_user_style_sheet_new_for_world(kInjectedStyleSheet, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, "WebExtensionTestScriptWorld", nullptr, nullptr);
    webkit_user_content_manager_add_style_sheet(test->m_userContentManager.get(), styleSheet);
    webkit_user_style_sheet_unref(styleSheet);
    g_assert_true(isStyleSheetInjectedForURLAtPath(test, randomPath, "WebExtensionTestScriptWorld"));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    fillURLListFromPaths(blacklist, randomPath, 0);
    styleSheet = webkit_user_style_sheet_new(kInjectedStyleSheet, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, nullptr, blacklist);
    webkit_user_content_manager_add_style_sheet(test->m_userContentManager.get(), styleSheet);
    webkit_user_style_sheet_unref(styleSheet);
    g_assert_false(isStyleSheetInjectedForURLAtPath(test, randomPath));
    g_assert_true(isStyleSheetInjectedForURLAtPath(test, "someotherrandompath"));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    static const char* inTheWhiteList = "inthewhitelist";
    static const char* notInWhitelist = "notinthewhitelist";
    static const char* inTheWhiteListAndBlackList = "inthewhitelistandblacklist";

    fillURLListFromPaths(whitelist, inTheWhiteList, inTheWhiteListAndBlackList, 0);
    fillURLListFromPaths(blacklist, inTheWhiteListAndBlackList, 0);
    styleSheet = webkit_user_style_sheet_new(kInjectedStyleSheet, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, whitelist, blacklist);
    webkit_user_content_manager_add_style_sheet(test->m_userContentManager.get(), styleSheet);
    webkit_user_style_sheet_unref(styleSheet);
    g_assert_true(isStyleSheetInjectedForURLAtPath(test, inTheWhiteList));
    g_assert_false(isStyleSheetInjectedForURLAtPath(test, inTheWhiteListAndBlackList));
    g_assert_false(isStyleSheetInjectedForURLAtPath(test, notInWhitelist));

    // It's important to clean up the environment before other tests.
    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);
}

static void testUserContentManagerInjectedScript(WebViewTest* test, gconstpointer)
{
    char* whitelist[3] = { 0, 0, 0 };
    char* blacklist[3] = { 0, 0, 0 };

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    // Without a whitelist or a blacklist all URLs should have the injected script.
    static const char* randomPath = "somerandompath";
    g_assert_false(isScriptInjectedForURLAtPath(test, randomPath));
    WebKitUserScript* script = webkit_user_script_new(kInjectedScript, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
    webkit_user_content_manager_add_script(test->m_userContentManager.get(), script);
    webkit_user_script_unref(script);
    g_assert_true(isScriptInjectedForURLAtPath(test, randomPath));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    g_assert_false(isScriptInjectedForURLAtPath(test, randomPath, "WebExtensionTestScriptWorld"));
    script = webkit_user_script_new_for_world(kInjectedScript, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, "WebExtensionTestScriptWorld", nullptr, nullptr);
    webkit_user_content_manager_add_script(test->m_userContentManager.get(), script);
    webkit_user_script_unref(script);
    g_assert_true(isScriptInjectedForURLAtPath(test, randomPath, "WebExtensionTestScriptWorld"));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    fillURLListFromPaths(blacklist, randomPath, 0);
    script = webkit_user_script_new(kInjectedScript, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, blacklist);
    webkit_user_content_manager_add_script(test->m_userContentManager.get(), script);
    webkit_user_script_unref(script);
    g_assert_false(isScriptInjectedForURLAtPath(test, randomPath));
    g_assert_true(isScriptInjectedForURLAtPath(test, "someotherrandompath"));

    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);

    static const char* inTheWhiteList = "inthewhitelist";
    static const char* notInWhitelist = "notinthewhitelist";
    static const char* inTheWhiteListAndBlackList = "inthewhitelistandblacklist";

    fillURLListFromPaths(whitelist, inTheWhiteList, inTheWhiteListAndBlackList, 0);
    fillURLListFromPaths(blacklist, inTheWhiteListAndBlackList, 0);
    script = webkit_user_script_new(kInjectedScript, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, whitelist, blacklist);
    webkit_user_content_manager_add_script(test->m_userContentManager.get(), script);
    webkit_user_script_unref(script);
    g_assert_true(isScriptInjectedForURLAtPath(test, inTheWhiteList));
    g_assert_false(isScriptInjectedForURLAtPath(test, inTheWhiteListAndBlackList));
    g_assert_false(isScriptInjectedForURLAtPath(test, notInWhitelist));

    // It's important to clean up the environment before other tests.
    removeOldInjectedContentAndResetLists(test->m_userContentManager.get(), whitelist, blacklist);
}

class UserScriptMessageTest : public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(UserScriptMessageTest);

    UserScriptMessageTest()
        : m_userScriptMessage(nullptr)
    {
    }

    ~UserScriptMessageTest()
    {
        if (m_userScriptMessage)
            webkit_javascript_result_unref(m_userScriptMessage);
    }

    bool registerHandler(const char* handlerName, const char* worldName = nullptr)
    {
        return worldName ? webkit_user_content_manager_register_script_message_handler_in_world(m_userContentManager.get(), handlerName, worldName)
            : webkit_user_content_manager_register_script_message_handler(m_userContentManager.get(), handlerName);
    }

    void unregisterHandler(const char* handlerName, const char* worldName = nullptr)
    {
        return worldName ? webkit_user_content_manager_unregister_script_message_handler_in_world(m_userContentManager.get(), handlerName, worldName)
            : webkit_user_content_manager_unregister_script_message_handler(m_userContentManager.get(), handlerName);
    }

    static void scriptMessageReceived(WebKitUserContentManager* userContentManager, WebKitJavascriptResult* jsResult, UserScriptMessageTest* test)
    {
        g_signal_handlers_disconnect_by_func(userContentManager, reinterpret_cast<gpointer>(scriptMessageReceived), test);
        if (!test->m_waitForScriptRun)
            g_main_loop_quit(test->m_mainLoop);

        g_assert_null(test->m_userScriptMessage);
        test->m_userScriptMessage = webkit_javascript_result_ref(jsResult);
    }

    WebKitJavascriptResult* waitUntilMessageReceived(const char* handlerName)
    {
        if (m_userScriptMessage) {
            webkit_javascript_result_unref(m_userScriptMessage);
            m_userScriptMessage = nullptr;
        }

        GUniquePtr<char> signalName(g_strdup_printf("script-message-received::%s", handlerName));
        g_signal_connect(m_userContentManager.get(), signalName.get(), G_CALLBACK(scriptMessageReceived), this);

        g_main_loop_run(m_mainLoop);
        g_assert_false(m_waitForScriptRun);
        g_assert_nonnull(m_userScriptMessage);
        return m_userScriptMessage;
    }

    static void runJavaScriptFinished(GObject*, GAsyncResult* result, UserScriptMessageTest* test)
    {
        g_assert_true(test->m_waitForScriptRun);
        test->m_waitForScriptRun = false;
        g_main_loop_quit(test->m_mainLoop);
    }

    WebKitJavascriptResult* postMessageAndWaitUntilReceived(const char* handlerName, const char* javascriptValueAsText, const char* worldName = nullptr)
    {
        GUniquePtr<char> javascriptSnippet(g_strdup_printf("window.webkit.messageHandlers.%s.postMessage(%s);", handlerName, javascriptValueAsText));
        m_waitForScriptRun = true;
        if (worldName)
            webkit_web_view_run_javascript_in_world(m_webView, javascriptSnippet.get(), worldName, nullptr, reinterpret_cast<GAsyncReadyCallback>(runJavaScriptFinished), this);
        else
            webkit_web_view_run_javascript(m_webView, javascriptSnippet.get(), nullptr, reinterpret_cast<GAsyncReadyCallback>(runJavaScriptFinished), this);
        return waitUntilMessageReceived(handlerName);
    }

private:
    WebKitJavascriptResult* m_userScriptMessage;
    bool m_waitForScriptRun { false };
};

static void testUserContentManagerScriptMessageReceived(UserScriptMessageTest* test, gconstpointer)
{
    g_assert_true(test->registerHandler("msg"));

    // Trying to register the same handler a second time must fail.
    g_assert_false(test->registerHandler("msg"));

    test->loadHtml("<html></html>", nullptr);
    test->waitUntilLoadFinished();

    // Check that the "window.webkit.messageHandlers" namespace exists.
    GUniqueOutPtr<GError> error;
    WebKitJavascriptResult* javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers ? 'y' : 'n';", &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());
    GUniquePtr<char> valueString(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "y");

    // Check that the "document.webkit.messageHandlers.msg" namespace exists.
    javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers.msg ? 'y' : 'n';", &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());
    valueString.reset(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "y");

    valueString.reset(WebViewTest::javascriptResultToCString(test->postMessageAndWaitUntilReceived("msg", "'user message'")));
    g_assert_cmpstr(valueString.get(), ==, "user message");

    // Messages should arrive despite of other handlers being registered.
    g_assert_true(test->registerHandler("anotherHandler"));

    // Check that the "document.webkit.messageHandlers.msg" namespace still exists.
    javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers.msg ? 'y' : 'n';", &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());
    valueString.reset(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "y");

    // Check that the "document.webkit.messageHandlers.anotherHandler" namespace exists.
    javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers.anotherHandler ? 'y' : 'n';", &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());
    valueString.reset(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "y");

    valueString.reset(WebViewTest::javascriptResultToCString(test->postMessageAndWaitUntilReceived("msg", "'handler: msg'")));
    g_assert_cmpstr(valueString.get(), ==, "handler: msg");

    valueString.reset(WebViewTest::javascriptResultToCString(test->postMessageAndWaitUntilReceived("anotherHandler", "'handler: anotherHandler'")));
    g_assert_cmpstr(valueString.get(), ==, "handler: anotherHandler");

    // Unregistering a handler and re-registering again under the same name should work.
    test->unregisterHandler("msg");

    javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers.msg.postMessage('42');", &error.outPtr());
    g_assert_null(javascriptResult);
    g_assert_nonnull(error.get());

    // Re-registering a handler that has been unregistered must work
    g_assert_true(test->registerHandler("msg"));
    valueString.reset(WebViewTest::javascriptResultToCString(test->postMessageAndWaitUntilReceived("msg", "'handler: msg'")));
    g_assert_cmpstr(valueString.get(), ==, "handler: msg");

    test->unregisterHandler("anotherHandler");
}

static void testUserContentManagerScriptMessageInWorldReceived(UserScriptMessageTest* test, gconstpointer)
{
    g_assert_true(test->registerHandler("msg"));

    test->loadHtml("<html></html>", nullptr);
    test->waitUntilLoadFinished();

    // Check that the "window.webkit.messageHandlers" namespace doesn't exist in isolated worlds.
    GUniqueOutPtr<GError> error;
    WebKitJavascriptResult* javascriptResult = test->runJavaScriptInWorldAndWaitUntilFinished("window.webkit.messageHandlers ? 'y' : 'n';", "WebExtensionTestScriptWorld", &error.outPtr());
    g_assert_null(javascriptResult);
    g_assert_error(error.get(), WEBKIT_JAVASCRIPT_ERROR, WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED);
    test->unregisterHandler("msg");

    g_assert_true(test->registerHandler("msg", "WebExtensionTestScriptWorld"));

    // Check that the "window.webkit.messageHandlers" namespace exists in the world.
    javascriptResult = test->runJavaScriptInWorldAndWaitUntilFinished("window.webkit.messageHandlers ? 'y' : 'n';", "WebExtensionTestScriptWorld", &error.outPtr());
    g_assert_nonnull(javascriptResult);
    g_assert_no_error(error.get());
    GUniquePtr<char> valueString(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "y");

    valueString.reset(WebViewTest::javascriptResultToCString(test->postMessageAndWaitUntilReceived("msg", "'user message'", "WebExtensionTestScriptWorld")));
    g_assert_cmpstr(valueString.get(), ==, "user message");

    // Post message in main world should fail.
    javascriptResult = test->runJavaScriptAndWaitUntilFinished("window.webkit.messageHandlers.msg.postMessage('42');", &error.outPtr());
    g_assert_null(javascriptResult);
    g_assert_error(error.get(), WEBKIT_JAVASCRIPT_ERROR, WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED);

    test->unregisterHandler("msg", "WebExtensionTestScriptWorld");
}

#if PLATFORM(GTK)
static void testUserContentManagerScriptMessageFromDOMBindings(UserScriptMessageTest* test, gconstpointer)
{
    g_assert_true(test->registerHandler("dom"));

    test->loadHtml("<html>1</html>", nullptr);
    WebKitJavascriptResult* javascriptResult = test->waitUntilMessageReceived("dom");
    g_assert_nonnull(javascriptResult);
    GUniquePtr<char> valueString(WebViewTest::javascriptResultToCString(javascriptResult));
    g_assert_cmpstr(valueString.get(), ==, "DocumentLoaded");

    test->unregisterHandler("dom");
}
#endif

static void serverCallback(SoupServer* server, SoupMessage* message, const char* path, GHashTable*, SoupClientContext*, gpointer)
{
    soup_message_set_status(message, SOUP_STATUS_OK);
    soup_message_body_append(message->response_body, SOUP_MEMORY_STATIC, kStyleSheetHTML, strlen(kStyleSheetHTML));
    soup_message_body_complete(message->response_body);
}

void beforeAll()
{
    kServer = new WebKitTestServer();
    kServer->run(serverCallback);

    Test::add("WebKitWebView", "new-with-user-content-manager", testWebViewNewWithUserContentManager);
    WebViewTest::add("WebKitUserContentManager", "injected-style-sheet", testUserContentManagerInjectedStyleSheet);
    WebViewTest::add("WebKitUserContentManager", "injected-script", testUserContentManagerInjectedScript);
    UserScriptMessageTest::add("WebKitUserContentManager", "script-message-received", testUserContentManagerScriptMessageReceived);
    UserScriptMessageTest::add("WebKitUserContentManager", "script-message-in-world-received", testUserContentManagerScriptMessageInWorldReceived);
#if PLATFORM(GTK)
    UserScriptMessageTest::add("WebKitUserContentManager", "script-message-from-dom-bindings", testUserContentManagerScriptMessageFromDOMBindings);
#endif
}

void afterAll()
{
    delete kServer;
}
