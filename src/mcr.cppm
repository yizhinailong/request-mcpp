/**
 * @file mcr.cppm
 * @brief Library entry module re-exporting the public request-mcpp interfaces.
 */
export module mcr;

export import mcr.accept_encoding;
export import mcr.error;
export import mcr.redirect;
export import mcr.reserve_size;
export import mcr.resolve;
export import mcr.secure_string;
export import mcr.singleton;
export import mcr.sse;
export import mcr.status_code;
export import mcr.threadpool;
export import mcr.timeout;
export import mcr.types;
export import mcr.unix_socket;
export import mcr.verbose;
