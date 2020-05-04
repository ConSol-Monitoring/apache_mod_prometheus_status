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
	"io"
	"io/ioutil"
	"net"
	"os"
	"strings"
	"time"
	"unsafe"
)

const (
	ServerMetrics int = iota
	RequestMetrics
)

// Build contains the current git commit id
// compile passing -ldflags "-X main.Build=<build sha1>" to set the id.
var Build string

var metricsSocket = ""
var listener *net.Listener
var defaultSocketTimeout = 1

//export prometheusStatusInit
func prometheusStatusInit(serverDesc *C.char, serverHostName, version *C.char, debug, userID, groupID C.int, labelNames *C.char, mpmName *C.char, socketTimeout int, tmpFolder, timeBuckets, sizeBuckets *C.char) unsafe.Pointer {
	defaultSocketTimeout = socketTimeout

	// avoid double initializing
	if metricsSocket == "" {
		initLogging(int(debug))
		err := registerMetrics(C.GoString(serverDesc), C.GoString(serverHostName), C.GoString(labelNames), C.GoString(mpmName), C.GoString(timeBuckets), C.GoString(sizeBuckets))
		if err != nil {
			logErrorf("failed to initialize metrics: %s", err.Error())
			return unsafe.Pointer(C.CString(""))
		}
		tmpdir := ""
		if tmpFolder != nil {
			tmpdir = C.GoString(tmpFolder)
		}
		tmpfile, err := ioutil.TempFile(tmpdir, "metrics.*.sock")
		if err != nil {
			logErrorf("failed to get tmpfile: %s", err.Error())
			return unsafe.Pointer(C.CString(""))
		}
		metricsSocket = tmpfile.Name()
	} else {
		// close old server
		logDebugf("prometheusStatusInit closing old listener")
		if listener != nil {
			(*listener).Close()
			listener = nil
		}
	}
	startChannel := make(chan bool)
	go startMetricServer(startChannel, metricsSocket, int(userID), int(groupID))
	<-startChannel

	logInfof("mod_prometheus_status v%s initialized - socket:%s - uid:%d - gid:%d - build:%s", C.GoString(version), metricsSocket, userID, groupID, Build)
	return unsafe.Pointer(C.CString(metricsSocket))
}

func startMetricServer(startChannel chan bool, socketPath string, userID, groupID int) {
	defer func() {
		if listener != nil {
			(*listener).Close()
			listener = nil
		}
	}()
	os.Remove(socketPath)
	logDebugf("InitMetricsCollector: %s (uid: %d, gid: %d)", socketPath, userID, groupID)
	l, err := net.Listen("unix", socketPath)
	if err != nil {
		logErrorf("listen error: %s", err.Error())
		startChannel <- false
		return
	}
	err = os.Chown(socketPath, userID, groupID)
	if err != nil {
		logErrorf("cannot chown metricssocket: %s", err.Error())
		startChannel <- false
		return
	}
	listener = &l

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
		if err == io.EOF {
			break
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
		}
	}
}

func main() {}
