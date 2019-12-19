/*
**  mod_prometheus_status.c -- Apache sample prometheus_status module
*/

#include "prom.h"
#include "httpd.h"
#include "mpm_common.h"
#include "http_log.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"

#define VERSION 0.0.1
#define NAME "mod_prometheus_status"

typedef struct {
    char  context[256];
    int   enabled;    /* Enable or disable our module */
    char  label[256]; /* Add custom label */
} prometheus_status_config;

/* setup prototypes */
const char *prometheus_status_set_enabled(cmd_parms *cmd, void *cfg, const char *arg);
const char *prometheus_status_set_label(cmd_parms *cmd, void *cfg, const char *arg);
static void prometheus_status_register_hooks(apr_pool_t *p);
void *create_dir_conf(apr_pool_t *pool, char *context);
void *merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD);

/* available configuration directives */
static const command_rec prometheus_status_directives[] = {
    AP_INIT_TAKE1("PrometheusStatusEnabled", prometheus_status_set_enabled, NULL, RSRC_CONF, "Enable or disable mod_prometheus_status"),
    AP_INIT_TAKE1("PrometheusStatusLabel", prometheus_status_set_label, NULL, OR_ALL, "Set a custom label from within apache directives"),
    { NULL }
};

/* register mod_prometheus_status within the apache */
module AP_MODULE_DECLARE_DATA prometheus_status_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_conf,       /* create per-dir    config structures */
    merge_dir_conf,        /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    prometheus_status_directives,     /* table of config file commands       */
    prometheus_status_register_hooks  /* register hooks                      */
};

/* list of global counter */
prom_counter_t *server_info_counter = NULL;
prom_counter_t *request_counter = NULL;
prom_histogram_t *response_time_histogram = NULL;
prom_histogram_t *response_size_histogram = NULL;

/* Handler for the "PrometheusStatusEnabled" directive */
const char *prometheus_status_set_enabled(cmd_parms *cmd, void *cfg, const char *arg) {
    prometheus_status_config *conf = (prometheus_status_config *) cfg;
    if(conf) {
        if(!strcasecmp(arg, "on")) conf->enabled = 1;
        else conf->enabled = 0;
    }
    return NULL;
}

/* Handler for the "PrometheusStatusLabel" directive */
const char *prometheus_status_set_label(cmd_parms *cmd, void *cfg, const char *arg) {
    prometheus_status_config *conf = (prometheus_status_config *) cfg;
    if(conf) {
        strcpy(conf->label, arg);
    }
    return NULL;
}

/* prometheus_status_handler responds to /metrics requests */
static int prometheus_status_handler(request_rec *r) {
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "prometheus_status_handler: %s", r->handler);
    if(!r->handler || strcmp(r->handler, "prometheus-metrics")) return(DECLINED);

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

    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(r->per_dir_config, &prometheus_status_module);

    snprintf(status, 4, "%d", r->status);
    prom_counter_inc(request_counter, (const char *[]){r->method, status, config->label});

    prom_histogram_observe(response_time_histogram, (long)duration/(double)APR_USEC_PER_SEC, (const char *[]){config->label});
    prom_histogram_observe(response_size_histogram, (double)r->bytes_sent, (const char *[]){config->label});
    return OK;
}

/* This hook gets run periodically by a maintenance function inside the MPM. */
static int prometheus_status_monitor(apr_pool_t *p, server_rec *s)
{
    return OK;
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
    prom_collector_registry_default_init();

    // initialize server_info counter
    prom_counter_destroy(server_info_counter);
    server_info_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_server_info", "information about the apache version", 1, (const char *[]) { "server_description" }));
    prom_counter_add(server_info_counter, 0, (const char *[]){ap_get_server_description()});

    // initialize request counter with known standard http methods
    prom_counter_destroy(request_counter);
    request_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_requests_total", "is the total number of http requests", 3, (const char *[]) { "method", "status", "label" }));
    prom_counter_add(request_counter, 0, (const char *[]){"GET", "200", ""});
    prom_counter_add(request_counter, 0, (const char *[]){"POST", "200", ""});
    prom_counter_add(request_counter, 0, (const char *[]){"HEAD", "200", ""});

    prom_histogram_destroy(response_time_histogram);
    response_time_histogram = prom_collector_registry_must_register_metric(
        prom_histogram_new(
          "apache_response_time_seconds",
          "response time histogram",
          prom_histogram_buckets_exponential(0.1, 10, 3),
          1,
          (const char *[]) {"label"}
        )
    );

    prom_histogram_destroy(response_size_histogram);
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
    ap_hook_monitor(prometheus_status_monitor, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Function for creating new configurations for per-directory contexts */
void *create_dir_conf(apr_pool_t *pool, char *context) {
    context = context ? context : "Newly created configuration";

    prometheus_status_config *cfg = apr_pcalloc(pool, sizeof(prometheus_status_config));

    if(cfg) {
        // Set some default values
        strcpy(cfg->context, context);
        cfg->enabled = 0;
        memset(cfg->label, 0, 256);
    }

    return cfg;
}

/* Merging function for configurations */
void *merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD) {
    prometheus_status_config *base = (prometheus_status_config *) BASE;
    prometheus_status_config *add = (prometheus_status_config *) ADD;
    prometheus_status_config *conf = (prometheus_status_config *) create_dir_conf(pool, "Merged configuration");

    conf->enabled = (add->enabled == 0) ? base->enabled : add->enabled;
    strcpy(conf->label, strlen(add->label) ? add->label : base->label);
    return conf;
}