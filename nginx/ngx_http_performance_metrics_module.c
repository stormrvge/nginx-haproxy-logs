#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_file.h>
#include <ngx_files.h>

typedef struct {
    ngx_flag_t enable;
    ngx_str_t output_file;
} ngx_http_performance_metrics_loc_conf_t;

static ngx_int_t ngx_http_performance_metrics_init(ngx_conf_t *cf);
static char *ngx_http_performance_metrics_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_performance_metrics_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_log_performance_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_performance_metrics_log_handler(ngx_http_request_t *r);
static void log_performance_metrics(ngx_http_request_t *r);

static ngx_command_t ngx_http_performance_metrics_commands[] = {
    { ngx_string("log_performance_metrics"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_log_performance_metrics,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_performance_metrics_loc_conf_t, enable),
      NULL },

    ngx_null_command
};

static ngx_http_module_t ngx_http_performance_metrics_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_http_performance_metrics_init,      /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_http_performance_metrics_create_loc_conf,  /* create location configuration */
    ngx_http_performance_metrics_merge_loc_conf,   /* merge location configuration */
};

ngx_module_t ngx_http_performance_metrics_module = {
    NGX_MODULE_V1,
    &ngx_http_performance_metrics_module_ctx,
    ngx_http_performance_metrics_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_performance_metrics_init(ngx_conf_t *cf) {
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_performance_metrics_log_handler;

    return NGX_OK;
}

static void *ngx_http_performance_metrics_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_performance_metrics_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_performance_metrics_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    return conf;
}

static char *ngx_http_performance_metrics_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_performance_metrics_loc_conf_t *prev = parent;
    ngx_http_performance_metrics_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->output_file, prev->output_file, "");

    return NGX_CONF_OK;
}

static char *ngx_http_log_performance_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_performance_metrics_loc_conf_t *pmcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;
    if (ngx_conf_full_name(cf->cycle, &value[1], 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    pmcf->output_file = value[1];
    pmcf->enable = 1;

    // Log the output file path using ngx_log_error
    ngx_log_error(NGX_LOG_DEBUG, cf->cycle->log, 0, "Output file path: %V", &pmcf->output_file);

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_performance_metrics_log_handler(ngx_http_request_t *r) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] before log_performance_metrics function");
    log_performance_metrics(r);
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] after log_performance_metrics function");
    return NGX_OK;
}

static void log_performance_metrics(ngx_http_request_t *r) {
    ngx_http_performance_metrics_loc_conf_t *pmcf;
    ngx_msec_int_t response_time;
    ngx_int_t duration;
    ngx_uint_t response_code;
    ngx_file_t file;
    ngx_int_t rc;
    u_char buf[256], *p;
    ngx_chain_t out;

    ngx_http_upstream_t *u;
    ngx_str_t upstream_addr;

    pmcf = ngx_http_get_module_loc_conf(r, ngx_http_performance_metrics_module);
    if (!pmcf->enable) {
        return;
    }


ngx_time_t *tp = ngx_timeofday();
ngx_msec_t current_msec = (ngx_msec_t) (tp->sec * 1000 + tp->msec);
response_time = (ngx_msec_int_t) (current_msec - r->start_msec);

ngx_msec_t request_time = (ngx_msec_t) (r->start_sec * 1000 + r->start_msec);

// Correct the duration calculation
duration = (ngx_int_t) (current_msec - request_time);
    response_code = r->headers_out.status;

    u = r->upstream;
    if (u && u->peer.name) {
        upstream_addr.data = u->peer.name->data;
        upstream_addr.len = u->peer.name->len;
    } else {
        upstream_addr.data = (u_char *) "-";
        upstream_addr.len = 1;
    }

    p = ngx_snprintf(buf, sizeof(buf), "%i,%i,%i,%ui,%V\n", request_time, response_time, duration, response_code, &upstream_addr);

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.name = pmcf->output_file;
    file.log = r->connection->log;

    file.fd = ngx_open_file(pmcf->output_file.data, NGX_FILE_APPEND, NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, ngx_open_file_n " \"%s\" failed", pmcf->output_file.data);
        return;
    }

    out.buf = ngx_create_temp_buf(r->pool, p - buf);
    if (out.buf == NULL) {
        return;
    }

    ngx_memcpy(out.buf->pos, buf, p - buf);
    out.buf->last = out.buf->pos + (p - buf);
    out.next = NULL;

    rc = ngx_write_chain_to_file(&file, &out, (off_t) 0, r->pool);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] in log function after write");

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to write performance metrics to file");
    }

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_errno, ngx_close_file_n " \"%s\" failed", pmcf->output_file.data);
    }
}
