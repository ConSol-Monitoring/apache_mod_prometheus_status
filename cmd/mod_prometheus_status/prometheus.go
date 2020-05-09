package main

import (
	"bytes"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/common/expfmt"
	"github.com/shirou/gopsutil/process"
)

var registry *prometheus.Registry
var collectors = make(map[string]interface{})
var labelCount = 0

const (
	// ProcUpdateInterval set the minimum update interval in seconds for proc statistics
	ProcUpdateInterval int64 = 3
)

var lastProcUpdate int64

type procUpdate struct {
	Total      int
	Threads    int
	OpenFD     int
	RSS        uint64
	VMS        uint64
	ReadBytes  uint64
	WriteBytes uint64
}

func registerMetrics(serverDesc, serverName, labelNames, mpmName, timeBuckets, sizeBuckets string) (err error) {
	if registry != nil {
		return
	}
	registry = prometheus.NewRegistry()
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
		[]string{"server_description", "mpm"})
	registry.MustRegister(promServerInfo)
	promServerInfo.WithLabelValues(serverDesc, mpmName).Add(1)
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

	/* process related metrics */
	promProcessCounter := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_counter",
			Help:      "number of apache processes",
		})
	registry.MustRegister(promProcessCounter)
	promProcessCounter.Set(0)
	collectors["promProcCounter"] = promProcessCounter

	promThreads := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_threads",
			Help:      "total number of threads over all apache processes",
		})
	registry.MustRegister(promThreads)
	promThreads.Set(0)
	collectors["promThreads"] = promThreads

	promMemoryReal := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_rss_memory_bytes",
			Help:      "total rss bytes over all apache processes",
		})
	registry.MustRegister(promMemoryReal)
	promMemoryReal.Set(0)
	collectors["promMemoryReal"] = promMemoryReal

	promMemoryVirt := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_virt_memory_bytes",
			Help:      "total virt bytes over all apache processes",
		})
	registry.MustRegister(promMemoryVirt)
	promMemoryVirt.Set(0)
	collectors["promMemoryVirt"] = promMemoryVirt

	promReadBytes := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_io_read_bytes",
			Help:      "total read bytes over all apache processes",
		})
	registry.MustRegister(promReadBytes)
	promReadBytes.Set(0)
	collectors["promReadBytes"] = promReadBytes

	promWriteBytes := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_io_write_bytes",
			Help:      "total write bytes over all apache processes",
		})
	registry.MustRegister(promWriteBytes)
	promWriteBytes.Set(0)
	collectors["promWriteBytes"] = promWriteBytes

	promOpenFD := prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "apache",
			Name:      "process_total_open_fd",
			Help:      "total open file handles over all apache processes",
		})
	registry.MustRegister(promOpenFD)
	promOpenFD.Set(0)
	collectors["promOpenFD"] = promOpenFD

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

	timeBucketList, err := expandBuckets(timeBuckets)
	if err != nil {
		return
	}
	promResponseTime := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Namespace: "apache",
			Name:      "response_time_seconds",
			Help:      "response time histogram",
			Buckets:   timeBucketList,
		},
		requestLabels)
	registry.MustRegister(promResponseTime)
	collectors["promResponseTime"] = promResponseTime

	sizeBucketList, err := expandBuckets(sizeBuckets)
	if err != nil {
		return
	}
	promResponseSize := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Namespace: "apache",
			Name:      "response_size_bytes",
			Help:      "response size histogram",
			Buckets:   sizeBucketList,
		},
		requestLabels)
	registry.MustRegister(promResponseSize)
	collectors["promResponseSize"] = promResponseSize
	return
}

func metricsGet() []byte {
	now := time.Now().Unix()
	if now-lastProcUpdate > ProcUpdateInterval {
		lastProcUpdate = now
		updateProcMetrics()
	}
	var buf bytes.Buffer
	gathering, err := registry.Gather()
	if err != nil {
		logErrorf("internal prometheus error: %s", err.Error())
		return (buf.Bytes())
	}
	for _, m := range gathering {
		expfmt.MetricFamilyToText(&buf, m)
	}
	buf.WriteString("\n\n")
	return (buf.Bytes())
}

// updateProcMetrics updates memory statistics for all children with match httpd/apache in its cmdline
func updateProcMetrics() {
	stats := &procUpdate{}

	pid := os.Getppid()
	if pid == 1 {
		pid = os.Getpid()
	}
	mainProcess, _ := process.NewProcess(int32(pid))
	countProcStats(mainProcess, stats)

	collectors["promProcCounter"].(prometheus.Gauge).Set(float64(stats.Total))
	collectors["promThreads"].(prometheus.Gauge).Set(float64(stats.Threads))
	collectors["promOpenFD"].(prometheus.Gauge).Set(float64(stats.OpenFD))
	collectors["promMemoryReal"].(prometheus.Gauge).Set(float64(stats.RSS))
	collectors["promMemoryVirt"].(prometheus.Gauge).Set(float64(stats.VMS))
	collectors["promReadBytes"].(prometheus.Gauge).Set(float64(stats.ReadBytes))
	collectors["promWriteBytes"].(prometheus.Gauge).Set(float64(stats.WriteBytes))
}

func countProcStats(proc *process.Process, stats *procUpdate) {
	cmdLine, err := proc.Cmdline()
	if err != nil {
		return
	}

	// only count apache processes
	if !strings.Contains(cmdLine, "apache") && !strings.Contains(cmdLine, "httpd") {
		return
	}

	memInfo, err := proc.MemoryInfo()
	if err != nil {
		return
	}
	ioInfo, err := proc.IOCounters()
	if err != nil {
		return
	}
	openFD, err := proc.NumFDs()
	if err != nil {
		return
	}
	numThreads, err := proc.NumThreads()
	if err != nil {
		return
	}
	stats.Total++
	stats.Threads += int(numThreads)
	stats.OpenFD += int(openFD)
	stats.RSS += memInfo.RSS
	stats.VMS += memInfo.VMS
	stats.ReadBytes += ioInfo.ReadBytes
	stats.WriteBytes += ioInfo.WriteBytes
	children, err := proc.Children()
	if err != nil {
		return
	}
	for _, child := range children {
		countProcStats(child, stats)
	}
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
		logErrorf("unknown metric: %s", name)
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
		logErrorf("unknown type: %T from metric %s", col, data)
	}
}

func expandBuckets(input string) (list []float64, err error) {
	for _, s := range strings.Split(input, ";") {
		s = strings.TrimSpace(s)
		n, fErr := strconv.ParseFloat(s, 64)
		if fErr != nil {
			err = fErr
			return
		}
		list = append(list, n)
	}
	return
}
