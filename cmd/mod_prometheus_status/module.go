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

var EnableDebug = "1"
var metricsSocket = ""

//export prometheusStatusInit
func prometheusStatusInit(serverDesc *C.char, rptr uintptr) unsafe.Pointer {
	serverRec := (*C.server_rec)(unsafe.Pointer(rptr))

	// avoid double initializing
	if metricsSocket != "" {
		return unsafe.Pointer(C.CString(metricsSocket))
	}

	initLogging()
	log("prometheusStatusInit: %d", os.Getpid())

	registerMetrics(C.GoString(serverDesc), C.GoString(serverRec.server_hostname))

	tmpfile, err := ioutil.TempFile("", "metrics.*.sock")
	if err != nil {
		log("failed to get tmpfile: %s", err.Error())
	}
	metricsSocket = tmpfile.Name()
	go startMetricServer()

	return unsafe.Pointer(C.CString(metricsSocket))
}

func startMetricServer() {
	os.Remove(metricsSocket)
	log("InitMetricsCollector")
	l, err := net.Listen("unix", metricsSocket)
	if err != nil {
		log("listen error: %s", err.Error())
	}
	defer l.Close()

	log("listening on metricsSocket: %s", metricsSocket)
	for {
		conn, err := l.Accept()
		if err != nil {
			log("accept error: %s", err)
		}
		conn.SetDeadline(time.Now().Add(1 * time.Second))
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
				log("Writing client error: %s", err.Error())
				return
			}
			return
		case "update":
			metricsUpdate(args[1])
		}
	}
}

func main() {}
