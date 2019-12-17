# mod_prometheus_status

mod_prometheus_status is a [Prometheus](https://prometheus.io/) white box exporter for [Apache](https://httpd.apache.org/) metrics similar to mod_status.

## Requirements

  - gcc compiler to build
    - apache header files
  - docker for running tests only

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

## Roadmap

  - [ ] add response time histogram
  - [ ] add response size histogram
  - [ ] add per location match label
  - [ ] make vhost label optional
  - [ ] add server info metric
  - [ ] add number of vhosts metric
  - [ ] add docker based tests

## License
[MIT](https://choosealicense.com/licenses/mit/)
