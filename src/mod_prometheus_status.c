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
        free(buf);
    }
    return OK;
}

/* prometheus_status_counter is called on each request to update counter */
static int prometheus_status_counter(request_rec *r) {
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "prometheus_status_counter");
    // TODO: write test for vhost
    char status[4];
    snprintf(status, 4, "%d", r->status);
    prom_counter_inc(request_counter, (const char *[]){r->method, status, r->server->addrs->virthost});
    return OK;
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    server_addr_rec *vhost;
    prom_collector_registry_default_init();

    /* TODO:
         metrics:
           - server_info
           - number vhosts
    */

    // initialize request counter with known standard http methods and all known vhosts
    request_counter = prom_collector_registry_must_register_metric(prom_counter_new("request_counter", "count http requests using the method and virtualhost-name as label", 3, (const char *[]) { "method", "status", "vhost" }));
    vhost = s->addrs;
    while(vhost != NULL) {
        prom_counter_add(request_counter, 0, (const char *[]){"GET", "200", s->addrs->virthost});
        prom_counter_add(request_counter, 0, (const char *[]){"POST", "200", s->addrs->virthost});
        vhost = vhost->next;
    }

    return OK;
}

/* prometheus_status_register_hooks registers all required hooks */
static void prometheus_status_register_hooks(apr_pool_t *p) {
    ap_hook_handler(prometheus_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(prometheus_status_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(prometheus_status_counter, NULL, NULL, APR_HOOK_MIDDLE);
}

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
