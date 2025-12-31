#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <microhttpd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_BODY (1 << 20) /* 1 MiB */
#define MAX_QUEUE 1024

static CRITICAL_SECTION g_lock;
static int g_lock_init = 0;

static struct MHD_Daemon *g_daemon = NULL;

struct query_buf {
    char  *buf;
    size_t len;
    size_t cap;
};

/* Per-request state for POST bodies */
struct ReqState
{
    char *body;
    size_t len;
};

struct ReqCtx {
    struct MHD_Connection *connection;
    // optionally: url/method/query/headers/body pointers, etc.
    int replied;  // guard against double replies
};

static enum MHD_Result qb_append(struct query_buf *qb, const char *s)
{
    size_t n = strlen(s);

    if (qb->len + n + 1 > qb->cap)
    {
        size_t newcap = qb->cap ? qb->cap * 2 : 256;
        while (newcap < qb->len + n + 1)
            newcap *= 2;

        char *nb = realloc(qb->buf, newcap);
        if (!nb)
            return MHD_NO;

        qb->buf = nb;
        qb->cap = newcap;
    }

    memcpy(qb->buf + qb->len, s, n);
    qb->len += n;
    qb->buf[qb->len] = '\0';
    return MHD_YES;
}


static enum MHD_Result
iterate_headers(void *cls,
                enum MHD_ValueKind kind,
                const char *key,
                const char *value)
{
    (void)kind;

    struct query_buf *kb = (struct query_buf *)cls;

    if (!key)
        return MHD_YES;

    if (kb->len)
        if (qb_append(kb, "\n") == MHD_NO)
            return MHD_NO;

    if (qb_append(kb, key) == MHD_NO)
        return MHD_NO;

    if (qb_append(kb, ": ") == MHD_NO)
        return MHD_NO;

    if (value)
        if (qb_append(kb, value) == MHD_NO)
            return MHD_NO;

    return MHD_YES;
}

// Iterator function that is called for each key-value pair
enum MHD_Result
iterate_get_arguments(void *cls,
                      enum MHD_ValueKind kind,
                      const char *key,
                      const char *value)
{
    (void)kind;

    struct query_buf *qb = (struct query_buf *)cls;

    if (!key)
        return MHD_YES;

    if (qb->len)
        if (qb_append(qb, "&") == MHD_NO)
            return MHD_NO;

    if (qb_append(qb, key) == MHD_NO)
        return MHD_NO;

    if (qb_append(qb, "=") == MHD_NO)
        return MHD_NO;

    if (value)
        if (qb_append(qb, value) == MHD_NO)
            return MHD_NO;

    return MHD_YES;
}

__declspec(dllexport) int __cdecl mhd_reply_text(struct MHD_Connection *c, unsigned int code, const char *mime_type, const char *text)
{
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(text), (void *)text, MHD_RESPMEM_PERSISTENT);
    if (!r)
        return MHD_NO;
    enum MHD_Result ret = MHD_queue_response(c, code, r);
    MHD_destroy_response(r);
    return ret;
}

__declspec(dllexport) int __cdecl mhd_reply_bytes(struct MHD_Connection *c, unsigned int code, const char *mime_type, const void *buf, size_t len)
{
    struct MHD_Response *r =
        MHD_create_response_from_buffer(len, (void *)buf, MHD_RESPMEM_MUST_COPY);
    if (!r)
        return MHD_NO;
    enum MHD_Result ret = MHD_queue_response(c, code, r);
    MHD_destroy_response(r);
    return ret;
}



// URL, method, body, body_len, output_ph, output_ph_len
typedef int(__cdecl *fb_handler_t)(struct MHD_Connection *,const char *, const char *, const char *, const char *,const char *, int, char *, int);
static fb_handler_t g_fb = NULL;

static enum MHD_Result handler(void *cls,
                               struct MHD_Connection *connection,
                               const char *url,
                               const char *method,
                               const char *version,
                               const char *upload_data, size_t *upload_data_size,
                               void **con_cls)
{
    (void)cls;
    (void)version;

    /* Initialize per-request state once */
    if (*con_cls == NULL)
    {
        struct ReqState *st = (struct ReqState *)calloc(1, sizeof(struct ReqState));
        if (!st)
            return MHD_NO;
        *con_cls = st;
        return MHD_YES;
    }
    struct ReqState *st = (struct ReqState *)(*con_cls);

    struct query_buf qb = {0};
    struct query_buf headers = {0};

    MHD_get_connection_values(connection,
                          MHD_HEADER_KIND,
                          iterate_headers,
                          &headers);


    /* Collect upload_data for POST */
    if (0 == strcmp(method, "POST"))
    {
        if (*upload_data_size != 0)
        {
            size_t add = *upload_data_size;
            if (st->len + add > MAX_BODY)
                return MHD_NO;

            char *nb = (char *)realloc(st->body, st->len + add);
            if (!nb)
                return MHD_NO;
            st->body = nb;
            memcpy(st->body + st->len, upload_data, add);
            st->len += add;

            *upload_data_size = 0;
            return MHD_YES;
        }
        /* upload complete: fall through to respond */
    }
    else //Get request. Get the query string if there is one
    {

        MHD_get_connection_values(connection,
                          MHD_GET_ARGUMENT_KIND,
                          iterate_get_arguments,
                          &qb);

        if (*upload_data_size != 0)
        {
            *upload_data_size = 0;
            return MHD_YES;
        }
    }

    /* Routes */
    if (!g_fb)
    {
        enum MHD_Result ret = mhd_reply_text(connection, 503, "text/plain; charset=utf-8", "{\"error\":\"no handler\"}\n");
        free(st->body);
        free(st);
        free(qb.buf);
        *con_cls = NULL;
        return ret;
    }

    // Call into FreeBASIC
    int cap = (int)(MAX_BODY + 4096); // pick a cap you like
    char *out = (char *)malloc((size_t)cap);
    if (!out)
    {
        free(st->body);
        free(st);
        *con_cls = NULL;
        free(qb.buf);
        return MHD_NO;
    }

    // Call fb handler
    int n = g_fb(connection, url, headers.buf, qb.buf, method, st->body, (int)st->len, out, cap);
    free(qb.buf);
    free(headers.buf);

    if (n < 0)
    {
        free(out);
        enum MHD_Result ret = mhd_reply_text(connection, 500, "text/plain; charset=utf-8", "{\"error\":\"handler failed\"}\n");
        free(st->body);
        free(st);
        *con_cls = NULL;
        return ret;
    }

    // Return response bytes as JSON
    // (reply_bytes uses MUST_COPY, so it copies before we free(out))
//    enum MHD_Result ret = mhd_reply_bytes(connection, 200, "text/plain; charset=utf-8", out, (size_t)n);
    free(out);

    free(st->body);
    free(st);
    *con_cls = NULL;
//    return ret;
    return MHD_HTTP_OK;
}

__declspec(dllexport) void __cdecl mhd_set_handler(fb_handler_t fn)
{
    g_fb = fn;
}

__declspec(dllexport) int __cdecl mhd_start(unsigned short port)
{
    if (g_daemon)
        return 1;
    g_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port,
                                NULL, NULL, &handler, NULL,
                                MHD_OPTION_END);
    return g_daemon ? 1 : 0;
}

__declspec(dllexport) void __cdecl mhd_stop(void)
{
    if (!g_daemon)
        return;
    MHD_stop_daemon(g_daemon);
    g_daemon = NULL;
}

