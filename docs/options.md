# 请求配置选项

`src/options/` 中的公开类型、枚举、函数和常量统一使用 `mcr::options` 命名空间。
TLS 选项标签位于 `mcr::options::ssl`，通过 `mcr::options::Ssl(...)` 组合。

```cpp
import std;
import mcr;

auto response = mcr::Get(
    mcr::Url{ "http://127.0.0.1:8080/hello" },
    mcr::options::Verbose{ true },
    mcr::options::Timeout{ std::chrono::seconds{ 5 } }
);

mcr::Session session;
session.SetSslOptions(mcr::options::Ssl(
    mcr::options::ssl::CaInfo{ "test-root.pem" },
    mcr::options::ssl::TLSv1_2{}
));
```

模块名保持原样，例如 `import mcr.verbose;`、`import mcr.timeout;` 和
`import mcr.ssl_options;`。`import mcr;` 仍导出全部选项。

迁移现有调用时，将 `mcr::Verbose`、`mcr::Timeout` 等选项类型改为
`mcr::options::Verbose`、`mcr::options::Timeout`；将 `mcr::Ssl` 和 `mcr::ssl`
分别改为 `mcr::options::Ssl` 和 `mcr::options::ssl`。旧命名空间不提供兼容别名。
重定向辅助函数和编码名称映射也分别改为 `mcr::options::any` 和
`mcr::options::ACCEPT_ENCODING_METHODS_STRING_MAP`。
`Session`、`Response`、`Url`、`Header`、请求正文及回调等其他类型继续使用 `mcr`。
