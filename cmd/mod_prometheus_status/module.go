// package name: mod_prometheus_status
package main

/*
#cgo CFLAGS: -I/usr/include/httpd
#cgo CFLAGS: -I/usr/include/apache2
#cgo CFLAGS: -I/usr/include/apr-1
#cgo CFLAGS: -I/usr/include/apr-1.0

#include <httpd.h>
#include <http_config.h>

*/
import "C"

import (
	"bufio"
	"errors"
	"io"
	"net"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"
)

const (
	// ServerMetrics have different labels as RequestMetrics and are used on server level
	ServerMetrics int = iota

	// RequestMetrics are used to gather request specific metrics
	RequestMetrics
)

// Build contains the current git commit id
// compile passing -ldflags "-X main.Build=<build sha1>" to set the id.
var Build string

var defaultSocketTimeout = 1

const (
	// SigHupDelayExitSeconds sets the amount of extra seconds till exiting after receiving a SIGHUP
	SigHupDelayExitSeconds = 5
)

//export prometheusStatusInit
func prometheusStatusInit(metricsSocket, serverDesc *C.char, serverHostName, version *C.char, debug, userID, groupID C.int, labelNames *C.char, mpmName *C.char, socketTimeout C.int, timeBuckets, sizeBuckets *C.char) C.int {
	defaultSocketTimeout = int(socketTimeout)

	initLogging(int(debug))

	err := registerMetrics(C.GoString(serverDesc), C.GoString(serverHostName), C.GoString(labelNames), C.GoString(mpmName), C.GoString(timeBuckets), C.GoString(sizeBuckets))
	if err != nil {
		logErrorf("failed to initialize metrics: %s", err.Error())
		return C.int(1)
	}

	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP)
	go func() {
		sig := <-sigs
		logDebugf("got signal: %d(%s)", sig, sig.String())
		// wait a few extra seconds on sighups to answer metrics requests during reloads
		if sig == syscall.SIGHUP {
			time.Sleep(time.Duration(SigHupDelayExitSeconds) * time.Second)
		}
		os.Remove(C.GoString(metricsSocket))
		os.Exit(0)
	}()

	startChannel := make(chan bool)
	go startMetricServer(startChannel, C.GoString(metricsSocket), int(userID), int(groupID))
	<-startChannel

	logInfof("mod_prometheus_status v%s initialized - socket:%s - uid:%d - gid:%d - build:%s", C.GoString(version), C.GoString(metricsSocket), userID, groupID, Build)
	return C.int(0)
}

func startMetricServer(startChannel chan bool, socketPath string, userID, groupID int) {
	logDebugf("InitMetricsCollector: %s (uid: %d, gid: %d)", socketPath, userID, groupID)
	l, err := net.Listen("unix", socketPath)
	if err != nil {
		logErrorf("listen error: %s", err.Error())
		startChannel <- false
		return
	}
	defer l.Close()
	err = os.Chown(socketPath, userID, groupID)
	if err != nil {
		logErrorf("cannot chown metricssocket: %s", err.Error())
		startChannel <- false
		return
	}

	logDebugf("listening on metricsSocket: %s", socketPath)
	startChannel <- true
	for {
		conn, err := l.Accept()
		if err != nil {
			// listener closed
			return
		}
		conn.SetDeadline(time.Now().Add(time.Duration(defaultSocketTimeout) * time.Second))
		go metricServer(conn)
	}
}

func metricServer(c net.Conn) {
	defer c.Close()

	buf := bufio.NewReader(c)

	for {
		line, err := buf.ReadString('\n')
		if err != nil {
			if errors.Is(err, io.EOF) {
				return
			}
			logErrorf("Reading client error: %s", err.Error())
			return
		}
		line = strings.TrimSpace(line)
		if line == "" {
			return
		}
		args := strings.SplitN(line, ":", 2)
		switch args[0] {
		case "metrics":
			_, err = c.Write(metricsGet())
			if err != nil {
				logErrorf("Writing client error: %s", err.Error())
				return
			}
			return
		case "server":
			metricsUpdate(ServerMetrics, args[1])
		case "request":
			metricsUpdate(RequestMetrics, args[1])
		default:
			logErrorf("unknown metrics update request: %s", args[0])
			return
		}
	}
}

func main() {}
