/**
 * @file api.cppm
 * @brief One-shot HTTP requests, asynchronous continuations and tuple-based batches.
 */
export module mcr.api;
export import mcr.session;
export import mcr.filesystem;
import std;

namespace mcr::detail {
    /**
     * @brief Unwrap an explicitly borrowed option.
     * @tparam T Option type.
     * @param value Borrowed option.
     * @return Referenced option.
     */
    template <typename T>
    auto unwrap_option(std::reference_wrapper<T> value) -> T& {
        return value.get();
    }

    /**
     * @brief Preserve an ordinary option's value category.
     * @tparam T Option type.
     * @param value Option to forward.
     * @return Forwarded option.
     */
    template <typename T>
    auto unwrap_option(T&& value) -> T&& {
        return std::forward<T>(value);
    }

    /**
     * @brief Apply options in order, merging all Header arguments.
     * @tparam Ts Option types.
     * @param session Destination session.
     * @param options Options to apply.
     */
    template <typename... Ts>
    auto set_options(Session& session, Ts&&... options) -> void {
        bool has_header{ false };
        auto set = [&](auto&& argument) {
            auto&& option{ unwrap_option(std::forward<decltype(argument)>(argument)) };
            if constexpr (std::same_as<std::remove_cvref_t<decltype(option)>, Header>) {
                if (has_header) {
                    session.UpdateHeader(option);
                } else {
                    session.SetHeader(option);
                    has_header = true;
                }
            } else {
                session.SetOption(std::forward<decltype(option)>(option));
            }
        };
        (set(std::forward<Ts>(options)), ...);
    }

    /**
     * @brief Expand one tuple of request options.
     * @tparam Tuple Tuple type.
     * @param session Destination session.
     * @param options Tuple to expand.
     */
    template <typename Tuple>
    auto apply_options(Session& session, Tuple&& options) -> void {
        std::apply([&](auto&&... values) { set_options(session, std::forward<decltype(values)>(values)...); }, std::forward<Tuple>(options));
    }

    /**
     * @brief Configure and execute a temporary session.
     * @tparam Action Session method.
     * @tparam Ts Option types.
     * @param options Options to apply.
     * @return Independent response.
     */
    template <auto Action, typename... Ts>
    auto request(Ts&&... options) -> Response {
        Session session;
        set_options(session, std::forward<Ts>(options)...);
        return std::invoke(Action, session);
    }

    /**
     * @brief Submit owned request options to the pool.
     * @tparam Action Session method.
     * @tparam Ts Option types.
     * @param options Options to own.
     * @return Asynchronous response.
     */
    template <auto Action, typename... Ts>
    auto request_async(Ts&&... options) -> AsyncResponse {
        return mcr::async([](auto... values) { return request<Action>(std::move(values)...); }, std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit a request followed by a continuation.
     * @tparam Action Session method.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Continuation to own.
     * @param options Options to own.
     * @return Future preserving the continuation's result type.
     */
    template <auto Action, typename Then, typename... Ts>
    auto request_callback(Then&& then, Ts&&... options) {
        return mcr::async<true>([](auto handler, auto... values) -> decltype(auto) {
            return std::invoke(handler, request<Action>(std::move(values)...));
        },
                                std::forward<Then>(then),
                                std::forward<Ts>(options)...);
    }

    /**
     * @brief Register one configured session.
     * @tparam Tuple Option tuple type.
     * @param multi Destination batch.
     * @param options One request's options.
     */
    template <typename Tuple>
    auto add_request(MultiPerform& multi, Tuple&& options) -> void {
        auto session{ std::make_shared<Session>() };
        apply_options(*session, std::forward<Tuple>(options));
        multi.AddSession(session);
    }

    /**
     * @brief Perform tuple-based requests concurrently.
     * @tparam Action Batch method.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request.
     * @return Responses in argument order.
     */
    template <auto Action, typename... Tuples>
    auto multi_request(Tuples&&... options) -> std::vector<Response> {
        MultiPerform multi;
        (add_request(multi, std::forward<Tuples>(options)), ...);
        return std::invoke(Action, multi);
    }

    /**
     * @brief Submit one cooperatively cancellable request.
     * @tparam Action Session method.
     * @tparam Tuple Option tuple type.
     * @param options Options copied or moved into the task.
     * @return Response future sharing the task's cancellation flag.
     */
    template <auto Action, typename Tuple>
    auto cancellable_request(Tuple&& options) -> utils::AsyncWrapper<Response, true> {
        auto* pool{ GlobalThreadPool::GetInstance() };
        if (!pool) {
            throw std::logic_error{ "mcr::MultiAsync: global thread pool has been cleaned up." };
        }
        auto cancelled{ std::make_shared<std::atomic_bool>(false) };
        auto future{ pool->Submit([cancelled, values = std::forward<Tuple>(options)]() mutable {
            if (cancelled->load()) {
                return Response{};
            }
            Session session;
            session.SetCancellationParam(cancelled);
            apply_options(session, std::move(values));
            return std::invoke(Action, session);
        }) };
        return utils::AsyncWrapper<Response, true>{ std::move(future), std::move(cancelled) };
    }

    /**
     * @brief Submit independently cancellable requests in argument order.
     * @tparam Action Session method.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per task.
     * @return One future per request.
     */
    template <auto Action, typename... Tuples>
    auto multi_async(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        std::vector<utils::AsyncWrapper<Response, true>> responses;
        responses.reserve(sizeof...(Tuples));
        (responses.push_back(cancellable_request<Action>(std::forward<Tuples>(options))), ...);
        return responses;
    }
} // namespace mcr::detail

export namespace mcr {

    /**
     * @brief Perform one GET request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Get(Ts&&... options) -> Response {
        return detail::request<&Session::Get>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one GET request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto GetAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Get>(std::move(options)...);
    }

    /**
     * @brief Submit GET followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto GetCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Get>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent GET requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiGet(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Get>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable GET requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiGetAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Get>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one POST request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Post(Ts&&... options) -> Response {
        return detail::request<&Session::Post>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one POST request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PostAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Post>(std::move(options)...);
    }

    /**
     * @brief Submit POST followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PostCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Post>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent POST requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPost(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Post>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable POST requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPostAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Post>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one PUT request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Put(Ts&&... options) -> Response {
        return detail::request<&Session::Put>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one PUT request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PutAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Put>(std::move(options)...);
    }

    /**
     * @brief Submit PUT followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PutCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Put>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent PUT requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPut(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Put>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable PUT requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPutAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Put>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one HEAD request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Head(Ts&&... options) -> Response {
        return detail::request<&Session::Head>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one HEAD request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto HeadAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Head>(std::move(options)...);
    }

    /**
     * @brief Submit HEAD followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto HeadCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Head>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent HEAD requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiHead(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Head>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable HEAD requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiHeadAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Head>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one DELETE request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Delete(Ts&&... options) -> Response {
        return detail::request<&Session::Delete>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one DELETE request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto DeleteAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Delete>(std::move(options)...);
    }

    /**
     * @brief Submit DELETE followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto DeleteCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Delete>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent DELETE requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiDelete(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Delete>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable DELETE requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiDeleteAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Delete>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one OPTIONS request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Options(Ts&&... options) -> Response {
        return detail::request<&Session::Options>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one OPTIONS request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto OptionsAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Options>(std::move(options)...);
    }

    /**
     * @brief Submit OPTIONS followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto OptionsCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Options>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent OPTIONS requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiOptions(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Options>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable OPTIONS requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiOptionsAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Options>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one PATCH request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Patch(Ts&&... options) -> Response {
        return detail::request<&Session::Patch>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one PATCH request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PatchAsync(Ts... options) -> AsyncResponse {
        return detail::request_async<&Session::Patch>(std::move(options)...);
    }

    /**
     * @brief Submit PATCH followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving the response.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PatchCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Patch>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent PATCH requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPatch(Tuples&&... options) -> std::vector<Response> {
        return detail::multi_request<&MultiPerform::Patch>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable PATCH requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPatchAsync(Tuples&&... options) -> std::vector<utils::AsyncWrapper<Response, true>> {
        return detail::multi_async<&Session::Patch>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Download into a borrowed output stream.
     * @tparam Ts Option types.
     * @param file Open stream, normally in binary mode.
     * @param options Request options.
     * @return Metadata with an empty response body.
     * @throws std::runtime_error If the stream is not writable before the request.
     */
    template <typename... Ts>
    auto Download(std::ofstream& file, Ts&&... options) -> Response {
        if (!file.is_open() || !file.good()) {
            throw std::runtime_error{ "mcr::Download: output stream is not writable." };
        }
        Session session;
        detail::set_options(session, std::forward<Ts>(options)...);
        return session.Download(file);
    }

    /**
     * @brief Download into a copied callback.
     * @tparam Ts Option types.
     * @param write Body consumer.
     * @param options Request options.
     * @return Metadata with an empty response body.
     */
    template <typename... Ts>
    auto Download(WriteCallback const& write, Ts&&... options) -> Response {
        Session session;
        detail::set_options(session, std::forward<Ts>(options)...);
        return session.Download(write);
    }

    /**
     * @brief Download asynchronously to a file opened in binary truncation mode.
     * @tparam Ts Option types.
     * @param local_path Destination owned by the task.
     * @param options Request options owned by the task.
     * @return Future; opening or closing failures surface through Get().
     * @note Failed transfers may leave a partial file.
     */
    template <typename... Ts>
    auto DownloadAsync(utils::fs::path local_path, Ts... options) -> AsyncResponse {
        return mcr::async([](utils::fs::path path, auto... values) {
            std::ofstream file{ path, std::ios::binary | std::ios::trunc };
            auto          response{ Download(file, std::move(values)...) };
            file.close();
            if (file.fail()) {
                throw std::runtime_error{ "mcr::DownloadAsync: could not finish writing the output file." };
            }
            return response;
        },
                          std::move(local_path),
                          std::move(options)...);
    }
} // namespace mcr
