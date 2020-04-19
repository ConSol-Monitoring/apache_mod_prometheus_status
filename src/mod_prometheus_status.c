/*
**  mod_prometheus_status.c -- Apache sample prometheus_status module
*/

#include "ap_config.h"
#include "httpd.h"
#include "http_core.h"
#include "http_log.h"
#include "http_config.h"
#include "http_protocol.h"
#include "mpm_common.h"
#include <unistd.h>
#include <dlfcn.h>

#include "mod_prometheus_status_go.h"

#define VERSION "0.0.1"
#define NAME "mod_prometheus_status"

typedef struct {
    char  context[256];
    int   enabled;    /* Enable or disable our module */
    char  label[256]; /* Add custom label */
} prometheus_status_config;

/* Server object for main server as supplied to prometheus_status_init(). */
static server_rec *main_server = NULL;

char* (*prometheusStatusInitFn)() = NULL;
char *metric_socket = NULL;
int metric_socket_fd = 0;

#define SERVER_DISABLED SERVER_NUM_STATUS
#define MOD_STATUS_NUM_STATUS (SERVER_NUM_STATUS+1)
static int status_flags[MOD_STATUS_NUM_STATUS];
static int server_limit, thread_limit, threads_per_child, max_servers;

module AP_MODULE_DECLARE_DATA prometheus_status_module;

/* global logger */
#define log(_fmt, ...) if(main_server != NULL) {\
    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, main_server, \
    "[%s][%s:%d] "_fmt, NAME, __FILE__, __LINE__, ## __VA_ARGS__); }

/* Handler for the "PrometheusStatusEnabled" directive */
const char *prometheus_status_set_enabled(cmd_parms *cmd, void *cfg, int val) {
    prometheus_status_config *conf = (prometheus_status_config *) cfg;
    if(conf) {
        conf->enabled = val;
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

/* open the communication socket */
static int prometheus_status_open_communication_socket() {
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if(metric_socket_fd != 0) {
        return(TRUE);
    }
    strcpy(addr.sun_path, metric_socket);
    metric_socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(connect(metric_socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        log("failed to open metrics socket at %s: %s", metric_socket, strerror(errno));
        return(FALSE);
    }

    struct timeval timeout;
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    if(setsockopt(metric_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        log("setsockopt failed: %s", strerror(errno));
        return(FALSE);
    }

    if(setsockopt(metric_socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        log("setsockopt failed: %s", strerror(errno));
        return(FALSE);
    }

    return(TRUE);
}

/* close the communication socket */
static int prometheus_status_close_communication_socket() {
    if(metric_socket_fd == 0) {
        return(TRUE);
    }
    close(metric_socket_fd);
    metric_socket_fd = 0;
    return(TRUE);
}

/* send something over the communication socket */
static int prometheus_status_send_communication_socket(const char *fmt, ...) {
    char buffer[4096];
    int nbytes;
    va_list ap;

    // open socket unless open
    if(metric_socket_fd == 0) {
        if(!prometheus_status_open_communication_socket()) {
            return(FALSE);
        }
    }

    va_start(ap, fmt);
    nbytes = vsnprintf(buffer, 4096, fmt, ap);
    va_end(ap);
    if(write(metric_socket_fd, buffer, nbytes) == -1) {
        log("failed to send to metrics collector at %s: %s", metric_socket, strerror(errno));
    }
    return(TRUE);
}

/* gather non-request runtime metrics */
static int prometheus_status_monitor() {
    int busy = 0;
    int ready = 0;
    int i, j, res;
    worker_score *ws_record;
    process_score *ps_record;
    ap_generation_t mpm_generation;
    apr_interval_time_t uptime;
    apr_time_t nowtime;
    ap_loadavg_t cpu;

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
    uptime = (apr_uint32_t) apr_time_sec(nowtime - ap_scoreboard_image->global->restart_time);
    prometheus_status_send_communication_socket("update:promServerUptime;%ld\n", uptime);

    prometheus_status_send_communication_socket("update:promMPMGeneration;%d\n", mpm_generation);
    prometheus_status_send_communication_socket("update:promConfigGeneration;%d\n", ap_state_query(AP_SQ_CONFIG_GEN));

    ap_get_loadavg(&cpu);
    prometheus_status_send_communication_socket("update:promCPULoad;%f\n", cpu.loadavg);

    for(i = 0; i < server_limit; ++i) {
        ps_record = ap_get_scoreboard_process(i);
        for(j = 0; j < thread_limit; ++j) {
            ws_record = ap_get_scoreboard_worker_from_indexes(i, j);
            res = ws_record->status;

            if ((i >= max_servers || j >= threads_per_child)
                && (res == SERVER_DEAD))
                status_flags[SERVER_DISABLED]++;
            else
                status_flags[res]++;

            if(!ps_record->quiescing && ps_record->pid) {
                if(res == SERVER_READY) {
                    if(ps_record->generation == mpm_generation)
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

    prometheus_status_send_communication_socket("update:promScoreboard;%d;idle\n",          status_flags[SERVER_READY]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;startup\n",       status_flags[SERVER_STARTING]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;read\n",          status_flags[SERVER_BUSY_READ]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;reply\n",         status_flags[SERVER_BUSY_WRITE]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;keepalive\n",     status_flags[SERVER_BUSY_KEEPALIVE]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;logging\n",       status_flags[SERVER_BUSY_LOG]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;closing\n",       status_flags[SERVER_CLOSING]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;graceful_stop\n", status_flags[SERVER_GRACEFUL]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;idle_cleanup\n",  status_flags[SERVER_IDLE_KILL]);
    prometheus_status_send_communication_socket("update:promScoreboard;%d;disabled\n",      status_flags[SERVER_DISABLED]);

    prometheus_status_send_communication_socket("update:promWorkers;%d;ready\n", ready);
    prometheus_status_send_communication_socket("update:promWorkers;%d;busy\n", busy);

    return OK;
}

/* prometheus_status_handler responds to /metrics requests */
static int prometheus_status_handler(request_rec *r) {
    int nbytes;
    char buffer[4096];

    // is the module enabled at all?
    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(r->per_dir_config, &prometheus_status_module);
    if(config->enabled == 0) {
        return(OK);
    }

    if(!r->handler || strcmp(r->handler, "prometheus-metrics")) return(DECLINED);
    if(r->header_only) {
        return(OK);
    }

    // update runtime metrics
    prometheus_status_monitor();

    ap_set_content_type(r, "text/plain");

    if(!prometheus_status_send_communication_socket("metrics\n")) {
        return(OK);
    }

    while((nbytes = read(metric_socket_fd, buffer, 4095)) > 1) {
        buffer[nbytes] = 0;
        ap_rputs(buffer, r);
    }

    prometheus_status_close_communication_socket();

    return(OK);
}

/* prometheus_status_counter is called on each request to update counter */
static int prometheus_status_counter(request_rec *r) {
    apr_time_t now = apr_time_now();
    apr_time_t duration = now - r->request_time;

    // is the module enabled at all?
    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(r->per_dir_config, &prometheus_status_module);
    if(config->enabled == 0) {
        return(OK);
    }

    prometheus_status_send_communication_socket("update:promRequests;1;%s;%d;%s\n", r->method, r->status, config->label);
    prometheus_status_send_communication_socket("update:promResponseTime;%f;%s\n", (long)duration/(double)APR_USEC_PER_SEC, config->label);
    prometheus_status_send_communication_socket("update:promResponseSize;%d;%s\n", (int)r->bytes_sent, config->label);
    prometheus_status_close_communication_socket();
    return(OK);
}

static apr_status_t prometheus_status_cleanup_handler() {
    if(metric_socket != NULL) {
        unlink(metric_socket);
        metric_socket = NULL;
    }
    return(OK);
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    void *data = NULL;
    const char *key = "prometheus_status_init";

    // This code is used to prevent double initialization of the module during Apache startup
    apr_pool_userdata_get(&data, key, s->process->pool);
    if(data == NULL) {
        apr_pool_userdata_set((const void *)1, key, apr_pool_cleanup_null, s->process->pool);
        return OK;
    }

    /* cache main server */
    main_server = s;

    // is the module enabled at all?
    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(s->module_config, &prometheus_status_module);
    if(config->enabled == 0) {
        return(OK);
    }

    log("prometheus_status_init version %s", VERSION);

    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
    ap_mpm_query(AP_MPMQ_MAX_DAEMONS, &max_servers);
    ap_mpm_query(AP_MPMQ_MAX_THREADS, &threads_per_child);

    /* work around buggy MPMs */
    if (threads_per_child == 0)
        threads_per_child = 1;

    // load go module part
    void *go_module_handle = NULL;
    char origin[PATH_MAX];
    apr_os_dso_handle_t *osdso;
    apr_os_dso_handle_get((void *)&osdso, prometheus_status_module.dynamic_load_handle);
    if(dlinfo(osdso, RTLD_DI_ORIGIN, &origin) != -1) {
        char go_so_path[PATH_MAX+100];
        snprintf(go_so_path, PATH_MAX+100, "%s/mod_prometheus_status_go.so", origin);

        go_module_handle = dlopen(go_so_path, RTLD_LAZY);
        if(!go_module_handle) {
            log("loading %s failed: %s\n", go_so_path, dlerror());
            exit(1);
        }
    }
    log("prometheus_status_init gomodule loaded: %p", go_module_handle);

    prometheusStatusInitFn = dlsym(go_module_handle, "prometheusStatusInit");

    // run go initializer
    metric_socket = (*prometheusStatusInitFn)(ap_get_server_description(), s);
    log("mod_prometheus_status initialized: %s", metric_socket);

    apr_pool_cleanup_register(plog, NULL, prometheus_status_cleanup_handler, apr_pool_cleanup_null);
    return OK;
}

/* prometheus_status_register_hooks registers all required hooks */
static void prometheus_status_register_hooks(apr_pool_t *p) {
    log("prometheus_status_register_hooks: %s\n", __FILE__);
    ap_hook_handler(prometheus_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(prometheus_status_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(prometheus_status_counter, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Function for creating new configurations for per-directory contexts */
void *prometheus_status_create_dir_conf(apr_pool_t *pool, char *context) {
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
void *prometheus_status_merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD) {
    prometheus_status_config *base = (prometheus_status_config *) BASE;
    prometheus_status_config *add = (prometheus_status_config *) ADD;
    prometheus_status_config *conf = (prometheus_status_config *) prometheus_status_create_dir_conf(pool, "Merged configuration");

    conf->enabled = (add->enabled == 0) ? base->enabled : add->enabled;
    strcpy(conf->label, strlen(add->label) ? add->label : base->label);
    return conf;
}

/* available configuration directives */
static const command_rec prometheus_status_directives[] = {
    AP_INIT_FLAG("PrometheusStatusEnabled",   prometheus_status_set_enabled, NULL, RSRC_CONF, "Set to Off to disable mod_prometheus_status completely."),
    AP_INIT_RAW_ARGS("PrometheusStatusLabel", prometheus_status_set_label,   NULL, OR_ALL,   "Set a custom label from within apache directives"),
    { NULL }
};

/* register mod_prometheus_status within the apache */
module AP_MODULE_DECLARE_DATA prometheus_status_module = {
    STANDARD20_MODULE_STUFF,
    prometheus_status_create_dir_conf, /* create per-dir    config structures */
    prometheus_status_merge_dir_conf,  /* merge  per-dir    config structures */
    NULL,                              /* create per-server config structures */
    NULL,                              /* merge  per-server config structures */
    prometheus_status_directives,      /* table of config file commands       */
    prometheus_status_register_hooks   /* register hooks                      */
};
