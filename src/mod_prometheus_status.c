/*
**  mod_prometheus_status.c -- Apache sample prometheus_status module
*/

#include "prom.h"
#include "httpd.h"
#include "http_log.h"
#include "http_core.h"
#include "http_config.h"
#include "http_protocol.h"
#include "mpm_common.h"
#include "ap_config.h"

#define VERSION 0.0.1
#define NAME "mod_prometheus_status"

static int server_limit, thread_limit, threads_per_child, max_servers;

#define SERVER_DISABLED SERVER_NUM_STATUS
#define MOD_STATUS_NUM_STATUS (SERVER_NUM_STATUS+1)
static int status_flags[MOD_STATUS_NUM_STATUS];
#define PROMETHEUS_STATUS_DEFAULT_LOGLEVEL APLOG_DEBUG

typedef struct {
    char  context[256];
    int   enabled;    /* Enable or disable our module */
    char  label[256]; /* Add custom label */
} prometheus_status_config;

struct ap_sb_handle_t
{
  int child_num;
  int thread_num;
};

/* setup prototypes */
const char *prometheus_status_set_enabled(cmd_parms *cmd, void *cfg, const char *arg);
const char *prometheus_status_set_label(cmd_parms *cmd, void *cfg, const char *arg);
static void prometheus_status_register_hooks(apr_pool_t *p);
void *create_dir_conf(apr_pool_t *pool, char *context);
void *merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD);
static int prometheus_status_monitor(request_rec *r);

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
prom_counter_t *server_name_counter = NULL;
prom_counter_t *request_counter = NULL;
prom_gauge_t *server_uptime_gauge = NULL;
prom_gauge_t *server_mpm_generation_gauge = NULL;
prom_gauge_t *server_config_generation_gauge = NULL;
prom_gauge_t *server_cpu_load_gauge = NULL;
prom_gauge_t *workers_gauge = NULL;
prom_gauge_t *workers_scoreboard_gauge = NULL;
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
    if(!r->handler || strcmp(r->handler, "prometheus-metrics")) return(DECLINED);

    ap_log_rerror(APLOG_MARK, PROMETHEUS_STATUS_DEFAULT_LOGLEVEL, 0, r, "[%d] prometheus_status_handler: %s", getpid(), r->handler);
    ap_set_content_type(r, "text/plain");

    if(!r->header_only) {
        // update none-request specific metrics
        prometheus_status_monitor(r);

        const char *buf = prom_collector_registry_bridge(PROM_COLLECTOR_REGISTRY_DEFAULT);
        ap_rputs(buf, r);
    }
    return OK;
}

/* prometheus_status_counter is called on each request to update counter */
static int prometheus_status_counter(request_rec *r) {
    ap_log_rerror(APLOG_MARK, PROMETHEUS_STATUS_DEFAULT_LOGLEVEL, 0, r, "[%d] prometheus_status_counter", getpid());
    char status[4];
    apr_time_t now = apr_time_now();
    apr_time_t duration = now - r->request_time;

    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(r->per_dir_config, &prometheus_status_module);

    snprintf(status, 4, "%d", r->status);
    prom_counter_inc(request_counter, (const char *[]){r->method, status, config->label});

    if(r->connection->sbh) {
        ap_sb_handle_t * sbh = r->connection->sbh;
        ap_log_rerror(APLOG_MARK, PROMETHEUS_STATUS_DEFAULT_LOGLEVEL, 0, r, "[%d] prometheus_status_counter, child: %d", getpid(), sbh->child_num);
    }
    prom_histogram_observe(response_time_histogram, (long)duration/(double)APR_USEC_PER_SEC, (const char *[]){config->label});
    prom_histogram_observe(response_size_histogram, (double)r->bytes_sent, (const char *[]){config->label});
    return OK;
}

/* This hook gets run periodically by a maintenance function inside the MPM. */
static int prometheus_status_monitor(request_rec *r) {
    int busy = 0;
    int ready = 0;
    int i, j, res;
    worker_score *ws_record;
    process_score *ps_record;
    ap_generation_t mpm_generation;
    apr_interval_time_t up_time;
    apr_time_t nowtime;
    ap_loadavg_t cpu;
    char *stat_buffer;

    status_flags[SERVER_DEAD] = 0;
    status_flags[SERVER_READY] = 0;
    status_flags[SERVER_STARTING] = 0;
    status_flags[SERVER_BUSY_READ] = 0;
    status_flags[SERVER_BUSY_WRITE] = 0;
    status_flags[SERVER_BUSY_KEEPALIVE] = 0;
    status_flags[SERVER_BUSY_LOG] = 0;
    status_flags[SERVER_BUSY_DNS] = 0;
    status_flags[SERVER_CLOSING] = 0;
    status_flags[SERVER_GRACEFUL] = 0;
    status_flags[SERVER_IDLE_KILL] = 0;
    status_flags[SERVER_DISABLED] = 0;

    ap_mpm_query(AP_MPMQ_GENERATION, &mpm_generation);

    nowtime = apr_time_now();
    up_time = (apr_uint32_t) apr_time_sec(nowtime - ap_scoreboard_image->global->restart_time);
    prom_gauge_set(server_uptime_gauge, up_time, NULL);

    prom_gauge_set(server_mpm_generation_gauge, mpm_generation, NULL);
    prom_gauge_set(server_config_generation_gauge, ap_state_query(AP_SQ_CONFIG_GEN), NULL);

    ap_get_loadavg(&cpu);
    prom_gauge_set(server_cpu_load_gauge, cpu.loadavg, NULL);

    for(i = 0; i < server_limit; ++i) {
        ps_record = ap_get_scoreboard_process(i);
        for(j = 0; j < thread_limit; ++j) {
            int indx = (i * thread_limit) + j;

            ws_record = ap_get_scoreboard_worker_from_indexes(i, j);
            res = ws_record->status;

            if ((i >= max_servers || j >= threads_per_child)
                && (res == SERVER_DEAD))
                status_flags[SERVER_DISABLED]++;
            else
                status_flags[res]++;

            if(!ps_record->quiescing && ps_record->pid) {
                if(res == SERVER_READY) {
                    if (ps_record->generation == mpm_generation)
                        ready++;
                }
                else if (res != SERVER_DEAD &&
                         res != SERVER_STARTING &&
                         res != SERVER_IDLE_KILL) {
                    busy++;
                }
            }
        }
    }

    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_DEAD], (const char *[]){"open_slot"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_READY], (const char *[]){"idle"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_STARTING], (const char *[]){"startup"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_BUSY_READ], (const char *[]){"read"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_BUSY_WRITE], (const char *[]){"reply"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_BUSY_KEEPALIVE], (const char *[]){"keepalive"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_BUSY_LOG], (const char *[]){"logging"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_CLOSING], (const char *[]){"closing"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_GRACEFUL], (const char *[]){"graceful_stop"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_IDLE_KILL], (const char *[]){"idle_cleanup"});
    prom_gauge_set(workers_scoreboard_gauge, status_flags[SERVER_DISABLED], (const char *[]){"disabled"});

    prom_gauge_set(workers_gauge, ready, (const char *[]){"ready"});
    prom_gauge_set(workers_gauge, busy, (const char *[]){"busy"});

    return OK;
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    ap_log_error(APLOG_MARK, PROMETHEUS_STATUS_DEFAULT_LOGLEVEL, 0, s, "[%d] prometheus_status_init", getpid());
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
    ap_mpm_query(AP_MPMQ_MAX_DAEMONS, &max_servers);
    ap_mpm_query(AP_MPMQ_MAX_THREADS, &threads_per_child);
    /* work around buggy MPMs */
    if (threads_per_child == 0)
        threads_per_child = 1;

    prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
    prom_collector_registry_default_init();

    // initialize server_info counter
    prom_counter_destroy(server_info_counter);
    server_info_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_server_info", "information about the apache version", 1, (const char *[]) { "server_description" }));
    prom_counter_add(server_info_counter, 1, (const char *[]){ap_get_server_description()});

    // initialize server_name counter
    prom_counter_destroy(server_name_counter);
    server_name_counter = prom_collector_registry_must_register_metric(prom_counter_new("apache_server_name", "contains the server name", 1, (const char *[]) { "server_name" }));
    prom_counter_add(server_name_counter, 1, (const char *[]){s->server_hostname});

    // initialize server_uptime counter
    prom_gauge_destroy(server_uptime_gauge);
    server_uptime_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_server_uptime_seconds", "server uptime in seconds", 0, NULL));
    prom_gauge_set(server_uptime_gauge, 0, NULL);

    // initialize server_config_generation_gauge
    prom_gauge_destroy(server_config_generation_gauge);
    server_config_generation_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_server_config_generation", "current config generation", 0, NULL));
    prom_gauge_set(server_config_generation_gauge, 0, NULL);

    // initialize server_mpm_generation_gauge
    prom_gauge_destroy(server_mpm_generation_gauge);
    server_mpm_generation_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_server_mpm_generation", "current mpm generation", 0, NULL));
    prom_gauge_set(server_mpm_generation_gauge, 0, NULL);

    // initialize server_cpu_load_gauge
    prom_gauge_destroy(server_cpu_load_gauge);
    server_cpu_load_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_cpu_load", "CPU Load 1", 0, NULL));
    prom_gauge_set(server_cpu_load_gauge, 0, NULL);

    prom_counter_destroy(workers_gauge);
    workers_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_workers", "is the total number of apache workers", 1, (const char *[]) { "state" }));
    prom_gauge_set(workers_gauge, 0, (const char *[]){"ready"});
    prom_gauge_set(workers_gauge, 0, (const char *[]){"busy"});

    prom_counter_destroy(workers_scoreboard_gauge);
    workers_scoreboard_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("apache_workers_scoreboard", "is the total number of apache workers", 1, (const char *[]) { "state" }));

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