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

<Location /metrics>
  SetHandler prometheus-metrics
</Location>
```

You may want to protect this location by password or domain so no one
else can look at it. Then you can access the metrics with a URL like:

http://your_server_name/metrics

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

Run the unit/integration tests like this:

```bash
  make test
```

Cleanup docker machines and test environment by

```bash
  make clean
```

## Roadmap

  - [ ] add response time histogram
  - [ ] add response size histogram
  - [ ] add per location match label
  - [ ] add optional vhost label
  - [ ] add number of vhosts metric
  - [ ] add docker based tests
  - [ ] add test for reloading apache

## License
[MIT](https://choosealicense.com/licenses/mit/)
