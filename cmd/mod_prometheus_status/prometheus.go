package main

import (
	"bytes"
	"strconv"
	"strings"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/common/expfmt"
)

var (
	registry   = prometheus.NewRegistry()
	collectors = make(map[string]interface{}, 0)
)

var labelCount = 0

func registerMetrics(serverDesc, serverName string, labelNames string) {
	requestLabels := make([]string, 0)
	labelNames = strings.TrimSpace(labelNames)
	if labelNames != "" {
		requestLabels = strings.Split(labelNames, ";")
	}
	labelCount = len(requestLabels)

	/* server related metrics */
	promServerInfo := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Namespace: "apache",
			Name:      "server_info",
			Help:      "information about the apache version",
		},
		[]string{"server_description"})
	registry.MustRegister(promServerInfo)
	promServerInfo.WithLabelValues(serverDesc).Add(1)
	collectors["promServerInfo"] = promServerInfo

	promServerName := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Namespace: "apache",
			Name:      "server_name",
			Help:      "contains the server name",
		},
		[]string{"server_name"})
	registry.MustRegister(promServerName)
	promServerName.WithLabelValues(serverName).Add(1)
	collectors["promServerName"] = promServerName

	promServerUptime := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "server_uptime_seconds",
			Help:      "server uptime in seconds",
		})
	registry.MustRegister(promServerUptime)
	collectors["promServerUptime"] = promServerUptime

	promCPULoad := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "cpu_load",
			Help:      "CPU Load 1",
		})
	registry.MustRegister(promCPULoad)
	collectors["promCPULoad"] = promCPULoad

	promMPMGeneration := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "server_mpm_generation",
			Help:      "current mpm generation",
		})
	registry.MustRegister(promMPMGeneration)
	collectors["promMPMGeneration"] = promMPMGeneration

	promConfigGeneration := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "server_config_generation",
			Help:      "current config generation",
		})
	registry.MustRegister(promConfigGeneration)
	collectors["promConfigGeneration"] = promConfigGeneration

	promWorkers := prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "workers",
			Help:      "is the total number of apache workers",
		},
		[]string{"state"})
	registry.MustRegister(promWorkers)
	collectors["promWorkers"] = promWorkers
	promWorkers.WithLabelValues("ready").Set(0)
	promWorkers.WithLabelValues("busy").Set(0)

	promScoreboard := prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "workers_scoreboard",
			Help:      "is the total number of workers from the scoreboard",
		},
		[]string{"state"})
	registry.MustRegister(promScoreboard)
	collectors["promScoreboard"] = promScoreboard

	/* request related metrics */
	promRequests := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Namespace: "apache",
			Name:      "requests_total",
			Help:      "is the total number of http requests",
		},
		requestLabels)
	registry.MustRegister(promRequests)
	collectors["promRequests"] = promRequests

	promResponseTime := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Namespace: "apache",
			Name:      "response_time_seconds",
			Help:      "response time histogram",
			Buckets:   prometheus.ExponentialBuckets(0.1, 10, 3),
		},
		requestLabels)
	registry.MustRegister(promResponseTime)
	collectors["promResponseTime"] = promResponseTime

	promResponseSize := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Namespace: "apache",
			Name:      "response_size_bytes",
			Help:      "response size histogram",
			Buckets:   prometheus.ExponentialBuckets(1000, 10, 6),
		},
		requestLabels)
	registry.MustRegister(promResponseSize)
	collectors["promResponseSize"] = promResponseSize
}

func metricsGet() []byte {
	var buf bytes.Buffer
	gathering, err := registry.Gather()
	if err != nil {
		log("internal prometheus error: %s", err.Error())
		return (buf.Bytes())
	}
	for _, m := range gathering {
		expfmt.MetricFamilyToText(&buf, m)
	}
	return (buf.Bytes())
}

func metricsUpdate(metricsType int, data string) {
	args := strings.Split(data, ";")
	name := args[0]
	val, _ := strconv.ParseFloat(args[1], 64)
	label := args[2:]

	// trim / expand labels to expected size
	if metricsType == RequestMetrics {
		switch {
		case len(label) > labelCount:
			label = label[0:labelCount]
		case len(label) < labelCount:
			label = append(label, make([]string, labelCount-len(label))...)
		}
	}

	collector, ok := collectors[name]
	if !ok {
		log("unknown metric: %s", name)
		return
	}
	switch col := collector.(type) {
	case prometheus.Gauge:
		col.Set(val)
	case prometheus.Counter:
		col.Add(val)
	case *prometheus.CounterVec:
		col.WithLabelValues(label...).Add(val)
	case *prometheus.GaugeVec:
		col.WithLabelValues(label...).Set(val)
	case *prometheus.HistogramVec:
		col.WithLabelValues(label...).Observe(val)
	default:
		log("unknown type: %T from metric %s", col, data)
	}
}