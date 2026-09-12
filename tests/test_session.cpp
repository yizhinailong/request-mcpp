/**
 * @file test_session.cpp
 * @brief Exercise Session against a controlled loopback HTTP/1.1 server.
 */
#ifdef _WIN32
    #include <winsock2.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
#else
    #include <arpa/inet.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif
#include <curl/curl.h>

import std;
import mcr.session;
import mcr.util;

static_assert(!std::is_copy_constructible_v<mcr::Session> && !std::is_move_constructible_v<mcr::Session>);
static_assert(std::is_copy_constructible_v<mcr::Response> && std::is_move_constructible_v<mcr::Response>);
static_assert(std::is_same_v<mcr::AsyncResponse, mcr::AsyncWrapper<mcr::Response>>);
static_assert(std::variant_size_v<mcr::Content> == 5);

namespace {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

#ifdef _WIN32
    using NativeSocket = SOCKET;
    using SocketLength = int;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ INVALID_SOCKET };
#else
    using NativeSocket = int;
    using SocketLength = socklen_t;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ -1 };
#endif

    /** @brief Close a socket on all test exit paths. */
    struct Socket {
        NativeSocket handle;

        explicit Socket(NativeSocket value) : handle{ value } {
            if (handle == INVALID_NATIVE_SOCKET) {
                throw std::runtime_error{ "fixture socket creation failed" };
            }
        }

        Socket(Socket const&)                    = delete;
        auto operator=(Socket const&) -> Socket& = delete;

        ~Socket() {
#ifdef _WIN32
            closesocket(handle);
#else
            close(handle);
#endif
        }
    };

    /** @brief Bound all socket waits so fixture teardown cannot hang. */
    auto readable(NativeSocket socket) -> bool {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);
        timeval timeout{ 0, 50000 };
#ifdef _WIN32
        auto const result{ select(0, &set, nullptr, nullptr, &timeout) };
#else
        auto const result{ select(socket + 1, &set, nullptr, nullptr, &timeout) };
#endif
        if (result < 0) {
            throw std::runtime_error{ "fixture select failed" };
        }
        return result > 0;
    }

    /** @brief Serve persistent connections and both fixed-length and chunked uploads. */
    class HttpServer {
    private:
        Socket                    m_listener{ socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
        std::string               m_url;
        std::atomic_int           m_connections{};
        std::mutex                m_failure_mutex;
        std::exception_ptr        m_failure;
        std::vector<std::jthread> m_clients;
        std::jthread              m_worker;

    public:
        HttpServer() {
            sockaddr_in address{};
            address.sin_family      = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(m_listener.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(m_listener.handle, 16) != 0) {
                throw std::runtime_error{ "fixture bind/listen failed" };
            }
            SocketLength length{ sizeof(address) };
            if (getsockname(m_listener.handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
                throw std::runtime_error{ "fixture getsockname failed" };
            }
            m_url    = std::format("http://127.0.0.1:{}", ntohs(address.sin_port));
            m_worker = std::jthread{ [this](std::stop_token stop) {
                try {
                    while (!stop.stop_requested()) {
                        if (!readable(m_listener.handle)) {
                            continue;
                        }
                        auto client{ std::make_shared<Socket>(accept(m_listener.handle, nullptr, nullptr)) };
                        ++m_connections;
                        m_clients.emplace_back([this, client](std::stop_token client_stop) {
                            try {
                                serve(client->handle, client_stop);
                            } catch (...) {
                                recordFailure();
                            }
                        });
                    }
                } catch (...) {
                    recordFailure();
                }
            } };
        }

        ~HttpServer() { stop(); }

        auto Url(std::string_view path = "/hello") const -> mcr::Url { return mcr::Url{ m_url + std::string{ path } }; }

        auto Connections() const -> int { return m_connections.load(); }

        auto Check() -> void {
            stop();
            if (m_failure) {
                std::rethrow_exception(m_failure);
            }
        }

    private:
        auto stop() -> void {
            if (m_worker.joinable()) {
                m_worker.request_stop();
                m_worker.join();
            }
            for (auto& client : m_clients) {
                client.request_stop();
            }
            m_clients.clear();
        }

        auto recordFailure() -> void {
            std::lock_guard lock{ m_failure_mutex };
            if (!m_failure) {
                m_failure = std::current_exception();
            }
        }

        static auto sendAll(NativeSocket socket, std::string_view bytes) -> bool {
            while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
                constexpr int flags{ MSG_NOSIGNAL };
#else
                constexpr int flags{ 0 };
#endif
                auto const sent{ send(socket, bytes.data(), static_cast<int>(bytes.size()), flags) };
                if (sent <= 0) {
                    return false;
                } // Cancellation and timeout intentionally close sockets.
                bytes.remove_prefix(static_cast<std::size_t>(sent));
            }
            return true;
        }

        static auto receive(NativeSocket socket, std::string& pending, std::stop_token stop) -> bool {
            while (!stop.stop_requested()) {
                if (!readable(socket)) {
                    continue;
                }
                std::array<char, 4096> buffer{};
                auto const             count{ recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0) };
                if (count <= 0) {
                    return false;
                }
                pending.append(buffer.data(), static_cast<std::size_t>(count));
                return true;
            }
            return false;
        }

        static auto line(NativeSocket socket, std::string& pending, std::string& output, std::stop_token stop) -> bool {
            while (pending.find("\r\n") == std::string::npos) {
                if (!receive(socket, pending, stop)) {
                    return false;
                }
            }
            auto const end{ pending.find("\r\n") };
            output = pending.substr(0, end);
            pending.erase(0, end + 2);
            return true;
        }

        static auto serve(NativeSocket socket, std::stop_token stop) -> void {
            std::string pending;
            while (!stop.stop_requested()) {
                std::string request_line;
                if (!line(socket, pending, request_line, stop)) {
                    return;
                }
                std::istringstream request{ request_line };
                std::string        method, target;
                request >> method >> target;
                std::string raw_headers, field;
                while (true) {
                    if (!line(socket, pending, field, stop)) {
                        return;
                    }
                    if (field.empty()) {
                        break;
                    }
                    raw_headers += field + "\r\n";
                }
                auto headers{ mcr::util::parse_header(raw_headers) };
                if (headers["Expect"] == "100-continue" && !sendAll(socket, "HTTP/1.1 100 Continue\r\n\r\n")) {
                    return;
                }
                std::string body;
                if (headers["Transfer-Encoding"] == "chunked") {
                    while (true) {
                        if (!line(socket, pending, field, stop)) {
                            return;
                        }
                        auto const length{ std::stoull(field, nullptr, 16) };
                        if (length == 0) {
                            do {
                                if (!line(socket, pending, field, stop)) {
                                    return;
                                }
                            } while (!field.empty());
                            break;
                        }
                        while (pending.size() < length + 2) {
                            if (!receive(socket, pending, stop)) {
                                return;
                            }
                        }
                        body.append(pending.data(), static_cast<std::size_t>(length));
                        pending.erase(0, static_cast<std::size_t>(length) + 2);
                    }
                } else {
                    auto const length{ headers["Content-Length"].empty() ? 0ULL : std::stoull(headers["Content-Length"]) };
                    while (pending.size() < length) {
                        if (!receive(socket, pending, stop)) {
                            return;
                        }
                    }
                    body = pending.substr(0, static_cast<std::size_t>(length));
                    pending.erase(0, static_cast<std::size_t>(length));
                }
                std::string status{ "200 OK" }, output{ "Hello session!" }, extra;
                if (target.starts_with("/echo")) {
                    output = body;
                } else if (target == "/redirect" || target == "/loop") {
                    status = "302 Found";
                    output = "redirect body";
                    extra  = std::string{ "X-Intermediate: yes\r\nLocation: " } + (target == "/loop" ? "/loop" : "/hello") + "\r\n";
                } else if (target == "/cookie") {
                    extra = "Set-Cookie: stored=yes; Path=/; HttpOnly\r\n";
                } else if (target == "/sse") {
                    output = "id: 7\ndata: first\n\ndata: second\n\ndata: unfinished";
                } else if (target == "/error") {
                    status = "404 Not Found";
                    output = "missing";
                } else if (target == "/slow") {
                    std::this_thread::sleep_for(150ms);
                } else if (target == "/partial") {
                    (void)sendAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\nshort");
                    return;
                } else if (target == "/range" && headers["Range"] == "bytes=2-5") {
                    status = "206 Partial Content";
                    output = "2345";
                }
                for (std::string const name : { "Cookie", "User-Agent", "Authorization", "X-Custom", "X-Empty", "Accept-Encoding", "Content-Type", "Transfer-Encoding", "Range", "Expect" }) {
                    extra += "X-Request-" + name + ": " + headers[name] + "\r\n";
                }
                extra += std::format("X-Method: {}\r\nX-Target: {}\r\n", method, target);
                auto response{ std::format("HTTP/1.1 {}\r\nContent-Length: {}\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\n{}\r\n", status, output.size(), extra) };
                if (method != "HEAD") {
                    response += output;
                }
                if (!sendAll(socket, response)) {
                    return;
                }
            }
        }
    };

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_session: {}", message);
        }
        return condition;
    }

    auto configure(mcr::Session& session, HttpServer const& server, std::string_view path = "/hello") -> void {
        session.SetOption(server.Url(path));
        session.SetOption(mcr::Proxies{
            {     "http",  "" },
            { "no_proxy", "*" }
        });
        session.SetOption(mcr::Timeout{ 3000ms });
        session.SetOption(mcr::HttpVersion{ mcr::HttpVersionCode::VERSION_1_1 });
    }

    auto check_reuse(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server);
        auto const before{ server.Connections() };
        auto       first{ session.Get() };
        bool       passed{ check(first.status_code == 200 && !first.error && first.text == "Hello session!", "GET must return body and status") };
        passed &= check(first.url == server.Url() && first.header["content-type"] == "text/plain" && first.reason == "OK" && first.status_line == "HTTP/1.1 200 OK", "response must expose parsed final headers and effective URL");
        passed &= check(first.downloaded_bytes == 14 && first.uploaded_bytes == 0 && first.primary_ip == "127.0.0.1" && first.primary_port != 0 && first.elapsed >= 0 && first.GetCertInfos().empty(), "HTTP metadata must be initialized and plain HTTP certificates must be empty");
        for (int i{}; i < 4; ++i) {
            passed &= check(session.Get().text == first.text, "repeated GET must clear prior buffers");
        }
        passed &= check(server.Connections() == before + 1, "sequential requests must reuse the same connection");
        passed &= check(session.Head().text.empty() && session.GetDownloadFileLength() == 14 && session.Get().text == first.text, "HEAD and length probing must not corrupt subsequent GET");
        session.SetUrl(server.Url("/error"));
        auto error{ session.Get() };
        passed &= check(error.status_code == 404 && !error.error && error.text == "missing" && first.status_code == 200 && first.text == "Hello session!", "HTTP errors are responses and previous response snapshots stay independent");
        session.SetUrl(mcr::Url{ "http://[" });
        auto malformed{ session.Get() };
        passed &= check(malformed.error.code == mcr::ErrorCode::URL_MALFORMAT && !malformed.error.message.empty() && malformed.text.empty(), "malformed URL must return an initialized transport failure");
        session.SetUrl(server.Url());
        passed &= check(!session.Get().error, "successful reuse must clear previous curl errors");
        return passed;
    }

    auto check_methods_and_content(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/echo");
        using Method = mcr::Response (mcr::Session::*)();
        std::pair<Method, std::string_view> const methods[]{
            {    &mcr::Session::Post,    "POST" },
            {     &mcr::Session::Get,     "GET" },
            {     &mcr::Session::Put,     "PUT" },
            { &mcr::Session::Options, "OPTIONS" },
            {   &mcr::Session::Patch,   "PATCH" },
            {  &mcr::Session::Delete,  "DELETE" }
        };
        bool              passed{ true };
        std::string const binary{ "first\0last", 10 };
        for (auto const& [method, name] : methods) {
            session.SetBody(mcr::Body{ binary });
            auto response{ (session.*method)() };
            passed &= check(!response.error && response.header["X-Method"] == name && response.text == binary && response.uploaded_bytes == 10, "methods must preserve binary bodies and use their own HTTP verb after reuse");
            session.RemoveContent();
            response  = (session.*method)();
            passed   &= check(!response.error && response.header["X-Method"] == name && response.text.empty(), "RemoveContent must detach the old body for every method");
        }
        session.SetOption(mcr::Payload{
            { "space key", "x+y" },
            {     "empty",    "" }
        });
        auto payload{ session.Post() };
        passed &= check(payload.text == "space key=x%2By&empty=" && payload.header["X-Request-Content-Type"] == "application/x-www-form-urlencoded", "payloads must preserve cpr's raw keys and encode values using the session holder");
        std::string borrowed{ binary };
        session.SetOption(mcr::BodyView{ borrowed });
        passed      &= check(session.Post().text == binary, "BodyView must preserve exact byte length");
        borrowed[0]  = 'F';
        passed      &= check(session.Put().text == borrowed, "BodyView must observe borrowed storage on the next request");
        session.SetBodyView({});
        passed &= check(session.Post().text.empty(), "null empty BodyView must produce a zero-length body without reading stdin");
        session.SetBody(mcr::Body{ "retained" });
        passed &= check(session.Head().text.empty() && session.Post().text == "retained", "HEAD must suppress but retain configured content");
        session.SetOption(mcr::Multipart{
            {   "text", std::string_view{ binary } },
            { "number",                         42 }
        });
        auto multipart{ session.Post() };
        passed &= check(!multipart.error && multipart.text.contains(binary) && multipart.text.contains("name=\"number\"") && multipart.header["X-Request-Content-Type"].starts_with("multipart/form-data; boundary="), "multipart must encode text fields including embedded nulls");
        passed &= check(session.Get().header["X-Method"] == "GET", "GET with retained multipart must remain GET");
        session.SetBody(mcr::Body{ "replacement" });
        passed &= check(session.Post().text == "replacement", "switching MIME to a body must detach the MIME tree");
        session.SetMultipart({
            { "again", "value" }
        });
        passed &= check(session.Post().text.contains("name=\"again\""), "switching a body to MIME must detach the old POST fields");
        session.RemoveContent();
        passed &= check(session.Get().text.empty() && std::holds_alternative<std::monostate>(session.GetContent()), "removing multipart must leave no dangling MIME or body state");
        return passed;
    }

    auto check_options(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/echo?existing=1#fragment");
        session.SetOption(mcr::Parameters{
            {  "a b", "x+y" },
            { "flag",    "" }
        });
        session.SetOption(mcr::Header{
            { "X-Custom", "old" },
            {  "X-Empty",    "" }
        });
        session.UpdateHeader({
            { "x-custom", "new" }
        });
        session.GetHeader()["Another"] = "value";
        session.SetOption(mcr::UserAgent{ "session-test" });
        session.SetOption(mcr::AcceptEncoding{ mcr::AcceptEncodingMethods::disabled });
        session.SetOption(mcr::ReserveSize{ 4096 });
        session.SetOption(mcr::ConnectTimeout{ 1000ms });
        session.SetOption(mcr::LimitRate{ 0, 0 });
        auto response{ session.Get() };
        bool passed{ check(response.header["X-Target"] == "/echo?existing=1&a%20b=x%2By&flag" && session.GetFullRequestUrl() == server.Url("/echo?existing=1&a%20b=x%2By&flag#fragment").Str(), "parameters must merge with existing queries before fragments") };
        passed &= check(response.header["X-Request-X-Custom"] == "new" && response.header["X-Request-User-Agent"] == "session-test" && response.header["X-Request-Accept-Encoding"].empty(), "header replacement, user agent, and disabled encoding must reach the server");
        passed &= check(std::as_const(session).GetHeader().size() == 3, "case-insensitive header merging must retain unrelated entries");
        session.SetParameters({});
        session.SetUrl(server.Url("/echo"));
        session.SetOption(mcr::Authentication{ "user", "password", mcr::AuthMode::BASIC });
        passed &= check(session.Get().header["X-Request-Authorization"] == "Basic dXNlcjpwYXNzd29yZA==", "basic authentication must reach the server");
        session.SetOption(mcr::Bearer{ "token" });
        passed &= check(session.Get().header["X-Request-Authorization"] == "Bearer token", "bearer authentication must replace basic authentication");
        session.SetUrl(server.Url("/cookie"));
        auto cookie{ session.Get() };
        session.SetUrl(server.Url("/echo"));
        passed &= check(!cookie.cookies.empty() && session.Get().header["X-Request-Cookie"] == "stored=yes", "cookies must persist in the session engine");
        session.SetOption(mcr::Cookies{
            { "explicit", "a b" }
        });
        passed &= check(session.Get().header["X-Request-Cookie"] == "explicit=a%20b;", "SetCookies must replace stored cookies and retain cpr's trailing separator");
        session.SetUrl(server.Url("/range"));
        session.SetOption(mcr::Range{ 2, 5 });
        auto range{ session.Get() };
        passed &= check(range.status_code == 206 && range.text == "2345", "byte ranges must reach curl");
        session.SetUrl(server.Url("/echo"));
        passed &= check(session.Put().header["X-Request-Range"].empty(), "PUT must clear a previous range as in cpr");
        session.SetHeader({
            { "Expect", "100-continue" }
        });
        session.SetBody(mcr::Body{ "continue" });
        passed &= check(session.Post().text == "continue", "explicit Expect must be preserved and interim headers parsed");
        return passed;
    }

    auto check_redirects_and_failures(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/redirect");
        auto redirect{ session.Get() };
        bool passed{ check(!redirect.error && redirect.status_code == 200 && redirect.redirect_count == 1 && redirect.url == server.Url() && redirect.text == "Hello session!", "default redirects must follow Location") };
        passed &= check(redirect.raw_header.contains("302 Found") && !redirect.header.contains("X-Intermediate"), "raw headers retain redirects while parsed headers describe the final response");
        session.SetOption(mcr::Redirect{ false });
        passed &= check(session.Get().status_code == 302, "redirect following may be disabled");
        session.SetRedirect(mcr::Redirect{ 1L });
        session.SetUrl(server.Url("/loop"));
        passed &= check(session.Get().error.code == mcr::ErrorCode::TOO_MANY_REDIRECTS, "redirect limits must surface as transport errors");
        session.SetUrl(server.Url("/partial"));
        auto partial{ session.Get() };
        passed &= check(partial.error.code == mcr::ErrorCode::PARTIAL_FILE && partial.text == "short", "partial transfer failures must preserve received bytes");
        session.SetUrl(server.Url("/slow"));
        session.SetTimeout(mcr::Timeout{ 25ms });
        passed &= check(session.Get().error.code == mcr::ErrorCode::OPERATION_TIMEDOUT, "timeouts must cancel a delayed local response");
        session.SetTimeout(mcr::Timeout{ 3000ms });
        session.SetUrl(server.Url());
        auto cancellation{ std::make_shared<std::atomic_bool>(true) };
        session.SetCancellationParam(cancellation);
        int progress{};
        session.SetProgressCallback(mcr::ProgressCallback{ [&](auto, auto, auto, auto, auto) { ++progress; return true; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && progress == 0, "cancellation must precede the progress observer even if the observer is replaced later");
        cancellation->store(false);
        passed &= check(!session.Get().error && progress > 0, "cleared cancellation must allow reuse with progress callbacks");
        session.SetCancellationParam(nullptr);
        session.SetProgressCallback(mcr::ProgressCallback{ [](auto, auto, auto, auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "false progress callbacks must abort");
        session.SetProgressCallback({});
        passed &= check(!session.Get().error, "cleared progress callbacks must restore transfers");
        return passed;
    }

    auto check_callbacks(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server);
        std::string body, headers;
        session.SetOption(mcr::WriteCallback{ [&](std::string_view data, auto) { body += data; return true; } });
        session.SetOption(mcr::HeaderCallback{ [&](std::string_view data, auto) { headers += data; return true; } });
        auto response{ session.Get() };
        bool passed{ check(!response.error && response.text.empty() && body == "Hello session!" && response.raw_header == headers && response.header["Content-Type"] == "text/plain", "callbacks must stream the body and observe headers without losing response metadata") };
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::WRITE_ERROR, "false write callbacks must abort");
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) -> bool { throw std::runtime_error{ "callback failed" }; } });
        try {
            (void)session.Get();
            passed &= check(false, "write callback exceptions must be rethrown");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "callback failed"sv, "callback exception identity must be preserved");
        }
        session.SetWriteCallback({});
        passed &= check(session.Get().text == "Hello session!", "empty write callback must restore buffering after an exception");
        session.SetHeaderCallback(mcr::HeaderCallback{ [](auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::WRITE_ERROR, "header callbacks must be able to abort");
        session.SetHeaderCallback({});
        session.SetUrl(server.Url("/echo"));
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t& length, auto) { ++length; return true; } });
        passed &= check(session.Post().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "oversized producer counts must abort without reading outside the curl buffer");
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t&, auto) -> bool { throw std::runtime_error{ "read failed" }; } });
        try {
            (void)session.Post();
            passed &= check(false, "read callback exceptions must propagate after curl returns");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "read failed"sv, "read callback exception identity must be preserved");
        }
        for (mcr::CprOffT const size : { mcr::CprOffT{ 10 }, mcr::CprOffT{ -1 } }) {
            std::string source{ "read\0bytes", 10 };
            std::size_t offset{};
            session.SetOption(mcr::ReadCallback{ size, [&](char* buffer, std::size_t& length, auto) {
                                                    length = (std::min)(length, source.size() - offset);
                                                    std::memcpy(buffer, source.data() + offset, length);
                                                    offset += length;
                                                    return true;
                                                } });
            auto upload{ session.Post() };
            passed &= check(!upload.error && upload.text == source, "fixed-length and chunked read uploads must retain binary bytes");
            offset  = 0;
            passed &= check(session.Put().text == source, "read uploads must work when switching POST to PUT");
        }
        session.SetReadCallback({});
        passed &= check(session.Post().text.empty(), "cleared read callbacks must not read old upload state");
        session.SetUrl(server.Url("/sse"));
        std::vector<std::string> events;
        session.SetOption(mcr::ServerSentEventCallback{ [&](mcr::ServerSentEvent&& event, auto) { events.push_back(std::move(event.data)); return true; } });
        auto first{ session.Get() };
        auto second{ session.Get() };
        passed &= check(!first.error && !second.error && first.text.empty() && events == std::vector<std::string>{ "first", "second", "first", "second" }, "SSE must reset unfinished parser state between requests");
        session.SetWriteCallback({});
        passed &= check(session.Get().text.starts_with("id: 7"), "raw write selection must clear SSE consumption");
        int debug_count{};
        session.SetDebugCallback(mcr::DebugCallback{ [&](auto, auto, auto) { ++debug_count; } });
        passed &= check(!session.Get().error && debug_count > 0, "debug callbacks must enable curl diagnostics");
        session.SetDebugCallback(mcr::DebugCallback{ [](auto, auto, auto) { throw std::runtime_error{ "debug failed" }; } });
        try {
            (void)session.Get();
            passed &= check(false, "debug exceptions must propagate after curl returns");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "debug failed"sv, "debug callback exception identity must be preserved");
        }
        session.SetDebugCallback({});
        passed &= check(!session.Get().error, "cleared debug callbacks must permit reuse after an exception");
        return passed;
    }

    /** @brief Remove only this test's temporary file. */
    struct TempFile {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::format("mcr-session-{}.bin", std::chrono::steady_clock::now().time_since_epoch().count()) };

        ~TempFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    auto check_multipart_files(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/echo");
        TempFile          temporary;
        std::string const binary{ "file\0bytes", 10 };
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            file.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        }
        session.SetMultipart({
            { "file", mcr::File{ temporary.path.string(), "override.bin" }, "application/octet-stream" },
            { "buffer", mcr::Buffer{ binary.begin(), binary.end(), std::filesystem::path{ "buffer.bin" } } },
            { "empty", mcr::Buffer{ binary.begin(), binary.begin(), std::filesystem::path{ "empty.bin" } } }
        });
        auto       response{ session.Post() };
        bool       passed{ check(!response.error && response.text.contains("filename=\"override.bin\"") && response.text.contains("filename=\"buffer.bin\"") && response.text.contains("filename=\"empty.bin\"") && response.text.contains(binary), "MIME must support files, filename overrides, and binary or empty borrowed buffers") };
        auto const first{ response.text.find(binary) };
        passed &= check(first != std::string::npos && response.text.find(binary, first + binary.size()) != std::string::npos, "both file and buffer parts must preserve embedded null bytes");
        session.SetMultipart({
            { "missing", mcr::File{ temporary.path.string() + ".missing" } }
        });
        try {
            (void)session.Post();
            passed &= check(false, "a missing MIME file must fail preparation instead of uploading an empty field");
        } catch (std::runtime_error const&) {}
        session.SetMultipart({
            { "recovered", "text" }
        });
        passed &= check(session.Post().text.contains("name=\"recovered\""), "MIME may be replaced after a failed upload");
        return passed;
    }

    auto check_pool_and_resolve(HttpServer const& server) -> bool {
        mcr::ConnectionPool pool;
        int const           before{ server.Connections() };
        for (int index{}; index < 2; ++index) {
            mcr::Session session;
            configure(session, server);
            session.SetOption(pool);
            if (!check(!session.Get().error, "sessions attached to a pool must transfer successfully")) {
                return false;
            }
        }
        bool         passed{ check(server.Connections() == before + 1, "a shared pool must reuse a connection across session lifetimes") };
        mcr::Session session;
        configure(session, server);
        auto const address{ server.Url("").Str() };
        auto const port{ static_cast<std::uint16_t>(std::stoul(address.substr(address.rfind(':') + 1))) };
        session.SetOption(mcr::Resolve{ "session.test.invalid", "127.0.0.1", { port } });
        session.SetUrl(mcr::Url{ std::format("http://session.test.invalid:{}/hello", port) });
        auto response{ session.Get() };
        passed &= check(!response.error && response.text == "Hello session!", "DNS overrides must resolve a controlled hostname to the fixture");
        session.SetOption(std::vector<mcr::Resolve>{});
        session.SetUrl(server.Url());
        passed &= check(!session.Get().error, "clearing the configured resolve list must permit ordinary loopback requests");
        session.SetProxies({
            {     "http", address },
            { "no_proxy",      "" }
        });
        session.SetUrl(mcr::Url{ "http://proxy-target.test.invalid/hello" });
        response  = session.Get();
        passed   &= check(!response.error && response.header["X-Target"] == "http://proxy-target.test.invalid/hello", "proxy and no_proxy options must route an absolute-form request through the fixture");
        session.SetProxies({
            { "http", "" }
        });
        session.SetUrl(server.Url());
        response  = session.Get();
        passed   &= check(!response.error && response.header["X-Target"] == "/hello", "replacing the proxy configuration must restore direct requests");
        return passed;
    }

    auto check_downloads_and_async(HttpServer const& server) -> bool {
        auto session{ std::make_shared<mcr::Session>() };
        configure(*session, server);
        std::string normal, download;
        session->SetWriteCallback(mcr::WriteCallback{ [&](auto data, auto) { normal += data; return true; } });
        session->SetBody(mcr::Body{ "ignored while downloading" });
        auto response{ session->Download(mcr::WriteCallback{ [&](auto data, auto) { download += data; return true; } }) };
        bool passed{ check(!response.error && response.text.empty() && download == "Hello session!" && normal.empty(), "downloads must override stored content and body consumers for this transfer") };
        (void)session->Get();
        passed &= check(normal == "Hello session!" && download == normal, "ordinary writes must resume after callback downloads");
        session->SetWriteCallback({});
        session->RemoveContent();
        TempFile temporary;
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            auto          future{ session->DownloadAsync(file) };
            auto          result{ future.Get() };
            passed &= check(!result.error && result.text.empty(), "asynchronous file download must complete without buffering the body");
        }
        std::ifstream file{ temporary.path, std::ios::binary };
        std::string   bytes{ std::istreambuf_iterator<char>{ file }, {} };
        file.close();
        passed &= check(bytes == "Hello session!" && session->Get().text == bytes, "file download must not leave a dangling stream pointer for subsequent GET");
        std::ofstream closed;
        passed &= check(session->Download(closed).error.code == mcr::ErrorCode::WRITE_ERROR, "failed file writes must be reported as transport failures");
        session->PrepareHead();
        auto prepared{ session->Complete(curl_easy_perform(session->GetCurlHolder()->handle)) };
        passed &= check(!prepared.error && prepared.text.empty() && prepared.status_code == 200, "Prepare/Complete must support externally driven transfers");
        auto callback{ session->GetCallback([prefix = std::make_unique<std::string>("result: ")](mcr::Response result) { return *prefix + result.text; }) };
        passed &= check(callback.Get() == "result: Hello session!", "continuations must support move-only captures");
        auto future{ session->GetAsync() };
        session.reset();
        passed &= check(future.Get().text == "Hello session!", "async work must keep its session alive after external owners release it");
        mcr::Session stack;
        try {
            (void)stack.GetAsync();
            passed &= check(false, "async stack sessions must be rejected");
        } catch (std::runtime_error const&) {}
        return passed;
    }
} // namespace

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        HttpServer server;
        passed &= check_reuse(server);
        passed &= check_methods_and_content(server);
        passed &= check_options(server);
        passed &= check_redirects_and_failures(server);
        passed &= check_callbacks(server);
        passed &= check_multipart_files(server);
        passed &= check_pool_and_resolve(server);
        passed &= check_downloads_and_async(server);
        mcr::Async::Cleanup();
        server.Check();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
