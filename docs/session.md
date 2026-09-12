# Session 初始实现

参考 `cpr/include/cpr/session.h`、`cpr/session.cpp`、`cpr/response.cpp` 和
`test/session_tests.cpp`，将请求流程接入现有 C++23 模块。

```cpp
import std;
import mcr;

auto main() -> int {
    mcr::Session session;
    session.SetUrl(mcr::Url{ "http://127.0.0.1:8080/echo" });
    session.SetTimeout(mcr::Timeout{ std::chrono::seconds{ 5 } });
    session.SetBody(mcr::Body{ "hello" });
    auto response = session.Post();
    if (response.error) {
        std::println("{}", response.error.message);
        return 1;
    }
    std::println("{}: {}", response.status_code, response.text);
}
```

实现范围：

- `Get`、`Head`、`Post`、`Put`、`Patch`、`Delete`、`Options`，以及对应的
  `Prepare*`、`*Async` 和 `*Callback` 方法。
- 向 `WriteCallback` 或 `std::ofstream` 下载，同步/异步下载，
  `GetDownloadFileLength` 和供外部 curl 驱动使用的 `Complete` / `CompleteDownload`。
- 现有选项模块的 setter 和 `SetOption` 重载：URL、参数、请求头、认证、Cookie、
  Body / BodyView / Payload / Multipart、超时、重定向、压缩、限速、网络绑定、
  DNS 映射、代理地址、连接池、读写/进度/调试/SSE 回调。
- `Response` 的正文、最终响应头、原始响应头、Cookie、错误、URL、计时、
  字节数、连接地址和证书信息。

尚未移植 `ProxyAuthentication`、`VerifySsl` / `SslOptions`、`Interceptor` 和
`MultiPerform` 的集成接口。HTTPS 使用 libcurl 默认的证书及主机名校验；
高级 curl 配置可通过 `GetCurlHolder()` 访问。

生命周期与错误约定：

- 同一个 Session 的配置与请求必须串行使用。异步请求需要
  `std::make_shared<mcr::Session>()`；任务持有 Session 直到执行结束。
- Body 和 Payload 拥有数据；BodyView 和 Multipart 的 Buffer 借用数据。
  借用的数据、回调捕获对象、下载流和 ConnectionPool 必须覆盖使用它们的生命周期。
- Content 会跨请求保留，调用 `RemoveContent()` 清除。
  ReadCallback 独立保留，可通过 `SetReadCallback({})` 清除。
- `SetCancellationParam()` 使用共享原子标志，设置为 true 会终止传输。
  `AsyncResponse` 本身沿用不可取消的默认 AsyncWrapper 类型。
- curl 配置失败抛出异常；传输失败写入 `Response::error`，HTTP 4xx/5xx 保留为普通响应。
  用户回调的异常在 curl 返回之后重新抛出，异步调用通过 `Get()` 取得异常。
- 与现有 CurlHolder 一致，调用方负责需要显式管理的 curl 全局初始化/清理；
  Session 不执行进程级清理。库中其他 curl 使用者尚未结束时不能清理 curl。

有意区别于 cpr：

- 参数追加到已有 query，且位于 fragment 之前。
- 每次准备请求都清除上一请求的方法和 curl 内容配置，避免复用时残留；
  保留 Multipart 的 GET 仍使用 GET。HEAD 与 Download 忽略存储的 Content，
  之后仍可再次发送它。
- HeaderCallback 同时保留 Response 中的原始及解析后响应头。
- Download 的消费者仅作用于本次下载，后续请求恢复既有 WriteCallback / SSE / 正文缓冲。
  SetWriteCallback 与 SetServerSentEventCallback 相互替换；空回调恢复正文缓冲。
- 显式 Expect 请求头被保留；未提供时关闭 curl 的自动 100-continue。
- Multipart 文本使用显式字节长度，因此保留嵌入的空字符。SSE 解析器在每次请求前重置。
- 不忽略 MIME 文件准备错误；不存在的上传文件在准备阶段抛出异常，避免发送空文件字段。
- Response 在完成时保存所有元数据和证书，不持有活动句柄；后续请求不会改变旧响应。
  无证书或空 Response 的 GetCertInfos 安全返回空容器。
- Response 的默认移动操作沿用成员的异常说明，不强制承诺 `noexcept`。
- Session 销毁前重置 curl 选项，使外部保留的 CurlHolder 不引用已销毁的回调或正文数据。

本地验证：`mcpp build`、`mcpp test`。
`tests/test_session.cpp` 内置跨 Windows / POSIX 的回环 HTTP 服务，使用系统分配的端口，
不依赖 Python、外部网络或固定端口。覆盖连接复用、方法切换、上传下载、请求选项、
重定向、错误恢复、取消、回调异常、SSE、连接池和异步生命周期。
