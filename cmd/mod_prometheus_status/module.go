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

// TODO: make this build option
var EnableDebug = "1"
var metricsSocket = ""
var listener *net.Listener
var defaultSocketTimeout = 1

//export prometheusStatusInit
func prometheusStatusInit(serverDesc *C.char, rptr uintptr, labelNames *C.char, mpmName *C.char, socketTimeout int) unsafe.Pointer {
	serverRec := (*C.server_rec)(unsafe.Pointer(rptr))
	defaultSocketTimeout = socketTimeout

	// avoid double initializing
	if metricsSocket == "" {
		initLogging()
		registerMetrics(C.GoString(serverDesc), C.GoString(serverRec.server_hostname), C.GoString(labelNames), C.GoString(mpmName))
		tmpfile, err := ioutil.TempFile("", "metrics.*.sock")
		if err != nil {
			log("failed to get tmpfile: %s", err.Error())
		}
		metricsSocket = tmpfile.Name()
	} else {
		// close old server
		log("prometheusStatusInit closing old listener")
		if listener != nil {
			(*listener).Close()
			listener = nil
		}
	}
	go startMetricServer()

	log("prometheusStatusInit: %d", os.Getpid())
	return unsafe.Pointer(C.CString(metricsSocket))
}

func startMetricServer() {
	os.Remove(metricsSocket)
	log("InitMetricsCollector")
	l, err := net.Listen("unix", metricsSocket)
	if err != nil {
		log("listen error: %s", err.Error())
	}
	listener = &l
	defer func() {
		if listener != nil {
			(*listener).Close()
			listener = nil
		}
	}()

	log("listening on metricsSocket: %s", metricsSocket)
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
				log("Writing client error: %s", err.Error())
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
