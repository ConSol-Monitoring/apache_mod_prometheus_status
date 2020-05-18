# mod_prometheus_status

mod_prometheus_status is a [Prometheus](https://prometheus.io/) white box exporter for [Apache HTTPD](https://httpd.apache.org/) metrics similar to mod_status.

The exporter is a loadable Apache module and serves metrics directly via the
apache webserver. It comes with dynamic and flexible labeling, see the example
configuration below.

## How it works
Since prometheus exporter are usually bound to a single process and the apache
webserver is a multiprocess daemon, this module starts a metrics collector in
the parent httpd process.

Upon start the main collector creates the prometheus client library registry
based on the `PrometheusStatusLabelNames`. It then opens a unix socket to
receive the metrics updates from the child workers.

On each request, the client worker sends its metrics based on
`PrometheusStatusLabelValues`, which utilizes Apaches LogFormat, to the metrics
collector.

## Build Requirements

  - gcc compiler to build (4.9 or newer)
    - apache header files
  - golang >= 1.12
  - docker/docker-compose for running tests

## Installation

> **_NOTE:_** Prebuild modules are available at https://github.com/ConSol/apache_mod_prometheus_status/releases

Compile the module like this:

```bash
  make
```

Copy *BOTH* .so files to the apache module directory and adjust the example
configuration.

## Configuration

> **_NOTE:_** Loading this module the first time requires a apache restart. Reload is not sufficient.

### apache.conf:
```apache
<IfModule !mod_prometheus_status.c>
  LoadModule prometheus_status_module .../mod_prometheus_status.so
</IfModule>
PrometheusStatusEnabled               On
PrometheusStatusLabelNames            vhost;method;status;application
PrometheusStatusLabelValues           %v;%m;%s;
PrometheusStatusTmpFolder             /tmp
PrometheusStatusResponseTimeBuckets   0.01;0.1;1;10;30
PrometheusStatusResponseSizeBuckets   1000;10000;100000;1000000;10000000;100000000

<Location /metrics>
  # make collected metrics available at this url
  SetHandler prometheus-metrics
</Location>

# optional custom labels for specific locations
<Location /test>
  PrometheusStatusLabel %v;%m;%s;application1
</Location>

# disable collecting metrics for some locations
<Location /no_metrics_for_here>
  PrometheusStatusEnabled Off
</Location>
```

### Directives

#### PrometheusStatusEnabled
Enable or disable collecting metrics. Available on server and directory level.

  Default: On

#### PrometheusStatusLabelNames
Set label names separated by semicolon. This is a global setting and can only
be set once on server level since the metrics have to be registered and cannot
be changed later on.

  Default: method;status

> **_NOTE:_** Be aware of cardinality explosion and do not overuse labels.
Read more at https://prometheus.io/docs/practices/naming/#labels and
https://www.robustperception.io/cardinality-is-key

#### PrometheusStatusLabelValues
Set label values separated by semicolon. You can use the apache logformat here.
Some high cardinality variables are not implemented.

  Default: %m;%s

Useful examples are:

 - `%m` - request method: ex.: GET, POST, ...
 - `%s` - response code: ex.: 200, 404, 500, ...
 - `%v` - canonical ServerName

See http://httpd.apache.org/docs/current/mod/mod_log_config.html#formats for a
full list of available variables.

#### PrometheusStatusTmpFolder
Set the folder where the interprocess communication socket will be created in.

  Default: /tmp (or system default temporary folder)

#### PrometheusStatusResponseTimeBuckets
Set the buckets for the response time histogram.

  Default: 0.01;0.1;1;10;30

#### PrometheusStatusResponseSizeBuckets
Set the buckets for the response size histogram.

  Default: 1000;10000;100000;1000000;10000000;100000000

## Metrics

Then you can access the metrics with a URL like:

http://your_server_name/metrics

Or whatever you put your `SetHandler prometheus-metrics` to.

> **_NOTE:_** You may want to protect the /metrics location by password or domain so no one else can look at it.


So far this modules supports the following metrics:

```
  # HELP apache_cpu_load CPU Load 1
  # TYPE apache_cpu_load gauge
  # TYPE apache_process_counter gauge
  # HELP apache_process_counter number of apache processes
  # TYPE apache_process_total_io_read_bytes gauge
  # HELP apache_process_total_io_read_bytes total read bytes over all apache processes
  # TYPE apache_process_total_io_write_bytes gauge
  # HELP apache_process_total_io_write_bytes total write bytes over all apache processes
  # TYPE apache_process_total_open_fd gauge
  # HELP apache_process_total_open_fd total open file handles over all apache processes
  # TYPE apache_process_total_rss_memory_bytes gauge
  # HELP apache_process_total_rss_memory_bytes total rss bytes over all apache processes
  # TYPE apache_process_total_threads gauge
  # HELP apache_process_total_threads total number of threads over all apache processes
  # TYPE apache_process_total_virt_memory_bytes gauge
  # HELP apache_process_total_virt_memory_bytes total virt bytes over all apache processes
  # TYPE apache_requests_total counter
  # HELP apache_requests_total is the total number of http requests
  # TYPE apache_response_size_bytes histogram
  # HELP apache_response_size_bytes response size histogram
  # TYPE apache_response_time_seconds histogram
  # HELP apache_response_time_seconds response time histogram
  # HELP apache_server_config_generation current config generation
  # TYPE apache_server_config_generation gauge
  # TYPE apache_server_info counter
  # HELP apache_server_info information about the apache version
  # HELP apache_server_mpm_generation current mpm generation
  # TYPE apache_server_mpm_generation gauge
  # HELP apache_server_name contains the server name
  # TYPE apache_server_name counter
  # TYPE apache_server_uptime_seconds gauge
  # HELP apache_server_uptime_seconds server uptime in seconds
  # TYPE apache_workers gauge
  # HELP apache_workers is the total number of apache workers
  # TYPE apache_workers_scoreboard gauge
  # HELP apache_workers_scoreboard is the total number of workers from the scoreboard
```

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss
what you would like to change.

Please make sure to update tests as appropriate.

### Development Environment

There is a test/dev docker box located in `t/testbox` which can be started for
easy testing and development.

```bash
  make testbox
```

This creates a Centos 8 box which builds the module whenever the source file
changes. You can access the module at `http://localhost:3000/metrics`. It might
take a moment to startup.

You can access the grafana dashboard at
`http://localhost:3001/dashboard/grafana/` and the Prometheus instance at
`http://localhost:3001/dashboard/prometheus/`.

Run the unit/integration tests like this:

```bash
  make test
```

Cleanup docker machines and test environment by

```bash
  make clean
```

#### Ressources

Some useful ressources during development:

  - `Apache Module Development` - https://httpd.apache.org/docs/current/developer/modguide.html
  - `Apache API docs` - https://ci.apache.org/projects/httpd/trunk/doxygen/

## Roadmap

  - [ ] add trimmable path label, ex.: %{p:1:2} which uses 2 directories levels, starting at the first level

## Changes

```
0.0.7   Mon May 18 11:05:59 CEST 2020
          - fix issue when initally loaded by reload instead of a fresh apache start
          - improve responding to metrics requests during apache reloads
          - update example dashboard

0.0.6   Sat May  9 18:15:31 CEST 2020
          - add vhost to default label names
          - hide 'disabled' scoreboard entry, its not an actual slot
          - fix permission issue again

0.0.5   Sat May  9 15:31:40 CEST 2020
          - add process memory/counter/io metrics
          - start go metric server in subprocess

0.0.4   Mon May  4 10:28:17 CEST 2020
          - add example grafana dashboard
          - add new apache directives:
            - PrometheusStatusTmpFolder
            - PrometheusStatusResponseTimeBuckets
            - PrometheusStatusResponseSizeBuckets
          - fix permission issue with system apaches starting as root

0.0.3   Wed Apr 29 10:01:17 CEST 2020
          - fixed reload issue on some systems

0.0.2   Tue Apr 21 16:53:05 CEST 2020
          - add missing includes
          - improve build from dist tarball

0.0.1   Tue Apr 21 09:43:37 CEST 2020
          - initial release
          - dynamic labeling
          - basic set of metrics
```

## License
[MIT](https://choosealicense.com/licenses/mit/)
