/**
 * @file session.cppm
 * @brief Reusable synchronous and asynchronous HTTP sessions built on curl easy handles.
 */
module;

#include <curl/curl.h>

// Windows COM headers define this macro, which would corrupt mcr.interface imports.
#ifdef interface
    #undef interface
#endif

export module mcr.session;

export import mcr.accept_encoding;
export import mcr.async;
export import mcr.auth;
export import mcr.bearer;
export import mcr.body;
export import mcr.body_view;
export import mcr.callback;
export import mcr.connect_timeout;
export import mcr.connection_pool;
export import mcr.http_version;
export import mcr.interface;
export import mcr.limit_rate;
export import mcr.local_port;
export import mcr.local_port_range;
export import mcr.low_speed;
export import mcr.multipart;
export import mcr.parameters;
export import mcr.payload;
export import mcr.proxies;
export import mcr.range;
export import mcr.redirect;
export import mcr.reserve_size;
export import mcr.resolve;
export import mcr.response;
export import mcr.sse;
export import mcr.timeout;
export import mcr.unix_socket;
export import mcr.verbose;

import mcr.util;
import std;

export namespace mcr {

    using AsyncResponse = AsyncWrapper<Response>;                                           ///< Asynchronous transfer result.
    using Content       = std::variant<std::monostate, Payload, Body, BodyView, Multipart>; ///< Persistent request content.

    /**
     * @brief Reuse a curl connection cache, cookies, options, and content across requests.
     * @note A session must only be accessed by one caller at a time, including asynchronous work.
     * Async methods require std::shared_ptr ownership. Borrowed body buffers, files, callback captures,
     * and a configured ConnectionPool must outlive all transfers that use them.
     * Content persists until replaced or removed; HEAD and Download ignore it without removing it.
     * Curl option failures throw std::runtime_error. Transfer failures are reported in Response::error.
     * Callback exceptions are rethrown after curl returns, never through curl's C frames.
     * Advanced SSL options, proxy authentication, and interceptors are not part of this initial port.
     */
    class Session : public std::enable_shared_from_this<Session> {
    private:
        std::shared_ptr<CurlHolder>       m_curl{ std::make_shared<CurlHolder>() }; ///< Owned transfer resources.
        Url                               m_url;                                    ///< Base URL before adding parameters.
        Parameters                        m_parameters;                             ///< Persistent URL parameters.
        Header                            m_header;                                 ///< Persistent request headers.
        Proxies                           m_proxies;                                ///< Persistent proxy selection.
        AcceptEncoding                    m_accept_encoding;                        ///< Compression preference.
        Content                           m_content;                                ///< Owned or borrowed request content.
        ReadCallback                      m_read;                                   ///< Optional upload producer.
        HeaderCallback                    m_header_callback;                        ///< Optional header observer.
        WriteCallback                     m_write;                                  ///< Optional response consumer.
        ProgressCallback                  m_progress;                               ///< Optional progress observer.
        DebugCallback                     m_debug;                                  ///< Optional diagnostics observer.
        ServerSentEventCallback           m_sse;                                    ///< Optional event consumer.
        ServerSentEventParser             m_sse_parser;                             ///< Parser reset before every transfer.
        std::shared_ptr<std::atomic_bool> m_cancellation;                           ///< Shared cancellation flag.
        std::string                       m_response_string;                        ///< Current buffered response body.
        std::string                       m_header_string;                          ///< Current raw response headers.
        std::size_t                       m_reserve_size{};                         ///< Requested body buffer reservation.
        WriteCallback                     m_download_write;                         ///< Consumer used only for a prepared download.
        std::ofstream*                    m_download_file{};                        ///< Borrowed file for a prepared download.
        bool                              m_downloading{};                          ///< Selects the current body destination.
        std::exception_ptr                m_callback_error;                         ///< First exception caught inside a curl callback.

    public:
        /** @brief Initialize cpr-compatible redirects, cookies, compression, and keepalive defaults. */
        Session() {
            auto const* version{ curl_version_info(CURLVERSION_NOW) };
            SetUserAgent(UserAgent{ std::string{ "curl/" } + version->version });
            SetRedirect(Redirect{});
            setOption(CURLOPT_COOKIEFILE, "");
            setOption(CURLOPT_NOSIGNAL, 1L);
            setOption(CURLOPT_TCP_KEEPALIVE, 1L);
            setOption(CURLOPT_CERTINFO, 1L);
        }

        Session(Session const&)                    = delete;
        Session(Session&&)                         = delete;
        auto operator=(Session const&) -> Session& = delete;
        auto operator=(Session&&) -> Session&      = delete;

        /** @brief Detach borrowed callback and body pointers before destroying session state. */
        ~Session() { curl_easy_reset(m_curl->handle); }

        /** @brief Replace the base URL. @param url URL to copy. */
        auto SetUrl(Url const& url) -> void { m_url = url; }

        /** @brief Copy URL parameters. @param parameters Replacement parameters. */
        auto SetParameters(Parameters const& parameters) -> void { m_parameters = parameters; }

        /** @brief Move URL parameters. @param parameters Replacement parameters. */
        auto SetParameters(Parameters&& parameters) -> void { m_parameters = std::move(parameters); }

        /** @brief Replace all request headers. @param header Headers to copy. */
        auto SetHeader(Header const& header) -> void { m_header = header; }

        /** @brief Merge headers using case-insensitive replacement. @param header Headers to add or replace. */
        auto UpdateHeader(Header const& header) -> void {
            for (auto const& [name, value] : header) {
                m_header[name] = value;
            }
        }

        /** @brief Access persistent request headers. @return Mutable header map. */
        [[nodiscard]] auto GetHeader() -> Header& { return m_header; }

        /** @brief Inspect persistent request headers. @return Read-only header map. */
        [[nodiscard]] auto GetHeader() const -> Header const& { return m_header; }

        /** @brief Set the total transfer timeout. @param timeout Duration; zero disables the timeout. */
        auto SetTimeout(Timeout const& timeout) -> void { setOption(CURLOPT_TIMEOUT_MS, timeout.Milliseconds()); }

        /** @brief Set the connection timeout. @param timeout Connection establishment deadline. */
        auto SetConnectTimeout(ConnectTimeout const& timeout) -> void { setOption(CURLOPT_CONNECTTIMEOUT_MS, timeout.Milliseconds()); }

        /** @brief Attach a borrowed connection pool. @param pool Pool that must outlive this session's handle. */
        auto SetConnectionPool(ConnectionPool const& pool) -> void { pool.SetupHandler(m_curl->handle); }

        /** @brief Configure HTTP credentials. @param auth Owned credentials and authentication policy to copy into curl. */
        auto SetAuth(Authentication const& auth) -> void {
            long mode{};
            switch (auth.GetAuthMode()) {
                case AuthMode::BASIC    : mode = CURLAUTH_BASIC; break;
                case AuthMode::DIGEST   : mode = CURLAUTH_DIGEST; break;
                case AuthMode::NTLM     : mode = CURLAUTH_NTLM; break;
                case AuthMode::NEGOTIATE: mode = CURLAUTH_NEGOTIATE; break;
                case AuthMode::ANY      : mode = static_cast<long>(CURLAUTH_ANY); break;
                case AuthMode::ANYSAFE  : mode = static_cast<long>(CURLAUTH_ANYSAFE); break;
                default                 : throw std::invalid_argument{ "mcr::Session: unknown authentication mode." };
            }
            setOption(CURLOPT_HTTPAUTH, mode);
            setOption(CURLOPT_USERPWD, auth.GetAuthString());
        }

        /** @brief Configure a bearer token. @param token Token copied into curl. */
        auto SetBearer(Bearer const& token) -> void {
            setOption(CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_BEARER));
            setOption(CURLOPT_XOAUTH2_BEARER, token.GetToken());
        }

        /** @brief Replace the User-Agent header. @param ua User-agent text. */
        auto SetUserAgent(UserAgent const& ua) -> void { setOption(CURLOPT_USERAGENT, ua.CStr()); }

        /** @brief Copy form content for subsequent requests. @param payload URL-encoded form fields. */
        auto SetPayload(Payload const& payload) -> void { m_content = payload; }

        /** @brief Move form content for subsequent requests. @param payload URL-encoded form fields. */
        auto SetPayload(Payload&& payload) -> void { m_content = std::move(payload); }

        /** @brief Copy proxy mappings. @param proxies Protocol and no_proxy mappings. */
        auto SetProxies(Proxies const& proxies) -> void { m_proxies = proxies; }

        /** @brief Move proxy mappings. @param proxies Protocol and no_proxy mappings. */
        auto SetProxies(Proxies&& proxies) -> void { m_proxies = std::move(proxies); }

        /** @brief Copy multipart descriptors. @param multipart Parts; buffer bytes remain borrowed. */
        auto SetMultipart(Multipart const& multipart) -> void { m_content = multipart; }

        /** @brief Move multipart descriptors. @param multipart Parts; buffer bytes remain borrowed. */
        auto SetMultipart(Multipart&& multipart) -> void { m_content = std::move(multipart); }

        /** @brief Configure redirect handling. @param redirect Limits, credential forwarding, and POST preservation. */
        auto SetRedirect(Redirect const& redirect) -> void {
            setOption(CURLOPT_FOLLOWLOCATION, redirect.follow ? 1L : 0L);
            setOption(CURLOPT_MAXREDIRS, redirect.maximum);
            setOption(CURLOPT_UNRESTRICTED_AUTH, redirect.cont_send_cred ? 1L : 0L);
            long mask{};
            if (any(redirect.post_flags & PostRedirectFlags::POST_301)) {
                mask |= CURL_REDIR_POST_301;
            }
            if (any(redirect.post_flags & PostRedirectFlags::POST_302)) {
                mask |= CURL_REDIR_POST_302;
            }
            if (any(redirect.post_flags & PostRedirectFlags::POST_303)) {
                mask |= CURL_REDIR_POST_303;
            }
            setOption(CURLOPT_POSTREDIR, mask);
        }

        /** @brief Clear the cookie engine and set explicit request cookies. @param cookies Cookies to encode. */
        auto SetCookies(Cookies const& cookies) -> void {
            setOption(CURLOPT_COOKIELIST, "ALL");
            setOption(CURLOPT_COOKIE, cookies.GetEncoded(*m_curl).c_str());
        }

        /** @brief Copy body bytes for subsequent requests. @param body Bytes to own. */
        auto SetBody(Body const& body) -> void { m_content = body; }

        /** @brief Move body bytes for subsequent requests. @param body Bytes to own. */
        auto SetBody(Body&& body) -> void { m_content = std::move(body); }

        /** @brief Borrow body bytes for subsequent requests. @param body View whose bytes must outlive transfers. */
        auto SetBodyView(BodyView body) -> void { m_content = body; }

        /** @brief Configure low-speed cancellation. @param low_speed Minimum rate and observation duration. */
        auto SetLowSpeed(LowSpeed const& low_speed) -> void {
            setOption(CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(low_speed.limit));
            setOption(CURLOPT_LOW_SPEED_TIME, static_cast<long>(low_speed.time.count()));
        }

        /** @brief Configure a Unix socket. @param unix_socket Socket path copied into curl. */
        auto SetUnixSocket(UnixSocket const& unix_socket) -> void { setOption(CURLOPT_UNIX_SOCKET_PATH, unix_socket.GetUnixSocketString()); }

        /** @brief Set or clear the upload producer. @param read Callback used when no Content is configured. */
        auto SetReadCallback(ReadCallback const& read) -> void { m_read = read; }

        /** @brief Set or clear a header observer; response headers are still collected. @param header Observer to copy. */
        auto SetHeaderCallback(HeaderCallback const& header) -> void { m_header_callback = header; }

        /** @brief Set a body consumer and clear SSE consumption. @param write Consumer; an empty callback restores buffering. */
        auto SetWriteCallback(WriteCallback const& write) -> void {
            m_write = write;
            m_sse   = {};
        }

        /** @brief Set or clear a progress observer. @param progress Observer; false cancels the transfer. */
        auto SetProgressCallback(ProgressCallback const& progress) -> void { m_progress = progress; }

        /** @brief Set a diagnostic observer and enable verbose output when nonempty. @param debug Observer to copy. */
        auto SetDebugCallback(DebugCallback const& debug) -> void {
            m_debug = debug;
            SetVerbose(Verbose{ bool(m_debug.callback) });
        }

        /** @brief Set an SSE consumer and clear raw body consumption. @param sse Observer reset to a fresh stream each request. */
        auto SetServerSentEventCallback(ServerSentEventCallback const& sse) -> void {
            m_sse   = sse;
            m_write = {};
        }

        /** @brief Enable or disable curl diagnostics. @param verbose Logging preference. */
        auto SetVerbose(Verbose const& verbose) -> void { setOption(CURLOPT_VERBOSE, verbose.verbose ? 1L : 0L); }

        /** @brief Bind an outgoing interface. @param iface Empty text restores automatic selection. */
        auto SetInterface(Interface const& iface) -> void { setOption(CURLOPT_INTERFACE, iface.Str().empty() ? nullptr : iface.CStr()); }

        /** @brief Choose the first local port. @param local_port Port number. */
        auto SetLocalPort(LocalPort const& local_port) -> void { setOption(CURLOPT_LOCALPORT, static_cast<long>(static_cast<std::uint16_t>(local_port))); }

        /** @brief Choose the local port search range. @param local_port_range Number of ports to try. */
        auto SetLocalPortRange(LocalPortRange const& local_port_range) -> void { setOption(CURLOPT_LOCALPORTRANGE, static_cast<long>(static_cast<std::uint16_t>(local_port_range))); }

        /** @brief Set the preferred HTTP version. @param version Protocol preference supported by the linked curl build. */
        auto SetHttpVersion(HttpVersion const& version) -> void {
            long value{};
            switch (version.code) {
                case HttpVersionCode::VERSION_NONE               : value = CURL_HTTP_VERSION_NONE; break;
                case HttpVersionCode::VERSION_1_0                : value = CURL_HTTP_VERSION_1_0; break;
                case HttpVersionCode::VERSION_1_1                : value = CURL_HTTP_VERSION_1_1; break;
                case HttpVersionCode::VERSION_2_0                : value = CURL_HTTP_VERSION_2_0; break;
                case HttpVersionCode::VERSION_2_0_TLS            : value = CURL_HTTP_VERSION_2TLS; break;
                case HttpVersionCode::VERSION_2_0_PRIOR_KNOWLEDGE: value = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE; break;
                case HttpVersionCode::VERSION_3_0                : value = CURL_HTTP_VERSION_3; break;
                case HttpVersionCode::VERSION_3_0_ONLY           : value = CURL_HTTP_VERSION_3ONLY; break;
                default                                          : throw std::invalid_argument{ "mcr::Session: unknown HTTP version." };
            }
            setOption(CURLOPT_HTTP_VERSION, value);
        }

        /** @brief Request one byte range. @param range Range serialized for curl. */
        auto SetRange(Range const& range) -> void { setOption(CURLOPT_RANGE, range.Str().c_str()); }

        /** @brief Replace hostname resolution overrides. @param resolve One mapping. */
        auto SetResolve(Resolve const& resolve) -> void { SetResolves({ resolve }); }

        /** @brief Replace all hostname resolution overrides. @param resolves Mappings; empty clears the list. */
        auto SetResolves(std::vector<Resolve> const& resolves) -> void {
            CurlList list{ nullptr, &curl_slist_free_all };
            for (auto const& resolve : resolves) {
                for (auto port : resolve.ports) {
                    appendList(list, std::format("{}:{}:{}", resolve.host, port, resolve.addr));
                }
            }
            setOption(CURLOPT_RESOLVE, list.get());
            curl_slist_free_all(std::exchange(m_curl->resolve_curl_list, list.release()));
        }

        /** @brief Request multiple byte ranges. @param multi_range Ranges serialized for curl. */
        auto SetMultiRange(MultiRange const& multi_range) -> void { setOption(CURLOPT_RANGE, multi_range.Str().c_str()); }

        /** @brief Set response buffer reservation. @param reserve_size Minimum capacity requested before each transfer. */
        auto SetReserveSize(ReserveSize const& reserve_size) -> void { ResponseStringReserve(reserve_size.size); }

        /** @brief Copy compression preferences. @param accept_encoding Encodings to advertise and decode. */
        auto SetAcceptEncoding(AcceptEncoding const& accept_encoding) -> void { m_accept_encoding = accept_encoding; }

        /** @brief Move compression preferences. @param accept_encoding Encodings to advertise and decode. */
        auto SetAcceptEncoding(AcceptEncoding&& accept_encoding) -> void { m_accept_encoding = std::move(accept_encoding); }

        /** @brief Limit upload and download rates. @param limit_rate Bytes per second; zero means unlimited. */
        auto SetLimitRate(LimitRate const& limit_rate) -> void {
            setOption(CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(limit_rate.downrate));
            setOption(CURLOPT_MAX_SEND_SPEED_LARGE, static_cast<curl_off_t>(limit_rate.uprate));
        }

        /** @brief Inspect persistent request content. @return Read-only content variant. */
        [[nodiscard]] auto GetContent() const -> Content const& { return m_content; }

        /** @brief Remove stored content and detach body/MIME pointers; read callbacks remain configured. */
        auto RemoveContent() -> void {
            clearCurlContent();
            m_content = std::monostate{};
        }

        /** @brief Set a cancellation flag, independently of progress callback ordering. @param param Shared flag; null disables cancellation. */
        auto SetCancellationParam(std::shared_ptr<std::atomic_bool> param) -> void { m_cancellation = std::move(param); }

        /** @brief Reserve response capacity before each request. @param size Zero restores ordinary dynamic allocation. */
        auto ResponseStringReserve(std::size_t size) -> void { m_reserve_size = size; }

        /**
         * @brief Perform HEAD and obtain the server's advertised response length.
         * @return Length for a successful HTTP 200 response, or -1 if unknown or unsuccessful.
         */
        [[nodiscard]] auto GetDownloadFileLength() -> CprOffT {
            auto const response{ Head() };
            CprOffT    length{ -1 };
            if (!response.error && response.status_code == 200) {
                (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
            }
            return length;
        }

        /** @brief Access the easy handle for advanced configuration or prepared transfers. @return Shared holder; options are reset when the session dies. */
        [[nodiscard]] auto GetCurlHolder() -> std::shared_ptr<CurlHolder> { return m_curl; }

        /** @brief Combine encoded parameters with the URL's existing query, before any fragment. @return Full request URL. */
        [[nodiscard]] auto GetFullRequestUrl() -> std::string {
            auto       result{ m_url.Str() };
            auto const parameters{ m_parameters.GetContent(*m_curl) };
            if (parameters.empty()) {
                return result;
            }
            auto const             fragment{ result.find('#') };
            auto const             end{ fragment == std::string::npos ? result.size() : fragment };
            std::string_view const base{ result.data(), end };
            std::string            separator;
            if (base.find('?') == std::string_view::npos) {
                separator = "?";
            } else if (!base.ends_with('?') && !base.ends_with('&')) {
                separator = "&";
            }
            result.insert(end, separator + parameters);
            return result;
        }

        /** @brief Obtain shared ownership for asynchronous work. @return Shared session. @throws std::runtime_error If not managed by shared_ptr. */
        [[nodiscard]] auto GetSharedPtrFromThis() -> std::shared_ptr<Session> {
            auto shared{ weak_from_this().lock() };
            if (!shared) {
                throw std::runtime_error{ "mcr::Session: asynchronous requests require std::shared_ptr ownership." };
            }
            return shared;
        }

        /** @brief Prepare a GET download into a temporary consumer. @param write Consumer copied for this download only. */
        auto PrepareDownload(WriteCallback const& write) -> void {
            prepare("GET", true);
            m_download_write = write;
        }

        /** @brief Prepare a GET download into a borrowed binary stream. @param file Stream that must outlive completion. */
        auto PrepareDownload(std::ofstream& file) -> void {
            prepare("GET", true);
            m_download_file = &file;
        }

        /** @brief Download into a callback without retaining it for later requests. @param write Download consumer. @return Transfer metadata with an empty body. */
        auto Download(WriteCallback const& write) -> Response {
            PrepareDownload(write);
            return perform();
        }

        /** @brief Download into a borrowed binary output stream. @param file Output stream; the caller checks later flush/close errors. @return Transfer metadata with an empty body. */
        auto Download(std::ofstream& file) -> Response {
            PrepareDownload(file);
            return perform();
        }

        /** @brief Download asynchronously into a callback. @param write Copied consumer. @return Future retaining this session. */
        auto DownloadAsync(WriteCallback const& write) -> AsyncResponse {
            return async([self = GetSharedPtrFromThis(), write] { return self->Download(write); });
        }

        /** @brief Download asynchronously into a stream. @param file Stream that must outlive completion. @return Future retaining this session. */
        auto DownloadAsync(std::ofstream& file) -> AsyncResponse {
            return async([self = GetSharedPtrFromThis(), &file] { return self->Download(file); });
        }

        /**
         * @brief Capture a prepared transfer after curl_easy_perform or an external multi loop finishes.
         * @param curl_error Result returned by curl for this transfer.
         * @return Independent response snapshot.
         * @throws Any exception captured from user callbacks during the transfer.
         */
        auto Complete(CURLcode curl_error) -> Response {
            m_download_file  = nullptr;
            m_download_write = {};
            if (m_callback_error) {
                std::rethrow_exception(std::exchange(m_callback_error, {}));
            }
            curl_slist* raw_cookies{ nullptr };
            checkCurl(curl_easy_getinfo(m_curl->handle, CURLINFO_COOKIELIST, &raw_cookies));
            CurlList    owned_cookies{ raw_cookies, &curl_slist_free_all };
            auto        cookies{ util::parse_cookies(owned_cookies.get()) };
            std::string error_message{ m_curl->error.data() };
            if (curl_error != CURLE_OK && error_message.empty()) {
                error_message = curl_easy_strerror(curl_error);
            }
            return Response{
                m_curl,
                std::move(m_response_string),
                std::move(m_header_string),
                std::move(cookies),
                Error{ static_cast<std::int32_t>(curl_error), std::move(error_message) }
            };
        }

        /** @brief Complete a prepared download. @param curl_error Curl transfer result. @return Download metadata. */
        auto CompleteDownload(CURLcode curl_error) -> Response { return Complete(curl_error); }

        /** @brief Prepare DELETE without starting network I/O. */
        auto PrepareDelete() -> void { prepare("DELETE"); }

        /** @brief Execute DELETE with the stored options. @return Completed response. */
        auto Delete() -> Response {
            PrepareDelete();
            return perform();
        }

        /** @brief Execute DELETE asynchronously. @return Future retaining shared ownership of this session. */
        auto DeleteAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Delete(); });
        }

        /** @brief Pass a DELETE response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto DeleteCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Delete()); });
        }

        /** @brief Prepare GET without starting network I/O. */
        auto PrepareGet() -> void { prepare("GET"); }

        /** @brief Execute GET with the stored options. @return Completed response. */
        auto Get() -> Response {
            PrepareGet();
            return perform();
        }

        /** @brief Execute GET asynchronously. @return Future retaining shared ownership of this session. */
        auto GetAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Get(); });
        }

        /** @brief Pass a GET response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto GetCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Get()); });
        }

        /** @brief Prepare HEAD without starting network I/O. */
        auto PrepareHead() -> void { prepare("HEAD"); }

        /** @brief Execute HEAD with the stored options. @return Completed response. */
        auto Head() -> Response {
            PrepareHead();
            return perform();
        }

        /** @brief Execute HEAD asynchronously. @return Future retaining shared ownership of this session. */
        auto HeadAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Head(); });
        }

        /** @brief Pass a HEAD response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto HeadCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Head()); });
        }

        /** @brief Prepare OPTIONS without starting network I/O. */
        auto PrepareOptions() -> void { prepare("OPTIONS"); }

        /** @brief Execute OPTIONS with the stored options. @return Completed response. */
        auto Options() -> Response {
            PrepareOptions();
            return perform();
        }

        /** @brief Execute OPTIONS asynchronously. @return Future retaining shared ownership of this session. */
        auto OptionsAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Options(); });
        }

        /** @brief Pass a OPTIONS response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto OptionsCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Options()); });
        }

        /** @brief Prepare PATCH without starting network I/O. */
        auto PreparePatch() -> void { prepare("PATCH"); }

        /** @brief Execute PATCH with the stored options. @return Completed response. */
        auto Patch() -> Response {
            PreparePatch();
            return perform();
        }

        /** @brief Execute PATCH asynchronously. @return Future retaining shared ownership of this session. */
        auto PatchAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Patch(); });
        }

        /** @brief Pass a PATCH response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto PatchCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Patch()); });
        }

        /** @brief Prepare POST without starting network I/O. */
        auto PreparePost() -> void { prepare("POST"); }

        /** @brief Execute POST with the stored options. @return Completed response. */
        auto Post() -> Response {
            PreparePost();
            return perform();
        }

        /** @brief Execute POST asynchronously. @return Future retaining shared ownership of this session. */
        auto PostAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Post(); });
        }

        /** @brief Pass a POST response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto PostCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Post()); });
        }

        /** @brief Prepare PUT without starting network I/O. */
        auto PreparePut() -> void { prepare("PUT"); }

        /** @brief Execute PUT with the stored options. @return Completed response. */
        auto Put() -> Response {
            PreparePut();
            return perform();
        }

        /** @brief Execute PUT asynchronously. @return Future retaining shared ownership of this session. */
        auto PutAsync() -> AsyncResponse {
            return async([self = GetSharedPtrFromThis()] { return self->Put(); });
        }

        /** @brief Pass a PUT response to an asynchronous continuation. @tparam Then Continuation type. @param then Consumer of the response. @return Future containing the consumer's result. */
        template <typename Then>
        auto PutCallback(Then then) {
            return async([self = GetSharedPtrFromThis(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Put()); });
        }

        /** @brief Forward a Url option to its setter. @param value Option to apply. */
        auto SetOption(Url const& value) -> void { SetUrl(value); }

        /** @brief Forward a Parameters option to its setter. @param value Option to apply. */
        auto SetOption(Parameters const& value) -> void { SetParameters(value); }

        /** @brief Move a Parameters option into this session. @param value Option to transfer. */
        auto SetOption(Parameters&& value) -> void { SetParameters(std::move(value)); }

        /** @brief Forward a Header option to its setter. @param value Option to apply. */
        auto SetOption(Header const& value) -> void { SetHeader(value); }

        /** @brief Forward a Timeout option to its setter. @param value Option to apply. */
        auto SetOption(Timeout const& value) -> void { SetTimeout(value); }

        /** @brief Forward a ConnectTimeout option to its setter. @param value Option to apply. */
        auto SetOption(ConnectTimeout const& value) -> void { SetConnectTimeout(value); }

        /** @brief Forward a ConnectionPool option to its setter. @param value Option to apply. */
        auto SetOption(ConnectionPool const& value) -> void { SetConnectionPool(value); }

        /** @brief Forward a Authentication option to its setter. @param value Option to apply. */
        auto SetOption(Authentication const& value) -> void { SetAuth(value); }

        /** @brief Forward a Bearer option to its setter. @param value Option to apply. */
        auto SetOption(Bearer const& value) -> void { SetBearer(value); }

        /** @brief Forward a UserAgent option to its setter. @param value Option to apply. */
        auto SetOption(UserAgent const& value) -> void { SetUserAgent(value); }

        /** @brief Forward a Payload option to its setter. @param value Option to apply. */
        auto SetOption(Payload const& value) -> void { SetPayload(value); }

        /** @brief Move a Payload option into this session. @param value Option to transfer. */
        auto SetOption(Payload&& value) -> void { SetPayload(std::move(value)); }

        /** @brief Forward a Proxies option to its setter. @param value Option to apply. */
        auto SetOption(Proxies const& value) -> void { SetProxies(value); }

        /** @brief Move a Proxies option into this session. @param value Option to transfer. */
        auto SetOption(Proxies&& value) -> void { SetProxies(std::move(value)); }

        /** @brief Forward a Multipart option to its setter. @param value Option to apply. */
        auto SetOption(Multipart const& value) -> void { SetMultipart(value); }

        /** @brief Move a Multipart option into this session. @param value Option to transfer. */
        auto SetOption(Multipart&& value) -> void { SetMultipart(std::move(value)); }

        /** @brief Forward a Redirect option to its setter. @param value Option to apply. */
        auto SetOption(Redirect const& value) -> void { SetRedirect(value); }

        /** @brief Forward a Cookies option to its setter. @param value Option to apply. */
        auto SetOption(Cookies const& value) -> void { SetCookies(value); }

        /** @brief Forward a Body option to its setter. @param value Option to apply. */
        auto SetOption(Body const& value) -> void { SetBody(value); }

        /** @brief Move a Body option into this session. @param value Option to transfer. */
        auto SetOption(Body&& value) -> void { SetBody(std::move(value)); }

        /** @brief Forward a BodyView option to its setter. @param value Option to apply. */
        auto SetOption(BodyView value) -> void { SetBodyView(value); }

        /** @brief Forward a ReadCallback option to its setter. @param value Option to apply. */
        auto SetOption(ReadCallback const& value) -> void { SetReadCallback(value); }

        /** @brief Forward a HeaderCallback option to its setter. @param value Option to apply. */
        auto SetOption(HeaderCallback const& value) -> void { SetHeaderCallback(value); }

        /** @brief Forward a WriteCallback option to its setter. @param value Option to apply. */
        auto SetOption(WriteCallback const& value) -> void { SetWriteCallback(value); }

        /** @brief Forward a ProgressCallback option to its setter. @param value Option to apply. */
        auto SetOption(ProgressCallback const& value) -> void { SetProgressCallback(value); }

        /** @brief Forward a DebugCallback option to its setter. @param value Option to apply. */
        auto SetOption(DebugCallback const& value) -> void { SetDebugCallback(value); }

        /** @brief Forward a ServerSentEventCallback option to its setter. @param value Option to apply. */
        auto SetOption(ServerSentEventCallback const& value) -> void { SetServerSentEventCallback(value); }

        /** @brief Forward a LowSpeed option to its setter. @param value Option to apply. */
        auto SetOption(LowSpeed const& value) -> void { SetLowSpeed(value); }

        /** @brief Forward a Verbose option to its setter. @param value Option to apply. */
        auto SetOption(Verbose const& value) -> void { SetVerbose(value); }

        /** @brief Forward a UnixSocket option to its setter. @param value Option to apply. */
        auto SetOption(UnixSocket const& value) -> void { SetUnixSocket(value); }

        /** @brief Forward a Interface option to its setter. @param value Option to apply. */
        auto SetOption(Interface const& value) -> void { SetInterface(value); }

        /** @brief Forward a LocalPort option to its setter. @param value Option to apply. */
        auto SetOption(LocalPort const& value) -> void { SetLocalPort(value); }

        /** @brief Forward a LocalPortRange option to its setter. @param value Option to apply. */
        auto SetOption(LocalPortRange const& value) -> void { SetLocalPortRange(value); }

        /** @brief Forward a HttpVersion option to its setter. @param value Option to apply. */
        auto SetOption(HttpVersion const& value) -> void { SetHttpVersion(value); }

        /** @brief Forward a Range option to its setter. @param value Option to apply. */
        auto SetOption(Range const& value) -> void { SetRange(value); }

        /** @brief Forward a MultiRange option to its setter. @param value Option to apply. */
        auto SetOption(MultiRange const& value) -> void { SetMultiRange(value); }

        /** @brief Forward a ReserveSize option to its setter. @param value Option to apply. */
        auto SetOption(ReserveSize const& value) -> void { SetReserveSize(value); }

        /** @brief Forward a AcceptEncoding option to its setter. @param value Option to apply. */
        auto SetOption(AcceptEncoding const& value) -> void { SetAcceptEncoding(value); }

        /** @brief Move a AcceptEncoding option into this session. @param value Option to transfer. */
        auto SetOption(AcceptEncoding&& value) -> void { SetAcceptEncoding(std::move(value)); }

        /** @brief Forward a LimitRate option to its setter. @param value Option to apply. */
        auto SetOption(LimitRate const& value) -> void { SetLimitRate(value); }

        /** @brief Forward a Resolve option to its setter. @param value Option to apply. */
        auto SetOption(Resolve const& value) -> void { SetResolve(value); }

        /** @brief Forward a std::vector<Resolve> option to its setter. @param value Option to apply. */
        auto SetOption(std::vector<Resolve> const& value) -> void { SetResolves(value); }

    private:
        using CurlList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
        using CurlMime = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;

        /** @brief Translate curl setup failures into exceptions. @param code Setup result. */
        static auto checkCurl(CURLcode code) -> void {
            if (code != CURLE_OK) {
                throw std::runtime_error{ std::string{ "mcr::Session: " } + curl_easy_strerror(code) };
            }
        }

        /** @brief Apply an option with its exact curl argument type. @tparam T Argument type. @param option Curl option. @param value Option value. */
        template <typename T>
        auto setOption(CURLoption option, T value) -> void { checkCurl(curl_easy_setopt(m_curl->handle, option, value)); }

        /** @brief Append without losing ownership on allocation failure. @param list Owned list. @param value Entry text. */
        static auto appendList(CurlList& list, std::string const& value) -> void {
            auto* next{ curl_slist_append(list.get(), value.c_str()) };
            if (!next) {
                throw std::bad_alloc{};
            }
            (void)list.release();
            list.reset(next);
        }

        /** @brief Detach the previous content before freeing MIME data or replacing borrowed bytes. */
        auto clearCurlContent() -> void {
            setOption(CURLOPT_MIMEPOST, static_cast<curl_mime*>(nullptr));
            setOption(CURLOPT_POSTFIELDS, static_cast<char const*>(nullptr));
            setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(-1));
            curl_mime_free(std::exchange(m_curl->multipart, nullptr));
        }

        /** @brief Rebuild headers, preserving explicit Expect and Transfer-Encoding options. @param chunked Whether an unknown-sized upload needs chunking. */
        auto prepareHeader(bool chunked) -> void {
            CurlList list{ nullptr, &curl_slist_free_all };
            for (auto const& [name, value] : m_header) {
                appendList(list, name + (value.empty() ? ";" : ": " + value));
            }
            if (chunked && !m_header.contains("Transfer-Encoding")) {
                appendList(list, "Transfer-Encoding: chunked");
            }
            if (!m_header.contains("Expect")) {
                appendList(list, "Expect:");
            }
            setOption(CURLOPT_HTTPHEADER, list.get());
            curl_slist_free_all(std::exchange(m_curl->chunk, list.release()));
        }

        /** @brief Select proxy options afresh so prior protocols and no_proxy settings cannot leak. */
        auto prepareProxy() -> void {
            auto protocol{ m_url.Str().substr(0, m_url.Str().find(':')) };
            std::ranges::transform(protocol, protocol.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            setOption(CURLOPT_PROXY, m_proxies.Has(protocol) ? m_proxies[protocol].c_str() : nullptr);
            char const* no_proxy{ nullptr };
            if (m_proxies.Has("no_proxy")) {
                no_proxy = m_proxies["no_proxy"].c_str();
            } else if (m_proxies.Has("NO_PROXY")) {
                no_proxy = m_proxies["NO_PROXY"].c_str();
            }
            setOption(CURLOPT_NOPROXY, no_proxy);
        }

        /** @brief Copy MIME fields into an owned curl MIME tree. @param multipart Persistent descriptors. */
        auto prepareMultipart(Multipart const& multipart) -> void {
            CurlMime mime{ curl_mime_init(m_curl->handle), &curl_mime_free };
            if (!mime) {
                throw std::bad_alloc{};
            }
            for (auto const& part : multipart.parts) {
                auto add_part = [&] {
                    auto* item{ curl_mime_addpart(mime.get()) };
                    if (!item) {
                        throw std::bad_alloc{};
                    }
                    checkCurl(curl_mime_name(item, part.name.c_str()));
                    if (!part.content_type.empty()) {
                        checkCurl(curl_mime_type(item, part.content_type.c_str()));
                    }
                    return item;
                };
                if (part.is_file) {
                    for (auto const& file : part.files) {
                        auto* item{ add_part() };
                        checkCurl(curl_mime_filedata(item, file.filepath.c_str()));
                        auto const filename{ file.HasOverridenFilename() ? file.overriden_filename : std::filesystem::path{ file.filepath }.filename().string() };
                        checkCurl(curl_mime_filename(item, filename.c_str()));
                    }
                } else {
                    auto* item{ add_part() };
                    if (part.is_buffer) {
                        checkCurl(curl_mime_data(item, part.datalen == 0 ? "" : part.data, part.datalen));
                        checkCurl(curl_mime_filename(item, part.value.c_str()));
                    } else {
                        checkCurl(curl_mime_data(item, part.value.data(), part.value.size()));
                    }
                }
            }
            setOption(CURLOPT_MIMEPOST, mime.get());
            m_curl->multipart = mime.release();
        }

        /** @brief Configure a binary body with an explicit byte length. @param body Body bytes. @param copy Whether curl must own a copy. */
        auto prepareBytes(std::string_view body, bool copy) -> void {
            setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
            setOption(copy ? CURLOPT_COPYPOSTFIELDS : CURLOPT_POSTFIELDS, body.empty() ? "" : body.data());
        }

        /** @brief Reset method state and prepare a complete transfer. @param method HTTP method. @param download Whether to bypass stored content and body consumers. */
        auto prepare(std::string_view method, bool download = false) -> void {
            clearCurlContent();
            setOption(CURLOPT_UPLOAD, 0L);
            setOption(CURLOPT_NOBODY, 0L);
            setOption(CURLOPT_HTTPGET, 1L);
            setOption(CURLOPT_CUSTOMREQUEST, static_cast<char const*>(nullptr));
            m_downloading    = download;
            m_download_file  = nullptr;
            m_download_write = {};
            m_callback_error = {};
            m_curl->error.fill('\0');
            m_response_string.clear();
            m_header_string.clear();
            if (!download) {
                m_response_string.reserve(m_reserve_size);
            }
            m_sse_parser.Reset();
            bool const has_content{ !std::holds_alternative<std::monostate>(m_content) };
            bool const read_upload{ !download && method != "HEAD" && !has_content && bool(m_read.callback) };
            if (!download && method != "HEAD") {
                if (auto const* payload{ std::get_if<Payload>(&m_content) }) {
                    prepareBytes(payload->GetContent(*m_curl), true);
                } else if (auto const* body{ std::get_if<Body>(&m_content) }) {
                    prepareBytes(body->Str(), true);
                } else if (auto const* view{ std::get_if<BodyView>(&m_content) }) {
                    prepareBytes(view->Str(), false);
                } else if (auto const* multipart{ std::get_if<Multipart>(&m_content) }) {
                    prepareMultipart(*multipart);
                } else if (read_upload) {
                    setOption(CURLOPT_POST, 1L);
                    setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_read.size));
                } else if (method == "POST" || method == "PUT" || method == "PATCH") {
                    prepareBytes({}, false);
                }
            }
            if (method == "HEAD") {
                setOption(CURLOPT_NOBODY, 1L);
            } else if (method != "POST" && (method != "GET" || (!download && (has_content || read_upload)))) {
                setOption(CURLOPT_CUSTOMREQUEST, std::string{ method }.c_str());
            }
            if (method == "PUT") {
                setOption(CURLOPT_RANGE, static_cast<char const*>(nullptr));
            }
            prepareHeader(read_upload && m_read.size == -1);
            setOption(CURLOPT_URL, GetFullRequestUrl().c_str());
            prepareProxy();
            auto const encodings{ m_accept_encoding.GetString() };
            setOption(CURLOPT_ACCEPT_ENCODING, m_accept_encoding.Disabled() ? nullptr : encodings.c_str());
            setOption(CURLOPT_WRITEFUNCTION, &writeCallback);
            setOption(CURLOPT_WRITEDATA, static_cast<void*>(this));
            setOption(CURLOPT_HEADERFUNCTION, &headerCallback);
            setOption(CURLOPT_HEADERDATA, static_cast<void*>(this));
            setOption(CURLOPT_READFUNCTION, &readCallback);
            setOption(CURLOPT_READDATA, static_cast<void*>(this));
            setOption(CURLOPT_XFERINFOFUNCTION, &progressCallback);
            setOption(CURLOPT_XFERINFODATA, static_cast<void*>(this));
            setOption(CURLOPT_NOPROGRESS, m_cancellation || m_progress.callback || m_debug.callback ? 0L : 1L);
            setOption(CURLOPT_DEBUGFUNCTION, &debugCallback);
            setOption(CURLOPT_DEBUGDATA, static_cast<void*>(this));
        }

        /** @brief Execute the prepared easy handle and snapshot its result. @return Completed response. */
        auto perform() -> Response { return Complete(curl_easy_perform(m_curl->handle)); }

        /** @brief Receive body bytes without allowing C++ exceptions through curl. */
        static auto writeCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return 0;
            }
            auto const length{ size * count };
            try {
                std::string_view const bytes{ data, length };
                if (self.m_downloading) {
                    if (self.m_download_file) {
                        return util::write_file_function(data, size, count, self.m_download_file);
                    }
                    return self.m_download_write(bytes) ? length : 0;
                }
                if (self.m_write.callback) {
                    return self.m_write(bytes) ? length : 0;
                }
                if (self.m_sse.callback) {
                    return self.m_sse_parser.Parse(bytes, [&](ServerSentEvent&& event) { return self.m_sse(std::move(event)); }) ? length : 0;
                }
                self.m_response_string.append(bytes);
                return length;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 0;
            }
        }

        /** @brief Collect headers and notify the observer while containing exceptions. */
        static auto headerCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return 0;
            }
            auto const length{ size * count };
            try {
                std::string_view const bytes{ data, length };
                self.m_header_string.append(bytes);
                return self.m_header_callback(bytes) ? length : 0;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 0;
            }
        }

        /** @brief Fill upload bytes while validating the producer's reported size. */
        static auto readCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return CURL_READFUNC_ABORT;
            }
            try {
                if (!self.m_read.callback) {
                    return 0;
                }
                auto length{ size * count };
                if (!self.m_read(data, length) || length > size * count) {
                    return CURL_READFUNC_ABORT;
                }
                return length;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return CURL_READFUNC_ABORT;
            }
        }

        /** @brief Combine explicit cancellation and progress observer decisions. */
        static auto progressCallback(void* context, curl_off_t total_down, curl_off_t now_down, curl_off_t total_up, curl_off_t now_up) noexcept -> int {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error || (self.m_cancellation && self.m_cancellation->load())) {
                return 1;
            }
            try {
                return self.m_progress(total_down, now_down, total_up, now_up) ? 0 : 1;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 1;
            }
        }

        /** @brief Deliver curl diagnostics while containing observer exceptions. */
        static auto debugCallback(CURL*, curl_infotype type, char* data, std::size_t size, void* context) noexcept -> int {
            auto& self{ *static_cast<Session*>(context) };
            if (!self.m_callback_error) {
                try {
                    self.m_debug(static_cast<DebugCallback::InfoType>(type), { data, size });
                } catch (...) {
                    self.m_callback_error = std::current_exception();
                }
            }
            return 0;
        }
    };

} // namespace mcr
