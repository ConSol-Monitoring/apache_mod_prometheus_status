/*
**  mod_prometheus_status_format.c -- Label format as taken from mod_log_config.c
*/

#include "mod_prometheus_status.h"

apr_hash_t *log_hash;

typedef struct {
    ap_log_handler_fn_t *func;
    char *arg;
    int condition_sense;
    int want_orig;
    apr_array_header_t *conditions;
} log_format_item;


static char *pfmt(apr_pool_t *p, int i)
{
    if (i <= 0) {
        return "-";
    }
    else {
        return apr_itoa(p, i);
    }
}

static const char *constant_item(request_rec *dummy, char *stuff)
{
    return stuff;
}

static const char *log_remote_host(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, ap_get_remote_host(r->connection,
                                                         r->per_dir_config,
                                                         REMOTE_NAME, NULL));
}

static const char *log_remote_address(request_rec *r, char *a)
{
    if (a && !strcmp(a, "c")) {
        return r->connection->client_ip;
    }
    else {
        return r->useragent_ip;
    }
}

static const char *log_local_address(request_rec *r, char *a)
{
    return r->connection->local_ip;
}

static const char *log_remote_user(request_rec *r, char *a)
{
    char *rvalue = r->user;

    if (rvalue == NULL) {
        rvalue = "-";
    }
    else if (strlen(rvalue) == 0) {
        rvalue = "\"\"";
    }
    else {
        rvalue = ap_escape_logitem(r->pool, rvalue);
    }

    return rvalue;
}

static const char *log_request_uri(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, r->uri);
}

static const char *log_request_method(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, r->method);
}

static const char *log_request_protocol(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, r->protocol);
}

static const char *log_status(request_rec *r, char *a)
{
    return pfmt(r->pool, r->status);
}

static const char *log_handler(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, r->handler);
}

static const char *log_header_in(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, apr_table_get(r->headers_in, a));
}

static APR_INLINE char *find_multiple_headers(apr_pool_t *pool,
                                              const apr_table_t *table,
                                              const char *key)
{
    const apr_array_header_t *elts;
    const apr_table_entry_t *t_elt;
    const apr_table_entry_t *t_end;
    apr_size_t len;
    struct sle {
        struct sle *next;
        const char *value;
        apr_size_t len;
    } *result_list, *rp;

    elts = apr_table_elts(table);

    if (!elts->nelts) {
        return NULL;
    }

    t_elt = (const apr_table_entry_t *)elts->elts;
    t_end = t_elt + elts->nelts;
    len = 1; /* \0 */
    result_list = rp = NULL;

    do {
        if (!strcasecmp(t_elt->key, key)) {
            if (!result_list) {
                result_list = rp = apr_palloc(pool, sizeof(*rp));
            }
            else {
                rp = rp->next = apr_palloc(pool, sizeof(*rp));
                len += 2; /* ", " */
            }

            rp->next = NULL;
            rp->value = t_elt->val;
            rp->len = strlen(rp->value);

            len += rp->len;
        }
        ++t_elt;
    } while (t_elt < t_end);

    if (result_list) {
        char *result = apr_palloc(pool, len);
        char *cp = result;

        rp = result_list;
        while (rp) {
            if (rp != result_list) {
                *cp++ = ',';
                *cp++ = ' ';
            }
            memcpy(cp, rp->value, rp->len);
            cp += rp->len;
            rp = rp->next;
        }
        *cp = '\0';

        return result;
    }

    return NULL;
}

static const char *log_header_out(request_rec *r, char *a)
{
    const char *cp = NULL;

    if (!strcasecmp(a, "Content-type") && r->content_type) {
        cp = ap_field_noparam(r->pool, r->content_type);
    }
    else if (!strcasecmp(a, "Set-Cookie")) {
        cp = find_multiple_headers(r->pool, r->headers_out, a);
    }
    else {
        cp = apr_table_get(r->headers_out, a);
    }

    return ap_escape_logitem(r->pool, cp);
}


static const char *log_env_var(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, apr_table_get(r->subprocess_env, a));
}

static const char *log_cookie(request_rec *r, char *a)
{
    const char *cookies_entry;

    /*
     * This supports Netscape version 0 cookies while being tolerant to
     * some properties of RFC2109/2965 version 1 cookies:
     * - case-insensitive match of cookie names
     * - white space between the tokens
     * It does not support the following version 1 features:
     * - quoted strings as cookie values
     * - commas to separate cookies
     */

    if ((cookies_entry = apr_table_get(r->headers_in, "Cookie"))) {
        char *cookie, *last1, *last2;
        char *cookies = apr_pstrdup(r->pool, cookies_entry);

        while ((cookie = apr_strtok(cookies, ";", &last1))) {
            char *name = apr_strtok(cookie, "=", &last2);
            if (name) {
                char *value = name + strlen(name) + 1;
                apr_collapse_spaces(name, name);

                if (!strcasecmp(name, a)) {
                    char *last;
                    value += strspn(value, " \t");  /* Move past leading WS */
                    last = value + strlen(value) - 1;
                    while (last >= value && apr_isspace(*last)) {
                       *last = '\0';
                       --last;
                    }

                    return ap_escape_logitem(r->pool, value);
                }
            }
            cookies = NULL;
        }
    }
    return NULL;
}


/* These next two routines use the canonical name:port so that log
 * parsers don't need to duplicate all the vhost parsing crud.
 */
static const char *log_virtual_host(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, r->server->server_hostname);
}

static const char *log_server_port(request_rec *r, char *a)
{
    apr_port_t port;

    if (*a == '\0' || !strcasecmp(a, "canonical")) {
        port = r->server->port ? r->server->port : ap_default_port(r);
    }
    else if (!strcasecmp(a, "remote")) {
        port = r->useragent_addr->port;
    }
    else if (!strcasecmp(a, "local")) {
        port = r->connection->local_addr->port;
    }
    else {
        /* bogus format */
        return a;
    }
    return apr_itoa(r->pool, (int)port);
}

/* This respects the setting of UseCanonicalName so that
 * the dynamic mass virtual hosting trick works better.
 */
static const char *log_server_name(request_rec *r, char *a)
{
    return ap_escape_logitem(r->pool, ap_get_server_name(r));
}

static const char *log_connection_status(request_rec *r, char *a)
{
    if (r->connection->aborted)
        return "X";

    if (r->connection->keepalive == AP_CONN_KEEPALIVE &&
        (!r->server->keep_alive_max ||
         (r->server->keep_alive_max - r->connection->keepalives) > 0)) {
        return "+";
    }
    return "-";
}

static void prometheus_status_register_log_handler(apr_pool_t *p, char *tag, ap_log_handler_fn_t *handler, int def)
{
    ap_log_handler *log_struct = apr_palloc(p, sizeof(*log_struct));
    log_struct->func = handler;
    log_struct->want_orig_default = def;

    apr_hash_set(log_hash, tag, 1, (const void *)log_struct);
}

static char *parse_log_misc_string(apr_pool_t *p, log_format_item *it,
                                   const char **sa)
{
    const char *s;
    char *d;

    it->func = constant_item;
    it->conditions = NULL;

    s = *sa;
    while (*s && *s != '%') {
        s++;
    }
    /*
     * This might allocate a few chars extra if there's a backslash
     * escape in the format string.
     */
    it->arg = apr_palloc(p, s - *sa + 1);

    d = it->arg;
    s = *sa;
    while (*s && *s != '%') {
        if (*s != '\\') {
            *d++ = *s++;
        }
        else {
            s++;
            switch (*s) {
            case '\\':
                *d++ = '\\';
                s++;
                break;
            case 'r':
                *d++ = '\r';
                s++;
                break;
            case 'n':
                *d++ = '\n';
                s++;
                break;
            case 't':
                *d++ = '\t';
                s++;
                break;
            default:
                /* copy verbatim */
                *d++ = '\\';
                /*
                 * Allow the loop to deal with this *s in the normal
                 * fashion so that it handles end of string etc.
                 * properly.
                 */
                break;
            }
        }
    }
    *d = '\0';

    *sa = s;
    return NULL;
}

static char *parse_log_item(apr_pool_t *p, log_format_item *it, const char **sa)
{
    const char *s = *sa;
    ap_log_handler *handler;

    if (*s != '%') {
        return parse_log_misc_string(p, it, sa);
    }

    ++s;
    it->condition_sense = 0;
    it->conditions = NULL;

    if (*s == '%') {
        it->arg = "%";
        it->func = constant_item;
        *sa = ++s;

        return NULL;
    }

    it->want_orig = -1;
    it->arg = "";               /* For safety's sake... */

    while (*s) {
        int i;

        switch (*s) {
        case '!':
            ++s;
            it->condition_sense = !it->condition_sense;
            break;

        case '<':
            ++s;
            it->want_orig = 1;
            break;

        case '>':
            ++s;
            it->want_orig = 0;
            break;

        case ',':
            ++s;
            break;

        case '{':
            ++s;
            it->arg = ap_getword(p, &s, '}');
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            i = *s - '0';
            while (apr_isdigit(*++s)) {
                i = i * 10 + (*s) - '0';
            }
            if (!it->conditions) {
                it->conditions = apr_array_make(p, 4, sizeof(int));
            }
            *(int *) apr_array_push(it->conditions) = i;
            break;

        default:
            handler = (ap_log_handler *)apr_hash_get(log_hash, s++, 1);
            if (!handler) {
                char dummy[2];

                dummy[0] = s[-1];
                dummy[1] = '\0';
                return apr_pstrcat(p, "Unrecognized LabelFormat directive %", dummy, NULL);
            }
            it->func = handler->func;
            if (it->want_orig == -1) {
                it->want_orig = handler->want_orig_default;
            }
            *sa = s;
            return NULL;
        }
    }

    return "Ran off end of LabelFormat parsing args to some directive";
}

apr_array_header_t *parse_log_string(apr_pool_t *p, const char *s, const char **err)
{
    apr_array_header_t *a = apr_array_make(p, 30, sizeof(log_format_item));
    char *res;

    while (*s) {
        if ((res = parse_log_item(p, (log_format_item *) apr_array_push(a), &s))) {
            *err = res;
            return NULL;
        }
    }

    return a;
}

static const char *process_item(request_rec *r, request_rec *orig,
                          log_format_item *item)
{
    const char *cp;

    /* First, see if we need to process this thing at all... */

    if (item->conditions && item->conditions->nelts != 0) {
        int i;
        int *conds = (int *) item->conditions->elts;
        int in_list = 0;

        for (i = 0; i < item->conditions->nelts; ++i) {
            if (r->status == conds[i]) {
                in_list = 1;
                break;
            }
        }

        if ((item->condition_sense && in_list)
            || (!item->condition_sense && !in_list)) {
            return "-";
        }
    }

    /* We do.  Do it... */

    cp = (*item->func) (item->want_orig ? orig : r, item->arg);
    return cp ? cp : "-";
}

int prometheus_status_register_all_log_handler(apr_pool_t *p)
{
    prometheus_status_register_log_handler(p, "h", log_remote_host, 0);
    prometheus_status_register_log_handler(p, "a", log_remote_address, 0 );
    prometheus_status_register_log_handler(p, "A", log_local_address, 0 );
    prometheus_status_register_log_handler(p, "u", log_remote_user, 0);
    prometheus_status_register_log_handler(p, "i", log_header_in, 0);
    prometheus_status_register_log_handler(p, "o", log_header_out, 0);
    prometheus_status_register_log_handler(p, "e", log_env_var, 0);
    prometheus_status_register_log_handler(p, "V", log_server_name, 0);
    prometheus_status_register_log_handler(p, "v", log_virtual_host, 0);
    prometheus_status_register_log_handler(p, "p", log_server_port, 0);
    prometheus_status_register_log_handler(p, "H", log_request_protocol, 0);
    prometheus_status_register_log_handler(p, "m", log_request_method, 0);
    prometheus_status_register_log_handler(p, "X", log_connection_status, 0);
    prometheus_status_register_log_handler(p, "C", log_cookie, 0);
    prometheus_status_register_log_handler(p, "U", log_request_uri, 1);
    prometheus_status_register_log_handler(p, "s", log_status, 1);
    prometheus_status_register_log_handler(p, "R", log_handler, 1);

    return OK;
}

void prometheus_status_expand_variables(apr_array_header_t *format, request_rec *r, const char**output) {
    log_format_item *items;
    request_rec *orig;
    int i;

    items = (log_format_item *) format->elts;

    orig = r;
    while (orig->prev) {
        orig = orig->prev;
    }
    while (r->next) {
        r = r->next;
    }

    for (i = 0; i < format->nelts; ++i) {
        const char *str = process_item(r, orig, &items[i]);
        if(*output == NULL) {
            *output = str;
        } else {
            *output = apr_psprintf(r->pool, "%s%s", *output, str);
        }
    }

    return;
}