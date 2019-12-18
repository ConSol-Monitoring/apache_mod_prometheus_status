/*
**  mod_prometheus_status.c -- Apache sample prometheus_status module
*/

#include "prom.h"
#include "httpd.h"
#include "http_log.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"

#define VERSION 0.0.1
#define NAME "mod_prometheus_status"

prom_counter_t *request_counter;
prom_histogram_t *response_time_histogram;
prom_histogram_t *response_size_histogram;

/* prometheus_status_handler responds to /metrics requests */
static int prometheus_status_handler(request_rec *r) {
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "prometheus_status_handler: %s", r->handler);
    if(strcmp(r->handler, "prometheus-metrics")) {
        return DECLINED;
    }

    ap_set_content_type(r, "text/plain");

    if(!r->header_only) {
        const char *buf = prom_collector_registry_bridge(PROM_COLLECTOR_REGISTRY_DEFAULT);
        ap_rputs(buf, r);
    }
    return OK;
}

/* prometheus_status_counter is called on each request to update counter */
static int prometheus_status_counter(request_rec *r) {
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "prometheus_status_counter");
    char status[4];
    apr_time_t now = apr_time_now();
    apr_time_t duration = now - r->request_time;

    snprintf(status, 4, "%d", r->status);
    prom_counter_inc(request_counter, (const char *[]){r->method, status});

    prom_histogram_observe(response_time_histogram, (long)duration/(double)APR_USEC_PER_SEC, (const char *[]){""});
    prom_histogram_observe(response_size_histogram, (double)r->bytes_sent, (const char *[]){""});
    return OK;
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    prom_counter_t *server_info_counter;

    prom_collector_registry_default_init();

    // initialize server_info counter
    server_info_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_server_info", "information about the apache version", 1, (const char *[]) { "server_description" }));
    prom_counter_add(server_info_counter, 0, (const char *[]){ap_get_server_description()});

    // initialize request counter with known standard http methods
    request_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_requests_total", "is the total number of http requests", 2, (const char *[]) { "method", "status" }));
    prom_counter_add(request_counter, 0, (const char *[]){"GET", "200"});
    prom_counter_add(request_counter, 0, (const char *[]){"POST", "200"});
    prom_counter_add(request_counter, 0, (const char *[]){"HEAD", "200"});

    response_time_histogram = prom_collector_registry_must_register_metric(
        prom_histogram_new(
          "apache_response_time_seconds",
          "response time histogram",
          prom_histogram_buckets_exponential(0.1, 10, 3),
          1,
          (const char *[]) {"label"}
        )
    );

    response_size_histogram = prom_collector_registry_must_register_metric(
        prom_histogram_new(
          "apache_response_size_bytes",
          "response size histogram",
          prom_histogram_buckets_exponential(1000, 10, 6),
          1,
          (const char *[]) {"label"}
        )
    );

    return OK;
}

/* prometheus_status_register_hooks registers all required hooks */
static void prometheus_status_register_hooks(apr_pool_t *p) {
    ap_hook_handler(prometheus_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(prometheus_status_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(prometheus_status_counter, NULL, NULL, APR_HOOK_MIDDLE);
}

// TODO: handle destroy

/* register mod_prometheus_status within the apache */
module AP_MODULE_DECLARE_DATA prometheus_status_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    prometheus_status_register_hooks  /* register hooks                      */
};
