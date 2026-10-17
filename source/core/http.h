#pragma once

// Detailed failure text from the most recent libcurl transfer (empty when none). Valid until
// the next download starts; downloads are serialized, so this is safe to read from the
// error dialog.
const char* http_curl_detail_message(void);

// Set the TCP socket buffer sizes libcurl requests for its connections, in bytes; 0 leaves
// that socket option untouched (system default). Applies to connections opened afterwards.
void http_curl_set_socket_buffers(int rcvbuf, int sndbuf);

Result http_download_callback(const char* url, u32 bufferSize, void* userData, Result (*callback)(void* userData, void* buffer, size_t size),
                                                                               Result (*checkRunning)(void* userData),
                                                                               Result (*progress)(void* userData, u64 total, u64 curr));
Result http_download_buffer(const char* url, u32* downloadedSize, void* buf, size_t size);
Result http_download_json(const char* url, json_t** json, size_t maxSize);
Result http_download_seed(u64 titleId);

// The two transports behind http_download_callback, exposed individually for side-by-side
// profiling (see netload-test/). Each allocates its own buffer; bytes delivered to the
// callback before a failure are reported through bytesDelivered (curl) and are 0 on early
// failure, which is what gates the fallback inside http_download_callback.
Result http_download_transport_curl(const char* url, u32 bufferSize, void* userData, Result (*callback)(void* userData, void* buffer, size_t size),
                                    Result (*checkRunning)(void* userData), Result (*progress)(void* userData, u64 total, u64 curr), u64* bytesDelivered);
Result http_download_transport_httpc(const char* url, u32 bufferSize, void* userData, Result (*callback)(void* userData, void* buffer, size_t size),
                                     Result (*checkRunning)(void* userData), Result (*progress)(void* userData, u64 total, u64 curr));