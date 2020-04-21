#define VERSION "0.0.2"
#define NAME "mod_prometheus_status"

#include "ap_config.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "httpd.h"
#include "http_core.h"
#include "http_log.h"
#include "http_config.h"
#include "http_protocol.h"
#include "mpm_common.h"
#include "mod_log_config.h"
#include <unistd.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>

module AP_MODULE_DECLARE_DATA prometheus_status_module;

/* global logger */
#define log(_fmt, ...) if(main_server != NULL) {\
    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, main_server, \
    "[%s][%s:%d] "_fmt, NAME, __FILE__, __LINE__, ## __VA_ARGS__); }

apr_array_header_t *parse_log_string(apr_pool_t *p, const char *s, const char **err);
void prometheus_status_expand_variables(apr_array_header_t *format, request_rec *r, const char**output);
int prometheus_status_register_all_log_handler(apr_pool_t *p);

