# mod_prometheus_status

mod_prometheus_status is a [Prometheus](https://prometheus.io/) white box exporter for [Apache](https://httpd.apache.org/) metrics similar to mod_status.

## Requirements

  - gcc compiler to build (4.9 or newer)
    - apache header files
  - docker/docker-compose for running tests

## Installation

Compile the module like this:

```bash
  make
```

## Configuration

```apache
LoadModule prometheus_status_module .../mod_prometheus_status.so
PrometheusStatusEnabled On

<Location /metrics>
  SetHandler prometheus-metrics
</Location>

# optionall set custom label for specific locations
<Location /test>
  PrometheusStatusLabel /test
</Location>
```

You may want to protect this location by password or domain so no one
else can look at it. Then you can access the metrics with a URL like:

http://your_server_name/metrics

## Metrics

So far this modules supports the following metrics:

```
  # HELP apache_server_info information about the apache version
  # TYPE apache_server_info counter
  # HELP apache_server_name contains the server name
  # TYPE apache_server_name counter
  # HELP apache_server_uptime_seconds server uptime in seconds
  # TYPE apache_server_uptime_seconds gauge
  # HELP apache_server_config_generation current config generation
  # TYPE apache_server_config_generation gauge
  # HELP apache_server_mpm_generation current mpm generation
  # TYPE apache_server_mpm_generation gauge
  # HELP apache_cpu_load CPU Load 1
  # TYPE apache_cpu_load gauge
  # HELP apache_workers is the total number of apache workers
  # TYPE apache_workers gauge
  # HELP apache_workers_scoreboard is the total number of apache workers
  # TYPE apache_workers_scoreboard gauge
  # HELP apache_requests_total is the total number of http requests
  # TYPE apache_requests_total counter
  # HELP apache_response_time_seconds response time histogram
  # TYPE apache_response_time_seconds histogram
  # HELP apache_response_size_bytes response size histogram
  # TYPE apache_response_size_bytes histogram
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

This creates a centos 7 box which builds the module whenever the source file changes.
You can access the module at `http://localhost:3000/metrics`. It might take a moment
to startup.

You can access the grafana dashboard at `http://localhost:3001/dashboard/grafana/` and the
Prometheus instance at `http://localhost:3001/dashboard/prometheus/`.

Run the unit/integration tests like this:

```bash
  make test
```

Cleanup docker machines and test environment by

```bash
  make clean
```

## Roadmap

  - [ ] add optional vhost label
  - [ ] add number of vhosts metric
  - [ ] add worker/threads metrics
  - [ ] add memory / cpu metrics
  - [ ] add example grafana dashboard

## License
[MIT](https://choosealicense.com/licenses/mit/)
