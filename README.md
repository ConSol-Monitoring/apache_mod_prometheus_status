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

  * server_info
  * server_uptime
  * server worker totals
  * server mpm/configuration generation
  * requests total number
  * request response size histogram
  * request response size histogram


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
